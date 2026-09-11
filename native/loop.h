/* native/loop.h -- ASYNC_PLAN.md A5: the event loop.
 *
 * One VM, one thread, several tasks. The loop picks a task that can run,
 * runs it until it suspends or finishes, and repeats. When nothing can run it
 * looks for something that can be *made* to run -- a worker that has finished
 * on its own thread, a timer that is due -- and blocks until there is one.
 *
 * It is deliberately boring. There is no preemption (a task yields only at
 * `await_fr`), no priorities, and no work stealing. Tasks are picked in the
 * order they were created, which is the only choice that makes a concurrency
 * golden possible at all: §5 rules out asserting anything that depends on
 * which thread got there first, and a scheduler that picked at random would
 * make the *language's* own ordering unassertable too.
 *
 * Where the parallelism actually is: `interns`. A task is a line of execution
 * inside one thread; a worker is a whole other thread. Async is what makes
 * waiting for several workers ergonomic, and §2.5 is honest that on its own
 * it would be a toy.
 */
#ifndef FUNNY_LOOP_H
#define FUNNY_LOOP_H

#include "vm.h"

struct Task;

/* Runs `entry` to completion, running every other task whenever `entry` is
   waiting, then drains whatever is left: `funny run` does not exit with work
   still outstanding, and a program that walked away from an `otw` nobody ever
   awaited gets told so rather than silently truncated (§3.4).
   `entry`'s return value goes in `*resultOut` when it is not NULL. */
VmResult loop_run(VM *vm, struct Task *entry, Value *resultOut);

struct ObjOtw;

/* Drives one `otw` to settled by blocking this whole thread: joining the
   worker behind it, or sleeping out the timer. `interns.wait_up` is the
   caller -- the non-async way to wait. An `otw` that a *task* would settle is
   refused with `CantWaitRightNow` rather than deadlocking the program against
   itself: the task needs the interpreter this call is holding. */
void loop_settle_blocking(VM *vm, struct ObjOtw *p);

#endif /* FUNNY_LOOP_H */
