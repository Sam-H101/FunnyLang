#include "pointa.h"

#include <stdlib.h>

#include "gc.h"

ObjPointa *pointa_new_cell(GC *gc, ObjUpvalue *cell, ObjString *label) {
    ObjPointa *p = (ObjPointa *)malloc(sizeof(ObjPointa));
    p->obj.type = OBJ_POINTA;
    p->obj.marked = false;
    p->obj.size = 0;
    p->obj.next = NULL;
    p->kind = POINTA_CELL;
    p->label = label;
    p->cell = cell;
    p->vm = NULL;
    p->globalName = NULL;
    gc_track(gc, (Obj *)p, sizeof(ObjPointa));
    return p;
}

ObjPointa *pointa_new_global(GC *gc, VM *vm, ObjString *globalName) {
    ObjPointa *p = (ObjPointa *)malloc(sizeof(ObjPointa));
    p->obj.type = OBJ_POINTA;
    p->obj.marked = false;
    p->obj.size = 0;
    p->obj.next = NULL;
    p->kind = POINTA_GLOBAL;
    p->label = globalName;
    p->cell = NULL;
    p->vm = vm;
    p->globalName = globalName;
    gc_track(gc, (Obj *)p, sizeof(ObjPointa));
    return p;
}
