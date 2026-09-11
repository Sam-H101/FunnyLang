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
 * `interns.hire` hands back an `otw` (A2) and `interns.wait_up` blocks on
 * one. Blocking is deliberate for now: it proves isolation and the deep copy
 * before any suspension machinery exists to confuse a diagnosis, and it is a
 * complete, useful feature on its own -- `everybody` over four hires already
 * uses four cores.
 *
 * Interns are numbered per hiring VM, and that number never leaves this file:
 * an `otw` is the only handle FunnyLang ever sees, and an `otw` is not one of
 * the things that can cross to a worker (see portable.h), so an intern can
 * never be handed a colleague's ticket. Small integers rather than pointers
 * because the thing on the other end is running on another thread, and
 * keeping it out of the collector entirely is what makes that safe.
 */
#ifndef FUNNY_INTERNS_H
#define FUNNY_INTERNS_H

#include <stdbool.h>

#include "value.h"

struct VM;

Value interns_build(struct VM *vm);

struct ObjOtw;

/* Is there a worker behind this `otw` at all? The event loop asks before
   concluding that a task waiting on it can never be woken. */
bool interns_has_worker(struct VM *vm, struct ObjOtw *p);

/* Has the worker behind this `otw` finished? Never blocks. False for an `otw`
   nobody is working on. A5's event loop asks this; `wait_up` does not need
   to. */
bool interns_ready(struct VM *vm, struct ObjOtw *p);

/* Joins the worker behind this `otw` and settles it -- fulfilled with what
   the worker delivered, rejected with the error that killed it. Replays what
   the worker printed to the waiting VM's streams first. A no-op on an `otw`
   that has already settled. */
void interns_collect(struct VM *vm, struct ObjOtw *p);

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
