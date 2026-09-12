/* native/otw.h -- ASYNC_PLAN.md A2: the pending-value type.
 *
 * "on the way". An `otw` is a value that does not have its answer yet:
 * `what_is_it(p)` says `"otw"`, alongside `numba`, `yapstring`, `pointa` and
 * `boolski`. It has exactly three states -- pending, fulfilled, rejected --
 * and once it leaves pending it never changes again.
 *
 * WHO IS ALLOWED TO TOUCH ONE, which is the whole design:
 *
 * An `otw` is an ordinary GC object on one VM's heap, and **only that VM's
 * thread ever reads or writes it**. A worker thread cannot settle one,
 * because settling means putting a Value on a heap, and a heap belongs to one
 * collector on one thread (§2.2). What a worker does instead is fill in its
 * own `Intern` -- plain malloc'd memory, no collector involved -- and say so
 * under a mutex. The owning thread notices and settles the `otw` itself, on
 * its own heap, at a moment of its choosing.
 *
 * That is stronger than §2.4 asked for. The plan says settling from a worker
 * thread is "the only cross-thread mutation in the design and is
 * mutex-guarded"; it turns out not to need to be a cross-thread mutation at
 * all, and the mutex-guarded box moved down into `interns.c` where the
 * memory is not the collector's. §9 records the swap.
 *
 * A2 is deliberately blocking: `interns.wait_up` drives an `otw` to settled
 * by joining the thread behind it. Nothing suspends yet -- there are no tasks
 * until A3 and no `await_fr` until A4 -- so the subscriber list an `otw`
 * eventually needs is not here either. A dead field is worse than a later
 * edit, and A5 is where a subscriber first has something to be.
 */
#ifndef FUNNY_OTW_H
#define FUNNY_OTW_H

#include <stdbool.h>

#include <stdint.h>

#include "object.h"
#include "value.h"

struct GC;

typedef enum {
    OTW_PENDING,
    OTW_FULFILLED,
    OTW_REJECTED,
} OtwState;

typedef struct ObjOtw {
    Obj obj;
    OtwState state;
    /* Only one of these is live, and only once state has left pending. */
    Value value; /* FULFILLED */
    Value error; /* REJECTED -- an ObjError on this same heap */

    /* The `interns` worker whose answer this is waiting for, numbered within
       the owning VM (see interns.c's intern_at), or 0 for an `otw` that was
       born settled. A4's tasks and A6's timers add their own sources; this
       stays the worker one. */
    int internId;

    /* A6: an `otw` that settles by itself when the clock gets there, from
       `clock.chill(ms)`. `dueAt` is in platform_monotonic_seconds() terms --
       monotonic, so a machine whose wall clock jumps backwards mid-program
       does not park a task forever. It fulfils with `ghost`: what you wanted
       was the delay, not a value. */
    bool isTimer;
    double dueAt;

    /* A socket this `otw` is waiting to have something to read on, or
       PLATFORM_SOCKET_NONE. It settles `fax` when the socket is ready and
       `cap` if `dueAt` arrives first -- which is what lets one thread serve
       several callers at once, instead of each of them sitting in a blocking
       read that nothing else can get past. `dueAt` means the deadline here,
       not "settle by itself", so `isTimer` stays false. */
    int64_t waitSocket;

    /* R2: this `otw` is waiting for a message in its VM's inbox. It settles
       with {"from", "msg"} when one arrives, or `ghost` if `dueAt` passes
       first. Like `waitSocket`, `dueAt` is a deadline here rather than a
       "settle by itself", so `isTimer` stays false. */
    bool waitMailbox;

    /* R5: this `otw` settles (with `ghost`) the first time the process is
       interrupted. There is no deadline and nothing else settles it: a
       program asks for it once and shuts down when it arrives. */
    bool waitInterrupt;

    /* Has anything ever awaited this? Not a state -- it says nothing about
       whether the value arrived -- but the loop needs it at exit: an `otw`
       that *rejected* and that nobody ever looked at is an error thrown into
       the void, and §3.4's "no silent truncation" rule says that must not
       vanish. A fulfilled one nobody looked at is fine; starting work you do
       not need the answer to is a legitimate thing to do. */
    bool awaited;
} ObjOtw;

/* Born pending, waiting on nothing. */
ObjOtw *otw_new(struct GC *gc);
/* Born pending, waiting on the worker with this VM-local id. */
ObjOtw *otw_for_intern(struct GC *gc, int internId);
/* Born settled -- what `await_fr` on a value that is already here produces. */
ObjOtw *otw_done(struct GC *gc, Value v);
/* Born pending, settling by itself at `dueAt` (monotonic seconds). */
ObjOtw *otw_for_timer(struct GC *gc, double dueAt);

/* Born pending, settling `fax` when `sock` has something to read, or `cap` at
   `deadline` (monotonic seconds; 0 means no deadline). */
ObjOtw *otw_for_socket(struct GC *gc, int64_t sock, double deadline);

/* Born pending, settling with the next message in this VM's inbox, or `ghost`
   at `deadline` (monotonic seconds; 0 means wait as long as it takes). */
ObjOtw *otw_for_mailbox(struct GC *gc, double deadline);

/* Born pending, settling the first time somebody presses Ctrl-C. */
ObjOtw *otw_for_interrupt(struct GC *gc);

/* Both are no-ops on an `otw` that has already settled: a settled `otw` is
   final, and a second answer is a bug in the settler rather than something
   the value should have an opinion about. */
void otw_fulfill(ObjOtw *p, Value v);
void otw_reject(ObjOtw *p, Value err);

#endif /* FUNNY_OTW_H */
