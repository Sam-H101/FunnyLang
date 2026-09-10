#include "runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "error.h"
#include "gc.h"
#include "platform.h"
#include "stash.h"
#include "da_string.h"
#include "value.h"
#include "vm.h"

int funny_run_bytecode(const uint8_t *data, size_t len, char **programArgs, int programArgc, RunnerOptions opts,
                        double *runMsOut) {
    VM vm;
    vm_init(&vm);
    builtins_install(&vm);

    ObjStash *args = stash_new(&vm.gc, NULL, 0);
    for (int i = 0; i < programArgc; i++) {
        stash_push(&vm.gc, args, OBJ_VAL(string_new(&vm.gc, programArgs[i], (uint32_t)strlen(programArgs[i]))));
    }
    vm.programArgs = OBJ_VAL(args);

    FILE *errOut = opts.err != NULL ? opts.err : stderr;
    vm.err = errOut;

    char *err = NULL;
    CompiledUnit *unit = NULL;
    CompiledPak *pak = NULL;
    if (chunk_is_funnypak(data, len)) {
        pak = chunk_load_funnypak(data, len, &vm.gc, &err);
    } else {
        unit = chunk_load_funnyc(data, len, &vm.gc, &err);
    }
    if (!unit && !pak) {
        fprintf(errOut, "%s\n", err);
        free(err);
        vm_destroy(&vm);
        return 1;
    }

    double startRun = platform_monotonic_seconds();
    FILE *out = opts.out != NULL ? opts.out : stdout;
    VmResult result = pak ? vm_run_pak(&vm, pak, out) : vm_run(&vm, unit, out);
    if (runMsOut) *runMsOut = (platform_monotonic_seconds() - startRun) * 1000.0;

    int exitCode = 0;
    if (result == VM_ERROR) {
        int64_t systemExitCode;
        if (vm_is_system_exit(vm.uncaughtError, &systemExitCode)) {
            /* dip(n): a clean process exit, not a crash -- no message,
               matching an uncaught Python SystemExit(n). */
            exitCode = (int)systemExitCode;
        } else {
            ObjError *e = (ObjError *)AS_OBJ(vm.uncaughtError);
            if (opts.errorLabel != NULL) {
                /* An error raised *by the compiler while compiling* is not
                   the user's program failing, and rendering it as one buries
                   the actual problem under a stack trace through
                   funnyc.funny. The self-hosted compiler encodes the real
                   flavour and position into its message (it can only
                   `chuck` a plain value), so the message is the useful part.
                   Full §4.2 diagnostics for source errors need the compiler
                   itself to report them properly -- N8's job, not something
                   to fake from out here. */
                fprintf(errOut, "%s\n  %s\n", opts.errorLabel, e->message->chars);
                exitCode = 1;
            } else {
                diag_render_error(errOut, e, opts.diag);
                /* computer.explode()'s ComputerExploded is an ordinary
                   catchable FunnyError everywhere else (sketchy/my_bad
                   catches it like any other) -- exit 69 is purely a
                   top-level uncaught-error mapping, matching
                   funnylang/cli.py's own dedicated `except ComputerExploded`
                   clause. */
                exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
            }
        }
    }

    if (pak) chunk_free_pak(pak);
    else chunk_free_unit(unit);
    vm_destroy(&vm);
    return exitCode;
}
