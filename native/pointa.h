/* native/pointa.h -- PLAN.md §3.10 `pointa`: a safe reference to a place,
 * never a raw address. N3 (task 7) only needs the two kinds that don't
 * require a container object to exist first -- POINTA_CELL (a local or a
 * captured upvalue, boxed via the identical ObjUpvalue mechanism closures
 * use) and POINTA_GLOBAL. POINTA_INDEX/POINTA_PROP (a stash element,
 * groupchat key, or squad-instance field) need Stash/GroupChat/Instance,
 * which are N4's; PTR_INDEX/PTR_PROP land there.
 *
 * Mirrors funnylang/values.py's Pointa class; deref/set logic (which needs
 * VM-internal global lookup) lives in vm.c next to the DEREF/SET_DEREF
 * opcode handlers, not here -- this file is just the object shape and
 * construction.
 */
#ifndef FUNNY_POINTA_H
#define FUNNY_POINTA_H

#include "frames.h"
#include "object.h"
#include "string.h"

typedef enum {
    POINTA_CELL,
    POINTA_GLOBAL,
} PointaKind;

typedef struct VM VM;

typedef struct {
    Obj obj;
    PointaKind kind;
    ObjString *label; /* what .where()/to_yap show */
    ObjUpvalue *cell;    /* POINTA_CELL */
    VM *vm;              /* POINTA_GLOBAL: re-look-up by name each access,
                            same reasoning as ObjUpvalue -- vm->globals can
                            realloc, so no raw GlobalEntry* is stored */
    ObjString *globalName; /* POINTA_GLOBAL */
} ObjPointa;

struct GC;
ObjPointa *pointa_new_cell(struct GC *gc, ObjUpvalue *cell, ObjString *label);
ObjPointa *pointa_new_global(struct GC *gc, VM *vm, ObjString *globalName);

#endif /* FUNNY_POINTA_H */
