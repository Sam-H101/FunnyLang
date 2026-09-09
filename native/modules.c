#include "modules.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "mafs.h"
#include "vm.h"
#include "yapper.h"

ObjModule *module_new(GC *gc, ObjString *name, Value members) {
    ObjModule *m = (ObjModule *)malloc(sizeof(ObjModule));
    m->obj.type = OBJ_MODULE;
    m->obj.marked = false;
    m->obj.size = 0;
    m->obj.next = NULL;
    m->name = name;
    m->members = members;
    gc_track(gc, (Obj *)m, sizeof(ObjModule));
    return m;
}

/* The stdlib registry (funnylang/stdlib/__init__.py's own STDLIB_NAMES/
   _build): one entry per module `gimme modulename` can resolve. Grows as
   later N5 sub-phases port yapper/stash/groupchat's free functions/rizz/
   filez/clock/sus/computer/internet. */
typedef Value (*StdlibBuilderFn)(VM *vm);

typedef struct {
    const char *name;
    StdlibBuilderFn build;
} StdlibEntry;

static const StdlibEntry STDLIB_REGISTRY[] = {
    {"mafs", mafs_build},
    {"yapper", yapper_build},
};
#define STDLIB_REGISTRY_COUNT (int)(sizeof(STDLIB_REGISTRY) / sizeof(STDLIB_REGISTRY[0]))

Value do_import(VM *vm, const char *path, int mode) {
    if (mode == 2) {
        for (int i = 0; i < STDLIB_REGISTRY_COUNT; i++) {
            if (strcmp(STDLIB_REGISTRY[i].name, path) == 0) return STDLIB_REGISTRY[i].build(vm);
        }
        vm_throw_native(vm, "ImportSkillIssue", "no stdlib module named '%s'.", path);
        return GHOST_VAL;
    }
    /* File-based `gimme "path.funny"`: needs either a native compiler
       (N8) or .funnypak bundle loading (chunk.c doesn't have it yet) --
       matches funnylang/vm.py's own un-wired _do_import fallback exactly
       (no module_loader means this same WhoDis) rather than silently
       misbehaving. */
    vm_throw_native(vm, "WhoDis", "can't find '%s'. modules aren't wired up yet.", path);
    return GHOST_VAL;
}
