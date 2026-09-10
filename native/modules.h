/* native/modules.h -- NATIVE_PLAN.md N5 task 3: `Module` (funnylang/
 * values.py's own, `{name, members}`) plus the IMPORT opcode's own
 * dispatch. Only mode 2 (`gimme modulename`, a stdlib module by name) is
 * wired up here -- file-based `gimme "path.funny"` (modes 0/1) needs
 * either a native compiler (N8) or `.funnypak` bundle loading, neither of
 * which exist yet, so it gets the same clean "modules aren't wired up
 * yet" WhoDis funnylang/vm.py's own un-wired `_do_import` fallback raises,
 * rather than silently misbehaving.
 */
#ifndef FUNNY_MODULES_H
#define FUNNY_MODULES_H

#include "object.h"
#include "da_string.h"
#include "value.h"

typedef struct {
    Obj obj;
    ObjString *name;
    Value members; /* an OBJ_VAL(ObjGroupChat*), string-keyed */
} ObjModule;

struct GC;
struct VM;

ObjModule *module_new(struct GC *gc, ObjString *name, Value members);

/* IMPORT's own dispatch (vm.c's OP_IMPORT case). On failure, returns
   GHOST_VAL with vm->hadError set, same convention as every other
   VM-internal helper that can raise. */
Value do_import(struct VM *vm, const char *path, int mode);

#endif /* FUNNY_MODULES_H */
