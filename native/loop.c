/* native/loop.c -- see loop.h. */
#include "loop.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "gc.h"
#include "interns.h"
#include "otw.h"
#include "task.h"

/* vm.c's, exported for exactly this. */
VmResult vm_resume_task(VM *vm, Task *t, Value *resultOut);

/* The first task that can run, in creation order. Deterministic on purpose:
   §5 rules out a golden that depends on which thread got there first, and a
   scheduler that picked at random would make the *language's* own ordering
   unassertable too. Creation order also reads the way people expect -- the
   first thing you started is the first thing that gets a turn. */
static Task *pick_ready(VM *vm) {
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t != NULL && t->state == TASK_READY) return t;
    }
    return NULL;
}

static bool any_waiting(VM *vm) {
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t != NULL && t->state == TASK_WAITING) return true;
    }
    return false;
}

/* A task's turn ended. Settle its `otw` with whatever it produced, so
   everyone awaiting it can move. */
static void finish_task(VM *vm, Task *t, VmResult result, Value value) {
    t->state = TASK_DONE;
    if (t->result != NULL) {
        if (result == VM_ERROR) {
            otw_reject(t->result, vm->pendingError);
            /* Consumed: the error now belongs to the `otw`, and whoever
               awaits it re-raises it there. An async function whose error
               nobody ever looks at is a `LeftOnRead` below, not a crash
               here -- the task that failed is not the task that asked. */
            vm->hadError = false;
            vm->pendingError = GHOST_VAL;
        } else {
            otw_fulfill(t->result, value);
        }
    }
    /* Its arrays go in reap_done, not here: `t` is still the running task
       at this point, and freeing the arrays the VM's own fields are pointing
       at is exactly the use-after-free this whole design is careful about.
       The Task itself always survives until the VM does -- something may
       still be holding its `otw`, and task_mark has to keep marking it. */
}

/* Frees the arrays of every finished task except the one that is switched in.
   Run at the top of each turn, so the task that finished last turn is reaped
   as soon as the loop moves on from it. */
static void reap_done(VM *vm) {
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t != NULL && t->state == TASK_DONE && t != vm->currentTask) vm_task_retire(vm, t);
    }
}

/* Anything that is waiting and can now stop waiting? Returns true if at least
   one task moved to READY.
 *
 * Two passes, and the order matters. The first is free -- an `otw` that has
 * already settled (another task fulfilled it, or a worker was collected on
 * somebody else's behalf) needs nothing but noticing. Only when nothing at
 * all can move does the second pass block, and it blocks on the *first*
 * waiter in task order rather than on whichever worker happens to finish
 * first, so the ordering a program observes does not depend on the machine
 * it is running on. */
static bool wake_waiters(VM *vm) {
    bool moved = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING) continue;
        if (t->awaiting == NULL || t->awaiting->state != OTW_PENDING) {
            t->state = TASK_READY;
            t->awaiting = NULL;
            moved = true;
        }
    }
    if (moved) return true;

    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        if (!interns_has_worker(vm, t->awaiting)) continue;
        /* Blocks. Nothing else in this VM can run, so there is nothing to
           lose by waiting for the thread that can. */
        interns_collect(vm, t->awaiting);
        t->state = TASK_READY;
        t->awaiting = NULL;
        return true;
    }
    return false;
}

/* Nothing is ready and nothing can be woken: every remaining waiter is
   waiting on an `otw` that will never settle. Tell each of them so, in its
   own task, so the error surfaces at the `await_fr` that is stuck rather than
   as a message about the program in general. */
static void strand_waiters(VM *vm) {
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjError *e = error_new(&vm->gc, "LeftOnRead",
                                "this otw is never going to settle -- nothing is working on it and nothing "
                                "else can run.",
                                NULL, NULL, 0, 0, NULL, GHOST_VAL, NULL, 0);
        otw_reject(t->awaiting, OBJ_VAL(e));
        t->state = TASK_READY;
        t->awaiting = NULL;
    }
}

/* An `otw` that a task *rejected* and nobody ever awaited.
 *
 * A fulfilled one nobody looked at is fine -- starting work you do not need
 * the answer to is a legitimate thing to do, and §1's own example leaves one
 * behind. A rejected one is different: it is an error thrown into the void,
 * and §3.4's "no silent truncation" rule says that must not vanish.
 *
 * Reported as the program's own `LeftOnRead`, with the dropped error's
 * flavor, message and position written into the text -- a diagnostic that
 * says only "left on read" and leaves you to go looking is barely better than
 * no diagnostic at all. It goes in as the error's *roast* as well as its
 * message, because §4.2 renders the roast by default and the message only
 * under `--serious`, and this detail is the whole point of the report.
 *
 * Returns true if one was found; `buf` then holds the text. */
static bool describe_dropped_rejection(VM *vm, char *buf, size_t bufLen) {
    int dropped = 0;
    ObjError *first = NULL;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->result == NULL) continue;
        if (t->result->state != OTW_REJECTED || t->result->awaited) continue;
        dropped++;
        t->result->awaited = true; /* said once */
        if (first != NULL) continue;
        if (IS_OBJ(t->result->error) && AS_OBJ(t->result->error)->type == OBJ_ERROR) {
            first = (ObjError *)AS_OBJ(t->result->error);
        }
    }
    if (dropped == 0) return false;

    char where[256];
    where[0] = '\0';
    if (first != NULL && first->line > 0) {
        const char *file = first->file->byteLen > 0 ? first->file->chars : "somewhere";
        snprintf(where, sizeof where, " at %s:%u", file, first->line);
    }
    char also[64];
    also[0] = '\0';
    if (dropped > 1) snprintf(also, sizeof also, " (and %d more like it)", dropped - 1);

    snprintf(buf, bufLen, "an async_ngl bet fell over%s and nobody ever awaited it -- %s: %s%s", where,
             first != NULL ? first->flavor->chars : "error",
             first != NULL ? first->message->chars : "it fell over.", also);
    return true;
}

VmResult loop_run(VM *vm, Task *entry, Value *resultOut) {
    VmResult entryResult = VM_OK;
    Value entryValue = GHOST_VAL;
    bool entryFinished = false;

    for (;;) {
        reap_done(vm);
        Task *t = pick_ready(vm);
        if (t == NULL && vm->currentTask != NULL && vm->currentTask->state == TASK_RUNNING) {
            t = vm->currentTask; /* the very first turn: entry is already in */
        }

        if (t != NULL) {
            vm_task_switch(vm, t);
            Value value = GHOST_VAL;
            VmResult r = vm_resume_task(vm, t, &value);
            if (r == VM_SUSPENDED) continue;
            if (t == entry) {
                entryResult = r;
                entryValue = value;
                entryFinished = true;
                /* Task zero's error is the *program's* error: it is not
                   settled into an `otw` and swallowed, it propagates. */
                t->state = TASK_DONE;
                if (r == VM_ERROR) break; /* an uncaught error stops everything */
            } else {
                finish_task(vm, t, r, value);
            }
            continue;
        }

        if (any_waiting(vm)) {
            if (wake_waiters(vm)) continue;
            strand_waiters(vm);
            continue;
        }
        break;
    }

    if (!entryFinished) {
        /* Only reachable if task zero itself was stranded, which
           strand_waiters makes impossible -- it hands every waiter an error
           to raise rather than leaving it parked. Belt and braces. */
        entryResult = VM_ERROR;
    }
    char dropped[640];
    if (entryResult != VM_ERROR && describe_dropped_rejection(vm, dropped, sizeof dropped)) {
        /* A fresh error rather than the dropped one itself: that one's stack
           of shame belongs to a task that is long gone, and printing it under
           the program's last line would say the failure happened somewhere it
           did not. The message carries the part that is actually useful. */
        entryResult = VM_ERROR;
        ObjError *e = error_new(&vm->gc, "LeftOnRead", dropped, dropped,
                                "await_fr it, or wrap its body in sketchy/my_bad if you don't care how it goes.", 0, 0,
                                NULL, GHOST_VAL, NULL, 0);
        vm->pendingError = OBJ_VAL(e);
        vm->hadError = true;
    }
    if (resultOut != NULL) *resultOut = entryValue;
    return entryResult;
}
