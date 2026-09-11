#include "frames.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "task.h"
#include "vm.h"

/* The array `slot` indexes into. While this upvalue's task is the running
   one the VM's own array is the live one -- it may have been realloc'd since
   the task was last switched in, so the Task's copy of the pointer is stale.
   While it is suspended, the Task's array is the only one there is. */
static Value *live_stack(const ObjUpvalue *uv) {
    return uv->task == uv->vm->currentTask ? uv->vm->stack : uv->task->stack;
}

ObjUpvalue *upvalue_new(GC *gc, VM *vm, Task *task, int slot) {
    ObjUpvalue *uv = (ObjUpvalue *)malloc(sizeof(ObjUpvalue));
    uv->obj.type = OBJ_UPVALUE;
    uv->obj.marked = false;
    uv->obj.size = 0;
    uv->obj.next = NULL;
    uv->vm = vm;
    uv->task = task;
    uv->slot = slot;
    uv->closed = false;
    uv->closedValue = GHOST_VAL;
    gc_track(gc, (Obj *)uv, sizeof(ObjUpvalue));
    return uv;
}

Value upvalue_get(const ObjUpvalue *uv) {
    return uv->closed ? uv->closedValue : live_stack(uv)[uv->slot];
}

void upvalue_set(ObjUpvalue *uv, Value v) {
    if (uv->closed) {
        uv->closedValue = v;
    } else {
        live_stack(uv)[uv->slot] = v;
    }
}

void upvalue_close(ObjUpvalue *uv) {
    uv->closedValue = live_stack(uv)[uv->slot];
    uv->closed = true;
}

ObjClosure *closure_new(GC *gc, FunctionProto *proto, ObjUpvalue **upvalues, int upvalueCount,
                         Value moduleGlobals, Value moduleExports, CompiledUnit *unit) {
    ObjClosure *c = (ObjClosure *)malloc(sizeof(ObjClosure));
    c->obj.type = OBJ_CLOSURE;
    c->obj.marked = false;
    c->obj.size = 0;
    c->obj.next = NULL;
    c->proto = proto;
    c->upvalueCount = upvalueCount;
    if (upvalueCount > 0) {
        c->upvalues = (ObjUpvalue **)malloc((size_t)upvalueCount * sizeof(ObjUpvalue *));
        memcpy(c->upvalues, upvalues, (size_t)upvalueCount * sizeof(ObjUpvalue *));
    } else {
        c->upvalues = NULL;
    }
    c->homeSquad = NULL;
    c->unit = unit;
    c->moduleGlobals = moduleGlobals;
    c->moduleExports = moduleExports;
    gc_track(gc, (Obj *)c, sizeof(ObjClosure));
    return c;
}

/* -- frames --------------------------------------------------------------- */

#define INITIAL_HANDLER_CAPACITY 4

void frame_init(Frame *frame, ObjClosure *closure, int slotBase) {
    frame->closure = closure;
    frame->ip = 0;
    frame->slotBase = slotBase;
    frame->handlers = NULL;
    frame->handlerCount = 0;
    frame->handlerCapacity = 0;
}

void frame_destroy(Frame *frame) {
    free(frame->handlers);
    frame->handlers = NULL;
}

void frame_push_handler(Frame *frame, int handlerIp, int finallyIp, int stackDepth) {
    if (frame->handlerCount == frame->handlerCapacity) {
        int newCap = frame->handlerCapacity < INITIAL_HANDLER_CAPACITY ? INITIAL_HANDLER_CAPACITY : frame->handlerCapacity * 2;
        frame->handlers = (Handler *)realloc(frame->handlers, (size_t)newCap * sizeof(Handler));
        frame->handlerCapacity = newCap;
    }
    frame->handlers[frame->handlerCount].handlerIp = handlerIp;
    frame->handlers[frame->handlerCount].finallyIp = finallyIp;
    frame->handlers[frame->handlerCount].stackDepth = stackDepth;
    frame->handlerCount++;
}

Handler frame_pop_handler(Frame *frame) {
    return frame->handlers[--frame->handlerCount];
}
