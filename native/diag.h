/* native/diag.h -- NATIVE_PLAN.md N6 task 1: PLAN.md §4.2's diagnostic
 * renderer, ported from funnylang/errors.py's `render_diagnostic`.
 *
 * The acceptance bar is byte-identical stderr against the Python VM for
 * every `err_*.funny` golden, in both funny and serious modes, so this is
 * a transcription rather than an interpretation: every space, every box-
 * drawing character and every blank line matches the Python by
 * construction, not by taste.
 */
#ifndef FUNNY_DIAG_H
#define FUNNY_DIAG_H

#include <stdbool.h>
#include <stdio.h>

/* ObjError is a typedef'd anonymous struct, so it can't be forward-
   declared the way a named one could -- error.h has to come in whole. */
#include "error.h"

typedef struct {
    bool serious; /* professional wording: --serious, or FUNNY_SERIOUS=1 */
    bool color;   /* ANSI colour */
} DiagOptions;

/* The defaults before any command-line flag is applied: `serious` from
   FUNNY_SERIOUS=1, `color` on only when stdout is a terminal. */
DiagOptions diag_default_options(void);

/* Writes the rendered diagnostic (trailing newline included) to `out`.
 *
 * The source snippet is read from the file `err` names, when that file is
 * still readable -- which is how a native run of a `.funnyc` still shows
 * the caret line, since the compiled unit carries the original path. When
 * it isn't readable the snippet is skipped and everything else renders
 * unchanged, which is also exactly what the Python VM prints when *it* is
 * running a `.funnyc` and has no `source` to show.
 */
void diag_render_error(FILE *out, const ObjError *err, DiagOptions opts);

#endif /* FUNNY_DIAG_H */
