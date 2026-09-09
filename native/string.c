#include "string.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"

ObjString *string_new(GC *gc, const char *chars, uint32_t len) {
    ObjString *s = (ObjString *)malloc(sizeof(ObjString));
    s->obj.type = OBJ_STRING;
    s->obj.marked = false;
    s->obj.size = 0;
    s->obj.next = NULL;
    s->chars = (char *)malloc((size_t)len + 1);
    memcpy(s->chars, chars, len);
    s->chars[len] = '\0';
    s->byteLen = len;
    gc_track(gc, (Obj *)s, sizeof(ObjString) + (size_t)len + 1);
    return s;
}

bool string_equal(const ObjString *a, const ObjString *b) {
    if (a == b) return true;
    if (a->byteLen != b->byteLen) return false;
    return memcmp(a->chars, b->chars, a->byteLen) == 0;
}
