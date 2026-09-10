/* native/main.c -- N0 task 2's toolchain smoke test, now also N2's minimal
 * test driver: `funny <file.funnyc>` loads and runs a compiled file. This
 * is NOT the real CLI (that's N7 -- `run`/`build`/`test`/`fmt`/`vibe`/...,
 * source compilation, diagnostics). It exists only so N2's differential
 * tests have something to invoke; expect it to be replaced wholesale.
 *
 * No-argument invocation still just prints the plain-ASCII smoke-test
 * banner (see the note below) and exits 0 -- N0's own CI check (`./funny`
 * with no args) keeps working unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "diag.h"
#include "error.h"
#include "gc.h"
#include "platform.h"
#include "stash.h"
#include "string.h"
#include "value.h"
#include "vm.h"

static uint8_t *read_file(const char *path, size_t *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(data);
        return NULL;
    }
    *outLen = (size_t)size;
    return data;
}

int main(int argc, char **argv) {
    platform_console_init(); /* UTF-8 + ANSI on Windows; a no-op elsewhere */

    /* N6 task 2's two global flags. The real CLI's full flag set is N7's
       job; these two exist now because the diagnostics they control are
       what N6 delivers, and its acceptance gate diffs both modes. Parsed
       out of argv in place so the .funnyc path and the_args() past it are
       unaffected wherever the flags appear. */
    DiagOptions diagOptions = diag_default_options();
    int argWrite = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serious") == 0) {
            diagOptions.serious = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--no-colour") == 0) {
            diagOptions.color = false;
        } else {
            argv[argWrite++] = argv[i];
        }
    }
    argc = argWrite;

    if (argc < 2) {
        /* Still plain ASCII, but no longer for the old reason: N6 gave
           platform.c the Windows UTF-8/VT console setup this banner was
           waiting on, and platform_console_init() above has already run.
           What's left is that printing funnylang/cli.py's real BANNER is
           the *CLI's* job, and the real CLI is N7. */
        puts("FunnyLang (native) -- it compiles. somehow.");
        puts("N2: pass a .funnyc file to run it. The real CLI lands in N7.");
        return 0;
    }

    size_t len;
    uint8_t *data = read_file(argv[1], &len);
    if (!data) {
        fprintf(stderr, "couldn't read '%s'.\n", argv[1]);
        return 1;
    }

    VM vm;
    vm_init(&vm); /* sets up vm.gc, which the loader below tracks constants in */
    builtins_install(&vm);

    /* the_args(): every argument past the .funnyc path itself, matching
       funnylang/cli.py's own `vm.program_args = list(extra_args)`. */
    ObjStash *programArgs = stash_new(&vm.gc, NULL, 0);
    for (int i = 2; i < argc; i++) {
        ObjString *s = string_new(&vm.gc, argv[i], (uint32_t)strlen(argv[i]));
        stash_push(&vm.gc, programArgs, OBJ_VAL(s));
    }
    vm.programArgs = OBJ_VAL(programArgs);

    /* Which loader to use comes from the file's own magic, not its
       extension -- a bundle renamed to .funnyc still runs, and a caller
       (the yeet stub, later) that only has bytes doesn't have to guess. */
    char *err = NULL;
    CompiledUnit *unit = NULL;
    CompiledPak *pak = NULL;
    if (chunk_is_funnypak(data, len)) {
        pak = chunk_load_funnypak(data, len, &vm.gc, &err);
    } else {
        unit = chunk_load_funnyc(data, len, &vm.gc, &err);
    }
    free(data);
    if (!unit && !pak) {
        fprintf(stderr, "%s\n", err);
        free(err);
        vm_destroy(&vm);
        return 1;
    }

    VmResult result = pak ? vm_run_pak(&vm, pak, stdout) : vm_run(&vm, unit, stdout);
    int exitCode = 0;
    if (result == VM_ERROR) {
        int64_t systemExitCode;
        if (vm_is_system_exit(vm.uncaughtError, &systemExitCode)) {
            /* dip(n): a clean process exit, not a crash -- no message,
               matching an uncaught Python SystemExit(n). */
            exitCode = (int)systemExitCode;
        } else {
            ObjError *e = (ObjError *)AS_OBJ(vm.uncaughtError);
            diag_render_error(stderr, e, diagOptions);
            /* computer.explode()'s ComputerExploded is an ordinary
               catchable FunnyError everywhere else (sketchy/my_bad
               catches it like any other) -- exit 69 is purely a
               top-level uncaught-error mapping, matching
               funnylang/cli.py's own dedicated `except ComputerExploded`
               clause (NATIVE_PLAN.md N5 task 4). */
            exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
        }
    }

    if (pak) chunk_free_pak(pak);
    else chunk_free_unit(unit);
    vm_destroy(&vm);
    return exitCode;
}
