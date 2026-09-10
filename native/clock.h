/* native/clock.h -- NATIVE_PLAN.md N5 task 2: `clock`, ported from
 * funnylang/stdlib/clock.py. All real timing goes through platform.h
 * (ARCHITECTURE.md's platform boundary lists timing alongside
 * filesystem access, sockets, and dlopen); this file only knows
 * platform.h's neutral interface.
 */
#ifndef FUNNY_CLOCK_H
#define FUNNY_CLOCK_H

#include "value.h"

struct VM;

Value clock_build(struct VM *vm);

#endif /* FUNNY_CLOCK_H */
