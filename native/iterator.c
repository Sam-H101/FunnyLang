#include "iterator.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"

ObjIterator *iterator_new(GC *gc, const Value *items, int count) {
    ObjIterator *it = (ObjIterator *)malloc(sizeof(ObjIterator));
    it->obj.type = OBJ_ITERATOR;
    it->obj.marked = false;
    it->obj.size = 0;
    it->obj.next = NULL;
    it->count = count;
    it->pos = 0;
    it->items = count > 0 ? (Value *)malloc((size_t)count * sizeof(Value)) : NULL;
    if (count > 0) memcpy(it->items, items, (size_t)count * sizeof(Value));
    gc_track(gc, (Obj *)it, sizeof(ObjIterator));
    return it;
}
