/* native/filez.h -- NATIVE_PLAN.md N5 task 2: `filez`, ported from
 * funnylang/stdlib/filez.py. All OS filesystem access goes through
 * platform.c (ARCHITECTURE.md's platform boundary); this file only
 * knows platform.h's neutral interface, plus pure string manipulation
 * for the four path-string functions (join_path/dir_of/base_of/ext_of)
 * that never touch the filesystem in Python either.
 */
#ifndef FUNNY_FILEZ_H
#define FUNNY_FILEZ_H

#include "value.h"

struct VM;

Value filez_build(struct VM *vm);

#endif /* FUNNY_FILEZ_H */
