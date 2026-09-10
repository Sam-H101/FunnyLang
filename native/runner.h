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

#include "diag.h"

typedef struct {
    DiagOptions diag;
    /* When non-NULL, an uncaught error prints this line followed by the
       error's message, instead of the full §4.2 diagnostic. Used for the
       compile phase, where the failure being reported is in the *user's*
       source and the compiler's own stack trace is noise. */
    const char *errorLabel;
} RunnerOptions;

/* Runs already-compiled bytes -- a `.funnyc` or a `.funnypak`, told apart
   by their own magic, not by any filename. Returns the process exit code
   (0, `dip(n)`'s own code, 69 for an uncaught ComputerExploded, else 1).
   `runMsOut` may be NULL. */
int funny_run_bytecode(const uint8_t *data, size_t len, char **programArgs, int programArgc, RunnerOptions opts,
                        double *runMsOut);

#endif /* FUNNY_RUNNER_H */
