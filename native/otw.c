/* native/otw.c -- see otw.h. */
#include "otw.h"

#include <stdlib.h>

#include "gc.h"
#include "platform.h"

static ObjOtw *alloc_otw(GC *gc) {
    ObjOtw *p = (ObjOtw *)malloc(sizeof(ObjOtw));
    p->obj.type = OBJ_OTW;
    p->obj.marked = false;
    p->obj.size = 0;
    p->obj.next = NULL;
    p->state = OTW_PENDING;
    p->value = GHOST_VAL;
    p->error = GHOST_VAL;
    p->internId = 0;
    p->isTimer = false;
    p->dueAt = 0.0;
    p->waitSocket = PLATFORM_SOCKET_NONE;
    p->awaited = false;
    gc_track(gc, (Obj *)p, sizeof(ObjOtw));
    return p;
}

ObjOtw *otw_new(GC *gc) { return alloc_otw(gc); }

ObjOtw *otw_for_intern(GC *gc, int internId) {
    ObjOtw *p = alloc_otw(gc);
    p->internId = internId;
    return p;
}

ObjOtw *otw_for_timer(GC *gc, double dueAt) {
    ObjOtw *p = alloc_otw(gc);
    p->isTimer = true;
    p->dueAt = dueAt;
    return p;
}

ObjOtw *otw_for_socket(GC *gc, int64_t sock, double deadline) {
    ObjOtw *p = alloc_otw(gc);
    p->waitSocket = sock;
    p->dueAt = deadline;
    return p;
}

ObjOtw *otw_done(GC *gc, Value v) {
    ObjOtw *p = alloc_otw(gc);
    p->state = OTW_FULFILLED;
    p->value = v;
    return p;
}

void otw_fulfill(ObjOtw *p, Value v) {
    if (p->state != OTW_PENDING) return;
    p->state = OTW_FULFILLED;
    p->value = v;
    p->internId = 0;
    p->waitSocket = PLATFORM_SOCKET_NONE;
}

void otw_reject(ObjOtw *p, Value err) {
    if (p->state != OTW_PENDING) return;
    p->state = OTW_REJECTED;
    p->error = err;
    p->internId = 0;
    p->waitSocket = PLATFORM_SOCKET_NONE;
}
