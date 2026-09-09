# native/ — architecture reference

This is the reference for anyone (human or agent) writing code under `native/`. It states the
contracts every file in this directory must honor. The full rationale and milestone-by-milestone
task breakdown live in [`../NATIVE_PLAN.md`](../NATIVE_PLAN.md); this document is the shorter,
durable summary of the parts that must never be violated by later code, not a plan of work.

`native/` is additive. Nothing under `funnylang/` (the Python implementation) moves or is deleted
by this work — it stays the differential-testing oracle (`NATIVE_PLAN.md` §5) until N9 replaces it
as the shipped product.

## Value representation

`Value` is a tagged union, not a NaN-boxed `double` (AGENT CHOICE, `NATIVE_PLAN.md` §9 — correctness
first; arbitrary-precision integers put many numbers on the heap anyway, which blunts NaN-boxing's
usual advantage). Five tags: `ghost`, `boolski`, a fixnum (`int64_t`), a float (`double`), and
`Obj*` for everything heap-allocated. Fixnum arithmetic detects overflow and promotes to a heap
bignum (`bignum.c`) rather than wrapping — `PLAN.md`'s `numba` is one type with no size limit, and
the C runtime must reproduce that exactly, including formatting: `numfmt.c`'s Ryū implementation
must reproduce Python's `repr()` byte for byte, since the differential suite diffs against it.

## The object model

Every heap value (`yapstring`, `stash`, `groupchat`, `Closure`, `Squad`, `Instance`, a boxed
upvalue cell, a `pointa`'s backing state, ...) is an `Obj` with a common header: a type tag, a mark
bit, and an intrusive `next` pointer threading every live object into one all-objects list —
`object.c`'s `allocate_obj` is the only place that list is appended to. This is the same layout
`clox`/Crafting Interpreters uses, chosen for the same reason: sweeping needs no separate
bookkeeping structure, just a walk of that list.

## Garbage collection: the contract

Mark-sweep, not generational, not incremental (post-2.0 territory if it's ever needed). `gc.c`
owns:

- **Roots.** The value stack, every call frame, the global table(s), every *open* upvalue, the
  module table, and the temp-root stack (below). A collection that misses any of these is a
  correctness bug, not a performance one — it frees something still reachable.
- **A gray stack** for the mark phase: push every root, pop-mark-push-children until empty, then
  sweep the all-objects list, freeing anything left unmarked.
- **A heap-growth threshold** that triggers a collection, plus `FUNNY_GC_STRESS=1`, which collects
  on *every* allocation instead. The full differential suite (`NATIVE_PLAN.md` §5) must pass under
  stress mode in CI — it is the only reliable way to turn a latent rooting bug into an immediate,
  reproducible failure instead of a heisencrash under real memory pressure months later.

**A `pointa` (`PLAN.md` §3.10) holds a strong reference to whatever it addresses** — the `Upvalue`
box for a local/upvalue-kind pointer, or the container object for an index/prop-kind one — and
that reference must be traced like any other object field. A place reference that let its target
get collected out from under it would be exactly the dangling pointer the whole design (safe place
references, never raw addresses) exists to prevent. This is the one GC-tracing detail that has no
analogue in the Python implementation (Python's refcounting keeps a `Pointa`'s `container`/`cell`
field alive for free) and is worth re-checking by hand when N1's tracing code is written.

## The #1 correctness hazard: GC and native (C) stdlib functions

The classic way to destroy a VM shaped like this: a native function allocates while holding an
unrooted `Obj*` in a C local, the allocation triggers a collection, the collection frees the
object the local still points at, and the bug doesn't surface until months later, under memory
pressure, nowhere near its actual cause. Mitigations, mandatory from N1 onward and non-negotiable:

1. Every native function receives its arguments on a **shadow stack slice** that is a GC root by
   construction — never a bare C array copied out from under the real root set.
2. A **`PUSH_TEMP`/`POP_TEMP` protocol** for intermediate values a native function allocates and
   needs to keep alive across a further allocation. Debug builds assert the temp stack is balanced
   (equal pushes and pops) on every native-function return — an unbalanced temp stack is a bug in
   that function, caught immediately instead of silently leaking a GC root forever.
3. `FUNNY_GC_STRESS=1` (above) is what actually finds violations of rules 1 and 2 in practice.

## The platform boundary

`platform.c` is the **only** file in `native/` allowed an `#ifdef _WIN32` (or any other
platform conditional). Filesystem access, timing, TTY detection, sockets, and `dlopen` all go
through it. Everything else — including `main.c` — calls a platform-neutral function and does not
know or care what OS it's running on. (`native/main.c` currently prints a plain-ASCII smoke-test
banner rather than the real Unicode one specifically because the real banner needs UTF-8 console
setup on Windows, which is `platform.c`'s job once it exists — not an excuse for a second ifdef
site.)

HTTPS follows the same rule at one remove: `platform_http_request()` is the single entry point the
VM and stdlib call, and only `platform.c` knows that Windows uses WinHTTP, macOS uses
`Security.framework`, and Linux `dlopen`s OpenSSL (falling back to a clean `SkillIssue`, never a
crash, when it's absent). Full detail in `NATIVE_PLAN.md` §3.1 — this file only needs you to know
the boundary exists and where it lives.

## Testing discipline

`native/` code is graded against the Python implementation, not against its own intuition about
correctness (`NATIVE_PLAN.md` §5): the differential suite runs every file in `tests/lang/` and
`examples/` through both VMs and asserts byte-identical stdout, stderr, and exit code. A C-only
unit test (`test_bignum.c`, the hash table, UTF-8 decoding, Ryū) is for the handful of components
with no Python counterpart worth diffing against. When in doubt about whether a behavior is
correct, the question is never "does this look right" — it's "does this match what
`funnylang/` already does."
