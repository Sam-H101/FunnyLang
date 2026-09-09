/* native/computer.h -- NATIVE_PLAN.md N5 task 2 (module) + task 4
 * (`explode()`/`blue_screen()`'s exit-69/harmless constraints), ported
 * from funnylang/stdlib/computer.py: "the harmless joke module". No
 * subprocess, no filesystem writes, no real system calls beyond printing
 * and reading basic platform info through platform.h.
 */
#ifndef FUNNY_COMPUTER_H
#define FUNNY_COMPUTER_H

#include "value.h"

struct VM;

Value computer_build(struct VM *vm);

#endif /* FUNNY_COMPUTER_H */
