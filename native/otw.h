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

/* Both are no-ops on an `otw` that has already settled: a settled `otw` is
   final, and a second answer is a bug in the settler rather than something
   the value should have an opinion about. */
void otw_fulfill(ObjOtw *p, Value v);
void otw_reject(ObjOtw *p, Value err);

#endif /* FUNNY_OTW_H */
