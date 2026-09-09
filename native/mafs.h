/* native/mafs.h -- funnylang/stdlib/mafs.py, the math module: 25 free
 * functions + 5 constants (`gimme mafs`) plus 6 numba instance methods
 * (`(5.5).floor()`, bound via GET_PROP for any bare int/float/bignum --
 * never boolski, which isn't a numba per PLAN.md §16).
 */
#ifndef FUNNY_MAFS_H
#define FUNNY_MAFS_H

#include "value.h"
#include "vm.h"

struct GC;
struct VM;

/* Builds a fresh mafs Module (its members never change afterward, so one
   per `gimme mafs` is wasteful but harmless -- matches funnylang/vm.py's
   own `_build`/`get_stdlib_module`, which rebuilds on every import too). */
Value mafs_build(struct VM *vm);

/* Numba's own instance methods -- same NativeMethodFn table convention
   as stash/groupchat/pointa's (native/vm.c's vm_get_prop/do_invoke). */
NativeMethodFn numba_find_method(const char *name, int *outMinArity, int *outMaxArity);

#endif /* FUNNY_MAFS_H */
