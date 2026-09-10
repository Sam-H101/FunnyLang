/* native/stash.h -- NATIVE_PLAN.md N4 task 3: `stash`, a dynamic array with
 * reference semantics. Method bodies port funnylang/stdlib/stash.py's own
 * functions close to mechanically -- args[0] is always the receiver, same
 * convention as ObjBoundNative (vm.h). Only the 21 entries in stash.py's
 * own `METHODS` dict (the instance-method table `.method()` syntax uses)
 * live here; the extra free-function-only entries (sort_by, group_by,
 * unique, flatten, chunk, sum_up, shuffle_it) are stdlib module functions,
 * N5's job once `gimme stash` exists.
 */
#ifndef FUNNY_STASH_H
#define FUNNY_STASH_H

#include "object.h"
#include "value.h"
#include "vm.h"

typedef struct {
    Obj obj;
    Value *items;
    int count;
    int capacity;
} ObjStash;

struct GC;

/* Copies `count` items from `items` into a new, GC-tracked ObjStash
   (NULL/0 for an empty stash). */
ObjStash *stash_new(struct GC *gc, const Value *items, int count);
void stash_push(struct GC *gc, ObjStash *s, Value v);

/* Looks up an instance method by name; returns NULL if `name` isn't one of
   stash.py's 21. `outMinArity`/`outMaxArity` are the *extra* args beyond
   the receiver. */
NativeMethodFn stash_find_method(const char *name, int *outMinArity, int *outMaxArity);

/* `gimme stash`'s own Module: every one of the 21 methods above, plus
   the 7 free-function-only extras (sort_by/group_by/unique/flatten/
   chunk/sum_up/shuffle_it) that funnylang/stdlib/stash.py's own build()
   exposes -- N5 task 2. */
Value stash_build(VM *vm);

#endif /* FUNNY_STASH_H */
