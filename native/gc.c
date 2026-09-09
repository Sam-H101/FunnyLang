#include "gc.h"

#include <stdlib.h>

#include "bignum.h"
#include "string.h"

#define INITIAL_NEXT_GC (1024 * 1024) /* 1 MiB before the first collection */
#define GC_HEAP_GROW_FACTOR 2
#define INITIAL_TEMP_CAPACITY 8
#define INITIAL_GRAY_CAPACITY 8

void gc_init(GC *gc) {
    gc->objects = NULL;
    gc->bytesAllocated = 0;
    gc->nextGC = INITIAL_NEXT_GC;
    gc->stressMode = getenv("FUNNY_GC_STRESS") != NULL;

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
        case OBJ_STRING: {
            ObjString *s = (ObjString *)obj;
            free(s->chars);
            free(s);
            return;
        }
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
    (void)gc;
    switch (obj->type) {
        case OBJ_BIGNUM:
        case OBJ_STRING:
            return;
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
    if (gc->stressMode || gc->bytesAllocated > gc->nextGC) {
        gc_collect(gc);
    }
}
