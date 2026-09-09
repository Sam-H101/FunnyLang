/* native/pointa.h -- PLAN.md §3.10 `pointa`: a safe reference to a place,
 * never a raw address. N3 (task 7) built the two kinds that don't require
 * a container object to exist first -- POINTA_CELL (a local or a captured
 * upvalue, boxed via the identical ObjUpvalue mechanism closures use) and
 * POINTA_GLOBAL. N4 (task 8) adds POINTA_INDEX (a stash/groupchat element)
 * and POINTA_PROP (a squad-instance field) now that Stash/GroupChat/
 * Instance exist -- both share the same container/key fields (mirroring
 * funnylang/values.py's own Pointa, which does exactly this), differing
 * only in what `key` means: a stash/groupchat key for INDEX, a field name
 * (as a yapstring Value) for PROP.
 *
 * Deref/set/arithmetic logic (which needs VM-internal helpers -- GET_INDEX/
 * GET_PROP/globals lookup) lives in vm.c next to the DEREF/SET_DEREF/PTR_*
 * opcode handlers, not here -- this file is just the object shape and
 * construction. Bounds/liveness are deliberately NOT checked here at
 * construction time (PTR_INDEX/PTR_PROP always succeed) -- only on read,
 * per PLAN.md §3.10 and NATIVE_PLAN.md's own note on this being the
 * milestone where "safe pointers" is either true or isn't.
 */
#ifndef FUNNY_POINTA_H
#define FUNNY_POINTA_H

#include "frames.h"
#include "object.h"
#include "string.h"
#include "value.h"

typedef enum {
    POINTA_CELL,
    POINTA_GLOBAL,
    POINTA_INDEX,
    POINTA_PROP,
} PointaKind;

typedef struct VM VM;

typedef struct {
    Obj obj;
    PointaKind kind;
    ObjString *label; /* what .where()/to_yap show for CELL/GLOBAL */
    ObjUpvalue *cell;    /* POINTA_CELL */
    VM *vm;              /* POINTA_GLOBAL: re-look-up by name each access,
                            same reasoning as ObjUpvalue -- vm->globals can
                            realloc, so no raw GlobalEntry* is stored */
    ObjString *globalName; /* POINTA_GLOBAL */
    Value container;        /* POINTA_INDEX/POINTA_PROP: the stash/groupchat/instance */
    Value key;               /* POINTA_INDEX: the stash/groupchat key.
                                 POINTA_PROP: the field name, as a yapstring
                                 Value (AS_STRING(key) recovers it) --
                                 .where() returns this `key` directly for
                                 both kinds, matching funnylang/vm.py's own
                                 _pointa_where. */
} ObjPointa;

struct GC;
ObjPointa *pointa_new_cell(struct GC *gc, ObjUpvalue *cell, ObjString *label);
ObjPointa *pointa_new_global(struct GC *gc, VM *vm, ObjString *globalName);
/* `kind` must be POINTA_INDEX or POINTA_PROP. */
ObjPointa *pointa_new_place(struct GC *gc, PointaKind kind, Value container, Value key, ObjString *label);

#endif /* FUNNY_POINTA_H */
