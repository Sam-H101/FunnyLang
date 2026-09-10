/* native/task.c -- see task.h. */
#include "task.h"

#include <stdlib.h>

#include "gc.h"
#include "vm.h"

/* Same starting sizes vm_init has always used for task zero. A task made by
   an `async_ngl` call rarely needs 256 slots, but sizing it down would be a
   guess, and a guess that is wrong costs a realloc on the first deep call. */
#define TASK_STACK_CAPACITY 256
#define TASK_FRAMES_CAPACITY 64
#define TASK_UPVALUES_CAPACITY 8

Task *task_new(int id) {
    Task *t = (Task *)calloc(1, sizeof(Task));
    t->id = id;
    t->state = TASK_READY;

    t->stackCapacity = TASK_STACK_CAPACITY;
    t->stack = (Value *)malloc((size_t)t->stackCapacity * sizeof(Value));
    t->stackCount = 0;

    t->frameCapacity = TASK_FRAMES_CAPACITY;
    t->frames = (Frame *)malloc((size_t)t->frameCapacity * sizeof(Frame));
    t->frameCount = 0;

    t->openUpvalueCapacity = TASK_UPVALUES_CAPACITY;
    t->openUpvalues = (ObjUpvalue **)malloc((size_t)t->openUpvalueCapacity * sizeof(ObjUpvalue *));
    t->openUpvalueCount = 0;

    t->currentFrameIndex = -1;
    t->currentInstrStart = 0;
    t->hadError = false;
    t->pendingError = GHOST_VAL;
    t->unit = NULL;
    t->currentModuleName = NULL;
    return t;
}

void task_free(Task *t) {
    if (t == NULL) return;
    for (int i = 0; i < t->frameCount; i++) frame_destroy(&t->frames[i]);
    free(t->stack);
    free(t->frames);
    free(t->openUpvalues);
    free(t);
}

void task_save(VM *vm, Task *t) {
    t->stack = vm->stack;
    t->stackCount = vm->stackCount;
    t->stackCapacity = vm->stackCapacity;
    t->frames = vm->frames;
    t->frameCount = vm->frameCount;
    t->frameCapacity = vm->frameCapacity;
    t->openUpvalues = vm->openUpvalues;
    t->openUpvalueCount = vm->openUpvalueCount;
    t->openUpvalueCapacity = vm->openUpvalueCapacity;
    t->currentFrameIndex = vm->currentFrameIndex;
    t->currentInstrStart = vm->currentInstrStart;
    t->hadError = vm->hadError;
    t->pendingError = vm->pendingError;
    t->unit = vm->unit;
    t->currentModuleName = vm->currentModuleName;
}

void task_restore(VM *vm, Task *t) {
    vm->stack = t->stack;
    vm->stackCount = t->stackCount;
    vm->stackCapacity = t->stackCapacity;
    vm->frames = t->frames;
    vm->frameCount = t->frameCount;
    vm->frameCapacity = t->frameCapacity;
    vm->openUpvalues = t->openUpvalues;
    vm->openUpvalueCount = t->openUpvalueCount;
    vm->openUpvalueCapacity = t->openUpvalueCapacity;
    vm->currentFrameIndex = t->currentFrameIndex;
    vm->currentInstrStart = t->currentInstrStart;
    vm->hadError = t->hadError;
    vm->pendingError = t->pendingError;
    vm->unit = t->unit;
    vm->currentModuleName = t->currentModuleName;
}

void task_mark(GC *gc, Task *t) {
    for (int i = 0; i < t->stackCount; i++) gc_mark_value(gc, t->stack[i]);
    for (int i = 0; i < t->frameCount; i++) gc_mark_object(gc, (Obj *)t->frames[i].closure);
    for (int i = 0; i < t->openUpvalueCount; i++) gc_mark_object(gc, (Obj *)t->openUpvalues[i]);
    gc_mark_value(gc, t->pendingError);
    /* Its unit's constants: a suspended task's next instruction may be a
       CONST out of a module the running task has never heard of. The running
       task's own unit is rooted by mark_vm_roots, and a bundle's modules are
       rooted through pakModuleCache, but a `.funnyc` run has only vm->unit --
       so a task suspended in one module while another runs would otherwise
       have its constants swept. */
    if (t->unit != NULL) {
        for (uint32_t i = 0; i < t->unit->constCount; i++) gc_mark_value(gc, t->unit->consts[i].value);
    }
}
