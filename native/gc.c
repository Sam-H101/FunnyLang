#include "gc.h"

#include <stdlib.h>

#include "bignum.h"
#include "blob.h"
#include "error.h"
#include "frames.h"
#include "builtins.h"
#include "groupchat.h"
#include "iterator.h"
#include "modules.h"
#include "otw.h"
#include "pointa.h"
#include "squad.h"
#include "stash.h"
#include "da_string.h"
#include "vm.h"

#define INITIAL_NEXT_GC (1024 * 1024) /* 1 MiB before the first collection */
#define GC_HEAP_GROW_FACTOR 2
#define INITIAL_TEMP_CAPACITY 8
#define INITIAL_GRAY_CAPACITY 8

void gc_init(GC *gc) {
    gc->objects = NULL;
    gc->bytesAllocated = 0;
    gc->nextGC = INITIAL_NEXT_GC;
    /* Any non-empty value turns stress on, as it always did. A positive
       integer sets the period; anything else means 1, so FUNNY_GC_STRESS=1
       and FUNNY_GC_STRESS=yes both still collect on every allocation. */
    const char *stress = getenv("FUNNY_GC_STRESS");
    gc->stressPeriod = 0;
    gc->stressCounter = 0;
    if (stress != NULL && stress[0] != '\0') {
        long period = strtol(stress, NULL, 10);
        gc->stressPeriod = period > 0 ? (unsigned)period : 1u;
    }

    gc->grayStack = NULL;
    gc->grayCount = 0;
    gc->grayCapacity = 0;

    gc->tempRoots = NULL;
    gc->tempRootCount = 0;
    gc->tempRootCapacity = 0;

    gc->markExternalRoots = NULL;
    gc->externalRootsUserdata = NULL;
}

static void free_object(Obj *obj) {
    switch (obj->type) {
        case OBJ_BIGNUM:
            bignum_free((ObjBignum *)obj);
            return;
        case OBJ_BLOB: {
            ObjBlob *b = (ObjBlob *)obj;
            free(b->bytes);
            free(b);
            return;
        }
        case OBJ_STRING: {
            ObjString *s = (ObjString *)obj;
            free(s->chars);
            free(s);
            return;
        }
        case OBJ_UPVALUE:
            free(obj);
            return;
        case OBJ_CLOSURE: {
            ObjClosure *c = (ObjClosure *)obj;
            free(c->upvalues); /* the upvalues themselves are separate GC objects */
            free(c);
            return;
        }
        case OBJ_ERROR: {
            ObjError *e = (ObjError *)obj;
            for (int i = 0; i < e->rawTraceCount; i++) free(e->rawTrace[i]);
            free(e->rawTrace);
            free(e); /* flavor/message/roast/hint/file are separate GC objects */
            return;
        }
        case OBJ_POINTA:
            free(obj); /* label/cell/globalName are separate GC objects; vm is borrowed */
            return;
        case OBJ_OTW:
            /* value/error are Value fields -- separate GC objects, marked not
               freed. The worker behind a pending one is a VM-local id, not a
               pointer, and is joined by that VM's teardown. */
            free(obj);
            return;
        case OBJ_BOUND_NATIVE:
            free(obj); /* receiver is a Value field, name is a static string literal */
            return;
        case OBJ_STASH: {
            ObjStash *s = (ObjStash *)obj;
            free(s->items);
            free(s);
            return;
        }
        case OBJ_GROUPCHAT: {
            ObjGroupChat *g = (ObjGroupChat *)obj;
            free(g->entries);
            free(g->index);
            free(g);
            return;
        }
        case OBJ_ITERATOR: {
            ObjIterator *it = (ObjIterator *)obj;
            free(it->items);
            free(it);
            return;
        }
        case OBJ_SQUAD: {
            ObjSquad *s = (ObjSquad *)obj;
            free(s->methods);
            free(s);
            return;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance *)obj;
            free(inst->fields);
            free(inst);
            return;
        }
        case OBJ_BOUND_METHOD:
            free(obj); /* receiver is a Value field; method is a separate GC object */
            return;
        case OBJ_NATIVE_FN:
            free(obj); /* name points into a separately-GC-owned ObjString; never freed here */
            return;
        case OBJ_COMBO:
            free(obj); /* fns is a Value field (an ObjStash), a separate GC object */
            return;
        case OBJ_MODULE:
            free(obj); /* name/members are separate GC objects */
            return;
    }
}

void gc_free_all(GC *gc) {
    Obj *obj = gc->objects;
    while (obj != NULL) {
        Obj *next = obj->next;
        free_object(obj);
        obj = next;
    }
    gc->objects = NULL;
    gc->bytesAllocated = 0;
}

void gc_destroy(GC *gc) {
    gc_free_all(gc);
    free(gc->grayStack);
    free(gc->tempRoots);
    gc->grayStack = NULL;
    gc->tempRoots = NULL;
}

Obj *gc_track(GC *gc, Obj *obj, size_t size) {
    obj->size = size;
    obj->next = gc->objects;
    gc->objects = obj;
    gc->bytesAllocated += size;
    return obj;
}

void gc_push_temp(GC *gc, Value v) {
    if (gc->tempRootCount == gc->tempRootCapacity) {
        int newCap = gc->tempRootCapacity < INITIAL_TEMP_CAPACITY ? INITIAL_TEMP_CAPACITY : gc->tempRootCapacity * 2;
        gc->tempRoots = (Value *)realloc(gc->tempRoots, (size_t)newCap * sizeof(Value));
        gc->tempRootCapacity = newCap;
    }
    gc->tempRoots[gc->tempRootCount++] = v;
}

void gc_pop_temp(GC *gc) {
    gc->tempRootCount--;
}

int gc_temp_count(const GC *gc) {
    return gc->tempRootCount;
}

static void push_gray(GC *gc, Obj *obj) {
    if (gc->grayCount == gc->grayCapacity) {
        int newCap = gc->grayCapacity < INITIAL_GRAY_CAPACITY ? INITIAL_GRAY_CAPACITY : gc->grayCapacity * 2;
        gc->grayStack = (Obj **)realloc(gc->grayStack, (size_t)newCap * sizeof(Obj *));
        gc->grayCapacity = newCap;
    }
    gc->grayStack[gc->grayCount++] = obj;
}

void gc_mark_object(GC *gc, Obj *obj) {
    if (obj == NULL || obj->marked) return;
    obj->marked = true;
    push_gray(gc, obj);
}

void gc_mark_value(GC *gc, Value v) {
    if (IS_OBJ(v)) gc_mark_object(gc, AS_OBJ(v));
}

/* Marks every Value field a composite object holds, so the mark phase
   reaches its children too. OBJ_BIGNUM has none (its limbs are raw
   uint32_t, not Values) -- this switch grows a case per type as N2-N4 add
   Closure/Stash/GroupChat/Instance/Pointa/Upvalue, each with its own
   fields to walk. A `pointa`'s strong reference to its box/container
   (native/ARCHITECTURE.md's own note) is exactly the kind of edge that
   belongs here once it exists. */
static void blacken_object(GC *gc, Obj *obj) {
    switch (obj->type) {
        case OBJ_BIGNUM:
        case OBJ_BLOB:
        case OBJ_STRING:
            return;
        case OBJ_UPVALUE: {
            /* Marked unconditionally, open or closed: harmless when open
               (the value is also reachable through the live stack, and
               marking is idempotent) and the only path to it when closed. */
            ObjUpvalue *uv = (ObjUpvalue *)obj;
            gc_mark_value(gc, upvalue_get(uv));
            return;
        }
        case OBJ_CLOSURE: {
            ObjClosure *c = (ObjClosure *)obj;
            for (int i = 0; i < c->upvalueCount; i++) gc_mark_object(gc, (Obj *)c->upvalues[i]);
            gc_mark_object(gc, (Obj *)c->homeSquad);
            gc_mark_value(gc, c->moduleGlobals);
            gc_mark_value(gc, c->moduleExports);
            return;
        }
        case OBJ_OTW: {
            ObjOtw *p = (ObjOtw *)obj;
            gc_mark_value(gc, p->value);
            gc_mark_value(gc, p->error);
            return;
        }
        case OBJ_ERROR: {
            ObjError *e = (ObjError *)obj;
            gc_mark_object(gc, (Obj *)e->flavor);
            gc_mark_object(gc, (Obj *)e->message);
            gc_mark_object(gc, (Obj *)e->roast);
            if (e->hint) gc_mark_object(gc, (Obj *)e->hint); /* NULL when the error has no fix suggestion */
            gc_mark_object(gc, (Obj *)e->file);
            gc_mark_value(gc, e->payload);
            return;
        }
        case OBJ_POINTA: {
            /* A pointa holds a *strong* reference to whatever it addresses
               (native/ARCHITECTURE.md's own note on this exact point): a
               place reference that let its target get collected out from
               under it would be the dangling pointer this whole design
               exists to prevent. */
            ObjPointa *p = (ObjPointa *)obj;
            gc_mark_object(gc, (Obj *)p->label);
            if (p->kind == POINTA_CELL) gc_mark_object(gc, (Obj *)p->cell);
            if (p->kind == POINTA_GLOBAL) {
                gc_mark_object(gc, (Obj *)p->globalName);
                gc_mark_value(gc, p->moduleGlobals);
            }
            if (p->kind == POINTA_INDEX || p->kind == POINTA_PROP) {
                gc_mark_value(gc, p->container);
                gc_mark_value(gc, p->key);
            }
            return;
        }
        case OBJ_BOUND_NATIVE: {
            ObjBoundNative *bn = (ObjBoundNative *)obj;
            gc_mark_value(gc, bn->receiver);
            return;
        }
        case OBJ_STASH: {
            ObjStash *s = (ObjStash *)obj;
            for (int i = 0; i < s->count; i++) gc_mark_value(gc, s->items[i]);
            return;
        }
        case OBJ_GROUPCHAT: {
            ObjGroupChat *g = (ObjGroupChat *)obj;
            for (int i = 0; i < g->count; i++) {
                gc_mark_value(gc, g->entries[i].key);
                gc_mark_value(gc, g->entries[i].value);
            }
            return;
        }
        case OBJ_ITERATOR: {
            ObjIterator *it = (ObjIterator *)obj;
            for (int i = 0; i < it->count; i++) gc_mark_value(gc, it->items[i]);
            return;
        }
        case OBJ_SQUAD: {
            ObjSquad *s = (ObjSquad *)obj;
            gc_mark_object(gc, (Obj *)s->name);
            gc_mark_object(gc, (Obj *)s->superclass);
            for (int i = 0; i < s->methodCount; i++) {
                gc_mark_object(gc, (Obj *)s->methods[i].name);
                gc_mark_object(gc, (Obj *)s->methods[i].method);
            }
            return;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance *)obj;
            gc_mark_object(gc, (Obj *)inst->squad);
            for (int i = 0; i < inst->fieldCount; i++) {
                gc_mark_object(gc, (Obj *)inst->fields[i].name);
                gc_mark_value(gc, inst->fields[i].value);
            }
            return;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bm = (ObjBoundMethod *)obj;
            gc_mark_value(gc, bm->receiver);
            gc_mark_object(gc, (Obj *)bm->method);
            return;
        }
        case OBJ_NATIVE_FN:
            /* `name` is a raw char* into a separately-rooted ObjString
               (the same one vm->builtins' own GlobalEntry keys on) --
               nothing here to mark through it directly. */
            return;
        case OBJ_COMBO: {
            ObjCombo *c = (ObjCombo *)obj;
            gc_mark_value(gc, c->fns);
            return;
        }
        case OBJ_MODULE: {
            ObjModule *m = (ObjModule *)obj;
            gc_mark_object(gc, (Obj *)m->name);
            gc_mark_value(gc, m->members);
            return;
        }
    }
}

static void mark_roots(GC *gc) {
    for (int i = 0; i < gc->tempRootCount; i++) {
        gc_mark_value(gc, gc->tempRoots[i]);
    }
    if (gc->markExternalRoots != NULL) {
        gc->markExternalRoots(gc, gc->externalRootsUserdata);
    }
}

static void trace_references(GC *gc) {
    while (gc->grayCount > 0) {
        Obj *obj = gc->grayStack[--gc->grayCount];
        blacken_object(gc, obj);
    }
}

static void sweep(GC *gc) {
    Obj *prev = NULL;
    Obj *obj = gc->objects;
    while (obj != NULL) {
        if (obj->marked) {
            obj->marked = false; /* reset for the next cycle */
            prev = obj;
            obj = obj->next;
        } else {
            Obj *unreached = obj;
            obj = obj->next;
            if (prev == NULL) {
                gc->objects = obj;
            } else {
                prev->next = obj;
            }
            gc->bytesAllocated -= unreached->size;
            free_object(unreached);
        }
    }
}

void gc_collect(GC *gc) {
    mark_roots(gc);
    trace_references(gc);
    sweep(gc);
    gc->nextGC = gc->bytesAllocated * GC_HEAP_GROW_FACTOR;
    if (gc->nextGC < INITIAL_NEXT_GC) gc->nextGC = INITIAL_NEXT_GC;
}

void gc_maybe_collect(GC *gc) {
    if (gc->stressPeriod != 0 && ++gc->stressCounter >= gc->stressPeriod) {
        gc->stressCounter = 0;
        gc_collect(gc);
        return;
    }
    if (gc->bytesAllocated > gc->nextGC) {
        gc_collect(gc);
    }
}
