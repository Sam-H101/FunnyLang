#include "squad.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"

#define INITIAL_METHOD_CAPACITY 4
#define INITIAL_FIELD_CAPACITY 4

ObjSquad *squad_new(GC *gc, ObjString *name) {
    ObjSquad *s = (ObjSquad *)malloc(sizeof(ObjSquad));
    s->obj.type = OBJ_SQUAD;
    s->obj.marked = false;
    s->obj.size = 0;
    s->obj.next = NULL;
    s->name = name;
    s->superclass = NULL;
    s->methods = NULL;
    s->methodCount = 0;
    s->methodCapacity = 0;
    gc_track(gc, (Obj *)s, sizeof(ObjSquad));
    return s;
}

void squad_add_method(GC *gc, ObjSquad *squad, ObjString *name, ObjClosure *method) {
    (void)gc;
    for (int i = 0; i < squad->methodCount; i++) {
        if (string_equal(squad->methods[i].name, name)) {
            squad->methods[i].method = method;
            return;
        }
    }
    if (squad->methodCount == squad->methodCapacity) {
        squad->methodCapacity = squad->methodCapacity < INITIAL_METHOD_CAPACITY ? INITIAL_METHOD_CAPACITY : squad->methodCapacity * 2;
        squad->methods = (SquadMethodEntry *)realloc(squad->methods, (size_t)squad->methodCapacity * sizeof(SquadMethodEntry));
    }
    squad->methods[squad->methodCount].name = name;
    squad->methods[squad->methodCount].method = method;
    squad->methodCount++;
}

ObjClosure *squad_find_method(ObjSquad *squad, const char *name) {
    for (ObjSquad *s = squad; s != NULL; s = s->superclass) {
        for (int i = 0; i < s->methodCount; i++) {
            if (strcmp(s->methods[i].name->chars, name) == 0) return s->methods[i].method;
        }
    }
    return NULL;
}

ObjInstance *instance_new(GC *gc, ObjSquad *squad) {
    ObjInstance *inst = (ObjInstance *)malloc(sizeof(ObjInstance));
    inst->obj.type = OBJ_INSTANCE;
    inst->obj.marked = false;
    inst->obj.size = 0;
    inst->obj.next = NULL;
    inst->squad = squad;
    inst->fields = NULL;
    inst->fieldCount = 0;
    inst->fieldCapacity = 0;
    gc_track(gc, (Obj *)inst, sizeof(ObjInstance));
    return inst;
}

bool instance_get_field(const ObjInstance *inst, const char *name, Value *out) {
    for (int i = 0; i < inst->fieldCount; i++) {
        if (strcmp(inst->fields[i].name->chars, name) == 0) {
            *out = inst->fields[i].value;
            return true;
        }
    }
    return false;
}

void instance_set_field(GC *gc, ObjInstance *inst, ObjString *name, Value value) {
    (void)gc;
    for (int i = 0; i < inst->fieldCount; i++) {
        if (string_equal(inst->fields[i].name, name)) {
            inst->fields[i].value = value;
            return;
        }
    }
    if (inst->fieldCount == inst->fieldCapacity) {
        inst->fieldCapacity = inst->fieldCapacity < INITIAL_FIELD_CAPACITY ? INITIAL_FIELD_CAPACITY : inst->fieldCapacity * 2;
        inst->fields = (FieldEntry *)realloc(inst->fields, (size_t)inst->fieldCapacity * sizeof(FieldEntry));
    }
    inst->fields[inst->fieldCount].name = name;
    inst->fields[inst->fieldCount].value = value;
    inst->fieldCount++;
}

ObjBoundMethod *bound_method_new(GC *gc, Value receiver, ObjClosure *method) {
    ObjBoundMethod *bm = (ObjBoundMethod *)malloc(sizeof(ObjBoundMethod));
    bm->obj.type = OBJ_BOUND_METHOD;
    bm->obj.marked = false;
    bm->obj.size = 0;
    bm->obj.next = NULL;
    bm->receiver = receiver;
    bm->method = method;
    gc_track(gc, (Obj *)bm, sizeof(ObjBoundMethod));
    return bm;
}
