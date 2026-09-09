/* native/sus.h -- NATIVE_PLAN.md N5 task 2: `sus`, ported from
 * funnylang/stdlib/sus.py (reflection / debug helpers).
 */
#ifndef FUNNY_SUS_H
#define FUNNY_SUS_H

#include "value.h"

struct VM;

Value sus_build(struct VM *vm);

#endif /* FUNNY_SUS_H */
