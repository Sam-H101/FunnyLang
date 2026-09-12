/* native/loop.c -- see loop.h. */
#include "loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "gc.h"
#include "interns.h"
#include "platform.h"
#include "status.h"
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

/* Every socket some task is parked on, gathered into one array so they can
   all be polled in a single call. That is the whole trick behind serving
   several callers at once on one thread: instead of each task sitting in its
   own blocking read, nobody blocks and the loop asks about all of them
   together. */
typedef struct {
    int64_t *handles;
    ObjOtw **owners;
    int count;
} SocketWaits;

static void gather_sockets(VM *vm, SocketWaits *out) {
    out->handles = NULL;
    out->owners = NULL;
    out->count = 0;
    int capacity = 0;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjOtw *p = t->awaiting;
        if (p->state != OTW_PENDING || p->waitSocket == PLATFORM_SOCKET_NONE) continue;
        if (out->count == capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            out->handles = (int64_t *)realloc(out->handles, (size_t)capacity * sizeof(int64_t));
            out->owners = (ObjOtw **)realloc(out->owners, (size_t)capacity * sizeof(ObjOtw *));
        }
        out->handles[out->count] = p->waitSocket;
        out->owners[out->count] = p;
        out->count++;
    }
}

static void free_sockets(SocketWaits *w) {
    free(w->handles);
    free(w->owners);
}

/* A socket wait settles `fax` when its socket has something to read and `cap`
   when its deadline passes first -- so the FunnyLang side reads like
   `sus (await_fr internet.hold_up(conn, 5000)) { ... }` and a client that
   connects and then says nothing cannot park a task forever. */
static bool settle_ready_sockets(VM *vm, double now) {

    SocketWaits w;
    gather_sockets(vm, &w);
    bool moved = false;

    if (w.count > 0) {
        unsigned char *ready = (unsigned char *)calloc((size_t)w.count, 1);
        /* Zero timeout: this pass only *notices*, it never waits. Waiting is
           the caller's decision, below, once it knows nothing else can run. */
        if (platform_poll_sockets(w.handles, w.count, 0, ready) > 0) {
            for (int i = 0; i < w.count; i++) {
                if (ready[i]) {
                    otw_fulfill(w.owners[i], BOOL_VAL(true));
                    moved = true;
                }
            }
        }
        free(ready);
    }

    for (int i = 0; i < w.count; i++) {
        ObjOtw *p = w.owners[i];
        if (p->state == OTW_PENDING && p->dueAt > 0.0 && p->dueAt <= now) {
            otw_fulfill(p, BOOL_VAL(false));
            moved = true;
        }
    }

    free_sockets(&w);
    return moved;
}

/* A mailbox wait settles with the next message in this VM's inbox, or with
   `ghost` when its deadline passes first -- the same shape as a socket wait,
   and for the same reason: the asking task waits, nothing else does. */
static bool settle_mailboxes(VM *vm, double now) {
    bool moved = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjOtw *p = t->awaiting;
        if (p->state != OTW_PENDING || !p->waitMailbox) continue;
        if (interns_settle_mailbox(vm, p)) {
            moved = true;
        } else if (p->dueAt > 0.0 && p->dueAt <= now) {
            otw_fulfill(p, GHOST_VAL);
            moved = true;
        }
    }
    return moved;
}

/* An interrupt settles every `otw` waiting for one, with `ghost`: what was
   asked for was the event. The flag is a latch, so a program that asks twice
   and awaits both gets both. */
static bool settle_interrupts(VM *vm) {
    bool moved = false;
    if (!platform_interrupt_seen()) return false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjOtw *p = t->awaiting;
        if (p->state == OTW_PENDING && p->waitInterrupt) {
            otw_fulfill(p, GHOST_VAL);
            moved = true;
        }
    }
    return moved;
}

/* A timer whose moment has come fulfils with `ghost`: what was asked for was
   the delay. */
static bool fire_due_timers(VM *vm, double now) {
    bool fired = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjOtw *p = t->awaiting;
        if (p->state == OTW_PENDING && p->isTimer && p->dueAt <= now) {
            otw_fulfill(p, GHOST_VAL);
            fired = true;
        }
    }
    return fired;
}

/* How long until the earliest deadline anything is waiting on, in
   milliseconds, or -1 if nothing has one. Covers both a `clock.chill` and a
   socket wait's timeout: the loop must not sleep past either. */
static int next_deadline_ms(VM *vm, double now) {
    double soonest = 0.0;
    bool any = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        ObjOtw *p = t->awaiting;
        if (p->state != OTW_PENDING || p->dueAt <= 0.0) continue;
        if (!any || p->dueAt < soonest) {
            soonest = p->dueAt;
            any = true;
        }
    }
    if (!any) return -1;
    double ms = (soonest - now) * 1000.0;
    if (ms < 0.0) ms = 0.0;
    if (ms > 60000.0) ms = 60000.0;
    return (int)ms;
}

/* When a worker thread and a socket are both outstanding, the loop cannot
   block on the condition variable and the sockets at the same time -- one
   call has to win. It polls the sockets, capped at this, and goes round
   again; a worker that finishes meanwhile is noticed within one cap.
   25ms is imperceptible next to anything a worker is hired to do, and the
   alternative (a self-pipe written by every finishing worker so one poll
   could cover both) is a lot of machinery for that 25ms. */
#define MIXED_WAIT_CAP_MS 25

/* Anything that is waiting and can now stop waiting? Returns true if at least
   one task moved to READY.
 *
 * The passes are in order of cost, and the order is what keeps the scheduling
 * a program observes from depending on the machine it is on. Noticing an
 * `otw` that has already settled is free. Firing a timer that is already due
 * is free. Polling sockets with a zero timeout is nearly free. Collecting a
 * worker that has already finished is a join that returns immediately. Only
 * when none of that moves anything does this block. */
static bool wake_waiters(VM *vm) {
    /* Whoever gets here first prints the whole table: a locked, tidy one,
       unlike the torn copy the signal handler writes for the case where
       nobody reaches a wait point at all (RUNTIME_PLAN.md R9). */
    if (platform_take_dump_request()) status_dump(vm->err);
    double now = platform_monotonic_seconds();
    bool moved = fire_due_timers(vm, now);
    if (settle_ready_sockets(vm, now)) moved = true;
    if (settle_mailboxes(vm, now)) moved = true;
    if (settle_interrupts(vm)) moved = true;

    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        if (t->awaiting->state == OTW_PENDING && interns_ready(vm, t->awaiting)) {
            interns_collect(vm, t->awaiting);
        }
    }
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

    /* Nothing has happened yet. Is anything still coming? */
    int deadlineMs = next_deadline_ms(vm, now);
    bool anyWorker = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        if (interns_has_worker(vm, t->awaiting)) {
            anyWorker = true;
            break;
        }
    }

    /* Nothing else can wake a Ctrl-C waiter -- there is no thread to finish
       and no socket to become readable -- so the wait is capped and the flag
       is read again next turn, at most one cap late. */
    bool anyInterrupt = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        if (t->awaiting->waitInterrupt) {
            anyInterrupt = true;
            break;
        }
    }

    bool anyMailbox = false;
    for (int i = 0; i < vm->taskCount; i++) {
        Task *t = vm->tasks[i];
        if (t == NULL || t->state != TASK_WAITING || t->awaiting == NULL) continue;
        if (t->awaiting->waitMailbox && interns_mail_possible(vm)) {
            anyMailbox = true;
            break;
        }
    }

    SocketWaits w;
    gather_sockets(vm, &w);
    if (w.count > 0) {
        int timeout = deadlineMs;
        if ((anyWorker || anyMailbox || anyInterrupt) && (timeout < 0 || timeout > MIXED_WAIT_CAP_MS)) {
            timeout = MIXED_WAIT_CAP_MS;
        }
        if (timeout < 0) timeout = 60000;
        unsigned char *ready = (unsigned char *)calloc((size_t)w.count, 1);
        status_set(vm, "loop: %d tasks; waiting on %d socket%s (up to %dms)", vm->taskCount, w.count,
                   w.count == 1 ? "" : "s", timeout);
        platform_poll_sockets(w.handles, w.count, timeout, ready);
        free(ready);
        free_sockets(&w);
        /* Whatever happened, go round again: the passes above are what
           decide, and they will see anything this wait woke for. */
        return true;
    }
    free_sockets(&w);

    if (anyWorker || anyMailbox) {
        if (anyInterrupt && (deadlineMs < 0 || deadlineMs > MIXED_WAIT_CAP_MS)) deadlineMs = MIXED_WAIT_CAP_MS;
        /* One wait covers a worker finishing, a message being posted (both
           broadcast on the same condition variable) and the next
           deadline (the timeout). */
        status_set(vm, "loop: %d tasks; waiting for an intern or a message (up to %dms)", vm->taskCount,
                   deadlineMs);
        interns_wait_any(vm, deadlineMs);
        return true;
    }
    if (anyInterrupt) {
        /* Only a Ctrl-C left to wait for. Sleeping the thread in short slices
           is exactly right: there is no task that could run, no thread that
           could finish, and a flag that only this loop will notice. */
        status_set(vm, "loop: %d tasks; waiting for Ctrl-C", vm->taskCount);
        platform_sleep_seconds((double)MIXED_WAIT_CAP_MS / 1000.0);
        return true;
    }
    if (deadlineMs >= 0) {
        /* Only timers left. Sleeping the whole thread is exactly right here:
           there is no task that could run and no thread that could finish. */
        status_set(vm, "loop: %d tasks; sleeping %dms until the next timer", vm->taskCount, deadlineMs);
        platform_sleep_seconds((double)deadlineMs / 1000.0);
        return true;
    }
    return false;
}

/* `interns.wait_up` -- the non-async way to wait. It blocks this whole thread
   rather than suspending a task, which is what it is for (§2.4: "for code
   that is not async").
 *
 * Only two things can be waited on this way, and the third is refused on
 * purpose. A worker is joined; a timer is slept through. An `otw` that some
 * *task* will settle cannot be: the task needs the interpreter, and this call
 * is holding it -- blocking here would deadlock the program against itself.
 * `await_fr` is the answer there, and the error says so rather than hanging
 * or, worse, quietly reporting `LeftOnRead` for something that was not
 * anybody's fault. */
void loop_settle_blocking(VM *vm, ObjOtw *p) {
    while (p->state == OTW_PENDING) {
        if (interns_has_worker(vm, p)) {
            interns_collect(vm, p);
            continue;
        }
        if (p->isTimer) {
            double left = p->dueAt - platform_monotonic_seconds();
            if (left > 0.0) platform_sleep_seconds(left);
            otw_fulfill(p, GHOST_VAL);
            continue;
        }
        if (p->waitSocket != PLATFORM_SOCKET_NONE) {
            int timeout = -1;
            if (p->dueAt > 0.0) {
                double left = (p->dueAt - platform_monotonic_seconds()) * 1000.0;
                timeout = left > 0.0 ? (int)left : 0;
            }
            unsigned char ready = 0;
            int64_t one = p->waitSocket;
            otw_fulfill(p, BOOL_VAL(platform_poll_sockets(&one, 1, timeout, &ready) > 0 && ready));
            continue;
        }
        if (p->waitInterrupt) {
            /* Blocking on Ctrl-C is legitimate the same way blocking on a
               worker is: it settles from outside this interpreter. */
            if (platform_interrupt_seen()) {
                otw_fulfill(p, GHOST_VAL);
                continue;
            }
            platform_sleep_seconds((double)MIXED_WAIT_CAP_MS / 1000.0);
            continue;
        }
        if (p->waitMailbox) {
            /* `wait_up(interns.check_dms())` -- blocking on a mailbox is as
               legitimate as blocking on a worker: a message comes from
               another thread, not from a task that needs this interpreter. */
            if (interns_settle_mailbox(vm, p)) continue;
            double now = platform_monotonic_seconds();
            if (p->dueAt > 0.0 && p->dueAt <= now) {
                otw_fulfill(p, GHOST_VAL);
                continue;
            }
            if (!interns_mail_possible(vm)) {
                ObjError *e = error_new(&vm->gc, "LeftOnRead",
                                        "nothing can dm you -- you have no interns running, and you are not "
                                        "one. that check_dms is never going to settle.",
                                        NULL, NULL, 0, 0, NULL, GHOST_VAL, NULL, 0);
                otw_reject(p, OBJ_VAL(e));
                continue;
            }
            int timeout = -1;
            if (p->dueAt > 0.0) {
                double left = (p->dueAt - now) * 1000.0;
                timeout = left > 0.0 ? (int)left : 0;
            }
            interns_wait_for_mail(vm, timeout);
            continue;
        }
        vm_throw_native(vm, "CantWaitRightNow",
                        "'wait_up' can only block on an intern or a clock.chill -- that otw belongs to an "
                        "async_ngl bet, and blocking the whole program is how you stop it from ever finishing. "
                        "use await_fr.");
        return;
    }
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
