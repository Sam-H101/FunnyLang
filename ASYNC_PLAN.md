# FunnyLang — Concurrency Plan (async + workers)

> **Status:** design, not yet built. Branch `feature/async-threading`.
> **Prerequisite:** `NATIVE_PLAN.md` complete — the runtime is C, the toolchain is FunnyLang, and
> there is no Python anywhere. This plan assumes all of that.
> **Deliverable:** `async_ngl` / `await_fr` with a real event loop, and OS threads through isolated
> workers — the two reserved keywords made real, and something worth awaiting.

---

## 0. Rules for the executing agent

1. **`PLAN.md` §11 says "Threads, async, or a GC" are non-goals. That entry is void, and knowing why
   matters.** It was written for the v1 Python implementation, and its own parenthetical gives the
   reason: *"(Python's refcounting is the GC)"*. There is a real mark-and-sweep collector now, in
   C, and the runtime it belongs to was built to a different set of constraints. `PLAN.md`'s framing
   block already declares the document historical outside §3–§6. This plan supersedes that line and
   says so out loud rather than quietly contradicting it.
2. **The reserved keywords are a contract.** `async_ngl`, `await_fr`, `vibin`, `yield_lol`,
   `match_this` and `when` are already lexed as keywords and rejected by the parser with "isn't
   implemented yet" (`selfhost/parser.funny`'s `RESERVED_FUTURE`). Use `async_ngl` and `await_fr`
   for exactly what their names say. Do not invent a third spelling.
3. **Every deliberate deviation gets a §9 entry**, same convention as `NATIVE_PLAN.md`. Including
   the ones that turn out to be wrong.
4. **`platform.c` is still the only file allowed `#ifdef _WIN32`.** Threads, mutexes and condition
   variables go behind `platform.h` like everything else.
5. **A golden that cannot be deterministic is not a golden.** Concurrency output ordering is the
   obvious trap; §5 says what to do instead.
6. **Commit per milestone, push, never merge to master.**

---

## 1. What "done" looks like

```funny
gimme crew
gimme clock

async_ngl bet fetch_both() {
    // Two workers, two OS threads, one wait.
    yo a = crew.hire("slow_job.funny", 21)
    yo b = crew.hire("slow_job.funny", 21)
    bounce await_fr a + await_fr b
}

yap await_fr fetch_both()      // 42
```

- `async_ngl bet f()` declares a function that returns a **promise** instead of running to
  completion.
- `await_fr p` suspends the current task until `p` settles, and evaluates to its value — or re-raises
  its error, with the awaiting site on the stack of shame.
- `crew.hire(...)` runs a function on a real OS thread, in its own VM with its own heap, and returns
  a promise.
- `clock.chill(ms)` returns a promise that settles after a delay, so an event loop has a timer.
- `funny run` drains the task queue before exiting; a program that leaves work pending gets a
  diagnostic naming it, not a silent truncation.

---

## 2. Why this shape

### 2.1 Async and threading are two features, and only one of them can share a heap

They get conflated because both are "concurrency". They are not the same problem here:

| | async (`async_ngl`) | workers (`crew`) |
|---|---|---|
| Parallelism | none — one OS thread | real, one thread per worker |
| Shares the heap | yes | **no**, its own VM and its own GC |
| Values crossing | ordinary references | deep-copied |
| Cost of getting it wrong | a suspended task never resumes | a data race in the collector |

Async is cheap *because* it does not introduce parallelism: there is exactly one mutator, so the
collector's world does not change. Workers introduce parallelism and therefore may not touch the
parent's heap at all.

### 2.2 Why not shared-memory threads

The obvious ask is "threads that share the heap". The honest answer is that this runtime cannot
have them without a rewrite:

- `GC` is one global-per-VM object with a single intrusive `objects` list, one `grayStack`, and one
  `tempRoots` stack. Two mutators allocating at once corrupt all three.
- `gc_push_temp`/`gc_pop_temp` — the documented rooting protocol every native function follows — is
  a stack with no owner. It is correct precisely because only one thread uses it.
- Making it safe means a lock on every allocation and every write barrier, or a per-thread nursery
  with a stop-the-world protocol. That is a bigger project than everything in this plan combined,
  and its failure mode is a heisenbug in the collector rather than a clear error.

### 2.3 The worker model already exists

`sus.run_bytecode` creates a **fresh VM with its own GC heap**, runs a bundle in it with argv, and
copies the results out as plain C memory before tearing the child down. Its own comment says: *"A
fresh VM with its own globals and its own GC heap, deliberately: this is isolation, not `eval`."*
And `NATIVE_PLAN.md` §9 already records the lesson learned the hard way at that boundary — **a
heap's roots are the job of the collector that owns the heap.**

A worker is that, on a thread. This is not a new architecture; it is the existing one with the
call made asynchronous. That is the strongest argument for this shape: the isolation is already
built, already tested, and already the thing the runtime is shaped around.

### 2.4 What makes async worth having

A single-threaded event loop with nothing genuinely concurrent to wait for is a toy: `await_fr` on
a value that is already computed is an expensive no-op. Async earns its place here because workers
give it something real to await, and because a timer gives it a reason to yield.

That is why the two land together rather than one at a time.

---

## 3. The honest cost sheet

### 3.1 The suspension boundary is real and must be enforced

The VM keeps its own `stack` and `frames` arrays rather than using the C stack, so a task's state is
a slice of two arrays — suspension is genuinely feasible. **But `vm_call_value` re-enters
`vm_execute` on the C stack**, which is how a native function calls back into FunnyLang
(`stash.sort_by`'s comparator, `combo`, a squad's magic method). A task cannot suspend across one of
those: its C frame cannot be saved.

This is the same restriction Python has, and the requirement is not to remove it but to **detect
it**: `await_fr` reached while nested inside a native callback must raise a clear, specific error
naming the callback, not corrupt the frame stack. A silent wrong answer here would be the worst
defect this plan could ship.

### 3.2 The GC must mark every task, not the running one

`mark_roots` delegates to `markExternalRoots` → `mark_vm_roots`, which walks *the* stack and *the*
frames. The moment there is more than one task, anything on a suspended task's stack is invisible to
the collector and gets swept while a live task still owns it.

This is the single most likely serious bug in the whole plan, and it will present as an
intermittent use-after-free rather than a test failure. Mitigation: `FUNNY_GC_STRESS` (now with a
period, so it can run over the whole corpus) plus ASan, and a golden that suspends a task with a
large freshly allocated structure on its stack and resumes it after forcing collections.

### 3.3 Deep copy across the worker boundary is a language-visible restriction

Only self-contained values can cross: `ghost`, `boolski`, `numba` (including bignums), `yapstring`,
and `stash`/`groupchat` recursively containing those. A closure references upvalues and a module's
globals; a squad instance references its class; a `pointa` references a cell in a specific heap.
None can be copied coherently, and none may be shared.

So `crew.hire` takes **a module path and a function name**, not a closure — the worker compiles or
loads the bundle itself. That is a real ergonomic cost and it is the price of not having a
thread-safe collector. It must be documented in the error message, not just here.

A cycle in a copied structure must be detected and rejected (or copied preserving sharing) rather
than recursing forever — the display path already had to learn this lesson (`[[...]]`).

### 3.4 Cancellation, and what happens at exit

Deliberately narrow for a first version: a worker runs to completion. There is no `kill`, because
killing a thread mid-allocation leaves its heap in a state nothing can safely free. `funny run`
joins outstanding workers before exiting; a program that leaves a promise unawaited gets a
diagnostic naming the site, in the spirit of the existing "no silent truncation" rule.

---

## 4. Repository layout (additive)

```
native/
  task.c / task.h        NEW  a task: its own stack + frames, and its state
  loop.c / loop.h        NEW  the event loop: ready queue, timers, settled promises
  promise.c / promise.h  NEW  the promise object and its settle/subscribe protocol
  crew.c                 NEW  the `crew` stdlib module (worker spawn/join)
  platform.h/.c          threads, mutexes, condvars behind the boundary
  gc.c                   mark every task, not just the running one
  vm.c/.h                task-aware execute; OP_AWAIT; the suspend path
  clock.c                `chill(ms)` -> promise

selfhost/
  lexer.funny            (no change -- keywords already lexed)
  parser.funny           async_ngl / await_fr out of RESERVED_FUTURE, into the grammar
  compiler.funny         async function protos; OP_AWAIT
  ...

tests/lang/async/        goldens (see §5)
tests/lang/crew/         goldens
```

---

## 5. Testing strategy

The corpus is the test suite; `funny test` runs it. Concurrency breaks the usual assumption that a
program's output is a fixed string, so:

1. **Force the order in the test, or assert on order-independent facts.** `await_fr` in a fixed
   sequence is deterministic and should be most of the corpus. Where genuine racing is the point,
   collect results and sort them, or assert a count.
2. **Never assert on timing.** `clock.chill(10)` finishing before `clock.chill(50)` is a scheduler
   property worth testing; "it took under 60 ms" is a property of the runner, and `NATIVE_PLAN.md`
   already rejected wall-clock budgets as goldens for that reason.
3. **A stress golden per hazard**, not just per feature: a suspended task holding a large structure
   across a forced collection (§3.2); `await_fr` inside a `sort_by` comparator, asserting the
   specific error (§3.1); a worker handed a cyclic structure, asserting the specific error (§3.3).
4. **The whole corpus runs under `FUNNY_GC_STRESS=50` in CI already.** Every new golden inherits
   that for free, which is exactly the coverage §3.2 needs.
5. **ASan and UBSan** already run the corpus in CI. A data race needs TSan, which is *not* currently
   in the matrix — A1 adds it for the worker tests specifically, because a race is otherwise
   invisible until it corrupts something in production.

---

## 6. Milestones

### A0 — Platform threading primitives
`platform_thread_start/join`, `platform_mutex_*`, `platform_cond_*` behind `platform.h`; Windows
(`CreateThread`, `SRWLOCK`, `CONDITION_VARIABLE`) and POSIX (`pthread_*`). No language change.
**Acceptance:** a C unit exercise starts N threads, each incrementing a mutex-guarded counter, and
joins them; clean under ASan and TSan on Linux, and builds clean under MSVC `/W4 /WX`.

### A1 — `crew`: workers, no syntax
`crew.hire(path, arg) -> handle`, `crew.wait(handle) -> value`. Blocking join only, no promises, no
async. Each worker is a fresh `VM` running a bundle, exactly as `sus.run_bytecode` does, on its own
thread. Deep copy in and out; the copier rejects what cannot cross with a specific error.
**Acceptance:** goldens for a value round trip, an error raised inside a worker surfacing in the
parent with its flavor intact, a rejected closure argument, a rejected cycle. TSan clean.

### A2 — Promises
A `promise` value type: pending / fulfilled / rejected, with subscribers. Settling from a worker
thread is the only cross-thread mutation in the design and is mutex-guarded. `crew.hire` returns one.
`what_is_it(p)` is `"promise"`.
**Acceptance:** `crew.wait` reimplemented on top of promises with no behaviour change; goldens for
each state and for the display form.

### A3 — Tasks in the VM
A task owns its `stack`/`frames`. `mark_vm_roots` walks **every** task (§3.2). The running task is
swapped in and out by the loop. Still no syntax — the entry program is simply task zero.
**Acceptance:** the entire existing corpus (376 goldens) passes unchanged, under
`FUNNY_GC_STRESS=50` and under ASan. This milestone must be invisible.

### A4 — `async_ngl` and `await_fr` in the language
Out of `RESERVED_FUTURE`, into the grammar. An `async_ngl bet` compiles to a proto flagged async;
calling it creates a task and returns a promise instead of running the body. `OP_AWAIT` suspends.
The §3.1 boundary is enforced with its own error flavor.
**Acceptance:** `funny xray --ast` and disassembly goldens for both forms; the nesting error golden;
`await_fr` on a non-promise evaluating to the value itself (so awaiting a plain value is legal and
cheap).

### A5 — The event loop
Ready queue, timer heap, settled-promise drain. `funny run` runs the entry program, then drains
until nothing is pending. A promise left unawaited at exit is a diagnostic naming its creation site.
**Acceptance:** ordering goldens; a golden proving the loop drains work created after the entry
program returned; the unawaited-promise diagnostic as a `!DIAG` golden.

### A6 — Timers and awaiting workers
`clock.chill(ms)`. A worker thread settling a promise wakes the loop. This is the milestone where
the two halves meet and the §1 example runs.
**Acceptance:** the §1 example as a golden; two `chill`s resolving in duration order; a worker
awaited from an async function.

### A7 — Docs, and the reserved-keyword list
`README.md`, `docs/`, `PLAN.md` §3.3's reserved list (two of six now real), `CHANGELOG.md`.
`vibin`/`yield_lol` stay reserved — generators reuse A3's task machinery and are a separate feature,
noted as cheap-from-here rather than smuggled in.

---

## 7. Order of operations

```
A0 platform threads → A1 crew (blocking) → A2 promises → A3 tasks in the VM
   → A4 async_ngl/await_fr → A5 event loop → A6 timers + worker integration → A7 docs
```

A1 before A2 on purpose: a blocking worker is a complete, useful, testable feature on its own, and
it proves the isolation and the deep copy before any of the suspension machinery exists to confuse
the diagnosis. A3 before A4 for the same reason — the riskiest change (multiple stacks, GC) lands
where its acceptance criterion is *"the existing corpus is unaffected"*, which is the clearest
signal available.

---

## 8. Definition of Done

- [ ] `async_ngl` and `await_fr` are out of `RESERVED_FUTURE` and do what their names say.
- [ ] `crew` runs real OS threads; a worker's crash or error surfaces in the parent as a normal
      FunnyLang error, never as a runtime abort.
- [ ] The existing 376 goldens pass unchanged, under `FUNNY_GC_STRESS=50` and ASan.
- [ ] New goldens for every hazard in §3, not just every feature in §6.
- [ ] TSan clean on the worker tests.
- [ ] `await_fr` inside a native callback raises a clear error naming the callback.
- [ ] A value that cannot cross the worker boundary is rejected with a message that says why.
- [ ] MSVC `/W4 /WX`, gcc and clang `-Wall -Wextra -Werror` clean; the corpus green on Linux,
      Windows and macOS.
- [ ] `funny bootstrap --verify` still reaches its fixed point — the self-hosted compiler must be
      unaffected by all of this.
- [ ] No third-party dependency, build-time or runtime. Still one C compiler.

---

## 9. Change log (executing agent: append here)

Same convention as `NATIVE_PLAN.md` §9 and `PLAN.md` §16: every deviation, every AGENT CHOICE, every
spec contradiction, and every wrong turn worth not repeating.
