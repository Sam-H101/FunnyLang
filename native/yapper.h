/* native/yapper.h -- funnylang/stdlib/yapper.py: string utilities
 * (`gimme yapper`) plus the yapstring instance-method table (bound via
 * GET_PROP/INVOKE for a bare yapstring receiver, e.g. `"hi".SCREAM()`).
 * Every function here treats `a[0]` as "the string" uniformly, whether
 * it's called as a module function (`yapper.split(s, ",")`, no implicit
 * receiver -- `a[0]` is just the first explicit argument) or as an
 * instance method (`s.split(",")`, `a[0]` is the bound receiver) --
 * exactly matching how funnylang/stdlib/yapper.py's own Python functions
 * are shared, unchanged, between `build()`'s `members` dict and its
 * separate `YAPSTRING_METHODS` dict. So this file registers each
 * function once and reuses the same NativeMethodFn in both tables,
 * rather than writing two wrappers per operation.
 *
 * AGENT CHOICE, logged here and in NATIVE_PLAN.md §9: case conversion
 * (SCREAM/whisper/title_case/sarcasm_case) and letter/alnum
 * classification (is_letter/is_alnum) are ASCII-only -- real Unicode
 * case-mapping/category tables (`unicode_tbl.c`, NATIVE_PLAN.md task 2)
 * don't exist yet, deferred from N4. Non-ASCII bytes pass through
 * unchanged for case conversion, and count as "not a letter"/"not
 * alnum" for classification -- correct for ASCII, a known, narrower-
 * than-Python gap otherwise.
 *
 * AGENT CHOICE: `format` supports only positional `{}`/`{N}` placeholders
 * (each filled in via to_display) -- not named fields or format specs
 * (`{:.2f}`), which would need reimplementing a real chunk of Python's
 * own format mini-language for one function, disproportionate to the
 * rest of this module.
 */
#ifndef FUNNY_YAPPER_H
#define FUNNY_YAPPER_H

#include "value.h"
#include "vm.h"

struct VM;

Value yapper_build(struct VM *vm);
NativeMethodFn yapstring_find_method(const char *name, int *outMinArity, int *outMaxArity);

#endif /* FUNNY_YAPPER_H */
