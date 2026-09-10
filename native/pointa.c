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
    p->moduleGlobals = GHOST_VAL;
    p->globalName = NULL;
    p->container = GHOST_VAL;
    p->key = GHOST_VAL;
    gc_track(gc, (Obj *)p, sizeof(ObjPointa));
    return p;
}

ObjPointa *pointa_new_global(GC *gc, Value moduleGlobals, ObjString *globalName) {
    ObjPointa *p = (ObjPointa *)malloc(sizeof(ObjPointa));
    p->obj.type = OBJ_POINTA;
    p->obj.marked = false;
    p->obj.size = 0;
    p->obj.next = NULL;
    p->kind = POINTA_GLOBAL;
    p->label = globalName;
    p->cell = NULL;
    p->moduleGlobals = moduleGlobals;
    p->globalName = globalName;
    p->container = GHOST_VAL;
    p->key = GHOST_VAL;
    gc_track(gc, (Obj *)p, sizeof(ObjPointa));
    return p;
}

ObjPointa *pointa_new_place(GC *gc, PointaKind kind, Value container, Value key, ObjString *label) {
    ObjPointa *p = (ObjPointa *)malloc(sizeof(ObjPointa));
    p->obj.type = OBJ_POINTA;
    p->obj.marked = false;
    p->obj.size = 0;
    p->obj.next = NULL;
    p->kind = kind;
    p->label = label;
    p->cell = NULL;
    p->moduleGlobals = GHOST_VAL;
    p->globalName = NULL;
    p->container = container;
    p->key = key;
    gc_track(gc, (Obj *)p, sizeof(ObjPointa));
    return p;
}
