/* native/frames.h -- NATIVE_PLAN.md N3 tasks 1-2: call frames, closures,
 * and the open-upvalue mechanism, ported *structurally* from
 * funnylang/vm.py's Frame/Closure/Upvalue (the same Lua/clox model
 * PLAN.md §3.6 already cites) rather than reinvented.
 *
 * One real translation issue Python's version never has to think about:
 * an open Upvalue there just keeps a reference to `self.stack` (the list
 * object itself) plus an index, and Python list reallocation is invisible
 * to that reference. The VM's `stack` here is a realloc'd C array, so a
 * raw `Value*` into it would dangle the moment the stack grows. ObjUpvalue
 * instead keeps a back-pointer to the owning VM and re-reads `vm->stack`
 * fresh on every access (see frames.c) -- the same "reference the
 * container, index into it live" trick, just spelled differently in C.
 */
#ifndef FUNNY_FRAMES_H
#define FUNNY_FRAMES_H

#include <stdbool.h>

#include "chunk.h"
#include "gc.h"
#include "object.h"
#include "value.h"

typedef struct VM VM; /* defined in vm.h; only a pointer is needed here */
typedef struct ObjSquad ObjSquad; /* defined in squad.h; only a pointer is needed here */

typedef struct ObjUpvalue {
    Obj obj;
    VM *vm;   /* for locating the live stack while open */
    int slot; /* absolute stack index while open */
    bool closed;
    Value closedValue;
} ObjUpvalue;

ObjUpvalue *upvalue_new(GC *gc, VM *vm, int slot);
Value upvalue_get(const ObjUpvalue *uv);
void upvalue_set(ObjUpvalue *uv, Value v);
void upvalue_close(ObjUpvalue *uv);

typedef struct ObjClosureStruct {
    Obj obj;
    FunctionProto *proto; /* borrowed: owned by the CompiledUnit */
    ObjUpvalue **upvalues;
    int upvalueCount;
    /* Set by the METHOD opcode: which ObjSquad this closure was defined in
       (NULL for an ordinary function) -- NOT the receiver's runtime class.
       `og` resolves relative to *this*, matching funnylang/vm.py's own
       Closure.home_squad: a super-call from a middle class in a 3+ level
       hierarchy must reach the next class up, not re-invoke its own
       defining class's method forever. */
    ObjSquad *homeSquad;
    /* Per-module isolated namespaces (PLAN.md §3.8's "non-flexed names are
       private"), each an OBJ_VAL(ObjGroupChat*) -- both mirror
       funnylang/vm.py's Closure.module_globals/module_exports exactly,
       down to how they propagate: the entry closure of a freshly run
       module gets brand-new (empty) ones, and every closure the OP_CLOSURE
       opcode creates *inside* that module inherits these same two Values
       unchanged from its enclosing frame's own closure, so every closure
       in one module shares one pair of namespaces by reference. */
    Value moduleGlobals;
    Value moduleExports;
    /* The compiled unit this closure's code indexes into, for constants
       and nested protos. Carried per closure rather than per VM because a
       .funnypak run has several units live at once and a call can cross
       from one module into another mid-execution -- exactly why
       funnylang/vm.py's own Closure carries `const_pool`/`protos` instead
       of reading them off the VM. Inherited unchanged by every closure
       OP_CLOSURE builds, same as the two namespaces above. */
    CompiledUnit *unit;
} ObjClosure;

ObjClosure *closure_new(GC *gc, FunctionProto *proto, ObjUpvalue **upvalues, int upvalueCount,
                         Value moduleGlobals, Value moduleExports, CompiledUnit *unit);

/* -- call frames ------------------------------------------------------- */

#define HANDLER_ABSENT (-1)

typedef struct {
    int handlerIp; /* HANDLER_ABSENT if this try has no `my_bad` */
    int finallyIp; /* HANDLER_ABSENT if this try has no `regardless` */
    int stackDepth;
} Handler;

typedef struct {
    ObjClosure *closure;
    uint32_t ip;
    int slotBase;
    Handler *handlers;
    int handlerCount;
    int handlerCapacity;
} Frame;

void frame_init(Frame *frame, ObjClosure *closure, int slotBase);
void frame_destroy(Frame *frame);
void frame_push_handler(Frame *frame, int handlerIp, int finallyIp, int stackDepth);
Handler frame_pop_handler(Frame *frame);

#endif /* FUNNY_FRAMES_H */
