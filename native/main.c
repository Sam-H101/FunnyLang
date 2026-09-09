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

#include "chunk.h"
#include "error.h"
#include "gc.h"
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
    if (argc < 2) {
        /* Deliberately plain ASCII: the real banner (funnylang/cli.py's
           BANNER) is full box-drawing Unicode, which needs UTF-8 console
           setup on Windows (SetConsoleOutputCP) before it's safe to print.
           That setup is platform.c's job once it exists (NATIVE_PLAN.md
           §4: "the ONLY file with #ifdef _WIN32") -- main.c doesn't get
           its own ad hoc ifdef for it. */
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

    char *err = NULL;
    CompiledUnit *unit = chunk_load_funnyc(data, len, &vm.gc, &err);
    free(data);
    if (!unit) {
        fprintf(stderr, "%s\n", err);
        free(err);
        vm_destroy(&vm);
        return 1;
    }

    VmResult result = vm_run(&vm, unit, stdout);
    int exitCode = 0;
    if (result == VM_ERROR) {
        /* Not the real diagnostic renderer (N6's diag.c) -- just enough to
           report what went wrong until that exists. */
        ObjError *e = (ObjError *)AS_OBJ(vm.uncaughtError);
        fprintf(stderr, "%s: %s\n", e->flavor->chars, e->message->chars);
        exitCode = 1;
    }

    chunk_free_unit(unit);
    vm_destroy(&vm);
    return exitCode;
}
