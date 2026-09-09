/* native/builtins.h -- NATIVE_PLAN.md N5 task 1 (native-function
 * registration) + the always-in-scope globals (funnylang/stdlib/
 * builtins.py, N5 task 2's first module). `ObjNativeFn` (vm.h) is the
 * registration mechanism itself; this file is its first, and largest so
 * far, single consumer.
 *
 * `ObjCombo` is combo()'s own return value -- a native function that
 * closes over a list of callables (composing them). No existing native
 * calling convention (ObjBoundNative's receiver, ObjNativeFn's plain
 * args) can express "a native function with its own captured state," so
 * this is a small, dedicated Obj type rather than a generalization of
 * NativeMethodFn's signature for the sake of this one function.
 */
#ifndef FUNNY_BUILTINS_H
#define FUNNY_BUILTINS_H

#include "object.h"
#include "value.h"

typedef struct {
    Obj obj;
    Value fns; /* an ObjStash of the composed callables, captured at combo() time */
} ObjCombo;

struct GC;
struct VM;
ObjCombo *combo_new(struct GC *gc, Value fns);
/* Threads `arg` (GHOST if the combo itself was called with none) through
   every captured function in order, feeding each one's result into the
   next. On error, returns GHOST_VAL with vm->hadError set, same
   convention as every other native-callback-taking function in this
   codebase. */
Value combo_call(struct VM *vm, ObjCombo *combo, Value arg);

/* Registers every funnylang/stdlib/builtins.py name into vm->builtins. */
void builtins_install(struct VM *vm);

#endif /* FUNNY_BUILTINS_H */
