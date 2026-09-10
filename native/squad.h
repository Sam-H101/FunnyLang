/* native/squad.h -- NATIVE_PLAN.md N4 task 6: `squad`/`spawn`/`me`/`og`,
 * ported structurally from funnylang/values.py's Squad/Instance/BoundMethod
 * and funnylang/vm.py's SQUAD/METHOD/INHERIT handling + `_construct`.
 *
 * `me` needs no special representation at all: compiler.py's own
 * `_compile_closure(..., is_method=True)` reserves local slot 0 for it and
 * bumps arity by one, so a method's ObjClosure is just an ordinary
 * ObjClosure -- the *same* CALL/vm_call_value paths N3 already built
 * handle it, receiver-as-first-argument, with zero special-casing. Only
 * three new pieces are genuinely new: a squad's method table (with
 * superclass-chain lookup for `find_method`), an instance's field table,
 * and `ObjBoundMethod` (a receiver+ObjClosure pair, for when a method is
 * read off an instance as a value rather than immediately invoked --
 * `yo bump = counter.bump` needs this to make `bump()` later still work).
 *
 * AGENT CHOICE: methods/fields are plain linear-scan arrays, keyed by
 * ObjString content (`string_equal`), not `table.c`'s real hash table --
 * same reasoning as N2's globals table and N4's GroupChat (NATIVE_PLAN.md
 * §9): correctness first, and a squad's method count or an instance's
 * field count is never large enough for this to matter.
 *
 * funnylang/values.py's `Squad.spawn` field is write-only in the Python
 * VM (only ever assigned by the METHOD opcode, never read -- construction
 * and `og.spawn(...)` both go through `find_method("spawn")` instead, per
 * `_construct`'s own comment on exactly why). Left out here rather than
 * carried along unused.
 */
#ifndef FUNNY_SQUAD_H
#define FUNNY_SQUAD_H

#include "frames.h"
#include "object.h"
#include "da_string.h"
#include "value.h"

typedef struct {
    ObjString *name;
    ObjClosure *method;
} SquadMethodEntry;

struct ObjSquad {
    Obj obj;
    ObjString *name;
    ObjSquad *superclass; /* NULL for a root squad */
    SquadMethodEntry *methods;
    int methodCount;
    int methodCapacity;
};

typedef struct {
    ObjString *name;
    Value value;
} FieldEntry;

typedef struct {
    Obj obj;
    ObjSquad *squad;
    FieldEntry *fields;
    int fieldCount;
    int fieldCapacity;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

struct GC;

ObjSquad *squad_new(struct GC *gc, ObjString *name);
/* Overwrites any existing entry for `name` (a squad redefining a method
   under the same name -- not reachable from today's grammar, but cheap to
   get right). */
void squad_add_method(struct GC *gc, ObjSquad *squad, ObjString *name, ObjClosure *method);
/* Walks the superclass chain; NULL if `name` isn't found anywhere in it. */
ObjClosure *squad_find_method(ObjSquad *squad, const char *name);

ObjInstance *instance_new(struct GC *gc, ObjSquad *squad);
bool instance_get_field(const ObjInstance *inst, const char *name, Value *out);
/* Overwrites an existing field or adds a new one. */
void instance_set_field(struct GC *gc, ObjInstance *inst, ObjString *name, Value value);

ObjBoundMethod *bound_method_new(struct GC *gc, Value receiver, ObjClosure *method);

#endif /* FUNNY_SQUAD_H */
