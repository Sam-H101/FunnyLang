/* native/runner.h -- loading and running compiled FunnyLang, shared by the
 * two entry points in native/: the CLI (main.c) and the yeet runtime stub
 * (stub_main.c).
 *
 * Shared rather than duplicated on purpose: a yeeted binary has to report
 * an uncaught error, and in particular exit 69 on an uncaught
 * `computer.explode()`, exactly the way `funny` does. Two copies of that
 * logic would be two things to keep in step.
 */
#ifndef FUNNY_RUNNER_H
#define FUNNY_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "diag.h"

typedef struct {
    DiagOptions diag;
    /* When non-NULL, an uncaught error prints this line followed by the
       error's message, instead of the full §4.2 diagnostic. Used for the
       compile phase, where the failure being reported is in the *user's*
       source and the compiler's own stack trace is noise. */
    const char *errorLabel;
    /* Where the program's `yap` goes. NULL means stdout, which is what
       `main.c` wants and what this always did.

       It exists because runs nest. `sus.run_program` is how the self-hosted
       CLI runs a user's program, and it used to send that program's output
       to the real stdout unconditionally -- so when the *CLI itself* was
       running inside a captured child VM (which is how a `.funny` test drives
       the command line), the user program's output escaped the capture and
       landed in the test runner's own stdout. Passing the calling VM's stream
       makes the nesting behave: at the top level that stream *is* stdout, so
       nothing changes, and inside a capture the output is captured. */
    FILE *out;
    /* Where an uncaught error's diagnostic and the program's own `yell` go.
       NULL means stderr. Same reason as `out`: a nested run needs to send its
       diagnostics wherever its caller's do, or a captured child leaks them
       into the parent's stderr. */
    FILE *err;
} RunnerOptions;

/* Runs already-compiled bytes -- a `.funnyc` or a `.funnypak`, told apart
   by their own magic, not by any filename. Returns the process exit code
   (0, `dip(n)`'s own code, 69 for an uncaught ComputerExploded, else 1).
   `runMsOut` may be NULL. */
int funny_run_bytecode(const uint8_t *data, size_t len, char **programArgs, int programArgc, RunnerOptions opts,
                        double *runMsOut);

#endif /* FUNNY_RUNNER_H */
