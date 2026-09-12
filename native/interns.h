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

/* Blocks until a worker owned by `vm` finishes, or `timeoutMs` elapses
   (negative: no timeout). True if one has. The event loop sleeps on this
   rather than spinning. */
bool interns_wait_any(struct VM *vm, int timeoutMs);

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

/* -- mailboxes (RUNTIME_PLAN.md R2) ---------------------------------------
 *
 * `interns.dm(who, value)` posts a message; `interns.check_dms(ms?)` takes
 * the next one. A message is a PortableValue, deep-copied out of the
 * sender's heap exactly like an assignment or a result, so the two threads
 * still share nothing at all. The mailbox itself is plain malloc'd memory
 * with its own mutex, which is why a worker may post to it while its owner
 * is running. */

/* Pops one message off `vm`'s inbox and settles `p` with
   {"from": handle-or-"boss", "msg": value}. False if the inbox is empty, in
   which case `p` is untouched. Runs on `vm`'s own thread: building the
   groupchat means allocating on `vm`'s heap. */
bool interns_settle_mailbox(struct VM *vm, struct ObjOtw *p);

/* Could a message still arrive? True inside a worker (its parent is alive
   for as long as it is, and may post at any moment) and true for any VM with
   an intern that has not been collected. False means a task waiting on
   `check_dms` with no deadline is stranded, and the loop says so rather than
   hanging. */
bool interns_mail_possible(struct VM *vm);

/* Blocks until something arrives in `vm`'s inbox or `timeoutMs` elapses
   (negative: no timeout, capped internally). The non-async `wait_up` path. */
void interns_wait_for_mail(struct VM *vm, int timeoutMs);

/* Frees `vm`'s inbox and every message left in it. A worker's inbox belongs
   to its `Intern` rather than to its VM, so this leaves that one alone. */
void interns_vm_teardown(struct VM *vm);


#endif /* FUNNY_INTERNS_H */
