/* native/interns.h -- ASYNC_PLAN.md A1: `gimme interns`, real OS threads.
 *
 * An intern is a worker: its own VM, its own GC heap, its own OS thread. It
 * is `sus.run_bytecode` -- which already builds a fresh VM, runs a bundle in
 * it, and copies the results out before tearing the child down -- with the
 * call made asynchronous. That is deliberately not a new architecture; §2.3
 * of the plan argues the point.
 *
 * The protocol, from a worker's side:
 *
 *     gimme interns
 *     yo n = interns.assignment()     // what hire() was given
 *     interns.deliver(n * 2)          // what wait_up() gets back
 *
 * `deliver` rather than a return value because a top-level script has no
 * `bounce`: the worker is a *program*, not a function, and inventing a
 * calling convention for it would mean a second way to enter FunnyLang code.
 *
 * A1 hands back a numba handle and `wait_up` blocks. That is a complete and
 * useful feature on its own, and it proves isolation and the deep copy
 * before any suspension machinery exists to confuse a diagnosis. A2 replaces
 * the handle with an `otw`.
 *
 * Handles are small integers rather than heap objects, following the same
 * reasoning `sus`'s REPL sessions already record: it keeps them out of the
 * collector entirely, which matters more here, because the thing on the
 * other end of the handle is running on another thread.
 */
#ifndef FUNNY_INTERNS_H
#define FUNNY_INTERNS_H

#include "value.h"

struct VM;

Value interns_build(struct VM *vm);

/* Joins every intern hired by `vm` that nobody waited on, and releases them.
   Called when a VM finishes -- by the runner, by `sus.run_bytecode`, and by a
   worker at the end of its own program -- because a handle is only meaningful
   to the VM that was given it, so an unwaited intern's ticket dies with that
   VM. Anything it printed is dropped: nobody asked.

   Runs on `vm`'s own thread, which is also the only thread allowed to wait on
   `vm`'s handles, so it can never race a `wait_up` for the same intern. */
void interns_join_owned_by(struct VM *vm);

/* The same, for every intern in the process, plus the compiled-worker cache.
   Called once at exit. A thread still running is joined rather than killed:
   §3.4 -- killing one mid-allocation leaves a heap nothing can safely free. */
void interns_shutdown(void);

#endif /* FUNNY_INTERNS_H */
