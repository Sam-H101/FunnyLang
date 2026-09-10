# FunnyLang — Native Runtime Build Plan (v2.0)

> Companion to `PLAN.md`. That document specified and built FunnyLang v1.0.0 (M0–M13): a complete
> Python-hosted toolchain whose *compiler* is self-hosted. This document specifies v2.0.0: removing
> Python from the shipped product entirely, by replacing the one component that was never going to be
> written in FunnyLang — the virtual machine — with a native C implementation.
>
> `PLAN.md` §3 (language), §4 (errors), §5 (bytecode) remain **FROZEN** and authoritative. This plan
> changes the *implementation*, not the language. Any genuine contradiction found between this plan
> and `PLAN.md` is resolved by editing the offending document and logging it in §9 below.

---

## 0. Rules for the executing agent

Same eight rules as `PLAN.md` §0, restated with the deltas that matter here:

1. **The language spec is frozen.** `PLAN.md` §3/§4/§5 are the contract. The C VM conforms to them;
   it does not get to simplify them. If conformance is genuinely impossible, that is a §9 entry with
   a written justification, not a silent behaviour change.
2. **Differential testing is the acceptance criterion, not "it compiles."** Every milestone below is
   accepted by running real programs on the C VM and diffing against the Python VM byte-for-byte —
   stdout, stderr, and exit code. The Python implementation is not deleted; it becomes the reference
   oracle. This is the single most important rule in this document.
3. **Work milestone by milestone.** N0 → N9. Do not start a milestone before the previous one's
   acceptance block passes.
4. **Never skip tests.** A milestone with a failing test is not done.
5. **Commit after each milestone**, with the same descriptive-message convention already in use.
6. **Comedy is still a hard product requirement.** The C VM renders the same roasts, the same
   `💀💀💀 FUNNYLANG MOMENT 💀💀💀`, the same mushroom cloud. A faithful port includes the jokes.
7. **AGENT CHOICE points:** pick the simplest option that satisfies the spec, log it in §9.
8. **Cross-platform is now load-bearing, not aspirational.** Windows is the primary dev platform, but
   the C must build clean on Windows (MSVC + MinGW), Linux (gcc + clang), and macOS (clang) with no
   third-party libraries. Every `#ifdef` is a liability; keep them in `platform.c` and nowhere else.

---

## 1. What "done" looks like

On a machine with **a C compiler and nothing else** — no Python, no pip, no PyInstaller:

```console
$ cc -O2 -o funny native/*.c            # or build.bat / build.sh
$ ./funny run examples/hello.funny
yo sup world

$ ./funny build examples/fizzbuzz.funny -o fizzbuzz.funnyc
$ ./funny yeet examples/hello.funny -o hello
$ ls -la hello
-rwxr-xr-x  1 sam  sam  612K  hello              # not 8.2 MB

$ ./hello
yo sup world

$ ./funny bootstrap --verify
🥁 stage 2... compiled.
🥁 stage 3... compiled by stage 2.
🥁 stage 4... compiled by stage 3.

stage3 and stage4 are byte-identical (1,005 bytes).
FunnyLang now compiles FunnyLang, on a runtime that needs nothing. we are so back. 🏆
```

Python appears nowhere in that transcript. It survives in the repo as (a) the historical bootstrap
that produced the checked-in stage0 artifact, and (b) the differential-testing oracle. Both are
development-time only and ship to no user.

And for the overwhelming majority of users, who want to *write* FunnyLang rather than build it, even
the C compiler is optional (N10):

```console
$ curl -fsSL https://github.com/Sam-H101/FunnyLang/releases/latest/download/install.sh | sh
$ funny run hello.funny
yo sup world
```

One downloaded file, under 2 MB, no toolchain of any kind. "No Python required" is not much of a
promise if the alternative is "install MSVC instead" — building from source must always work, but it
must never be the only way in.

---

## 2. Why this shape, and what is genuinely impossible

### 2.1 The regress (read this before proposing alternatives)

A VM written in FunnyLang would need a VM to run it. There is no arrangement of self-hosting that
escapes this — the bottom of every language stack is native code the OS can execute directly.
"Fully self-hosted" therefore means, and can only mean: *the compiler is written in FunnyLang, and
the runtime beneath it is native code with no interpreter dependency.* That is what this plan builds.

### 2.2 What is already true (and easy to misread)

`funny yeet` **already** produces binaries that run on machines without Python — PyInstaller bundles
CPython inside the executable. The current stub is 8,188,862 bytes, and roughly 8 MB of that is a
Python interpreter along for the ride. So the current state is not "broken"; it is *heavy, opaque,
and requires Python on the machine doing the building.* This plan fixes those three things, and
gets the binary to roughly 5% of its current size as a side effect.

### 2.3 Why not native code generation instead

`PLAN.md` §M14 floats a `.funnyc` → C transpiler. That is a *speed* feature, not a path off Python,
and it is strictly more work than this plan rather than less: emitting machine code still requires a
runtime library in C for GC, bignums, strings, dictionaries, and the whole stdlib. Native codegen is
this plan **plus** a backend. Build the runtime first; AOT compilation stays a post-2.0 stretch goal.

### 2.4 Why C, and not Rust or Zig

All three produce dependency-free native binaries, so all three satisfy the user-facing requirement.
C wins on the *build* side, which is the remaining constraint:

- The output has zero runtime dependencies on all three platforms — as do Rust and Zig.
- Building requires only a C compiler, which Linux and macOS already have and Windows gets from MSVC
  Build Tools or MinGW. Rust and Zig each require installing a fresh toolchain, which reintroduces
  the exact "you must install a language runtime first" friction this plan exists to remove.
- The build is a single compiler invocation over a flat directory. No build system, no lockfile, no
  package registry, no vendored crates.
- This VM's closure/upvalue design is already lifted from *Crafting Interpreters*' `clox`, so the C
  structure maps onto the existing Python structure nearly one-to-one.

**AGENT CHOICE (log in §9): C11**, `-std=c11`, no compiler extensions outside `platform.c`.
Additionally support `zig cc` as an *optional* cross-compiler for producing all-platform binaries
from one machine — it consumes the same source with no changes and requires no code to accommodate.

---

## 3. The honest cost sheet

These are the parts that Python was silently doing for free. Each is real work; none is a blocker.

| # | What Python gave us | What C needs | Size | Risk |
|---|---|---|---|---|
| 1 | Arbitrary-precision `int` | A schoolbook bignum: add/sub/mul/divmod/pow/shifts/bitwise/parse/format, with an int64 fixnum fast path that promotes on overflow | ~1,200 ln | Med |
| 2 | Refcounting GC (`PLAN.md` §11 called a GC a non-goal — it is now a goal) | Precise mark-and-sweep over an all-objects list, with a temp-root protocol for native functions | ~450 ln | **High** |
| 3 | `str.isalpha()`, `.upper()`, `.lower()` over full Unicode | Generated compact codepoint tables (derived from `unicodedata` at build time, checked in) | ~600 ln | Low |
| 4 | Codepoint-indexed strings (`len("héllo👋") == 6`) | UTF-8 storage + an ASCII fast path, O(n) fallback for indexing non-ASCII | ~500 ln | Med |
| 5 | `repr(float)` shortest-round-trip formatting | A Ryū or Grisu implementation — `%.17g` is **not** equivalent and will break golden files | ~250 ln | Med |
| 6 | `urllib.request`, incl. TLS | OS-native TLS behind one interface: WinHTTP / Security.framework / `dlopen`'d OpenSSL (§3.1) | ~900 ln | **High** |
| 7 | ~220 stdlib native functions across 12 modules | Straight ports, mechanical but voluminous | ~3,500 ln | Low |
| 8 | Dict with insertion order (`groupchat`) | Open-addressing hash table with an insertion-order vector | ~400 ln | Low |

Estimated total: **~10,500 lines of C**, comparable in scale to what `PLAN.md` already delivered.

### 3.1 TLS — OS-native, no bundled crypto

`internet.go_brrrr(url)` currently supports HTTPS because `urllib` does, and **v2.0 keeps that**.
Plain HTTP over raw sockets is ~200 lines of C; TLS is the hard part, and hand-rolling it is not on
the table at any scope. The resolution is to use the TLS each operating system already ships, so
FunnyLang gains no bundled crypto and no third-party dependency:

| Platform | Backend | Linkage |
|---|---|---|
| Windows | **WinHTTP** (`winhttp.dll`) | Part of the OS. Link `-lwinhttp`. Handles the whole request, TLS included. |
| macOS | **`Security.framework` + `CFNetwork`** | Part of the OS. Link `-framework Security -framework CFNetwork`. |
| Linux/BSD | **OpenSSL via `dlopen`** | Not linked at build time. Resolved at run time; see below. |

Windows and macOS therefore have full HTTPS with zero dependencies, because the OS *is* the
dependency. Linux has no OS-level TLS, so the rule there is:

- `dlopen("libssl.so.3")`, falling back to `libssl.so.1.1`, then `libssl.so`. Resolve the dozen
  symbols actually needed (`TLS_client_method`, `SSL_CTX_new`, `SSL_new`, `SSL_set_fd`,
  `SSL_connect`, `SSL_read`, `SSL_write`, `SSL_shutdown`, `SSL_CTX_set_default_verify_paths`,
  `SSL_get_verify_result`, `SSL_set_tlsext_host_name`, `SSL_CTX_free`) through function pointers.
- Present ⇒ HTTPS works. Absent ⇒ a clean `SkillIssue` naming the missing library, never a crash.
- Because it is `dlopen`'d rather than linked, **the binary still builds and runs on a machine with
  no OpenSSL at all** — it just cannot do HTTPS there. This is what preserves the "one C compiler,
  no dependencies" build promise from §1.

Certificate verification is **on by default on every platform** — WinHTTP and Security.framework
verify against the system trust store automatically, and the OpenSSL path must call
`SSL_CTX_set_default_verify_paths()` and check `SSL_get_verify_result()`. A verification failure
raises `SkillIssue` with the existing `"the internet said no. 🚫"` flavour text. There is no
`verify=false` option and none should be added; silently accepting bad certificates is the kind of
joke that stops being funny.

All of this lives behind a **single interface** so the VM and stdlib never see a platform:

```c
/* platform.c — the only file that knows any of the above exists */
FunnyHttpResult platform_http_request(const char *method, const char *url,
                                      const HttpHeaders *headers, const char *body,
                                      int timeout_ms);
```

`FUNNY_NO_NET=1` short-circuits this before any backend is touched, exactly as it does today.

### 3.2 The number-one correctness hazard: GC and native functions

The classic way to destroy a VM like this is a native stdlib function that allocates while holding an
unrooted value — the allocation triggers a collection, the collection frees the value the C local
still points at, and the bug surfaces months later as a heisencrash under memory pressure.

Mitigations, mandatory from N1 onward and non-negotiable:

- Every native function receives its arguments on a **shadow stack slice** that is a GC root by
  construction. No native function ever holds a bare `Obj*` across an allocation.
- A `PUSH_TEMP` / `POP_TEMP` protocol for intermediates, with a debug-build assertion that the temp
  stack is balanced on native-function return.
- A **stress mode** (`FUNNY_GC_STRESS=1`) that collects on *every* allocation. The full differential
  test suite must pass under it in CI. This turns latent rooting bugs into immediate, reproducible
  failures — it is the only reliable way to find them.

---

## 4. Repository layout (additive; nothing in `funnylang/` moves)

```
native/
  main.c            N0's toolchain smoke test / N2's minimal test driver -- NOT the real CLI (N7)
  opcodes.h         Op enum, PLAN.md §5.1 mirrored 1:1 (FROZEN, never renumber)
  value.h/.c        tagged-union Value, type predicates, equality, truthiness
  object.h          Obj header (struct + ObjType enum) -- no object.c; see §9's N1 entry
  gc.h/.c           mark-sweep, gray stack, temp roots, stress mode
  bignum.h/.c       arbitrary-precision integers + fixnum promotion
  numfmt.h/.c       shortest-round-trip float formatting (bignum-based, not literally Ryū — §9)
  da_string.h/.c    `yapstring`: an interned, UTF-8 ObjString. Named `da_string`, not
                    `string`, so it can never shadow the C standard <string.h> — §9
  unicode_tbl.c     GENERATED — do not hand-edit (see tools/gen_unicode.funny)
  toolchain_blob.c  GENERATED — the linked toolchain .funnypak as a byte array (N10)
  table.c           insertion-ordered hash table (groupchat + globals + interning) -- globals
                    are a plain linear-scan array as of N2 (§9); table.c itself is still N4's
  stash.c           dynamic array
  chunk.h/.c        .funnyc loader as of N2 (§5.2); .funnypak (§5.3) is N5's, once IMPORT exists
  vm.h/.c           the interpreter loop; a real call stack as of N3 (§5.1's opcodes 0-55, 59,
                    60-62, 70-79 -- INVOKE/squad/collection opcodes are N4/N5)
  frames.h/.c       call frames, ObjClosure, the open-upvalue mechanism (N3) -- try/catch/finally
                    unwinding itself lives directly in vm.c's dispatch loop, next to CHUCK/TRY_*
  error.h/.c        ObjError, PLAN.md §3.9's error field set -- built early for N3 (§9), not in
                    the plan's original sketch; `.trace` is a placeholder string until N4's Stash
  pointa.h/.c       ObjPointa (PLAN.md §3.10) -- POINTA_CELL/POINTA_GLOBAL as of N3; POINTA_INDEX/
                    POINTA_PROP need Stash/GroupChat/Instance and land with PTR_INDEX/PTR_PROP in N4
  squad.c           classes, instances, methods, inheritance, the 4 magic methods
  diag.c            the PLAN.md §4.2 diagnostic renderer
  platform.c        the ONLY file with #ifdef _WIN32 — fs, time, tty, sockets, dlopen
  stub_main.c       the yeet runtime stub (PLAN.md §5.4)
  stdlib/
    mafs.c yapper.c stash_mod.c groupchat_mod.c rizz.c filez.c
    clock.c sus.c computer.c internet.c builtins.c
tools/
  gen_unicode.funny regenerates unicode_tbl.c   (dev-time only, run by `funny`)
  bin2c.funny       regenerates toolchain_blob.c (dev-time only, run by `funny`)
bootstrap/
  funnyc.funnypak   CHECKED IN — the self-hosted compiler as bytecode (see §6.N9)
  STAGE0.md         provenance, regeneration instructions, staleness policy
install.sh / install.ps1        the one-line installers (N10)
build.sh / build.bat
.github/workflows/release.yml   build → publish → verify-published (N10)
```

Both `tools/` scripts need Python, and neither is needed to *build* — they regenerate checked-in
generated sources when their inputs change. A user with only a C compiler never runs them.

`selfhost/` grows the rest of the toolchain in FunnyLang (N8). `funnylang/` is untouched.

---

## 5. Testing strategy

The existing Python suite (1,065 tests as of M15) stays green throughout — this plan adds to it,
never subtracts.

1. **Differential golden testing (the backbone).** Eventually running every file in `tests/lang/`
   and `examples/` through both VMs, asserting identical stdout, stderr, and exit code — but none of
   those existing goldens fit inside N2's narrow opcode subset (every one declares at least one
   function), so N2 started instead with dedicated programs under `tests/native/n2_programs/` and
   `tests/native/test_n2_differential.py` (§9's N2 entry); both were renamed to `tests/native/
   programs/` and `tests/native/test_native_differential.py` in N3 once "N2-only" stopped being an
   honest description. The real `tests/lang/`-driven suite becomes possible once enough of the
   language exists natively — expected around N4 — and should grow directly out of this file rather
   than replacing it. This is the acceptance gate for N2 through N6, with the passing subset growing
   each milestone.
2. **C unit tests** for the components with no Python counterpart worth diffing: bignum arithmetic,
   the hash table, UTF-8 decoding, Ryū. Plain assertion-based `test_*.c`, run by the build script.
3. **Differential fuzzing.** Reuse `tests/test_fuzz.py`'s generators; additionally fuzz bignum ops
   against Python's `int` and float formatting against `repr()`. Millions of cases, cheap to run.
4. **GC stress.** The entire differential suite, re-run with `FUNNY_GC_STRESS=1`. Non-optional.
5. **Sanitizers.** CI runs the suite under ASan and UBSan on Linux. A leak or UB report fails the build.
6. **The bootstrap fixed point**, now executed natively (N9).

---

## 6. Milestones

### N0 — Decisions, scaffolding, build system

**Tasks**
1. `build.sh` / `build.bat`: one compiler invocation, no build system. `-std=c11 -O2 -Wall -Wextra
   -Werror`. Debug variant with `-g -fsanitize=address,undefined -DFUNNY_DEBUG`.
2. `native/main.c` that prints the banner and exits — proves the toolchain works on all three OSes.
3. CI: add a `native` job to `.github/workflows/ci.yml` building on Windows/Linux/macOS.
4. Write `native/ARCHITECTURE.md`: value representation, GC contract, the temp-root protocol.
5. Log the C11 choice and the TLS decision in §9.
6. **Interim binary release (do this first — it delivers value on day one).** The v1.1.0 PyInstaller
   toolchain already works; users just have no way to get it without a Python install. Add
   `release-build`/`release-publish` jobs to `.github/workflows/ci.yml` that run on tags, freeze the
   real `funny` CLI on each of the three OSes, and publish the results to **GitHub Releases**. Cut
   `v1.1.0` from it (not `v1.0.1` as originally written here — M15 landed first and bumped
   `__version__`, so `v1.1.0` is what a frozen binary of this exact commit actually reports). These
   binaries are ~8 MB and carry CPython inside — that is exactly what N1–N9 fixes — but they let
   someone start writing FunnyLang today, which is the point. N10 replaces this job wholesale.

**Acceptance:** `build.sh` and `build.bat` both produce a binary that prints the banner, on all three
platforms in CI, with `-Werror` clean. A `v1.1.0` GitHub Release exists carrying downloadable
Windows, Linux, and macOS binaries, and a freshly downloaded one runs `examples/hello.funny` on a
machine with no Python installed.

---

### N1 — Values, objects, GC, bignum

The foundation. Nothing here is user-visible; everything above it depends on it being right.

**Tasks**
1. `Value`: tagged union over `ghost`, `boolski`, fixnum (`int64_t`), float (`double`), and `Obj*`.
   **AGENT CHOICE:** tagged union over NaN-boxing — correctness first; NaN-boxing is a post-2.0
   optimization, and bignums make it less of a win than it looks.
2. `Obj` header (type tag, mark bit, next pointer), the all-objects list, `allocate_obj`.
3. Mark-sweep GC: gray stack, root marking (value stack, frames, globals, open upvalues, module
   table, temp roots), sweep. Heap-growth threshold. `FUNNY_GC_STRESS=1`.
   A `pointa` (`PLAN.md` §3.10) holds a **strong** reference to its box or its container and must be
   traced like any other object — a place reference that let its target be collected would be exactly
   the dangling pointer the design exists to prevent.
4. The temp-root protocol from §3.2, with balance assertions in debug builds.
5. `bignum.c`: sign-magnitude, `uint32_t` limbs. add, sub, mul (schoolbook), divmod (Knuth D), pow,
   shifts, bitwise ops on two's-complement semantics, parse from decimal/hex/binary/octal, format to
   decimal. Fixnum ops detect overflow and promote.
6. `numfmt.c`: Ryū shortest-round-trip. Must reproduce Python's `repr()` exactly.

**Acceptance**
- `test_bignum.c` passes, including Knuth D edge cases (borrow propagation, single-limb divisors).
- Differential fuzz: 1,000,000 random bignum ops match Python's `int` exactly.
- Differential fuzz: 1,000,000 random doubles format identically to Python's `repr()`.
- ASan/UBSan clean.

---

### N2 — Loader and the interpreter core

**Tasks**
1. `chunk.c`: read `.funnyc` (§5.2) and `.funnypak` (§5.3). Reject bad magic/version with a clean
   error. Round-trip against files the Python serializer produced.
2. The interpreter loop with a computed-goto dispatch table where available (`__GNUC__`), falling
   back to `switch` on MSVC.
3. Opcodes: `NOP CONST GHOST FAX CAP POP DUP SWAP GET/SET_LOCAL GET/SET/DEF_GLOBAL` + all arithmetic
   (`ADD`–`SHR`), comparisons (`EQ`–`IN`), all jumps (`JUMP`–`LOOP`, plus `JUMP_LONG`/`LOOP_LONG`
   71/72 from M11), `YAP`, `HALT`.
4. Arithmetic conforming to §3.2: mixed int/float promotes to float, `/` always yields float, `\` is
   floor division, negative shifts raise `MathAintMathin` (the M11 fix — port the behaviour, not just
   the opcode).

**Acceptance:** every file in `tests/lang/` that uses only the above features produces byte-identical
stdout under both VMs. Enumerate that subset explicitly in the test file; it grows each milestone.

---

### N3 — Functions, closures, control flow, errors

**Tasks**
1. Call frames, `CALL`, `RETURN`, arity checking, default parameters, variadics.
2. `CLOSURE`, `GET/SET_UPVAL`, `CLOSE_UPVAL` — the open-upvalue linked list and closing on scope exit.
   This is the Lua/clox model the Python VM already implements; port it structurally.
3. `TRY_PUSH`/`TRY_POP`/`CHUCK`, the unwinding search, and `regardless` (finally) semantics —
   including the awkward cases: `bounce` inside `sketchy` with a `regardless`, and a `chuck` inside
   the `regardless` block itself.
4. Error objects with the §3.9 field set (`.flavor .message .line .col .file .trace .payload`).
5. `ITER_NEW`/`ITER_NEXT` over stash, groupchat, yapstring, and ranges.
6. The 10,000-frame recursion cap from M11, raising cleanly rather than smashing the C stack.
7. Pointers to variables: `PTR_LOCAL` (73), `PTR_GLOBAL` (74), `PTR_UPVAL` (75), `DEREF` (78),
   `SET_DEREF` (79). These belong here rather than in N2 because an addressed local is boxed by the
   same machinery as a captured one — build them alongside upvalues, and verify the aliasing case
   from `PLAN.md` §3.10 (a closure capturing `x` and an `&x` must share one box).

**Acceptance:** all closure, recursion, try/catch/finally, and iteration tests in `tests/lang/` match
byte-for-byte. `examples/closures.funny` and `examples/errors.funny` match. Deep recursion produces
FunnyLang's error, not a segfault.

---

### N4 — Strings, collections, classes

**Tasks**
1. `yapstring`: immutable, UTF-8, interned. `is_ascii` flag for O(1) indexing; O(n) codepoint walk
   otherwise. `how_thicc()` counts codepoints — `len("héllo👋") == 6`, matching Python exactly.
2. `unicode_tbl.c` generated by `tools/gen_unicode.funny`: `isalpha`, `isalnum`, upper/lower
   mappings. Checked in so the build never needs the generator at all (N11 removes every `.py` from
   the repository, dev tooling included, so the generator is FunnyLang run by `funny` itself).
3. `stash`: dynamic array, reference semantics, all 24 methods from §3.9.
4. `groupchat`: insertion-ordered open-addressing table, all 11 methods, keys may be yapstring/numba/
   boolski.
5. `BUILD_STASH`/`BUILD_GROUPCHAT`/`BUILD_STRING`, `GET/SET_INDEX`, `GET_SLICE`, `GET_PROP`,
   `SET_PROP`, `GET_PROP_SAFE`.
6. `squad.c`: `SQUAD`/`METHOD`/`INHERIT`/`INVOKE`/`INVOKE_OG`, bound methods, `me`, `og`, `spawn`,
   and the 4 magic methods (`to_yap`, `how_thicc`, `get_it`/`set_it`, `same_energy`). Port the M9
   fixes: 3+ level `og` chains and inherited-`spawn` construction.
7. Self-referential `stash`/`groupchat` print as `[...]`/`{...}` (the M11 fix).
8. Pointers to places: `PTR_INDEX` (76) and `PTR_PROP` (77), plus stash-pointer arithmetic,
   difference, and ordering per `PLAN.md` §3.10. Bounds and liveness are checked on **read**, never
   on construction, so forming a one-past-the-end pointer is legal and reading through a stale one is
   a clean `OutOfPocket`/`KeyGhosted` — never a segfault. This is the milestone where "safe pointers"
   is either true or isn't; test it under `FUNNY_GC_STRESS=1` specifically.

**Acceptance:** all string, collection, squad, and inheritance tests match byte-for-byte, including
the unicode identifier and unicode string tests. `examples/squads.funny` and `examples/arrays.funny`
match.

---

### N5 — The standard library and modules

The largest milestone by volume, the least difficult by nature.

**Tasks**
1. A native-function registration mechanism mirroring `funnylang/values.py::NativeFn` (name, arity
   range, C function pointer), with arity errors identical to Python's.
2. Port all 12 modules — `mafs yapper stash groupchat rizz filez clock sus computer internet` plus
   the always-in-scope builtins. Roughly 220 functions. Work module by module, diffing each against
   `docs/STDLIB.md` (generated from the real registry, so it cannot have drifted).
3. `IMPORT`/`EXPORT` and the §3.8 resolution order, including `FUNNYPATH`, caching, cycle detection,
   and per-module global namespaces.
4. `computer.explode()` and `blue_screen()` — the mushroom cloud, exit 69, and the §7.9 constraint
   that they remain genuinely harmless: printing and `exit`, nothing else.
5. `internet` over raw sockets, HTTP/1.1, 10 s default timeout, `FUNNY_NO_NET=1` honoured. The
   plain-HTTP path only; HTTPS is N5b's job, and until it lands an `https://` URL raises a clean
   `SkillIssue` rather than silently downgrading to HTTP.
6. `rizz` PRNG: a seeded xoshiro256\*\*. Need not match Python's Mersenne Twister stream — but
   `rizz.seed(n)` must be reproducible within the C VM. Log in §9.

**Acceptance:** the *entire* `tests/lang/` and `examples/` corpus matches byte-for-byte, except
`chaos.funny`'s deliberately random line. `funny test examples/` passes 9/9 under the C VM.

---

### N5b — HTTPS via OS-native TLS (required)

Not a stretch goal: v2.0 does not ship without HTTPS. `internet.*` is the one part of the stdlib a
user can point at the real world, and a runtime that can only fetch `http://` in 2026 is a runtime
that can fetch nothing. Full design in §3.1.

**Tasks**
1. `platform_http_request()` in `platform.c` — the single interface from §3.1, with the VM and the
   `internet` module knowing nothing beneath it.
2. **Windows: WinHTTP.** `WinHttpOpen`/`Connect`/`OpenRequest`/`SendRequest`/`ReceiveResponse`/
   `QueryDataAvailable`/`ReadData`. Handles TLS, redirects, and the system proxy for free. Link
   `-lwinhttp` (MSVC: `winhttp.lib`).
3. **macOS: `Security.framework` + `CFNetwork`.** System trust store, system proxy settings.
4. **Linux/BSD: `dlopen`'d OpenSSL**, with the symbol list, the `libssl.so.3` → `.1.1` → `.so`
   fallback chain, and the clean `SkillIssue` when absent, exactly as §3.1 specifies.
5. Certificate verification on, unconditionally, on all three. No `verify=false` escape hatch.
6. Redirect following (cap at 10), `Content-Length` and `chunked` transfer-decoding, and the 10 s
   default timeout applied to connect *and* read.
7. `internet.download()`, `is_it_up()`, `speed_test()`, and `ping()` all routed through the same
   interface.

**Acceptance**
- A live-network test, marked `@pytest.mark.network` and skipped under `FUNNY_NO_NET=1`, fetches an
  `https://` URL and gets a 200 with a non-empty body — on all three OSes in CI.
- An expired/self-signed-certificate host raises `SkillIssue`, not a success and not a crash.
- `FUNNY_NO_NET=1` still short-circuits everything before any backend loads.
- On a Linux container with **no** OpenSSL installed: the binary still builds, still runs, still
  passes the whole non-network suite, and `https://` raises the documented `SkillIssue`.

---

### N6 — Diagnostics

**Tasks**
1. Port the §4.2 renderer to `diag.c`: source snippet with the caret line, the plain-English
   explanation, the `💡 skill issue fix:` hint, and the `🥞 stack of shame:` trace.
2. `--serious` (professional text) and `--no-color` (no ANSI). Auto-disable colour when stdout is not
   a TTY, and enable VT processing on Windows consoles.
3. All error flavours from §4.1 with their exact roast text.
4. UTF-8 output on Windows without mangling (set the console code page; never rely on the ANSI one).

**Acceptance:** for every `err_*.funny` golden, the C VM's **stderr** is byte-identical to the Python
VM's, in both funny and serious modes. This is a strictly stronger test than the existing suite,
which only checks the error flavour.

---

### N7 — The native CLI and native `yeet`

**Tasks**
1. `funny run` on `.funny` (via the self-hosted compiler bytecode), `.funnyc`, and `.funnypak`.
2. `stub_main.c`: the §5.4 trailer protocol, unchanged — read own file, seek `filesize - 17`,
   validate `FUNNYYEET`, load the payload. The format does not change; only the stub's language does.
3. `funny yeet`: copy the native stub, append payload + trailer, `chmod +x` on POSIX. No PyInstaller,
   no stub cache keyed on a Python hash, no 8 MB.
4. Keep the M13 packager fix (create `-o`'s parent directory) and the naked-stub message.
5. Global flags: `--version --serious --no-color --time --vibes`, before or after the subcommand.

**Acceptance:** `funny yeet examples/hello.funny -o hello` produces a binary **under 1 MB** that
prints `yo sup world`, still works after being moved, and exits 69 on an uncaught
`computer.explode()`. Verified on all three OSes in CI.

---

### N8 — The rest of the toolchain, in FunnyLang

Everything left in Python that a user could touch. None of this is C — it is FunnyLang running on the
C VM, which is the entire point of the exercise.

**Tasks**
1. `selfhost/linker.funny` — the `.funnypak` bundler (§5.3), replacing `funnylang/modules.py`'s
   build-bundle path.
2. `selfhost/disasm.funny` — `funny xray`.
3. `selfhost/fmt.funny` — the formatter. Port the known comment-dropping limitation, or fix it and log.
4. `selfhost/test.funny` — the `*.funny`/`*.expected` runner.
5. `selfhost/vibe.funny` — the REPL: persistent globals, multi-line continuation, last-expression
   auto-print, `.help/.exit/.clear/.xray/.time`. Needs `computer.readline`, added in N5.
6. `selfhost/cli.funny` — argument parsing and subcommand dispatch, so `native/main.c` shrinks to
   "load the toolchain bundle, hand it argv."

The self-hosting subset in `PLAN.md` §8 applies to every one of these files.

**Acceptance:** `funny run/build/xray/fmt/test/vibe/yeet/bootstrap` all work with the C binary, and
their output is byte-identical to the Python CLI's for every case the existing `tests/test_cli.py`
covers.

---

### N9 — The native bootstrap, CI, and release

**Tasks**
1. **Stage0.** Check in `bootstrap/funnyc.funnypak`: the self-hosted compiler, compiled once by the
   Python compiler. This is standard practice for self-hosted languages (rustc ships stage0 binaries;
   Go shipped a bootstrap tree for years). `bootstrap/STAGE0.md` records its provenance, the exact
   command that regenerates it, and the policy: **regenerate whenever `selfhost/` changes**, with CI
   enforcing freshness by rebuilding and diffing.
2. Native `funny bootstrap --verify`: stage2 → stage3 → stage4, all on the C VM, asserting
   stage3 == stage4 byte-for-byte. Keep `--keep` and `--diff`.
3. **The no-Python proof.** A CI job in a container with no Python at all: `cc -o funny native/*.c`,
   then build, run, yeet, and bootstrap. If this job passes, the plan's goal is met — this is the
   single most important test in the document.
4. Full CI matrix: 3 OSes × {gcc, clang, MSVC as available}, plus ASan/UBSan and `FUNNY_GC_STRESS=1`
   runs on Linux.
5. Docs: rewrite `README.md`'s install section around the C build; add `docs/NATIVE.md` (build
   instructions, porting notes, the GC contract); update `docs/BYTECODE.md` if any format detail moved
   (it should not).
6. Version → 2.0.0. `CHANGELOG.md` gets an N-series section.
7. Honesty pass on `README.md`'s "what this isn't": the VM is no longer Python, so that paragraph is
   now wrong and must be rewritten to say what *is* true — the compiler is FunnyLang, the runtime is
   C, and nothing about that is self-hosted-all-the-way-down, because nothing ever can be.

**Acceptance:** §1's transcript, reproduced exactly, on a machine with no Python installed.

---

### N10 — Prebuilt binary distribution

Building from source must always *work*, but it must never be the only option. Almost nobody who
wants to write FunnyLang wants to install a C toolchain first, and "no Python required" is a hollow
promise if the alternative is "install MSVC instead." The deliverable is a **single downloadable
file per platform** that runs immediately.

**Tasks**

1. **Embed the toolchain bundle into the binary.** After N8, `funny`'s compiler, linker, formatter,
   disassembler and REPL are FunnyLang bytecode. Generate `native/toolchain_blob.c` — a `static const
   unsigned char[]` of the linked `.funnypak` — at build time, so the shipped artifact is one file
   with no sidecar. A dev-time `bin2c` generator plus a checked-in generated blob, following the same
   pattern as `unicode_tbl.c`, so the build still needs nothing but a C compiler. Write the generator
   in FunnyLang (`tools/bin2c.funny`, run by the native binary), not Python: N11 removes every `.py`
   from the repository including dev tooling, and a generator written now in Python is one that has
   to be rewritten then.
2. **Release matrix.** Native runners where GitHub has them, `zig cc` cross-compilation where it does
   not:

   | Artifact | Runner | Notes |
   |---|---|---|
   | `funny-windows-x86_64.exe` | `windows-latest` | MSVC |
   | `funny-linux-x86_64` | `ubuntu-22.04` | Old glibc on purpose — see task 3 |
   | `funny-linux-aarch64` | `ubuntu-24.04-arm` | Native ARM runner |
   | `funny-macos-x86_64` | `macos-13` | Intel |
   | `funny-macos-aarch64` | `macos-14` | Apple Silicon |

3. **Build on the oldest glibc you can stand.** A binary linked against glibc 2.39 refuses to start on
   Debian 12. Build the Linux artifacts on `ubuntu-22.04` (glibc 2.35) so they run on anything newer,
   and assert the floor with `objdump -T funny | grep GLIBC_ | sort -u` in CI.
4. **Strip and check size.** `strip -s` on POSIX, `/RELEASE` on MSVC. CI fails the release if any
   artifact exceeds 2 MB — a regression past that means something (a debug section, an accidentally
   embedded corpus) got shipped by mistake.
5. **`SHA256SUMS`**, generated and uploaded with the release, so a download can be verified.
6. **Two publishing triggers**, both to **GitHub Releases** on `github.com/Sam-H101/FunnyLang`:
   - **Tagged releases** (`v*`) → a permanent, versioned GitHub Release with all five artifacts, the
     checksums, and the `CHANGELOG.md` section as the release body.
   - **Rolling `nightly`** on every green push to `master` → a prerelease tagged `nightly`, force-moved
     each time, so `master` is always downloadable without cutting a version.
7. **Install scripts.** `install.sh` (detects OS/arch via `uname`, downloads, verifies the checksum,
   drops the binary in `~/.local/bin`) and `install.ps1` (same, into `%LOCALAPPDATA%\Programs\funny`,
   and adds it to the user `PATH`). Both must refuse to proceed on checksum mismatch.
8. **README download table** at the top of the install section, above the build-from-source
   instructions — downloading is the default path, building is for contributors.
9. **Post-publish verification job.** After the release is created, a *separate* job on a clean runner
   downloads the published artifact from the release URL, verifies its checksum, and runs
   `funny run examples/hello.funny` plus `funny bootstrap --verify`. This is the task that catches the
   classic release bugs — an artifact that was never uploaded, uploaded truncated, uploaded without
   the executable bit, or built against a glibc the world does not have. **A release that fails this
   job is deleted, not patched.**

**The workflow, in outline** (`.github/workflows/release.yml`):

```yaml
name: release
on:
  push:
    tags: ["v*"]
    branches: [master]        # rolling `nightly`
permissions:
  contents: write             # required to create releases; nothing more
jobs:
  build:
    strategy:
      fail-fast: false        # one platform failing must not hide the others
      matrix:
        include:
          - { os: windows-latest,  target: windows-x86_64, ext: .exe }
          - { os: ubuntu-22.04,    target: linux-x86_64,   ext: "" }
          - { os: ubuntu-24.04-arm, target: linux-aarch64, ext: "" }
          - { os: macos-13,        target: macos-x86_64,   ext: "" }
          - { os: macos-14,        target: macos-aarch64,  ext: "" }
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - run: ./build.sh                       # build.bat on Windows
      - run: ./funny test examples/           # never publish a binary that fails its own suite
      - run: ./funny bootstrap --verify
      - run: mv funny${{ matrix.ext }} funny-${{ matrix.target }}${{ matrix.ext }}
      - uses: actions/upload-artifact@v4
        with: { name: "funny-${{ matrix.target }}", path: "funny-${{ matrix.target }}*" }

  publish:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with: { merge-multiple: true, path: dist }
      - run: cd dist && sha256sum funny-* > SHA256SUMS
      - uses: softprops/action-gh-release@v2
        with:
          files: dist/*
          tag_name: ${{ startsWith(github.ref, 'refs/tags/') && github.ref_name || 'nightly' }}
          prerelease: ${{ !startsWith(github.ref, 'refs/tags/') }}

  verify-published:                           # task 9 — the job that actually matters
    needs: publish
    strategy: { matrix: { os: [windows-latest, ubuntu-latest, macos-latest] } }
    runs-on: ${{ matrix.os }}
    steps:
      - name: Download from the release and run it
        run: |
          # fetch via `gh release download`, verify SHA256SUMS, chmod +x, then:
          ./funny run examples/hello.funny
```

Note `./funny test examples/` and `bootstrap --verify` running *before* upload: the release pipeline
is the last place a broken binary can be stopped, so it runs the real suite, not a smoke test.

**Acceptance**
- A pushed tag produces a GitHub Release with five binaries and a `SHA256SUMS`, and the
  `verify-published` job passes on all three OSes.
- On a clean VM with no Python, no C compiler, and no FunnyLang checkout:
  `curl -fsSL .../install.sh | sh` then `funny run hello.funny` works.
- Every artifact is under 2 MB.
- The Linux artifacts run on Debian 12 (verified in a container in CI).

---

### N11 — Removing Python from the repository

Added at the project owner's direction, and the actual destination of this whole plan: not "Python
isn't needed at run time" but **no `.py` file anywhere in the repository**. Asked how far that goes,
the owner chose the maximal reading twice — the implementation in `funnylang/`, the ~1,200-test
`pytest` suite in `tests/`, *and* dev-time tooling (`tools/gen_unicode.py`, `tools/bin2c.py`), even
though a generator only ever runs on a maintainer's machine and never touches a user's build.

The load-bearing problem is the **test oracle**. Almost every test in `tests/` compiles or runs
something through `funnylang` and diffs the result — delete `funnylang/` and those tests do not fail,
they stop existing. Two cheaper answers were offered and rejected: keeping `pytest` as a
subprocess-only driver (Python survives as dev tooling), and freezing today's differential output as
goldens (the goldens are then only as good as today's Python, with no oracle to re-derive them from).

**Tasks**
1. **Grow the golden corpus first, delete nothing yet.** `tests/lang/`'s `.funny`/`.expected` pairs
   are the only tests that already survive with zero Python, and `funny test` already runs them.
   Expand them from ~85 pairs to cover what the `pytest` suite covers: the whole stdlib surface,
   every error flavor and diagnostic shape, the GC under stress, bignum and float-formatting edge
   cases, pointers, modules, the CLI's own subcommands.
2. **Report the gap honestly.** Some coverage cannot become a golden — anything asserting on internal
   structure (`CompiledUnit` fields, GC bookkeeping), anything comparing two implementations, and
   the fuzzers. Each such test gets an explicit disposition: replaced by a golden, replaced by a C
   unit test, replaced by a `.funny` test that asserts the property from inside the language, or
   dropped with a stated reason. No silent deletions.
3. **Port the dev-time generators to FunnyLang.** `tools/gen_unicode.py` (regenerates
   `unicode_tbl.c`) and N10 task 1's `bin2c` become `.funny` scripts the native binary runs — which
   is possible precisely because the toolchain is self-hosted by then. Their outputs stay checked in,
   so the build still needs nothing but a C compiler.
4. **Rewrite CI without Python.** Every workflow step that invokes `python`/`pytest` becomes
   `funny test` or a C unit-test binary.
5. **Delete `funnylang/`, `tests/*.py`, `conftest.py`, `pyproject.toml`/packaging, and any remaining
   `.py`.** One commit, after the suite above is green without them.
6. **Rewrite the docs that describe Python as the implementation** — `README.md`, `PLAN.md`'s
   framing, `docs/` — so nothing tells a reader to install Python.

**Acceptance**
- `find . -name '*.py'` returns nothing.
- CI is green with no Python installed on any runner (N9 task 3's no-Python container becomes the
  *only* kind of job, not a special one).
- The coverage gap from task 2 is written down in §9, test by test, with its disposition.

---

## 7. Order of operations

```
PLAN.md M15 pointers (Python + selfhost, the differential oracle)
N0 scaffold → N1 values/GC/bignum → N2 loader+core loop → N3 functions/closures/errors
   → N4 strings/collections/classes → N5 stdlib+modules → N5b HTTPS/TLS
   → N6 diagnostics → N7 native CLI+yeet → N8 toolchain in FunnyLang → N9 bootstrap
   → N10 prebuilt binary distribution → N11 removing Python from the repository
```

N11 is last for a reason: it deletes the differential oracle every milestone before it relies on, so
it cannot start until the native toolchain is the thing being shipped and its own test corpus stands
on its own. Its first task (growing the goldens) can, and should, start earlier — every milestone
that adds a `.funny`/`.expected` pair instead of a Python-only test makes N11 smaller.

N1 is the hardest and most load-bearing; do not rush it. N5 is the longest; it is also the most
parallelizable and the most mechanical.

**`PLAN.md` M15 (pointers) comes before N2, not after N10.** The C VM must not be the first
implementation of a language feature: §5's whole testing strategy is diffing against the Python
runtime, and a feature Python cannot execute has no oracle to diff against. Build pointers in the
Python VM and in `selfhost/` first, get them into the golden corpus, and the C implementation is then
verified by the same machinery as everything else. This applies to *any* future language addition,
not just this one. N0's task 6 (the interim GitHub Release of the existing
PyInstaller build) is deliberately out of dependency order — it ships downloadable binaries on day
one rather than at the end, and N10 replaces it once the native toolchain exists.

---

## 8. Definition of Done (v2.0.0)

- [ ] `cc -O2 -o funny native/*.c` builds clean, `-Wall -Wextra -Werror`, on Windows/Linux/macOS.
- [ ] Zero third-party dependencies, build-time or runtime.
- [ ] The full `tests/lang/` + `examples/` corpus produces byte-identical stdout, stderr, and exit
      codes under the C VM and the Python VM.
- [ ] The whole differential suite passes under `FUNNY_GC_STRESS=1`.
- [ ] ASan and UBSan clean.
- [ ] `funny yeet` produces a binary under 1 MB that runs with no Python installed.
- [ ] `funny bootstrap --verify` reaches its fixed point entirely on the C VM.
- [ ] The no-Python CI job passes.
- [ ] `internet.*` performs real HTTPS with certificate verification on Windows, macOS, and Linux —
      and degrades to a clean `SkillIssue`, never a crash, where no TLS backend exists.
- [ ] A tagged push publishes five verified binaries to GitHub Releases, and `verify-published`
      downloads and runs each one successfully.
- [ ] `install.sh` / `install.ps1` get a working `funny` onto a clean machine with no toolchain.
- [ ] Pointers (`PLAN.md` §3.10) behave identically on both VMs, including every error row — and no
      pointer operation, on any input, can crash the C VM or corrupt its heap.
- [ ] `PLAN.md` §10's performance targets still met — and `fib(25)` should now be *much* faster.
- [ ] The Python implementation still passes its own tests (1,065 as of M15), unchanged, as the
      reference oracle.

---

## 9. Change log (executing agent: append here)

Same convention as `PLAN.md` §16 — every deviation, AGENT CHOICE resolution, and spec contradiction
gets an entry explaining what changed and why.

- **N0 · AGENT CHOICE · implementation language.** C11, no extensions outside `platform.c`. Rejected
  Rust and Zig: both require installing a toolchain to build, which reintroduces the dependency this
  plan exists to remove. `zig cc` is supported as an optional cross-compiler requiring no source
  changes.
- **N0 · AGENT CHOICE · value representation.** Tagged union, not NaN-boxing. Correctness first;
  arbitrary-precision integers mean many numbers are heap objects anyway, which blunts NaN-boxing's
  advantage. Revisit post-2.0 with benchmarks.
- **N0 · SPEC CHANGE · `PLAN.md` §11.** "Threads, async, or a GC (Python's refcounting is the GC)"
  is no longer accurate: the C runtime requires a real garbage collector, specified in N1. `PLAN.md`
  §11 must be amended to remove the GC clause. Threads and async remain non-goals.
- **Prerequisite complete · `PLAN.md` M15 (pointers).** Per §7's ordering, this had to land in the
  Python VM and `selfhost/` before N2, so the C VM never becomes the first implementation of a
  language feature. Done: `pointa` (§3.10) fully implemented across the lexer (no changes needed),
  parser, resolver, compiler, VM, and serializer (`BYTECODE_VERSION` → 2), plus `selfhost/parser.
  funny` and `selfhost/compiler.funny` learning to compile pointer syntax, cross-checked
  byte-for-byte against the Python compiler's output. Full log in `PLAN.md` §16. N0 can now begin.
- **N5b · DECISION · HTTPS via OS-native TLS.** `internet.*` keeps full HTTPS in the C runtime, using
  the TLS each OS already ships — WinHTTP on Windows, `Security.framework` on macOS, and `dlopen`'d
  OpenSSL on Linux. None of these is a third-party dependency in the sense `PLAN.md` §11 forbids: two
  are the operating system, and the third is resolved at run time, so the binary still builds and
  runs where it is absent. Bundling a crypto library (mbedTLS et al.) was rejected as a genuine §11
  violation, and HTTP-only was rejected as too large a capability loss to ship in a 2.0. Certificate
  verification is mandatory and has no opt-out.
- **N3/N4 · ADDITION · pointers.** `PLAN.md` §3.10 and M15 add a `pointa` type at the project owner's
  request. The C-side work is opcodes 73–79, split between N3 (variables, which reuse the upvalue
  boxing) and N4 (places, which need containers to exist first), plus GC tracing in N1. Sequenced
  **after** M15's Python implementation, never before it — see §7 for why a feature must exist in the
  oracle before it exists in the C VM. The safety argument for the whole design rests on N4's
  read-time bounds checking, so that is where it gets tested hardest.
- **N0 · complete.** `build.sh`/`build.bat`, `native/main.c` (a plain-ASCII toolchain smoke test —
  the real Unicode banner needs `platform.c`'s UTF-8 console setup, which doesn't exist yet),
  `native/ARCHITECTURE.md`, and a `native` CI job (Windows/Linux/macOS, release + debug/sanitizer
  builds) are all in place; YAML and shell syntax checked locally, full verification pending CI's
  actual compilers, since this environment has none installed.
  **AGENT CHOICE · Windows C compiler for CI.** MSVC via `ilammy/msvc-dev-cmd@v1` (provisions
  `cl.exe`'s environment on `windows-latest`), not MinGW — matches N10's own release-matrix choice
  (`NATIVE_PLAN.md` §6.N10) and needs no extra toolchain install on the runner. `build.bat` targets
  `cl.exe` accordingly; `PLAN.md`'s "MSVC + MinGW" requirement (§2.4) means MinGW must also keep
  working, but CI only needs to prove *one* Windows path continuously — MinGW gets exercised by
  `zig cc` compatibility testing later, not by a second parallel CI job now.
  **AGENT CHOICE / SPEC CORRECTION · task 6 freezes the CLI, not `funny yeet`.** Checked
  `packager.py`/`stub_main.py` before implementing and found that `funny yeet` freezes
  `stub_main.py`, which by design *only ever runs one already-linked `.funnypak` baked in at build
  time* (its own docstring: "a shipped .exe never compiles source, only runs already-linked
  bytecode") — it takes no source-file argument at run time. A `yeet`'d `examples/hello.funny`
  binary would only ever print "hello", forever; it would not let anyone "start writing FunnyLang,"
  directly contradicting this task's own stated purpose. Fixed by freezing
  `funnylang/__main__.py` (the `funny = funnylang.cli:main` console-script entry point) with
  PyInstaller directly instead — the real compiler/parser/VM/REPL, exactly like an editable
  `pip install` gives you, just with Python bundled in. This is a different PyInstaller target from
  `packager.py`'s, reusing only its `--hidden-import funnylang.stdlib.*` pattern (every stdlib
  module is chosen by name at runtime, so PyInstaller's static import scan needs the nudge either
  way). `release-build`/`release-publish` jobs added to `ci.yml`, gated on `startsWith(github.ref,
  'refs/tags/v')`; the CI smoke test runs the frozen binary's `--version`, `run examples/hello.funny`,
  and `test examples`, not just a version print, so a broken freeze fails CI instead of shipping.
- **N1 · complete (values, GC, bignum, numfmt) · all four acceptance criteria met.** `value.h/.c`
  (tagged-union `Value`, narrow equality/truthiness), `object.h` (the common `Obj` header),
  `gc.c` (mark-sweep with a gray stack, a temp-root stack for the native-function protocol, an
  external-roots callback N2's VM will register into, `FUNNY_GC_STRESS` via `getenv`), `bignum.c`
  (sign-magnitude, Knuth Algorithm D division), and `numfmt.c` (float formatting). Every acceptance
  bar hit for real, not approximately: `test_bignum`/`test_value`/`test_gc` assertion suites pass;
  1,000,000-case differential fuzz against Python's own `int` (bignum) and `repr()` (numfmt) each
  pass with **zero** mismatches (`tests/native/fuzz_*_check.py`); every unit test and both fuzzers
  are ASan/UBSan-clean under both gcc and clang. Verified via a WSL Ubuntu toolchain installed this
  session specifically for this purpose — see the persisted memory
  `wsl-c-toolchain-for-native-runtime-work.md`.
  **AGENT CHOICE · numfmt.c is not literally Ryū.** Full rationale lives in numfmt.h's own header
  comment: exact bignum rational arithmetic plus round-trip verification via `strtod`, reusing
  bignum.c's already-proven correctness, rather than a hand-rolled table-driven Ryu implementation.
  Behaviorally identical (matches `repr()` byte for byte, confirmed at 1M cases); revisit for raw
  formatting speed post-2.0 if it's ever actually on a hot path.
  **object.c was planned (§4) but never written** — `object.h` alone (a struct + enum) turned out to
  be sufficient; the "allocate and link" step differs enough per concrete type (`bignum_new`'s plain
  `malloc`, deliberately GC-agnostic, versus `gc_track()`'s separate opt-in) that a shared
  `allocate_obj` had no real body to contain. May return once N4 needs shared object-level
  utilities.
  Three real bugs, found only by testing (not by re-reading the code) — the concrete argument for
  why this milestone's acceptance criteria demand fuzzing, not just hand-picked unit tests:
  1. **bignum.c: every nonzero result of `bignum_add`/`bignum_sub`/`bignum_divmod_trunc`'s sign was
     silently zeroed.** Each called `bignum_is_zero(r)` (which reads `r->sign`) to decide whether to
     apply the caller's intended sign — but `r` came straight out of a magnitude-only helper
     (`mag_add`/`mag_sub`/`mag_divmod`) whose result's `sign` field is still the `bignum_new` default
     of 0 at that point, regardless of whether the magnitude is actually zero. `99999999999999999999
     + 1` printed as `"0"`. Fixed by checking `r->count == 0` (the real signal `trim()` already
     establishes) instead of the not-yet-meaningful `sign`. Caught immediately by
     `test_add_sub_basic`'s very first assertion.
  2. **numfmt.c: `compare_to_power_of_ten` leaked a bignum on every call taking its negative-exponent
     branch** (the `pw` allocated there was never freed). Invisible under a plain correctness run;
     caught by running the differential fuzzer under ASan with leak detection.
  3. **numfmt.c: `compute_dec_exp`'s original double-based initial guess could hang for a very long
     time on subnormal doubles.** It computed `bignum_to_double(num) / bignum_to_double(den)`, but a
     subnormal's exact denominator is up to 2^1074 — which overflows a `double` to `+Inf`, sending
     `log10` to `-Inf` and an out-of-range `(int)` cast (undefined behavior) to a garbage
     huge-magnitude value, which the correction loop then had to walk back one step at a time toward
     the real answer (~-324) — in practice, took effectively forever rather than genuinely never
     terminating. `numfmt_repr(5e-324)` (the smallest positive double) hung; found by timing out a
     debug harness that printed progress per test case and watching where it stalled. Fixed by
     estimating the initial guess from bit lengths instead of ever converting either bignum to a
     `double`.
- **N2 · complete for its stated opcode subset.** `opcodes.h` (the full 0-79 `Op` enum, mirroring
  `funnylang/opcodes.py` 1:1), `chunk.c` (a byte-for-byte `.funnyc` loader matching
  `funnylang/serializer.py`'s `load_funnyc`), and `vm.c` (a `switch`-dispatched interpreter for
  NOP/CONST/GHOST/FAX/CAP/POP/DUP/SWAP, GET/SET_LOCAL, GET/SET/DEF_GLOBAL, all of ADD..SHR, all of
  EQ..IN, every jump form including `JUMP_LONG`/`LOOP_LONG`, YAP, HALT, and RETURN — present in
  every compiled script's bytecode even though N2's own task list doesn't name it, so it has to be
  handled here too, as "stop and hand back the value" since there's no caller yet). Verified with a
  dedicated differential suite (`tests/native/test_n2_differential.py` + `tests/native/
  n2_programs/*.funny` — renamed to `test_native_differential.py`/`programs/` in N3, see that
  entry, since none of the existing `tests/lang/` goldens stay inside N2's narrow
  subset — every one of them declares at least one function, which needs `CLOSURE` just to bind the
  name) against the real Python VM, on gcc and clang, clean under ASan/UBSan, clean under
  `FUNNY_GC_STRESS=1`.
  **Bug found by the differential suite, not by re-reading the code: `BAND`/`BOR`/`BXOR` didn't
  preserve `boolski`-ness.** Python's `bool` overrides `&`/`|`/`^` to stay `bool` when *both*
  operands are `bool` (`True & False` is `False`, not `0`) but reverts to plain `int` the moment
  either side isn't (`True & 5` is `1`) — `funnylang/vm.py`'s `_bitwise` inherits this for free from
  Python's own operators, since it just does `a & b` on whatever Python values it's holding. The C
  VM has no such inheritance and has to check for it explicitly; the first implementation didn't,
  so `fax & cap` printed `0` instead of `cap`. Caught immediately by `bitwise.funny`'s own last two
  lines.
  Three scope calls worth recording:
  1. **No call-frame concept yet.** N2's opcode subset excludes CALL/CLOSURE/RETURN-to-a-caller, so
     the VM only ever runs exactly one `FunctionProto` (the entry proto) straight through, with
     internal jumps; `GET_LOCAL`/`SET_LOCAL` index the value stack directly at slot 0 base. A real
     `Frame`/call-stack concept arrives with N3.
  2. **Globals are a linear-scan array, not `table.c`'s real hash table.** Correctness first, and
     N2's own test programs have a handful of globals at most; `table.c` (insertion-ordered, needed
     for real once `groupchat` and interning exist) is still N4's to write.
  3. **Both shift directions always route through `bignum_shl`/`bignum_shr`, never a native `<<`/
     `>>` on `int64_t`.** `SHL` can outgrow 64 bits from a shift as small as ~64 regardless of the
     base, so it needs the bignum path unconditionally; sharing that path for `SHR` too (which
     would be safe as a native shift) avoids relying on C's implementation-defined behavior for
     shifting a negative value or shifting by ≥ the type's width, at a cost (an extra allocation per
     shift) that doesn't matter yet.
- **N3 · complete for its stated tasks, two deliberately deferred.** `frames.h/.c` (`ObjClosure`,
  `ObjUpvalue`, `Frame`, ported *structurally* from `funnylang/vm.py`'s own Frame/Closure/Upvalue —
  same Lua/clox model), `error.h/.c` (`ObjError` with PLAN.md §3.9's field set), `pointa.h/.c`
  (`ObjPointa`, the `POINTA_CELL`/`POINTA_GLOBAL` kinds N3 needs), and a substantially rewritten
  `vm.c`: CALL/RETURN with arity checking and default parameters, CLOSURE/GET_UPVAL/SET_UPVAL/
  CLOSE_UPVAL, TRY_PUSH/TRY_POP/CHUCK with full unwinding (including the awkward cases — `bounce`
  inside a `sketchy` with a `regardless`, and a `chuck` from inside the `regardless` block itself),
  PTR_LOCAL/PTR_GLOBAL/PTR_UPVAL/DEREF/SET_DEREF, and the 10,000-frame recursion cap. Also added
  `GET_PROP` (opcode 16), not in N3's own task list but required the moment a caught error's
  `.flavor`/`.message` needs reading — restricted for now to `ObjError` (Instance/Squad field access
  and Stash/GroupChat/yapstring/numba method binding stay N4/N5's).
  Verified against 12 new differential programs under `tests/native/programs/` (functions, closures
  with the aliasing case, try/catch/finally including nested rethrow-from-finally, pointers,
  10,000-deep recursion caught cleanly) — all match the Python VM exactly, on gcc and clang, under
  `FUNNY_GC_STRESS=1`, clean under ASan/UBSan. Genuinely no bugs found in this pass (unlike N1/N2,
  where the same testing discipline caught real ones) — worth recording plainly rather than only
  ever reporting the catches, so this log stays a fair record and not a highlight reel.
  **Architecture note, not a deviation:** the dispatch loop is fully iterative — `CALL` pushes onto
  `vm->frames` (a heap array) and the *same* `for(;;)` loop keeps running, it never recurses at the
  C level. So does `funnylang/vm.py`'s own `_run` (`_do_call` → `_push_closure_frame` just appends
  to `self.frames`; `_run`'s `while True: frame = self.frames[-1]` loop continues). Ported
  structurally, this means FunnyLang recursion depth was never actually bounded by the C stack in
  the first place — the 10,000-frame cap is `TooDeepBro`'s existing safety net (unbounded-memory /
  runaway-recursion protection), not stack-smashing prevention, on both VMs alike.
  **Two tasks deliberately deferred to N4, both for the same reason — the type they need doesn't
  exist yet:**
  - **Variadic functions (`...rest`).** Binding the "rest" args needs a real `Stash`. `do_call`
    rejects a variadic call with a clean `SkillIssue` rather than mishandling it silently.
  - **`ITER_NEW`/`ITER_NEXT`** (task 5). For-each iterates a stash, groupchat, or yapstring, and
    `BUILD_STASH` (needed just to construct one to iterate) is N4's opcode too — there was nothing
    real to test this against yet. `pointa`'s own `.deref()`/`.set()`/`.valid()`/`.where()` method
    forms are similarly deferred: they need `INVOKE`, which N4 adds alongside squad/instance
    methods; the core `&`/`*` mechanics N3 task 7 actually asks for don't depend on it.
- **N10 · ADDITION · prebuilt binary distribution.** Not in the original plan; added at the user's
  request so that using FunnyLang never requires building it. GitHub Releases on
  `github.com/Sam-H101/FunnyLang`, five platform artifacts per tag plus a rolling `nightly`, checksums,
  install scripts, and a `verify-published` job that re-downloads each artifact and runs it. An
  interim job in N0 publishes the *existing* PyInstaller binaries immediately, so downloadable builds
  exist long before the native runtime is finished.
- **N5 · AGENT CHOICE · PRNG.** `rizz` uses xoshiro256\*\* rather than reproducing CPython's Mersenne
  Twister stream. `rizz.seed(n)` is reproducible within the C VM; cross-VM stream equality was never
  specified and no golden file depends on it.
- **N4 · sub-phase 4a/4b complete · `stash` and `groupchat`.** `stash.h/.c` (`ObjStash`, all 21
  instance methods from `funnylang/stdlib/stash.py`'s own `METHODS` dict) and `groupchat.h/.c`
  (`ObjGroupChat`, all 11 from `groupchat.py`'s `METHODS`), plus the VM plumbing both need:
  `ObjBoundNative`/`bound_native_new`/`vm_call_value` (a receiver-bound native method as a
  first-class callable `Value`, and a way for one to call back into FunnyLang code), `GET_PROP`
  binding a Stash/GroupChat method, `SET_PROP`/`GET_PROP_SAFE`, `GET/SET_INDEX`, `GET_SLICE` (full
  Python `slice.indices()` semantics, ported — start/stop default differently depending on step's
  sign, and out-of-range explicit bounds clamp rather than error), `BUILD_STASH`/`BUILD_GROUPCHAT`/
  `BUILD_STRING`, `INVOKE`/`INVOKE_OG` (the latter always raises `NotACallableRizz` for now — `og`
  is only ever meaningful inside a squad method, and squad.c doesn't exist yet), `Stash`+`Stash`
  concatenation and `Stash`*`int` repetition in `vm_add`/`vm_mul`, and a real structural
  `vm_value_equal` (`funny_eq`, ported) so `OP_EQ`/`stash.contains`/`index_of`/`OP_IN` compare
  Stash/GroupChat contents recursively rather than by pointer identity — `value_equal_narrow`
  (N1, `value.c`) stays deliberately scalar-only, exactly as its own doc comment always said it
  would.
  **AGENT CHOICE · GroupChat is a linear-scan, insertion-ordered array (`GroupChatEntry*`), not
  `table.c`'s real hash table** — same reasoning as N2's globals table: `keys()`/`values()`/`pairs()`
  all depend on Python dict's insertion-order guarantee, so correctness and order-preservation come
  first; raw lookup performance is a later milestone's problem if one ever needs it.
  **Found & fixed in passing: N3 task 5 (`ITER_NEW`/`ITER_NEXT`) was deliberately deferred to N4 (see
  N3's own entry above) but nothing had circled back to actually write it.** Implemented now —
  `iterator.h/.c` (`ObjIterator`, a snapshot array materialized once at `ITER_NEW` time, exactly
  matching `funnylang/vm.py`'s own `iter(list(...))`/`iter(str)`: a stash mutated mid-loop is
  iterated as it was at loop-start, not live) — since `grind x in <stash-or-groupchat>` needed it to
  write any realistic test program at all.
  **GC-safety fix, caught by `FUNNY_GC_STRESS=1` before it ever shipped:** a bound native method's
  receiver and arguments are popped off `vm->stack` before the call (so a raw pointer into them
  can't dangle if the call reallocs `vm->stack`, the same hazard `ObjUpvalue`/`ObjPointa` already
  guard against) — but that also means `mark_vm_roots` can no longer see them mid-call, and a
  callback-taking method (`sort`/`glow_up`/`vibe_check`/`squish`/`any`/`all`) can trigger arbitrarily
  many collections via nested `vm_call_value` calls while it's still running. Fixed by
  `gc_push_temp`-rooting the receiver and every argument for the duration of the call
  (`call_bound_native`/`do_invoke`) — exactly the "#1 correctness hazard" `gc.h`'s own header comment
  already named. Verified by diffing native output against the Python VM's byte-for-byte, with
  `FUNNY_GC_STRESS=1`, on both gcc and clang, ASan/UBSan clean — this is the fix that discipline
  exists to catch, and it did.
  **Scope carried forward to later N4 sub-phases, not done here:** `squad.c` (methods/inheritance/
  `me`/`og`/`spawn`), real UTF-8 `yapstring` (indexing/slicing above is still byte-indexed, correct
  for ASCII only — `GET_INDEX`/`GET_SLICE`/`ITER_NEW`'s string cases all carry the same noted gap),
  self-referential-print's `[...]`/`{...}` guard is done for Stash/GroupChat but not yet exercised
  against Instance once Squad exists, and `PTR_INDEX`/`PTR_PROP` (pointers into stash/groupchat
  places). **`unicode_tbl.c`/`tools/gen_unicode.py` deferred to N5**, confirmed still correct: the
  only consumers (`yapper.is_letter`/`is_alnum`) are stdlib module functions, not wired up until N5.
  Verified: 2 new differential programs (`stash_methods.funny`, `groupchat_methods.funny`) plus all
  12 pre-existing ones, byte-identical against the Python VM on gcc and clang, `-Wall -Wextra
  -Werror` clean, ASan/UBSan clean, `FUNNY_GC_STRESS=1` clean (both normal and byte-diffed against
  the Python VM's own output, not just checked for a zero exit code).
- **N4 · sub-phase 4c complete · `squad.c` (task 6).** `ObjSquad` (name, superclass, a linear-scan
  method table -- same AGENT CHOICE as everywhere else this milestone), `ObjInstance` (a squad
  pointer plus a linear-scan field table), and `ObjBoundMethod` (a receiver+`ObjClosure` pair, for
  when a method is read off an instance as a value rather than immediately invoked -- `yo bump =
  counter.bump` needs this for `bump()` to still work later). `SQUAD`/`METHOD`/`INHERIT` opcodes;
  `CALL` construction (`_construct`, ported: `spawn` found via `find_method` walking the superclass
  chain, never a dedicated field, so a subclass with no `spawn` of its own inherits the nearest
  ancestor's, and a squad with no `spawn` anywhere still constructs fine with args silently
  discarded); `INVOKE`'s Instance fast path (a field that shadows a method name is called as a
  plain value, no receiver prepended -- caught and fixed a real bug here, see below); `INVOKE_OG`
  resolving relative to `frame.closure.homeSquad` (a new `ObjClosure` field, set by `METHOD`), never
  the receiver's own runtime class -- the actual M9 fix this task was asked to port, verified against
  a 3-level `og` chain (`squad_multilevel_inheritance.funny`); `GET_PROP`/`SET_PROP`/`GET_INDEX`/
  `SET_INDEX` all gaining Instance branches (fields, then `find_method` -> `ObjBoundMethod`; an
  unset field or unknown name reads as ghost, not `WhoDis` -- deliberately different from every other
  type's `GET_PROP`, matching `funnylang/vm.py` exactly; `get_it`/`set_it` for indexing); and the two
  magic methods that aren't reachable through ordinary property/index access -- `to_yap` (display)
  and `same_energy` (equality) -- wired into `value_to_display`/`vm_value_equal`, both of which
  therefore now take `VM*` (a real call back into FunnyLang, same as the callback-taking stash
  methods) and both of which root their argument(s) as GC temps for the call's duration, same reason
  and same fix as call_bound_native's own.
  **Bug caught before it shipped, by reasoning through the Python source rather than by a failing
  test:** `INVOKE`'s Instance fast path, when a field shadows a method name, must call the field's
  *value* (e.g. a stored closure), not the receiver itself -- an early draft called `do_call` without
  first overwriting the receiver's own stack slot with the field's value, which would have tried to
  call the *instance* as if it were the callable. Fixed before ever compiling it; the differential
  suite's own `squad_extra_coverage.funny` (written after the fix, not before) exercises this path
  specifically (`Shadow.greet` stores a closure under the same name as its own `bet greet()` method)
  so a regression here would be caught.
  **Scope note, not a deviation:** the always-in-scope `how_thicc(x)` builtin (a free function that
  dispatches to `x.squad.find_method("how_thicc")` for an Instance) is `builtins.py`'s, i.e. N5's --
  `tests/lang/squad_magic_how_thicc.funny` depends on it and was therefore *not* ported into this
  milestone's differential suite (everything else that file would exercise -- the magic method being
  found and called correctly -- is already covered by `squad_extra_coverage.funny` and
  `squad_magic_to_yap.funny`/`squad_magic_same_energy.funny`/`squad_magic_get_it_set_it.funny`
  going through squad.c's actual machinery directly).
  Verified: 13 differential programs (12 ported from `tests/lang/`'s own already-Python-verified
  squad suite, one rewritten to catch its uncaught error via `sketchy`/`my_bad` since the differential
  harness requires a clean exit, plus one new file covering the shadowing bug, 3-level `og`, and
  `same_energy` nested inside stash equality) -- byte-identical against the Python VM, gcc and clang,
  `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1` (byte-diffed against Python, not just a zero exit
  code). Real UTF-8 yapstring and pointers-to-places (`PTR_INDEX`/`PTR_PROP`) remain as N4's last
  two sub-phases.
- **N4 · sub-phase 4d complete · pointers to places (task 8).** `POINTA_INDEX`/`POINTA_PROP` join
  N3's `POINTA_CELL`/`POINTA_GLOBAL` -- `ObjPointa` gains `container`/`key` Value fields, shared by
  both kinds exactly as `funnylang/values.py`'s own `Pointa` shares them (a stash/groupchat key for
  INDEX, a field name for PROP). `PTR_INDEX`/`PTR_PROP` opcodes; `DEREF`/`SET_DEREF` extended via
  two new shared helpers (`pointa_deref_value`/`pointa_set_value`) that dispatch to the now-existing
  `vm_get_index`/`vm_set_index`/`vm_get_prop`/`vm_set_prop` (the latter two factored out of
  `GET_PROP`/`SET_PROP`'s own opcode bodies specifically so DEREF/SET_DEREF and the two opcodes share
  one implementation); pointer arithmetic in `vm_add`/`vm_sub`/`vm_compare` (only a "index"-kind
  pointa into a *Stash*, with a plain fixnum key, supports it -- `require_stash_pointa`, ported);
  place-identity equality in `vm_value_equal` (same box, or same container+key -- never the pointed-
  to value); and the pointa's own 4 instance methods (`.deref()` `.set()` `.valid()` `.where()`) via
  the same `NativeMethodFn`/`ObjBoundNative` machinery stash/groupchat's methods already use.
  `.valid()` is the one deliberate exception to "every native method leaves an error pending for the
  dispatch loop to unwind" -- it catches and clears `vm->hadError` itself, mirroring
  `funnylang/vm.py`'s own `try: deref(); return True except FunnyError: return False`.
  **Deliberate, narrow deviation from Python: a pointer's arithmetic requires its `key` to be a
  plain fixnum, not just "the container is a Stash."** `funnylang/vm.py`'s own
  `_require_stash_pointa` doesn't check this (a float-keyed stash pointer's arithmetic there just
  silently produces a float-keyed result -- a Python quirk nothing relies on, since forming a
  float-keyed pointer into a Stash isn't something any real program does), but `AS_INT` on a
  non-int `Value` in C reads the wrong tagged-union member -- undefined behavior, not merely wrong
  output. A clean, explicit `TypeVibeMismatch` instead is the safer call, and matches this
  milestone's own "never a segfault/UB" mandate better than reproducing the Python quirk exactly
  would.
  **GC-safety fix, found by tracing the code rather than by a failing test (unlike the earlier
  sub-phases' fixes, `FUNNY_GC_STRESS=1` didn't happen to catch this one -- none of the differential
  programs' pointer *keys* were containers holding a `to_yap`-able Instance, so the unsafe path was
  never actually exercised):** `PTR_INDEX`'s label (`vm_index_label`, used for both the opcode and
  pointer arithmetic) reprs the container/key to build e.g. `"stash[3]"` or `"groupchat[\"k\"]"` --
  which, for an arbitrary user-supplied key, can call back into FunnyLang (an Instance `key`, or one
  nested inside a Stash/GroupChat `key`, via `to_yap`) exactly like `value_to_display` already does.
  `OP_PTR_INDEX` pops both `container` and `key` off `vm->stack` *before* building the label, so
  neither was reachable from any root during that nested call -- fixed by rooting both for the
  call's duration, same fix and same reason as call_bound_native's own. Pointer *arithmetic*'s own
  calls to the same label function needed no such fix: the key they label is always a freshly
  computed `INT_VAL`, never anything that can carry a `to_yap`.
  Verified: 9 differential programs (8 ported from the Python-verified `tests/lang/` ptr suite --
  one, `ptr_methods.funny`, adapted to drop two calls to N5's `what_is_it`/`to_yap` builtins, neither
  of which exists yet; one, the 5 runtime-error `err_ptr_*.funny` files, merged into a single
  `ptr_errors.funny` wrapping each in `sketchy`/`my_bad` since the differential harness requires a
  clean exit -- `err_ptr_address_of_const.funny` was *not* ported: `ImmutableVibes` is a compile-time
  check in `resolver.py`, unreachable from any `.funnyc` the native VM ever loads, so there is
  nothing for this milestone to port; plus one new file covering groupchat/instance-field pointers,
  an Instance reached through a pointer's own key, and pointer arithmetic/comparison across two
  different stashes), byte-identical against the Python VM, gcc and clang, `-Werror`, ASan/UBSan,
  and `FUNNY_GC_STRESS=1` (byte-diffed against Python, not just a zero exit code) -- this is the
  milestone NATIVE_PLAN.md itself calls out as "safe pointers are either true or aren't," and this
  is that verification.
  Real UTF-8 yapstring (task 1) is now N4's only remaining task.
- **N4 · sub-phase 4e complete · real UTF-8 yapstring (task 1) -- N4 is now fully complete.**
  `ObjString` gains `codepointCount`/`isAscii` (string.c's new `utf8_seq_len`/`utf8_decode_cp`/
  `utf8_codepoint_count`/`utf8_byte_offset_of` primitives, computed once at construction, an O(n)
  pass amortized over every later index/slice/measure). `GET_INDEX` is now codepoint-indexed
  (`len("héllo👋") == 6`, matching Python exactly) with an O(1) fast path when `isAscii`, an O(n) walk
  otherwise -- exactly the tradeoff this task's own line in the plan asks for. `GET_SLICE`'s
  non-ASCII path builds a codepoint→byte-offset table once (a single O(n) walk) rather than
  re-walking from byte 0 for every codepoint in the result, which would have made reversing a
  non-ASCII string (`s[::-1]`, the classic case that stresses this hardest) quadratic. `ITER_NEW`
  walks codepoint-at-a-time, matching Python's own `iter(str)`. `json_quote_string` (stash/groupchat
  repr's string-quoting, deferred here explicitly in the earlier stash/groupchat sub-phase's own
  changelog entry) now emits `\uXXXX` for every non-ASCII codepoint, with a UTF-16 surrogate pair for
  anything above U+FFFF (a 4-byte UTF-8 sequence) -- matching Python's `json.dumps(...,
  ensure_ascii=True)` default byte-for-byte, astral codepoints included. yapstring\*int/int\*yapstring
  repetition (`_mul`, also deferred to this sub-phase in that same earlier entry) is plain byte
  repetition -- repeating valid UTF-8 N times stays valid UTF-8, no codepoint awareness needed.
  Byte-level comparison (`vm_compare`), substring search (`OP_IN`), concatenation, and sort's default
  ordering were *already* correct for UTF-8 without any change -- a well-known property of the
  encoding (byte-wise ordering/matching of valid UTF-8 always agrees with codepoint-wise ordering/
  matching), confirmed rather than assumed by this sub-phase's own differential test.
  **AGENT CHOICE · no string interning**, despite task 1's own line mentioning it: every yapstring
  already compares by content (`string_equal`/`vm_value_equal`), never by identity, so interning
  would only be a memory/allocation optimization here, not a correctness requirement -- deferred
  until a milestone that actually needs the memory savings, logged rather than silently dropped.
  Verified: 1 new differential program (multi-byte codepoints, an astral emoji, indexing/slicing/
  reversing/striding/iterating/concatenating/repeating/comparing all of it, plus a stash holding a
  mix of plain-ASCII and non-ASCII strings to exercise the new JSON escaping) byte-identical against
  the Python VM, gcc and clang, `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.
  **N4 ("Strings, collections, classes") is now fully complete** -- all 8 tasks (yapstring, stash,
  groupchat, the collection/property opcodes, squad, self-referential print, pointers to places)
  done across five sub-phases this session, each independently tested and committed. `unicode_tbl.c`
  (task 2) remains deferred to N5, confirmed still correct: its only consumers
  (`yapper.is_letter`/`is_alnum`) are stdlib module functions not wired up until then.
- **N5 · sub-phase 5a complete · native-function registration (task 1) + the always-in-scope
  builtins (task 2's first module, `funnylang/stdlib/builtins.py`'s 16 functions).** `ObjNativeFn`
  (vm.h) is the registration mechanism itself -- mirrors `funnylang/values.py`'s own `NativeFn`
  exactly: `fn(vm, args) -> value`, no implicit receiver (unlike `ObjBoundNative`'s methods, where
  `args[0]` is always the receiver). `vm->builtins`, a namespace *separate* from `vm->globals`, with
  `GET_GLOBAL` (and the `POINTA_GLOBAL` deref path, which needed the identical fallback and didn't
  have it) checking `globals` first, then falling back to `builtins` -- matching
  `funnylang/vm.py`'s own `_read_global` exactly. `native/builtins.c` implements all 16 functions,
  reusing every primitive already built this milestone (`vm_call_value`, `vm_value_to_display`, a new
  `vm_value_to_repr` public wrapper, `squad_find_method`, bignum parsing for `to_numba`/`to_int`).
  **`dip()` needed real design work, not just porting:** Python's `SystemExit` (what `dip()` raises)
  isn't a `FunnyError` subclass, so `sketchy`/`my_bad`/`regardless` never even see it at any nesting
  level -- confirmed empirically against the real Python CLI (`dip(5)` inside a `sketchy...my_bad...
  regardless` block skips all three and exits the whole process with code 5, no output past the
  `dip()` call). Replicated with a sentinel `ObjError` flavor (`vm_request_exit`/`vm_is_system_exit`)
  that `vm_unwind_to_handler` refuses to search for a handler on, at any level, before its normal
  logic runs -- reusing 100% of the already-tested nested-error-propagation machinery from the N4
  callback work rather than threading a new flag through every call site. `main.c` recognizes an
  uncaught one and calls it a clean exit, not a crash.
  **`combo()` needed a new, dedicated Obj type (`ObjCombo`, native/builtins.h):** it returns a native
  function that closes over a list of callables, and neither existing native-calling convention
  (`ObjBoundNative`'s receiver, `ObjNativeFn`'s plain args) can express "a native function with its
  own captured state" -- a small dedicated type was simpler than generalizing `NativeMethodFn`'s
  signature for the sake of this one function.
  **Bug caught by the differential test, not assumed correct:** `range_stash`'s float path initially
  promoted `start` to a float *upfront* whenever *any* argument was a float, which isn't what Python
  actually does -- `range_stash(0, 1, 0.25)` there yields `[0, 0.25, 0.5, 0.75]` (an int `0` first!),
  because Python's `n` starts as whatever type `start` itself is and only becomes a float once a
  float `step` is actually *added* to it on the first `n += step`, not merely because some argument
  somewhere is a float. Fixed to track the promotion exactly where it actually happens.
  the_args() returns a fresh Stash copy every call (not the same object), matching Python's own
  `Stash(list(...))` -- Stash has reference semantics, so aliasing here would be observable.
  Verified: 1 new differential program (all 16 functions, including nested combos, mixed-type
  range_stash, deep_clone's independence from the original, and squad `how_thicc` dispatch) plus a
  separate manual verification of `dip()`'s exit code and try/catch/finally-skipping behavior against
  the real Python CLI -- byte-identical/exit-code-identical, gcc and clang, `-Werror`, ASan/UBSan,
  and `FUNNY_GC_STRESS=1`. `ask()` is implemented but not exercised in the differential suite, same
  exception as `chaos.funny`'s randomness -- its behavior is interactive-input-dependent by design.
  Real stdlib modules (`mafs yapper stash groupchat rizz filez clock sus computer internet`),
  `IMPORT`/`EXPORT`/per-module namespaces (task 3), `computer.explode()`/`blue_screen()` (task 4),
  `internet` (task 5), and `rizz`'s PRNG (task 6) remain as N5's next sub-phases.
- **N5 · sub-phase 5b complete · per-module namespaces, `IMPORT`/`EXPORT` (task 3, scoped), and
  `mafs` (task 2's second module).** Real per-module global isolation (PLAN.md §3.8's "non-flexed
  names are private"): `ObjClosure` gains `moduleGlobals`/`moduleExports` (each an
  `OBJ_VAL(ObjGroupChat*)`), inherited unchanged by every nested closure `OP_CLOSURE` creates within
  one module and fresh (empty) for a newly-run module's entry closure -- exactly mirroring
  `funnylang/vm.py`'s own `Closure.module_globals`/`module_exports` propagation. `GET_GLOBAL`/
  `SET_GLOBAL`/`DEF_GLOBAL` and `PTR_GLOBAL` all now read/write the *current frame's* own
  `moduleGlobals` instead of a single VM-wide table -- `vm->globals` is gone entirely, replaced by
  this; `vm->builtins` (genuinely VM-wide, shared, and effectively read-only from FunnyLang code) is
  unaffected. `ObjPointa`'s own `POINTA_GLOBAL` kind had to change too, from a borrowed `VM*` to the
  *owning module's* `moduleGlobals` Value directly -- a global pointer must keep resolving against
  the namespace it was actually taken from, not "whichever frame happens to be executing when it's
  dereferenced" (unobservable with the one module that existed before this sub-phase, but a real bug
  waiting for the next one).
  **Scoped per an explicit decision this sub-phase, not silently narrowed:** `IMPORT`'s mode 2
  (`gimme modulename`, a stdlib module by name) is fully wired (`native/modules.c`'s registry);
  modes 0/1 (`gimme "path.funny"`, a real file) get the identical "modules aren't wired up yet"
  `WhoDis` `funnylang/vm.py`'s own un-wired `_do_import` fallback raises when no `module_loader` is
  set -- file-based imports need either a native compiler (N8) or `.funnypak` bundle loading
  (`chunk.c` doesn't have it yet), neither of which exist. `EXPORT` itself is implemented to spec
  (writes into the current closure's `moduleExports`) but has no differential-test-visible effect
  yet, since nothing can *import* a user file back to read those exports until file-based `gimme`
  exists -- tested only for "doesn't crash, program continues normally."
  `native/mafs.c` ports all 25 free functions + 5 constants (`gimme mafs`) plus the 6 numba instance
  methods (`(5.5).floor()`), which needed their own `vm_get_prop`/`do_invoke` dispatch branch (numba
  method binding was N4's own deferred item: "stash/groupchat now, numba/yapstring/pointa's method
  forms later" -- pointa's landed in N4's own pointers sub-phase; numba's lands here). `mafs.pow`
  reuses `vm_pow` (newly exposed as `vm_numeric_pow`) rather than reimplementing bignum
  exponentiation a second time. `factorial` is a real bignum loop (`20!` already overflows int64).
  `M_PI`/`M_E` aren't standard C11 (`-std=c11` hides them even on glibc, and MSVC never defines them
  without `_USE_MATH_DEFINES`) -- defined locally instead of depending on either.
  Verified: 1 new differential program (every `mafs` function and constant, all 6 numba methods, a
  module member passed as a first-class callback, `flex`, and the not-a-real-module error case)
  plus the full existing suite re-run end to end (confirming the whole globals-architecture rewrite
  caused zero regressions) -- byte-identical, gcc and clang, `-Werror`, ASan/UBSan, and
  `FUNNY_GC_STRESS=1`.
- **N5 · sub-phase 5c complete · `yapper` (task 2's third module).** 28 free functions + the 19
  yapstring instance methods -- ported as a *single* function per operation wherever
  `funnylang/stdlib/yapper.py` itself shares one Python function between `build()`'s `members` and
  its separate `YAPSTRING_METHODS` dict (15 of the 28 do; both conventions already treat `a[0]` as
  "the string" identically, receiver or not, so there was never a reason to write two wrappers).
  Codepoint-correct throughout (`slice`/`reverse`/`chars`/`ord_of`/`chr_of`/`at`/`code_at`/`pad_left`/
  `pad_right`/`how_thicc`), reusing N4's own `utf8_*` primitives plus a new `utf8_encode_cp`
  (`chr_of`'s own job -- encoding a scalar back into UTF-8, the encode-side counterpart N4 never
  needed). `reverse`'s non-ASCII path builds a codepoint-offset table once, the same O(n)-not-O(n²)
  approach `GET_SLICE`'s own non-ASCII path already established. `vm_get_prop`/`do_invoke` gained the
  `IS_STRING` dispatch branch this needed (yapstring method binding was N4's own deferred item,
  alongside numba's, which the `mafs` sub-phase just did).
  **Three deliberate, logged scope reductions, all because real Unicode tables (`unicode_tbl.c`,
  N4 task 2) still don't exist:** `SCREAM`/`whisper`/`title_case`/`sarcasm_case` are ASCII-only case
  conversion (non-ASCII bytes pass through unchanged); `is_letter`/`is_alnum` are ASCII-only
  classification; `format` supports only positional `{}`/`{N}` placeholders, not named fields or
  format specs (`{:.2f}`) -- reimplementing a real chunk of Python's format mini-language for one
  function would be disproportionate to the rest of this module's own scope.
  **Found and deliberately did *not* replicate an apparent bug in the Python reference, rather than
  silently matching or silently diverging:** empirically verified that `yapper.join` uses Python's
  raw `str(x)` on each item, not `to_display` -- observably different for a boolski ("True"/"False",
   not "fax"/"cap") and, for a nested Stash/GroupChat, Python's own debug `repr()` (e.g.
  `"Stash([1, 2])"`, using Python's dict/list syntax, not FunnyLang's). Replicated exactly for
  ghost/boolski/numba/yapstring (cheap, and the common case); NOT replicated for anything needing
  Python's own container repr, which uses `to_display` instead -- reproducing Python's `repr()`
  faithfully for arbitrary nested structures is disproportionate effort for what only manifests when
  joining a stash *of stashes/groupchats*, and looks like an oversight (`str(x)` where every other
  display path in the language uses `to_display`) rather than a specified behavior.
  Verified: 1 new differential program (every free function and instance method, UTF-8 codepoints
  throughout, the `join` boolean quirk, `format`'s positional placeholders, and two error paths)
  plus the full suite re-run, byte-identical, gcc and clang, `-Werror`, ASan/UBSan, and
  `FUNNY_GC_STRESS=1`.
- **N5 · sub-phase 5d complete · `stash`/`groupchat` as `gimme`-able modules (task 2's fourth and
  fifth).** `gimme stash`/`gimme groupchat` expose every existing instance method as a free function
  too (`stash_build`/`groupchat_build`, in stash.c/groupchat.c right next to the methods they wrap --
  the stash/groupchat itself becomes an explicit first arg, reusing the *exact same* `NativeMethodFn`
  bodies already written for N4's own `.method()` dispatch, the same "one function, two calling
  conventions" pattern `yapper`'s sub-phase already established), plus the 7 (`sort_by` `group_by`
  `unique` `flatten` `chunk` `sum_up` `shuffle_it`) and 2 (`invert` `from_pairs`) free-function-only
  extras `funnylang/stdlib/stash.py`/`groupchat.py`'s own `build()` adds beyond their `METHODS`
  dicts. `sum_up` reuses `vm_add`'s numeric-promotion logic directly (newly exposed as
  `vm_numeric_add`, the same pattern `mafs.pow`'s own `vm_numeric_pow` already established) rather
  than reimplementing int64-overflow-to-bignum promotion a third time.
  **Two AGENT CHOICEs, both logged rather than silently matched or silently diverged:**
  `sort_by`'s own key-comparator (`sort_by_less`) mirrors `_SortKeyWrap.__lt__` exactly -- a
  *different* rule from `.sort()`'s own default (`default_less`'s numbers-always-first tiering):
  either operand being boolski compares *truthiness*, not identity. `group_by` groups by this
  runtime's own `groupchat_set`/`value_equal_narrow` key equality (bool and int always distinct)
  rather than Python's own dict-key equality (where, because Python's `bool` is an `int` subtype,
  `1` and `fax` would land in the same bucket) -- correct for FunnyLang's own semantics, not a
  literal port of an aliasing quirk nothing should be relying on.
  `shuffle_it` (like `funnylang/stdlib/stash.py`'s own, which uses Python's global `random` module)
  is deliberately not tested for exact output -- no shared stream with whatever `rizz` eventually
  implements, the same established exception as `examples/chaos.funny`'s own randomness and
  `ask()`'s interactive-input dependency. Seeded once, lazily, from the current time; the
  differential test only checks it returns a same-length permutation of the same elements.
  Verified: 1 new differential program (every extra function on both modules, nested/typed
  `sort_by`/`group_by` keys, an error path per fallible function) plus the full suite re-run,
  byte-identical, gcc and clang, `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.
- **N5 · sub-phase 5e complete · `rizz` (task 2's sixth module, and task 6's PRNG).** A real
  xoshiro256\*\* generator (`native/rizz.c`, public-domain reference algorithm, splitmix64-seeded) --
  exactly the choice NATIVE_PLAN.md's own N5 task 6 line already called for: "need not match
  Python's Mersenne Twister stream — but `rizz.seed(n)` must be reproducible within the C VM." All 8
  functions (`roll` `float_roll` `pick` `shuffle` `coinflip` `seed` `uuid` `gamble`) port cleanly;
  `uuid` builds a real UUIDv4 (version/variant bits fixed, the rest from the shared generator).
  `stash.shuffle_it` (the previous sub-phase's own, seeded from `rand()`/`time()`) now shares this
  same generator too, rather than keeping two unrelated PRNGs in the codebase.
  Every function here is randomness-dependent by nature, so none of it is tested for exact output --
  the same established exception as `examples/chaos.funny`'s own randomness and `ask()`'s
  interactive-input dependency. The differential program instead checks *properties* common to both
  VMs regardless of their (necessarily different) random streams: results land in the requested
  range, `pick` returns an element that was actually in the stash, `shuffle` yields a same-length
  permutation, `uuid` is 36 characters and contains a hyphen, `gamble(1.0)`/`gamble(0.0)` are
  unconditionally `fax`/`cap`.
  **Found the Python reference itself crash uncatchably on bad input while writing this test, not
  while porting the C side:** `rizz.roll("a", "b")` raises a bare Python `ValueError` (from `int()`
  on a non-numeric string) that isn't a `FunnyError` at all, so no `sketchy`/`my_bad` in FunnyLang
  code can catch it -- confirmed by the differential harness's own Python-side run failing outright.
  Removed that specific case from the test (this native VM's own `roll` throws a clean
  `TypeVibeMismatch` for it instead, a deliberate, narrow safety improvement over the reference that
  a differential test structurally cannot exercise either side of).
  Verified: 1 new differential program plus the full suite re-run, byte-identical, gcc and clang,
  `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.

- **N5 · sub-phase 5f complete · `filez` (task 2's seventh module) and the platform boundary's first
  tenant.** All 14 functions port from `funnylang/stdlib/filez.py`. `slurp`/`yeet_out`/`append_to`/
  `exists`/`obliterate`/`list_dir`/`mkdir`/`read_bytes`/`write_bytes`/`abs_path` are real filesystem
  access, so — per ARCHITECTURE.md's platform-boundary rule that only one file may know the OS exists
  — they go through a brand-new `native/platform.c`/`.h`, the first file to actually need it.
  `join_path`/`dir_of`/`base_of`/`ext_of` stay in `filez.c` itself: pathlib never touches the
  filesystem for these either, they're pure string manipulation over a POSIX-flavoured parse
  (splitting on `/`, dropping empty/`.` segments, keeping `..` literal), which `filez.c` replicates
  directly rather than routing through `platform.c` for no reason.
  **AGENT CHOICE:** `platform.c` implements the POSIX path only (open/read/write/close, opendir/
  readdir, mkdir/rmdir/unlink, realpath, getcwd) — not the `#ifdef _WIN32` branch ARCHITECTURE.md's
  own contract reserves this file for. `build.sh` itself only targets Linux/macOS today (its own
  comment: "gcc or clang on Linux/macOS"), so there is no Windows build to compile or test a WinHTTP-
  style Win32 filesystem path against yet — the same deferral `main.c` already applies to its own
  Unicode banner, extended here rather than writing untested `FindFirstFile`/`_mkdir`/`_fullpath`
  code with zero way to exercise it this session. Lands whenever an actual Windows build target
  exists (N7, or N5b's own WinHTTP work, whichever comes first).
  `abs_path` tries `realpath()` first (exact, symlink-resolving, matches `Path.resolve()` when the
  path fully exists) and falls back to lexical `.`/`..` normalization of `cwd + path` when it
  doesn't — `Path.resolve()`'s default `strict=False` doesn't require existence either, and this
  covers every case the differential test (or any realistic caller) exercises without needing a
  component-by-component symlink walk for a nonexistent trailing segment.
  `write_bytes`'s `int(x) & 0xFF` per byte is replicated for bool/int/float/bignum inputs, except a
  bignum too large for `int64_t`, which throws a clean `TypeVibeMismatch` instead of chasing Python's
  arbitrary-precision `&` for a case no realistic byte-stash will ever hit — the same rationale as
  `rizz.roll`'s own narrow safety improvement two sub-phases ago.
  Verified: 1 new differential program (creates, reads, appends, lists, byte-round-trips, and cleans
  up real files/directories under `build/n4/filez_scratch`, plus every path-string edge case the test
  program exercises against the real Python CLI first) plus the full suite re-run, byte-identical,
  gcc and clang, `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.

- **N5 · sub-phase 5g complete · `clock` (task 2's eighth module).** All 5 functions port from
  `funnylang/stdlib/clock.py`, all real timing routed through 4 new `platform.c`/`.h` functions
  (`platform_now_seconds`/`platform_monotonic_seconds`/`platform_sleep_seconds`/
  `platform_strftime_now`) per ARCHITECTURE.md's platform-boundary rule, which lists timing
  alongside filesystem access as something only `platform.c` may know the OS-specific shape of
  (POSIX `clock_gettime`/`nanosleep`/`localtime_r`, same deferred-Win32-branch note as `filez`'s
  own sub-phase).
  **`stopwatch()`'s captured state:** Python's own `_stopwatch` returns a closure over a local
  `start` variable — C has no such thing, and `ObjNativeFn`'s C signature carries no userdata slot.
  Solved by repurposing `ObjBoundNative` (already built for ordinary receiver-bound methods): its
  `receiver` field holds `FLOAT_VAL(start)` instead of an actual receiver object, and
  `call_bound_native` already unconditionally prepends that value as `args[0]` on every call,
  bound-method-syntax or bare `sw()` alike (confirmed by reading `do_call`'s own dispatch, which
  treats `OBJ_BOUND_NATIVE` as directly callable) — so `elapsed()`'s body just reads `args[0]` back
  out as its captured start time. No new object type needed.
  Every function here touches the wall clock or a monotonic clock, so — the same established
  exception as `rizz`'s randomness and `ask()`'s interactive input — nothing is tested for exact
  output; the differential program checks *properties* (`what_is_it()` types, non-negative/
  monotonically-increasing elapsed times, a literal-format `date_yap` round-trip using formats with
  no time-dependent `%` directives) rather than any timestamp value that could legitimately differ
  by the time the Python run and the native run each reach that line.
  **Found the Python reference itself crash uncatchably twice while writing the test** (not while
  porting the C side): `touch_grass("nope")` hits `time.sleep()`'s own bare `TypeError` on a
  non-numeric argument, and `date_yap(42)` hits `time.strftime()`'s own bare `TypeError` on a
  non-string format — neither is a `FunnyError`, so no `sketchy`/`my_bad` can catch either on the
  Python side. Removed both cases from the test, same rationale as `rizz.roll`'s and
  `filez.write_bytes`'s own narrower-than-Python safety improvements (this native VM throws a clean
  `TypeVibeMismatch` for both instead).
  Verified: 1 new differential program plus the full suite re-run, byte-identical, gcc and clang,
  `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.

- **N5 · sub-phase 5h complete · `sus` (task 2's ninth module).** All 5 reflection/debug functions
  port from `funnylang/stdlib/sus.py`. `type_of`/`is_a` are thin wrappers over the existing
  `vm_type_name`; `fields_of` copies an `ObjInstance`'s flat `FieldEntry` array into a fresh
  groupchat (empty for anything that isn't an Instance, matching Python's own `isinstance` guard);
  `dump` is a byte-for-byte duplicate of `builtins.c`'s own `sheesh` (funnylang/stdlib/builtins.py's
  `_sheesh` and sus.py's `_dump` are themselves the same function body copy-pasted across two files,
  so replicating that duplication is the faithful port, not an oversight to "fix" by sharing code).
  **`stack_trace()` reuses existing machinery wholesale:** `vm.c` already had a private
  `build_trace()`/`free_trace()` pair (built for the error-diagnostics path, porting
  `funnylang/vm.py`'s own `_build_trace` line for line — innermost frame first, `<the big one>` for
  the top-level script frame, `max(ip-1, 0)` lookahead-correction for every non-topmost frame). Added
  one new public wrapper, `vm_stack_trace_stash()` (vm.h/vm.c), that calls the existing pair and
  copies the lines into a `Stash` of yapstrings — no new trace-building logic at all, just a Value
  wrapper around code that already existed and was already exercised by every existing error test.
  Unlike `rizz`'s randomness or `clock`'s timing, stack-trace output here is fully deterministic and
  reproducible byte-for-byte: `path` comes from the compiled unit's own embedded `sourceName`, set
  once at compile time from the literal `.funny` path string and identical in both the Python and
  native runs of the same differential test file, so the new test asserts exact trace-line content
  rather than falling back to a properties-only check.

  ---

  **Correction, reported by the user: `build.bat` (the real Windows/MSVC build entry point) had been
  broken since the filez sub-phase and no one had noticed, including this log.** Every "defer the
  Win32 branch" AGENT CHOICE logged above (filez, clock, computer, internet) was reasoned from
  `build.sh`'s own comment ("gcc or clang on Linux/macOS") without ever checking whether a sibling
  Windows build script existed — it did (`build.bat`, an N0-task-1 deliverable, MSVC's `cl.exe`), and
  it had been silently failing (`fatal error C1083: Cannot open include file: 'dirent.h'`) since the
  first POSIX-only header landed in `platform.c`. The user caught this by running it themselves.
  Fixed properly rather than re-deferred: `platform.c` now has real `#ifdef _WIN32` implementations
  for every function that had one, verified against actual `cl.exe` (Visual Studio 2022 Community,
  reachable via `vcvarsall.bat x64`) rather than assumed correct from reading MSDN alone --
  - **Filesystem:** `read`/`write`/`append` rewritten over plain standard-C stdio (`fopen`/`fread`/
    `fwrite`), portable without any `#ifdef` at all -- simpler than maintaining two OS-call paths.
    `exists`/`remove`/`mkdir_p`/`list_dir` get real Win32 implementations (`GetFileAttributesA`/
    `DeleteFileA`/`RemoveDirectoryA`/`_mkdir`/`FindFirstFileA`+`FindNextFileA`). `abs_path` uses
    `_fullpath` (simpler than the POSIX side: it never requires existence, so no lexical-fallback
    path is even needed on Windows).
  - **Timing:** `now_seconds` uses plain C11 `timespec_get` (`<time.h>`), portable on both platforms
    without an `#ifdef` -- only a *monotonic* clock genuinely has no portable C11 equivalent
    (`QueryPerformanceCounter` vs `clock_gettime(CLOCK_MONOTONIC)`), and only `sleep`/`strftime`
    need one for `Sleep`/`localtime_s` vs `nanosleep`/`localtime_r`.
  - **System info:** `GlobalMemoryStatusEx`/`GetTickCount64` mirror funnylang/stdlib/computer.py's own
    Windows ctypes fallback exactly (same two calls); `GetSystemInfo`/a deprecated-but-locally-
    suppressed `GetVersionExA` cover CPU count and OS release (still excluded from the byte-diffed
    suite regardless, per `flex()`'s own already-logged AGENT CHOICE).
  - **Networking, the big one:** real WinSock2 (`getaddrinfo`/`socket`/`connect`/`send`/`recv` are
    near-identical in name and shape to POSIX; `ioctlsocket`+`WSAPoll`+`WSAGetLastError()` replace
    `fcntl`+`poll`+`errno` for the non-blocking-connect-with-timeout dance, and `SO_RCVTIMEO` takes a
    raw `DWORD` milliseconds instead of a `struct timeval`). Isolated all of this behind 8 small
    platform-conditional primitives (`ensure_winsock`/`sock_set_nonblocking`/`sock_in_progress`/
    `sock_close`/`sock_set_recv_timeout`/`sock_poll_writable`/`sock_send`/`sock_recv`) so the entire
    HTTP/1.1 request/response/chunked-decode/redirect-following algorithm stays identical, platform-
    neutral C compiled unchanged on both sides -- not a parallel implementation to keep in sync.
    `strcasecmp`/`strcasestr` (POSIX, absent from MSVC) were replaced with two small hand-rolled
    portable helpers (`ci_eq`/`ci_contains`) used on *both* platforms, removing that difference
    entirely rather than adding a third `#ifdef`.
  Verified on Windows: both `build.bat` and `build.bat debug` (MSVC's ASan) compile and link clean
  under `/W4 /WX`; the release binary runs standalone, and the debug/ASan binary runs (needing its
  runtime DLL on `PATH`, normal for any ASan Windows binary) with zero ASan reports. Every existing
  differential `.funnyc` (filez, clock, sus, computer, internet) produces byte-identical output to
  the already-established Linux expected output, including `computer.explode()`'s uncaught exit 69.
  Real HTTP/1.1 networking (not just the `FUNNY_NO_NET=1` path) verified end-to-end against the same
  local `http.server` used for the Linux-side check -- Content-Length, chunked encoding, redirects,
  redirect chains, EOF-terminated bodies, POST with a body and custom headers, HTTPS rejection, and
  `download()` all byte-identical to the Python reference from native Windows. Re-verified the
  Linux/WSL side is unaffected by the rewrite: gcc and clang debug builds clean, the full differential
  suite (47/47) and the N5 acceptance corpus (78/80, same well-understood skips as before) still pass
  under `FUNNY_GC_STRESS=1`, and the complete pytest suite (1112 tests) passes in release mode.
  **Lesson for future platform.c work (N5b's HTTPS backends especially): check every `build.*` script
  in the repo root before writing "no build exists for platform X yet," not just the one already
  open.**

- **N5b complete (Linux and Windows verified on real hardware; macOS written but unrun) · HTTPS over
  OS-native TLS.** `internet.*` now reaches `https://` for real, with certificate verification on and
  no way to turn it off, exactly as §3.1 specifies. No bundled crypto, no third-party dependency, and
  `build.sh`/`build.bat` still take no new build-time library.
  **The shape of it:** rather than a second HTTP implementation per platform, TLS is a *transport*
  swap. A connection is now `HttpConn { socket, optional TLS session }`, and the entire HTTP/1.1
  layer above it — request building, status/header parsing, chunked decoding, redirect-following —
  reads and writes through `conn_send`/`conn_recv` and never learns which of the two it got. So
  `http://` and `https://` run the same code, and the only per-platform surface is a four-function
  backend (`tls_start`/`tls_read`/`tls_write`/`tls_finish`).
  - **Linux/BSD: OpenSSL, `dlopen`'d at run time** (§3.1's own design), never linked. Thirteen
    symbols resolved through function pointers with no OpenSSL header included anywhere — the four
    ABI constants that needs (`SSL_VERIFY_PEER`, the SNI ctrl code, `X509_V_OK`) are spelled out
    instead. `SSL_set_tlsext_host_name` is a macro over `SSL_ctrl`, so a header-free build has to
    call `SSL_ctrl` directly. Verification is `SSL_CTX_set_default_verify_paths` (system trust
    store) + `SSL_VERIFY_PEER` (handshake fails on a bad chain) + `SSL_get_verify_result` +
    **`SSL_set1_host`**, which §3.1's symbol list doesn't mention but which matters: without it a
    certificate that chains to a real CA but was issued for a completely different domain is
    accepted, and chain-verified-but-not-hostname-verified is the classic way TLS code is quietly
    broken.
  - **Windows: WinHTTP**, which per §3.1 runs the whole request — TLS, redirect-following and the
    system proxy are the OS's job. Plain `http://` deliberately stays on the shared socket path, so
    the behaviour the differential suite actually exercises is the same code everywhere; only
    `https://` diverges. The WinHTTP path reuses the socket path's own `parse_response_head` on
    `WINHTTP_QUERY_RAW_HEADERS_CRLF` (that blob already ends in the blank line the parser looks
    for), so header handling isn't duplicated either. The https check happens per redirect hop
    rather than once up front, so an `http://` → `https://` redirect hands over mid-chain instead of
    failing.
  - **macOS: Secure Transport** (`SSLCreateContext`/`SSLSetIOFuncs`/`SSLHandshake`/`SSLRead`), the
    transport-only shape, so it reuses the same HTTP layer as the OpenSSL path rather than being a
    third implementation. Verification is on by *default* here — Secure Transport evaluates the
    chain during the handshake unless `kSSLSessionOptionBreakOnServerAuth` is set, and it never is;
    `SSLSetPeerDomainName` covers both SNI and the hostname check. Deprecated since 10.15 (the
    replacement, Network.framework, is dispatch/async-only and unusable from this blocking path), so
    the deprecation warning is suppressed locally or `-Werror` would reject the file on macOS.
    **AGENT CHOICE / caveat, stated plainly: this is the one backend that has not been compiled or
    run**, because there is no Mac in this environment — unlike the Windows branch, which the
    previous entry's correction made sure was checked against real `cl.exe` rather than assumed. It
    is written against the documented API and is structurally identical to the OpenSSL backend that
    *is* verified, but it needs one run on real hardware before N5b's "all three OSes" line can
    honestly be ticked.
  Verified (Linux, gcc and clang, ASan/UBSan clean; Windows, MSVC `/W4 /WX`), against each of N5b's
  four acceptance criteria in turn: a live `https://example.com/` fetch returns 200 with a real body
  and **byte-identical output to the Python reference** on both OSes; `expired`, `self-signed`,
  `wrong.host` and `untrusted-root` badssl.com hosts are all rejected while the good control returns
  200 — identical to what urllib does; `FUNNY_NO_NET=1` still refuses before any backend loads; and
  with every `libssl` bind-mounted away inside a throwaway namespace (`build/n4/no_openssl_check.sh`,
  standing in for "a Linux container with no OpenSSL"), the binary still runs, plain `http://` still
  works, and `https://` fails with the library named — `"https needs OpenSSL, and none could be
  loaded here (tried libssl.so.3, libssl.so.1.1, libssl.so)."` That last message needed one small
  interface change: `PlatformHttpResponse` grew a `failReason`, since every other failure is
  supposed to collapse into Python's generic "the internet said no" and this one must not.
  `internet.speed_test()` also works for the first time as a side effect — it hardcodes an
  `https://example.com/` URL, so it could only ever fail before this milestone.
  New tests: `tests/native/test_native_network.py` (5 tests, marked `network`, skipped under
  `FUNNY_NO_NET=1`, deselectable with `-m "not network"`). Adding them meant the differential suite
  could no longer set `FUNNY_NO_NET=1` by assigning `os.environ` at import time — that would have
  reached across and silently skipped the entire new gate — so it now does it per test through
  `monkeypatch`, and the shared `native_binary` fixture moved to a new `tests/native/conftest.py`.
  Full suite after all of it: 52 native tests (47 differential + 5 network) green, including under
  `FUNNY_GC_STRESS=1`, plus the N5 acceptance corpus unchanged.

- **N6 complete · diagnostics.** PLAN.md §4.2's renderer now lives in `native/diag.c`, and the C VM's
  stderr is byte-identical to the Python VM's for every `err_*.funny` golden **in both funny and
  serious modes** — 32 comparisons, all matching, which is N6's stated acceptance bar and a strictly
  stronger check than the differential suite (that one only ever looked at stdout and the error
  flavour).
  The renderer is a transcription rather than an interpretation: every blank line, every box-drawing
  character, the two-space body indent, the five-space snippet gutter, the right-justified line
  numbers, and `splitlines() or [""]`'s empty-body quirk are all copied from
  `funnylang/errors.py::render_diagnostic` deliberately, because "looks right" and "is byte-identical"
  are different standards and only the second one is testable.
  **What the error object was missing:** `ObjError` had no `roast` and no `hint` at all, so there was
  nothing for funny mode to print. Both are now fields, with §4.1's `DEFAULT_ROASTS` table ported
  verbatim into `error.c` and `error_new` applying Python's exact fallback chain (site-specific roast,
  else the flavour's default, else the message). They are deliberately *not* in `error_get_field`:
  PLAN.md §3.9's language-visible field set is flavor/message/line/col/file/trace/payload, and N6 is
  not the milestone that changes what `my_bad (e)` can see.
  Rather than touch all ~94 throw sites, only the ones whose Python counterpart passes an explicit
  `roast=` changed, through five small helpers (`vm_throw_same_energy`, `vm_throw_too_deep`,
  `vm_throw_out_of_pocket`, `vm_throw_key_ghosted`, `vm_throw_no_such_method`,
  `vm_throw_wrong_homies`) — everything else keeps calling `vm_throw_fmt` and gets the default. Two
  quirks in there are copied, not fixed: the mismatch roast normalises to "a numba and a yapstring"
  whichever order the operands came in (while the *message* keeps the real order), and §4.2's worked
  hint example is attached on `+` alone and not on the other arithmetic operators.
  **The snippet, without a source file.** The C VM runs compiled `.funnyc`, so it has no source text —
  but the unit carries the original path, and `diag.c` reads the file back to print the caret line.
  When that file isn't readable the snippet is simply skipped, which is also exactly what the *Python*
  VM renders when it's the one running a `.funnyc`, so the degraded case matches too rather than being
  a special native-only shape.
  `--serious` and `--no-color` are parsed in `main.c` (out of argv in place, so they work either side
  of the path and never leak into `the_args()`); `FUNNY_SERIOUS=1` still works as the env-var form.
  Colour auto-enables from **stdout**'s tty-ness even though the diagnostic goes to stderr — odd, but
  that's what `_use_color` does, and matching it is the acceptance criterion. `platform.c` gained
  `platform_stdout_is_tty` and `platform_console_init` (Windows: `SetConsoleOutputCP(CP_UTF8)` plus
  `ENABLE_VIRTUAL_TERMINAL_PROCESSING`, so the emoji and box-drawing don't get mangled by the ANSI
  code page and ANSI colour isn't printed literally — N6 task 4). That also retires `main.c`'s
  long-standing "banner is ASCII until platform.c exists" note; the banner is now merely waiting on
  N7's CLI.
  **Stdlib roasts are partially done, and that's stated rather than glossed:** `vm_throw_native_roast`
  is the mechanism, and `filez`, `internet` and `computer.blue_screen` use it (so funny mode says
  "the filesystem said no" / "the internet said no. 🚫" / "FUNNYLANG_FAULT_NOT_HANDLED" rather than a
  bare "skill issue."). The remaining ~70 explicit `roast=` sites across `mafs`/`stash`/`yapper`/
  `groupchat`/`builtins` still fall back to their flavour's default roast. That is a real, remaining
  gap — it just isn't one N6's acceptance gate covers, and doing 70 hand-transcribed prose strings
  without a test watching would be the wrong order to do it in.
  New tests: `tests/native/test_native_diagnostics.py` (40 tests). The expected side is rendered live
  by `render_diagnostic` rather than read from checked-in files, so the two implementations can't
  drift behind a stale golden; it covers both modes for every golden, the three ported stdlib error
  shapes, a pty-backed check that colour switches itself on for a terminal *and* wraps exactly the
  spans Python wraps, and a guard that the two modes actually differ (without which every comparison
  could pass vacuously). Three goldens are skipped with the reason recorded in the file:
  `err_undefined_variable`, `err_immutable_reassign` and `err_ptr_address_of_const` fail during
  *resolution*, before bytecode exists, so this VM never sees them until N8 gives it a compiler.
  Verified on Linux (gcc and clang, ASan/UBSan clean) and Windows (MSVC `/W4 /WX`, with the emoji and
  box-drawing confirmed intact in a real console). 92 native tests green including under
  `FUNNY_GC_STRESS=1`; the N5 acceptance corpus still 71/71 and 7/7 after updating its own scraper,
  which had been reading the flavour out of the old one-line stderr format.

- **Self-hosting on the C VM — FunnyLang now compiles FunnyLang with no Python in the loop.** Taken
  ahead of the rest of N7 deliberately (user's call, asked at the boundary): N7's own first task is
  `funny run` on a `.funny` file, which needs exactly this, so building the CLI first would have meant
  building it around a hole.
  The C VM loads `selfhost/` — the compiler that was already written in FunnyLang during the Python-era
  milestones — as a §5.3 bundle, runs it to compile a `.funny` file to bytecode, and runs the result.
  Python's only remaining role is *linking* the bundle, a dev-time step of the same kind as
  `tools/gen_unicode.py` regenerating a checked-in table.
  **The load-bearing fix was not the bundle format, it was where constants live.** `chunk.c` gaining a
  `.funnypak` reader was the easy half. The real problem: `vm->unit` was a single global "the compiled
  unit", and `CONST`/`OP_CLOSURE` indexed straight into it — fine while exactly one unit is ever live,
  wrong the moment a call crosses from one bundled module into another (`compile_source`, imported
  from `compiler.funny`, runs with the *importer's* const pool still selected). `funnylang/vm.py`
  doesn't have this bug because its `Closure` carries `const_pool`/`protos` itself, so the port now
  does the same: `ObjClosure` gained a `unit`, inherited by every closure `OP_CLOSURE` creates exactly
  like `moduleGlobals`/`moduleExports` already were. `vm->unit` survives, but demoted to precisely what
  Python's `self.source` is — "which module is executing", swapped around an import so errors and
  stack traces name the right file. The GC roots every bundled module's pool, not just the entry's,
  since a closure from an already-imported module can still be called much later.
  Imports resolve the way `funnylang/modules.py`'s own pak loader resolves them: relative to the
  importing module's canonical bundle name, normalised, then cached so a module runs once however many
  times it is imported. Modes 0 and 1 (`gimme "x.funny"` and `gimme { a } from "x.funny"`) share one
  path, since mode 1 is just a `GET_PROP` per name afterwards. With no bundle loaded a quoted `gimme`
  still raises the same `WhoDis` as before rather than half-working — resolving one from source would
  need a compiler, and this runtime doesn't have one.
  **Results.** 62 of 64 corpus programs compile *and* run correctly through this path with zero
  Python; the two that don't are `variadics.funny` and `defaults_with_variadic.funny`, the
  pre-existing native gap the N5 acceptance corpus already skips (`vm.c`'s CALL opcode still says
  "variadic functions aren't supported natively yet") — nothing to do with self-hosting, and now the
  single remaining language gap between the two VMs. Verified on Linux (gcc and clang) and native
  Windows (MSVC).
  New tests: `tests/native/test_native_selfhost.py` (8 tests) — the C VM compiling and running a
  program end to end, six corpus programs compiled natively and diffed against their goldens, and the
  strongest one: the C VM and the Python VM each run the *same* FunnyLang compiler over the same input
  and must produce **byte-identical bytecode**, so any divergence in how the two VMs execute anything
  at all surfaces as different bytes rather than as a program that happens to print the right thing.
- **Variadics — the corpus is 64/64, and the last language gap between the two VMs is closed.**
  `vm.c`'s arity check had been rejecting every variadic outright ("variadic functions aren't
  supported natively yet") since N3. Binding them is what
  `funnylang/vm.py`'s `_push_closure_frame` does in three lines: take the first `arity` arguments as
  the named parameters (padding with ghost so the closure's own default-expression bytecode still
  fires), collect everything past them into a Stash, and make that Stash the slot right after. Note
  `arity` counts only the *named* parameters, so for a variadic there is no such thing as too many
  arguments -- only too few, and the arity message says "at least N" accordingly.
  The part worth being careful about wasn't the binding rule, it was that **five separate places in
  the C VM push a frame** -- a plain call, a bound-method value, `INVOKE` on an instance, a callback
  re-entering from native code, and construction -- each with its own hand-rolled ghost-padding loop.
  Fixing only the obvious one would have left variadic methods and variadic `spawn` quietly broken, so
  all five now share one `bind_args_in_place`, and the new differential program
  (`variadics_all_call_paths.funny`) deliberately exercises each path plus the too-few-arguments
  error, rather than just re-testing what the two existing goldens already covered.
  Result: `tests/lang/variadics.funny` and `defaults_with_variadic.funny` pass, the N5 acceptance
  corpus goes from 71/71-with-2-skipped to **73/73 with the variadic skips deleted**, and the
  zero-Python self-hosted corpus check goes **64 of 64** -- every program compiled by the self-hosted
  compiler on the C VM and run on the C VM, matching its golden. Verified on Linux (gcc and clang,
  ASan/UBSan) and native Windows (MSVC).

  Not done in that step, and deliberately: `bootstrap/funnyc.funnypak` was not checked in yet, and
  `funny run foo.funny` was not wired up — both landed in N7 immediately after, below.

- **N7 (part 1) · the native CLI: `funny foo.funny` works, in one command, with no Python.**
  `funny [--serious] [--no-color] [--time] [--vibes] [--version] [run] <file> [args...]`, where
  `<file>` is `.funny` (compiled on the spot by the self-hosted compiler), `.funnyc`, or `.funnypak`.
  Bytecode files are told apart by their own magic rather than their extension, so a bundle under any
  name still runs — which is what the yeet stub will need, since it reads a payload with no name at
  all. Source is the one case decided by extension, because there is no magic number for "FunnyLang
  source" and guessing from content would be worse than reading the name.
  `funny <file> [args]` still works exactly as before, since every native test invokes it that way.
  **Deliberately thin, and meant to get thinner**: N8 moves argument parsing and subcommand dispatch
  into `selfhost/cli.funny`, so what's in C is only what has to be — the flags that configure the
  C-side diagnostic renderer, choosing a loader, and driving the compiler. Writing a full C argument
  parser now would be writing something N8 deletes.
  New in `platform.c`: `platform_executable_path` (neither caller can use `argv[0]` — `funny` locates
  its bundle beside itself, and the yeet stub must read its *own* file) and `platform_temp_file`.
  **`bootstrap/funnyc.funnypak` is now checked in**, with `bootstrap/STAGE0.md` for provenance and
  regeneration. This is the deferral from the previous entry, taken now that it is load-bearing:
  without it `funny run foo.funny` cannot work at all, and more to the point it is what will let a
  machine with *only* a C compiler compile FunnyLang once Python is gone. The staleness policy it was
  waiting on is a test: `test_checked_in_bootstrap_is_not_stale` relinks from `selfhost/` and compares
  bytes (linking is deterministic), so editing `selfhost/` without regenerating fails CI rather than
  rotting quietly. `.gitignore` gains a matching `!bootstrap/funnyc.funnypak`.
  **Compile errors don't leak compiler internals.** A syntax error in the user's file is not the
  compiler failing, and rendering it as one buried the real problem under a stack trace through
  `funnyc.funny`. The self-hosted compiler encodes the real flavour and position into its message
  (it can only `chuck` a plain value), so the compile phase prints that message under a
  `couldn't compile <path>:` header instead. Proper §4.2 diagnostics for source errors need the
  *compiler* to report them properly — N8's job, and not something worth faking from out here.
  Caught by the test suite rather than by review: `find_toolchain`'s path join tripped a
  format-truncation error that only appears at `-O2`, which the `native_binary` fixture uses while the
  `build.sh debug` loop uses `-O0` — so the whole suite silently *skipped* rather than failed until
  the build was reproduced at the fixture's optimisation level.
  Verified on Linux (gcc and clang) and native Windows (MSVC), including finding the bundle beside the
  executable on both. 105 native tests green including under `FUNNY_GC_STRESS=1`; acceptance corpus
  73/73 and 7/7; self-hosted zero-Python corpus still 64/64.

- **N7 (part 2) complete · `funny yeet` and the native runtime stub — and N7's acceptance is met.**
  `funny yeet examples/hello.funny -o hello` produces a **228 KB** standalone executable on Linux
  (437 KB with MSVC) that prints `yo sup world`, still runs after being moved, and exits 69 on an
  uncaught `computer.explode()`. Against the plan's 1 MB budget, and against the ~8 MB PyInstaller
  bundle it replaces.
  `native/stub_main.c` implements PLAN.md §5.4 unchanged — the format didn't move, only the language:
  read your own file, seek `filesize - 17`, check `FUNNYYEET`, load the payload behind it. Worth
  recording because the docs disagree: §5.4's *diagram* shows the length before the magic, while the
  code that writes it (and `funnylang/stub_main.py`, which reads it) puts the magic first. Followed
  the code, since that's what existing yeeted binaries actually contain.
  **`build.sh`/`build.bat` now produce two binaries**, because `native/` has two entry points: `funny`
  (everything + `main.c`) and `funnyrt` (everything + `stub_main.c`). The stub deliberately contains
  no compiler — a shipped executable only ever runs bytecode — which is the whole reason a yeeted
  program is hundreds of kilobytes instead of megabytes. MSVC needed per-binary `/Fo` object
  directories, or the second link silently reuses the first's objects.
  The load-and-run path moved out of `main.c` into `native/runner.c`, shared by both entry points
  rather than copied: a yeeted binary has to report an uncaught error, and in particular exit 69,
  exactly the way `funny` does, and two copies of that would be two things to keep in step.
  **A real bug the Windows check caught, not review:** `-o out\prog` failed with "No such file or
  directory" because the parent-directory creation (M13's fix, kept) looked only for `/`. Both
  `main.c` and `platform_mkdir_p` now treat `\` as a separator too — but only under `_WIN32`, since on
  POSIX a backslash is a perfectly legal filename character and splitting on it there would be a
  different bug.
  Known limitation, stated rather than papered over: `yeet` compiles a *single* source file, since
  linking a multi-module program needs the bundler, which is `selfhost/linker.funny` — N8 task 1. A
  program with file imports isn't yeetable until that lands.
  New tests: `tests/native/test_native_yeet.py` (6), covering each acceptance clause separately plus
  the naked-stub message; the "still works after being moved" one is the one that matters most, since
  it's what forces the stub to find itself through the OS rather than through `argv[0]`.
  111 native tests green including under `FUNNY_GC_STRESS=1`, on Linux (gcc and clang) and Windows.
  **Found a language-grammar quirk while writing the test, not a bug in either VM:** `sus` is itself
  a reserved statement-leading keyword (FunnyLang's own conditional, "sus (cond) { }"), so
  `sus.dump(x)` as a bare statement fails to parse on *both* VMs identically — confirmed by checking
  that every existing Python-side `sus.*` test already only ever calls it from inside an expression
  context (`yap sus.dump(5) + 1`, never a bare statement). Adjusted the test file accordingly
  (`yo _ = sus.dump(x)` instead of a bare `sus.dump(x)`); no runtime-behavior difference, since this
  is parser-level and identical on both sides.
  Verified: 1 new differential program plus the full suite re-run, byte-identical, gcc and clang,
  `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.

- **N5 · sub-phase 5i complete · `computer` (task 2's tenth module, and task 4's
  `explode()`/`blue_screen()`).** All 8 functions port from `funnylang/stdlib/computer.py`, "the
  harmless joke module" — still true here: printing and (for `explode()` alone) an exit code,
  nothing else, per §7.9. `explode`/`beep`/`clear`/`blue_screen` print fully fixed literal content
  (the mushroom-cloud art extracted byte-for-byte via a throwaway script rather than hand-transcribed,
  `build/n4/dump_mushroom_cloud.py`), so they're fully byte-diffable in the differential suite same as
  anything else — no properties-only carve-out needed for them.
  **`ComputerExploded` is an ordinary catchable `FunnyError`, not a second `dip()`-style sentinel:**
  confirmed by reading `funnylang/cli.py`, which catches it in a dedicated `except ComputerExploded`
  clause *only at the top CLI level*, mapping an uncaught one to exit code 69 (any other uncaught
  `FunnyError` gets the generic 1) — inside the VM itself, `sketchy`/`my_bad` catches it exactly like
  any other error. Ported as exactly that: `vm_throw_native` (same as every other stdlib error), plus
  one small addition to `main.c`'s existing uncaught-error branch, `exitCode = strcmp(e->flavor->chars,
  "ComputerExploded") == 0 ? 69 : 1`. No new error-unwinding machinery.
  **`ram`/`uptime`/`flex` needed 4 new platform.c functions** (`platform_ram_bytes`,
  `platform_uptime_seconds`, `platform_os_info`, `platform_cpu_count` — `sysconf`/`/proc/uptime`/
  `uname`, POSIX-only per the same deferred-Win32-branch note as every other platform.c addition this
  milestone). Verified empirically (`build/n4/flex_check.sh`) that OS/CPU/RAM come back byte-identical
  to the Python reference on the same machine, confirming the `platform.c` queries are equivalent to
  Python's own `platform.system()`/`os.cpu_count()`/`sysconf`-based RAM calculation, not just
  plausible-looking.
  **AGENT CHOICE (funnylang-no-python-runtime-dependency, a standing project constraint): `flex()`'s
  one Python-specific line** (`f"python: {platform.python_version()}"`) has no native equivalent —
  this VM isn't running Python, and the shipped binary must not depend on Python being present at all.
  Replaced with `"runtime: FunnyLang native (C)\n"` rather than inventing a fake version number (no
  versioning scheme exists yet; that's N7's `--version` job). Because of that one irreducible
  difference, `flex()` itself is the one function excluded from the byte-diffed differential test
  file — verified instead by the empirical side-by-side script above, which confirms every other line
  matches exactly and only the intentionally-different line differs.
  **`explode()`'s exit-69 path is also excluded from the differential suite specifically** (verified
  instead via `build/n4/check_explode_exit.sh`, which confirms `RC=69`): the harness's own
  `_native_run` asserts `returncode == 0` for every program it diffs, so an uncaught error of any kind
  can't appear in that suite by construction — `explode()`'s catchable-error behavior and printed
  output are still fully covered there, wrapped in `sketchy`/`my_bad` like every other error test this
  milestone.
  Verified: 1 new differential program (`explode`/`ram`/`uptime`/`yeet_to_void`/`beep`/`clear`/
  `blue_screen`, `flex()` excluded) plus 2 standalone empirical checks (`flex_check.sh`'s side-by-side
  OS/CPU/RAM comparison, `check_explode_exit.sh`'s exit-69 confirmation) plus the full suite re-run,
  byte-identical, gcc and clang, `-Werror`, ASan/UBSan, and `FUNNY_GC_STRESS=1`.

- **N5 · sub-phase 5j complete · `internet` (task 2's last module, and all of task 5).** All 5
  functions port from `funnylang/stdlib/internet.py`. **User decision (asked explicitly, since the
  plan's own task 5 line — "the plain-HTTP path only" — reads narrower than what N5b's own task list
  separately claims):** chunked transfer-decoding and redirect-following (capped at 10 hops) are
  pulled forward into N5 instead of deferred to N5b alongside TLS, so the plain-HTTP path is
  genuinely useful against real servers today, not just a Content-Length-only toy. TLS/HTTPS itself
  stays N5b's job exactly as planned — an `https://` URL fails immediately in `platform_http_request`
  before any socket is touched, never a silent downgrade to plain HTTP.
  **New in `platform.c`:** `platform_net_disabled()` (checked per-function in `internet.c`, matching
  Python's own `_net_disabled()` call sites and per-function disabled-behavior exactly — most raise,
  `is_it_up()` just returns false), `platform_http_request()`/`platform_http_response_free()` (URL
  parsing, non-blocking connect with a `poll()`-based timeout then restored to blocking for the data
  phase, HTTP/1.1 request building, status-line/header parsing, `Content-Length` and chunked body
  decoding, redirect-following with 301/302/303 switching to `GET` and dropping the body while
  307/308 preserve both — a relative `Location` is treated as a failure rather than resolved against
  the base URL, a deliberate scope line given full URL-resolution is its own separate can of worms),
  and `platform_tcp_ping()` for `ping()`'s raw-connect timing.
  **New in `string.c`:** `string_new_utf8_lossy()` — an HTTP response body isn't guaranteed to be
  valid UTF-8 the way a source file always is, so `go_brrrr`'s `body` field needs Python's own
  `bytes.decode("utf-8", errors="replace")` semantics rather than the well-formed-input assumption
  every other yapstring constructor is allowed to make. AGENT CHOICE: replaces one invalid byte at a
  time with U+FFFD rather than replicating Python's exact "maximal subpart" grouping for a run of
  invalid bytes — never corrupts the codepointCount invariant either way, and nothing differential-
  tests the exact grouping (no live network in the automated suite).
  **Every internet.py failure mode collapses to the same generic `SkillIssue`** (confirmed by reading
  `_no_net_error()`'s single catch-all around every possible `urllib`/socket exception), which
  simplified the port considerably: `platform_http_request` just reports ok/not-ok, with no
  distinct-reason plumbing needed anywhere above it.
  **A real bug, caught before it shipped, not by the differential suite (which structurally can't
  reach this code — see below) but by a live local-server check:** the very first working version
  sent `Connection: close\r\n` with a hardcoded length of `20`, one past the string's actual 19
  bytes, silently including the literal's NUL terminator as if it were request data. That stray `\0`
  landed between the header block and the blank-line terminator, so the two `\r\n`s a real HTTP
  server looks for to recognize "end of headers" were never adjacent — the server sat waiting for
  more, and the client's own `recv()` correctly timed out with nothing to show for it. Read as "the
  socket must still be non-blocking despite the restore" at first; ruled that out by writing an
  isolated minimal reproduction outside the whole VM, which worked fine and pointed straight at the
  request bytes actually sent. Fixed by using `strlen()` on the literal instead of a second,
  independently-typed count. A related, smaller finding from the same session: `bb_append(buf, p, 0)`
  with a still-unallocated (`NULL`) buffer reached `memcpy(NULL, p, 0)` — well-defined in practice on
  every real libc, but undefined by the C standard regardless of length, and UBSan correctly flagged
  it the moment a real 0-byte response body (an intermediate redirect hop, in this case) exercised
  that path; fixed with an early return for `n == 0`.
  **Testing split three ways, since real network I/O can't be part of the automated differential
  suite at all:** (1) the differential test file exercises only `FUNNY_NO_NET=1` — the *only* path
  `tests/test_stdlib.py`'s own existing `internet` tests ever exercise either, so this isn't a new
  gap this port introduces — which needed `tests/native/test_native_differential.py` itself to set
  `FUNNY_NO_NET=1` unconditionally for the whole differential session (harmless: no other program
  under `programs/` touches networking, and `_native_run`'s subprocess inherits it automatically with
  no `env=` change needed); (2) the real HTTP/1.1 client (Content-Length, chunked, redirects,
  redirect chains, EOF-terminated bodies, POST with a body and custom headers, HTTPS rejection, a
  sub-timeout request against a deliberately slow endpoint, `ping()`, `download()`) is verified
  end-to-end against a local throwaway Python `http.server` (`build/n4/test_http_server.py`,
  `build/n4/http_check.funny` run through both VMs via `build/n4/http_check2.sh`) — byte-identical to
  the Python reference, including under `FUNNY_GC_STRESS=1`; (3) the actual bug above was found by
  exactly this local-server check, not by (1), which structurally cannot reach any of this code —
  concrete confirmation that (2)'s empirical-verification step is pulling real weight this milestone,
  not just ceremony.
  Verified: 1 new differential program (`FUNNY_NO_NET=1` path) plus the local-server empirical check
  above (normal and `FUNNY_GC_STRESS=1`) plus the full suite re-run, byte-identical, gcc and clang,
  `-Werror`, ASan/UBSan.

- **N5 acceptance verification, and a real pre-existing bug it caught.** With all of N5's tasks
  landed, ran the milestone's own acceptance line for the first time: the entire `tests/lang/` +
  `examples/` corpus (76 + 9 `.expected`-bearing programs) through the native VM, reusing
  `funnylang/cli.py`'s own `_run_one_test` semantics (an `!ERROR <flavor>` golden checks the raised
  error's flavor, not stdout) since a naive raw-stdout diff mishandles every `err_*.funny` golden.
  Found and fixed one real bug this surfaced, pre-existing since N4 and unrelated to any of this
  milestone's own module work: `value_is_truthy()` (`value.c`) had a `return true;` for "every other
  Obj kind" with a comment already flagging it as unfinished ("truthy unless empty -- N4's job") --
  an empty `stash`/`groupchat` was being treated as truthy, when `funnylang/values.py`'s own
  `is_truthy()` treats `[]`/`{}` as falsy (matching Python's own container truthiness) same as every
  other value tag it explicitly special-cases. Fixed by checking `count > 0` for both, caught by
  `tests/lang/truthiness.funny` specifically (2 of the empty-container cases were silently flipping
  false-negative before the fix).
  Every failure the first (naive) verification pass reported turned out to be a known, already-
  understood gap once re-checked properly: 3 `err_*` goldens fail at *compile/resolution* time in
  Python (undefined variable, const reassignment) — no native compiler exists yet to even reach that
  code path (N8's job) — 2 exercise variadic functions, already a documented native-VM gap from an
  earlier milestone (`vm.c`'s own CALL opcode: "variadic functions aren't supported natively yet"),
  and `examples/modules/main.funny` exercises file-based `gimme "path.funny"` imports, the exact
  scope this session already declined earlier (per-module namespaces + stdlib imports only). None of
  these are new gaps N5 introduced; all were either already known or explicitly out of scope before
  this verification ran.
  **Result: N5's acceptance line is met.** 71/71 `tests/lang/` programs (the 5 known exceptions
  above skipped), 7/9 `examples/` (`chaos.funny`'s randomness and `modules/main.funny`'s file-import
  scope skipped, matching the plan's own stated exception plus this session's own prior decision) --
  byte-identical on gcc and clang, ASan/UBSan clean, and the full `tests/native/` differential suite
  (47/47) still green under `FUNNY_GC_STRESS=1`.

- **N8 task 1 · `selfhost/bundler.funny` + `selfhost/linker.funny`, and the native-VM bug they
  exposed.** The `.funnypak` bundler in FunnyLang, replacing `funnylang/modules.py`'s `build_bundle`
  and `funnylang/serializer.py`'s `dump_funnypak` — between them the last Python needed to *build*
  anything, the self-hosted compiler's own bootstrap bundle included. Split into a library
  (`bundler.funny`: `resolve_import`, `canonical_name`, `collect_imports`, `build_bundle`,
  `emit_funnypak`) and a thin CLI (`linker.funny`), the same shape `funnyc.funny` has over
  compiler+emitter, so N8 task 6's `cli.funny` can use the bundler without triggering another file's
  argument handling.
  The bar was byte-identity with the Python bundler, not "the bundle runs": a `.funnypak` is a
  sequence of `.funnyc` blobs in a fixed order, so an equally-correct walk of the import graph in a
  different order still produces a different file. Reaching it took two fixes.
  (1) **Walk order.** The first draft was breadth-first with an explicit queue, reasoning that a deep
  import chain shouldn't be able to hit the VM's frame limit. Correct, but it does not match
  `modules.py`'s `visit()` recursion, which is pre-order depth-first. Rewritten to pre-order DFS with
  an explicit *stack* — keeping the no-recursion property, matching the order. Each stack job carries
  the *unresolved* quoted import plus the file that asked for it, resolving only when popped, so the
  order in which paths get resolved — and therefore which bad import is reported first — matches
  Python's too.
  (2) **A real native-VM bug: NUL bytes were being truncated out of strings.** Chasing a one-entry
  const-pool difference on `lexer.funny` (375 vs 374) led to `SIMPLE_ESCAPES` in
  `selfhost/lexer.funny` itself, whose `"0"` key maps to a one-character NUL string: the self-hosted
  compiler running on the C VM emitted an empty string where the Python compiler emitted that NUL. A
  FunnyLang string may legitimately contain NUL bytes, but `vm_value_to_display` returns a plain
  NUL-terminated `char *`, and every caller measured it with `strlen()` — silently cutting the value
  at the first NUL. That affected `yap`, template interpolation (`OP_BUILD_STRING`), `to_yap`,
  `stash.join`, `yapper.join`, `yapper.format`, and `ask`'s prompt; `.how_thicc()` and concatenation
  were already correct, which is why it hid this long. Fixed with `vm_value_to_display_len`, which
  reports the true byte length: a top-level string yields its own bytes, and an instance whose
  `to_yap` returns one is followed through. Nested strings needed no change — they render via
  `repr_value_rec`, which json-escapes a NUL exactly like `funnylang/values.py`'s `to_repr` does.
  Covered by a new differential program, `tests/native/programs/nul_bytes_in_strings.funny`,
  exercising every display-producing operation.
  With both fixed, the FunnyLang linker and the Python bundler produce **byte-identical** bundles,
  and the linker reaches a fixed point: a linker linked by itself relinks the compiler to the same
  bytes. `tests/native/test_native_linker.py` (6 tests) asserts byte-identity, the fixed point,
  module order and entry name, relative imports from a subdirectory, an end-to-end
  link → compile → run with no Python past the bootstrap, and that a missing import is reported
  rather than dropped.
  **AGENT CHOICE · no `FUNNYPATH` in the FunnyLang resolver.** §3.8's resolution order is (1) relative
  to the importing file, (2) `FUNNYPATH`, (3) `./funny_modules/` walking up. The FunnyLang bundler
  implements 1 and 3 and skips 2: reading an environment variable is not something the FunnyLang
  stdlib can do, and inventing a stdlib function for it here would put the two implementations out of
  step over something no bundled program has ever used. Logged rather than silently skipped; if
  `FUNNYPATH` support is wanted, it needs a stdlib addition first.
  Verified: full suite 1183 passed (Linux, gcc, `-Werror`); MSVC `/W4 /WX` build clean on Windows,
  both binaries, with the new differential program producing identical output there.

- **N8 task 2 · `funny xray`, in FunnyLang.** Four new files: `selfhost/disasm.funny` (a port of
  `funnylang/disasm.py`), `selfhost/loader.funny` (the exact inverse of `emitter.funny`, so a
  `.funnyc`/`.funnypak` on disk disassembles through the same path as a program just compiled),
  `selfhost/astdump.funny` (promoted out of `selfhost/_drivers/dump_ast.funny`, which is now a thin
  wrapper, since `--ast` needs the same rendering the parser cross-check already had), and
  `selfhost/xray.funny` (the CLI, a port of `cli.py`'s `cmd_xray`). `prelude.funny` gained the byte
  *readers* mirroring its writers, a `utf8_decode`, and a `json_quote` matching Python's
  `json.dumps` defaults (the driver's older one escaped only five characters and would have been
  wrong on any control or non-ASCII character).
  **ADDITION · `mafs.bits_to_float(bits)`.** The inverse of `mafs.float_to_bits`, which PLAN.md §16
  added at M12 for exactly the mirror-image reason: the emitter must write a float constant's raw 8
  bytes, and now the loader must read them back. Nothing in the language gives a program bit-level
  access to a float, so decoding IEEE-754 by hand in the self-hosting subset would be fragile and
  pointless when both runtimes can reinterpret the bits directly. Out-of-range input (negative, a
  float, or wider than 64 bits) raises `MathAintMathin` in both implementations rather than letting
  Python's `struct` raise an `OverflowError` no FunnyLang program could catch. Documented in
  `docs/STDLIB.md`, covered in `tests/native/programs/modules_mafs.funny`.
  **Fixed · template-substitution EOF spans in `selfhost/lexer.funny`.** `lexer.py` gives the EOF
  token it synthesizes at the end of a `{...}` substitution the span of the *last token consumed*
  (the closing brace); the self-hosted lexer was using wherever the scanner had since moved to, one
  column further on. Nothing had cross-checked it: `_drivers/lex_template.funny` compares only the
  token *kinds* inside a template. `xray --tokens`, which prints those nested tokens in full, is what
  surfaced it.
  **KNOWN DIVERGENCE · `--tokens` and non-ASCII.** Token reprs go through a hand-rolled port of
  CPython's `repr` for strings, because that is what `tokens.py`'s f-string `!r` produces. It matches
  CPython exactly across ASCII -- quote selection, backslash escapes, lowercase `\xNN` below space
  and for DEL. CPython additionally escapes any non-ASCII code point `str.isprintable()` rejects
  (zero-width spaces, soft hyphens, unassigned code points); deciding that needs Unicode
  general-category tables the language does not expose to a program, so non-ASCII is passed through
  literally instead. Every printable character renders identically either way, and the corpus check
  below covers files with non-ASCII identifiers and string literals.
  Verified: `selfhost/xray.funny` on the C VM vs `python3 -m funnylang xray` across
  `tests/lang/` + `examples/` + `selfhost/` -- **0 mismatches out of 101 files** disassembling,
  104 with `--ast`, 104 with `--tokens` -- plus 84 already-compiled `.funnyc`/`.funnypak` files
  through the loader path. `tests/native/test_native_xray.py` (33 tests) pins a representative slice
  of that. `bootstrap/funnyc.funnypak` regenerated, since `selfhost/lexer.funny` and
  `selfhost/prelude.funny` both changed. Full suite green; MSVC `/W4 /WX` clean, with `xray` output
  byte-identical on Windows too.

- **N8 task 3 · `funny fmt`, in FunnyLang.** `selfhost/fmt.funny` ports `funnylang/formatter.py` and
  `selfhost/fmtcli.funny` ports `cli.py`'s `cmd_fmt`.
  **DECISION · the comment-dropping limitation is ported, not fixed.** The plan offers either. It is
  inherent to formatting from an AST that never recorded comments, so fixing it means changing what
  the *parser* retains — and this formatter's whole acceptance criterion is producing the same bytes
  as `funnylang/formatter.py`, which would no longer be possible. Comment preservation is a real
  feature worth having; it belongs in a change that moves both implementations together, not in a
  port whose job is to be indistinguishable from what it replaces.
  `tests/native/test_native_fmt.py::test_comments_are_dropped_like_the_python_formatter` pins it
  deliberately, so a future change that starts preserving comments breaks loudly rather than drifting
  apart quietly.
  **ADDITION · `yell(...)`.** `cmd_fmt --check` prints "isn't formatted" to **stderr** and exits 1 —
  and nothing in the language could write to stderr at all. `yap`/`mumble` are statements with their
  own opcodes, so a third keyword would mean lexer, parser, compiler, opcode-table and bytecode
  changes in both implementations; a plain builtin costs a table entry each. `yell` is `yap`'s stderr
  counterpart: values space-separated, newline-terminated, rendered identically. Four places had to
  learn the name — both stdlibs and both resolvers' `BUILTIN_GLOBAL_NAMES` (an unknown global is a
  *compile-time* `WhoDis`, which is how the omission announced itself). Documented in
  `docs/STDLIB.md`, covered by `tests/native/test_native_yell.py`, which compares **both** streams
  across both VMs — the rest of the differential suite compares only stdout, and which stream the
  bytes land on is the entire point of this builtin.
  **Fixed · `filez.slurp` newline translation, and `filez.yeet_out`'s inverse.** Found while checking
  `fmt --check` against a CRLF file: `filez.py`'s `Path.read_text()` does universal-newline
  translation on every platform, while the native `slurp` returned raw bytes — so the same CRLF file
  was 17 characters natively and 15 in Python, and `fmt --check` disagreed about whether it was
  formatted. The native `slurp` now translates too (in `filez.c`, not `platform.c`: a CRLF file is a
  CRLF file on Linux as well, so this is text-format semantics, not an OS difference), with
  `read_bytes` as the escape hatch for the bytes as they are. The write direction had the mirror-image
  problem in the *other* implementation: `Path.write_text()` rewrites `\n` to `os.linesep`, so Python
  would have written CRLF on Windows where the native runtime writes LF. Fixed by passing
  `newline=""` in `filez.py` — a language's file-write primitive should put down the bytes it was
  given, and reading is already platform-independent, so writing has to be too. Both directions are
  now covered in `tests/native/programs/modules_filez.funny`, using `write_bytes` to lay down the
  CRLF fixture so the test does not depend on how the repository was checked out.
  Verified: `selfhost/fmtcli.funny` on the C VM vs `funnylang.formatter` across `tests/lang/` +
  `examples/` + `selfhost/` — **0 mismatches out of 106 files**, plus 24 tests in
  `tests/native/test_native_fmt.py` covering idempotence, `--check`'s exit code and stream, the
  "formatted X." announcement only when something changed, and an end-to-end run of both CLIs over
  the same messy file. `bootstrap/funnyc.funnypak` regenerated (`selfhost/compiler.funny` gained
  `yell`). Full suite 1242 passed; MSVC `/W4 /WX` clean, with `fmt --check` on a CRLF file and
  `fmt`'s output bytes both matching the Python CLI on Windows.

- **N11 · ADDITION · removing Python from the repository.** Added at the project owner's direction,
  mid-N8. The plan already ended at "the shipped runtime needs no Python"; the owner wants "the
  repository contains no `.py` file". Asked how far that goes, they took the maximal reading twice:
  `funnylang/` **and** the ~1,200-test `pytest` suite **and** dev-time tooling like a Unicode-table
  generator, even though such a generator runs only on a maintainer's machine and never touches a
  user's build.
  The load-bearing problem is the **test oracle**, and it was worth putting to the owner rather than
  deciding: almost every test in `tests/` compiles or runs something through `funnylang` and diffs
  the result, so deleting `funnylang/` does not make those tests fail — it makes them stop existing.
  Three routes were offered. Chosen: **grow the `.funny`/`.expected` golden corpus** (run by
  `funny test`, which needs no Python) until it covers what `pytest` covers, then delete. Rejected:
  keeping `pytest` as a subprocess-only driver that never imports `funnylang` (Python survives as dev
  tooling — ruled out by the "everything" answer), and freezing today's differential output as
  goldens (cheap, but the goldens are then only as good as today's Python, with no oracle left to
  re-derive them from if one is ever wrong).
  Written up as **N11** in §6 with its own tasks and acceptance, added to §7's ordering as the last
  milestone, with the standing note that its first task starts *now*: every milestone from here that
  can add a golden instead of a Python-only test makes N11 smaller. N10 task 1's `bin2c` and N5's
  `gen_unicode` were changed from `.py` to `.funny` in the same pass — writing them in Python today
  only creates work for N11, and the native toolchain can already run them.
- **N8 task 4 · `funny test`, in FunnyLang — and the two language additions it needed.**
  `selfhost/test.funny` ports `cli.py`'s `cmd_test`/`_run_one_test`. It matters well beyond N8: N11
  deletes the Python implementation and with it the `pytest` suite that uses it as an oracle, and what
  survives is the `.funny`/`.expected` golden corpus — which this is what runs.
  **ADDITION · `sus.run_bytecode(bytes, args?)`.** A test runner has to observe *another* program's
  stdout and find out which error flavor escaped it. In Python that is one line —
  `VM(stdout=StringIO()); vm.interpret(unit)` — and there was no equivalent a FunnyLang program could
  reach at all. Two designs were possible: spawn a subprocess, or run the bytecode in-process in a
  fresh VM. In-process won on three counts: it mirrors the reference's semantics exactly (so no
  parsing a rendered diagnostic back into a flavor), it needs no process-spawning code in
  `platform.c` (a large, security-relevant surface to add for one caller), and the *same* primitive
  is what N8 task 5's REPL and N9 task 2's `bootstrap --verify` need. A **fresh** VM with its own
  globals and its own GC heap: this is isolation, not `eval` — nothing escapes the child's heap, every
  string handed back is copied out before it is destroyed, and the child never calls back, so the
  caller's collector cannot run while it is alive. stdout is captured via `tmpfile()`, since
  `open_memstream` is POSIX-only and `vm_run` takes a plain `FILE *`. Verified clean under
  `FUNNY_GC_STRESS=1` and under ASan/UBSan — a second heap built and torn down inside a native call is
  exactly the shape that hides use-after-free.
  **ADDITION · `oops(flavor, message?, line?, col?)`.** `chuck "text"` can only ever raise a
  `SkillIssue`, so nothing written *in* FunnyLang could raise the `WhoDis` an undefined variable
  deserves — which is why `!ERROR WhoDis` and `!ERROR ImmutableVibes` goldens could not pass under the
  self-hosted compiler at all. `runner.c`'s own comment already called this out as "N8's job". `oops`
  builds an error value; `chuck` on an error value already re-raises it flavor and all, so this was
  the only missing half. The flavor set stays **closed** to §4.1's taxonomy — an invented name is a
  `TypeVibeMismatch`, because an error flavor should mean the same thing everywhere rather than being
  whatever string a library made up. `selfhost/lexer.funny`, `parser.funny` and `compiler.funny` now
  raise real flavors instead of encoding the intended one into a `SkillIssue`'s message. `OP_CHUCK`
  stamps the chuck site onto an error that has no position yet, mirroring `funnylang/vm.py` filling in
  `err.span`/`err.frames` for any error propagating without them. `what_is_it(e)` on an error also
  used to raise an uncatchable Python `TypeError`; `values.py` now answers `"error"`, which the C VM
  always did.
  **Fixed · three cross-platform path bugs, found by running the runner on Windows.** (1)
  `filez.join_path` hardcoded `/`; `filez.py` uses `pathlib`, which is `WindowsPath` on Windows, so
  every path a FunnyLang program built there was POSIX-shaped. (2) The same for `dir_of`, which also
  dropped a drive prefix. (3) `join_path` *concatenated* where `pathlib` **normalizes** — parsing each
  part into components and re-rendering, so `"a/"` joined with `"b"` is `a\b` and repeated or `.`
  components collapse. Fixed with `platform_path_sep`/`platform_is_path_sep`/
  `platform_drive_prefix_len` and a parse-then-re-render `join_path`. Covered in
  `tests/native/programs/modules_filez.funny` with a round trip
  (`dir_of(join_path(...)) == join_path(...)`) that holds on every platform even though the strings
  differ between them.
  **ADDITION · `filez.is_dir` / `filez.is_file`.** `exists` alone cannot drive a recursive walk, and
  finding `*.funny`/`*.expected` pairs under a directory tree is exactly that. Both answer `cap` for a
  path that isn't there, matching `Path.is_dir()`/`is_file()`.
  **Fixed · a bug in the Python runner, rather than replicating it.** A test calling `dip()` raised
  `SystemExit` straight through `_run_one_test` and killed the whole `funny test` process, silently
  truncating the run at that test. The native runner isolates properly and kept going, so the two
  disagreed; the reference was simply wrong, and `cmd_test` now catches `SystemExit` per test.
  Verified: `selfhost/test.funny` on the C VM vs `python3 -m funnylang test` over `tests/lang` (76),
  `examples` (9) and the whole repository (85) — **identical output and exit code, 0 mismatches** —
  plus 12 tests in `tests/native/test_native_test_runner.py` covering error-flavor goldens, the
  CPython-`repr` mismatch detail, nested-directory ordering, multi-module tests (linked, not just
  compiled, since the native runtime resolves a file import at *link* time), and isolation between
  tests. Full suite 1256 passed; MSVC `/W4 /WX` clean, with `funny test tests\lang` byte-identical to
  the Python CLI on Windows too.
- **N8 task 5 · `funny vibe`, the REPL, in FunnyLang.** `selfhost/vibe.funny` ports `cli.py`'s
  `cmd_vibe`. What separates a REPL from every other subcommand is *persistence* — `yo x = 1` on one
  line has to still be there on the next — and nothing in the language could express that, so this
  task is mostly the four things underneath it.
  **ADDITION · `sus.new_session()` / `sus.run_in(id, bytes, args?)` / `sus.close_session(id)`.**
  Task 4's `sus.run_bytecode` is deliberately a *fresh* VM every time, which is exactly wrong for a
  REPL. A session is the same isolated child VM, kept alive between calls, with its top-level
  namespaces living on the VM itself so nothing else has to root them. Two other differences, both
  driven by what a REPL needs: output is **not** captured (it goes straight to the caller's stream, so
  a long input streams and an `ask()` prompt appears before its input), and the entry's return value
  comes back — as `repr` **text**, not as a value. That is not a shortcut: the child has its own
  collector, and handing one of its objects to the caller would let the caller's GC free something it
  does not own. Sessions are addressed by a small integer rather than a heap object, keeping them out
  of the collector entirely; the cost is that an unclosed session lives until its owning VM dies,
  which is what `close_session` (the REPL's `.clear`) is for.
  **A real GC bug ASan caught, worth writing down.** The first cut had each session's `CompiledUnit`s
  owned by the *parent*, and the parent's `mark_vm_roots` marking the constants inside them — but
  those constants are strings on the **child's** heap, so once the child collected one, the parent's
  next mark read freed memory. Units now belong to the VM that ran them (`vm_adopt_unit`), which is
  also the collector that must root and free them. The rule this makes concrete: *a heap's roots are
  the job of the collector that owns the heap*, and the boundary between two VMs is exactly where that
  is easy to get wrong.
  **ADDITION · `computer.readline(prompt?)`.** The plan listed this as N5 work; it was never added.
  `ask()` returns `""` for both an empty line and end of input, and a REPL has to tell Ctrl-D from a
  blank line, so `readline` returns `ghost` at end of input.
  **Compiler and resolver knobs**, both optional so no existing caller changed:
  `compile_program(program, fold_constants, repl_capture_last?)` compiles a bare trailing expression
  to RETURN its value instead of POPping it, and `resolve_program(program, known_globals?,
  const_globals?)` accepts the names the previous input defined and mutates them in place — exactly
  the two sets `cmd_vibe` threads through a fresh `Resolver` each time.
  **Fixed · the self-hosted parser's error bundle lost its flavor.** `parse_program` collected
  recovered errors as message *strings* and then `chuck`ed them joined, which made every multi-error
  parse failure a `SkillIssue` positioned inside `parser.funny` itself. It now keeps each error's
  flavor and position and re-raises the first one's, the way `parser.py`'s `ParseErrorBundle` exposes
  `errors[0]`. Visible immediately in the REPL, which is how it was found.
  **KNOWN DIVERGENCE · REPL diagnostics are a summary line.** The Python REPL renders the full §4.2
  diagnostic, source snippet and caret included, because it still has the input text sitting next to
  the error. Reproducing that needs a FunnyLang port of §4.2's renderer, which belongs with N6's
  renderer rather than bolted onto the REPL — so this prints flavor, position and message. Everything
  else about a session is byte-identical.
  **Two more reserved-word collisions**, both found by writing this file: `vibe` is a keyword
  (§3.3's `vibe` statement) so `bet vibe()` will not parse, and `sus` opening an if-statement means a
  statement *starting* with `sus.` is a parse error — so `sus.dump(x)` can never be a bare statement,
  only `yo _ = sus.dump(x)`. The second is a genuine usability wart with a one-token-lookahead fix
  (`sus` followed by `.` is not an if), but changing the grammar is not N8's job; logged here so it
  is decided deliberately rather than discovered again.
  Verified: 19 tests in `tests/native/test_native_vibe.py`, 11 of them running the *same scripted
  session* through both REPLs and comparing everything after the banner **byte for byte** —
  expressions, statements, persistent globals, multi-line blocks, a squad across lines, `.help`,
  `.clear`, blank lines, `.quit`, Ctrl-D, a stdlib import, and a closure created in one input and
  called in two later ones. Plus `.xray`, `.time`'s `:.2f` shape, `dip()` not killing the host, and
  errors of both kinds leaving the session alive. Clean under `FUNNY_GC_STRESS=1` and ASan/UBSan.
  Full suite 1276 passed; MSVC `/W4 /WX` clean, REPL verified on Windows.
- **N8 task 6 · the CLI, in FunnyLang — and `native/main.c` down from 379 lines to 118.**
  `selfhost/cli.funny` owns argument parsing and every subcommand: `run`, `build`, `yeet`, `xray`,
  `fmt`, `test`, `vibe`, plus the bare-file shorthand and the global flags. `main.c` now does the
  three things that cannot be done from inside the language before the language is running — set up
  the console, read the two diagnostic flags, find the CLI bundle and hand it argv. (Half of what is
  left is the comment explaining why those three.)
  **DECISION · how far to shrink, put to the project owner.** Moving `build`/`xray`/`fmt`/`test`/
  `vibe` was mechanical; `run` and especially `yeet` were not, because `yeet` is a hundred lines of
  reading an env var, finding the running executable, appending bytes to a binary, marking it
  executable and making a temp file — none of which the language could express. Three options were
  offered: add those primitives and shrink all the way, keep `yeet`'s binary surgery behind one
  narrow C entry point, or leave `run`/`yeet` native and log a partial task. The owner chose to add
  the primitives, so:
  **ADDITIONS · `computer.env(name, default?)`, `computer.exe_path()`, `filez.append_bytes()`,
  `filez.make_executable()`, `filez.temp_file(prefix?)`, `sus.run_program(bytes, args?, label?)`.**
  None is CLI logic; each is platform I/O a systems-capable language plausibly wants anyway.
  `append_bytes` in particular was a plain gap — `write_bytes` existed, `append_to` was text-only, so
  a program could *create* a binary but never add to one. `run_program` is the third member of the
  run family and the one for a caller that *is* the command line: output straight through, the full
  §4.2 diagnostic on an uncaught error, and the process exit code back (`run_bytecode` captures and
  reports a flavor; `run_in` is a persistent session).
  **`computer.exe_path()` differs between implementations by nature** — the native runtime answers
  with the `funny` binary, which is how it finds its sidecars; the Python implementation has no such
  binary and answers with the entry script. Tested on properties (non-empty, exists, its directory is
  a directory) rather than by byte-diff, the same exception `computer.flex()` already carries.
  **`diag_set_default_options`.** `--serious`/`--no-color` configure the *runtime's* renderer, so
  they cannot live in the FunnyLang half: they have to apply to a program the CLI runs in a nested
  VM. `main.c` parses them once and installs them process-wide; `cli.funny` strips them again on its
  side so they never reach a program's own `the_args()`.
  **`bootstrap/cli.funnypak` is now checked in too**, with the same staleness test and a rewritten
  `bootstrap/STAGE0.md`. This is a real change in what the binary needs: before, `funny` could run a
  `.funnyc` on its own; now it does nothing without this bundle. `--version` is deliberately answered
  by the C loader anyway — it is the one command someone runs when their install is broken, so it
  must not depend on the thing that might be broken. N10 embeds both bundles and the sidecars go
  away.
  **Fixed · single-file programs were losing their diagnostic snippet.** The first cut compiled every
  `funny run` through the bundler, which replaces the entry's source name with its canonical bundle
  key — so `diag_render_error` could no longer read the file back and every one-file program silently
  lost its source snippet and caret, which is most of what §4.2 is for. A single-module program is
  now emitted as a plain `.funnyc` carrying its own path; only a genuinely multi-module program gets
  a bundle.
  **A process note worth keeping.** Three separate times this milestone, a shell check of the form
  `./build.sh 2>&1 | grep -E "error" ; echo built` reported success for a build that had actually
  failed, and the stale binary then produced a baffling wrong answer — `funny` dispatching every
  subcommand to `cmd_run` because the *old* `main.c` was still linked. `build.sh` has `set -e` and
  does exit non-zero; the `;` threw the status away. Checks now use `&&`.
  Verified: every subcommand run through both CLIs and diffed — `run` (source, `.funnyc`,
  `.funnypak`, multi-module), the bare-file shorthand, `build` (bytes *and* message), `xray` in all
  three modes, `fmt --check`, `test` over `tests/lang` and `examples` — **0 mismatches**, plus the
  uncaught-error diagnostic byte-identical on stderr, `dip`'s exit code, `computer.explode()`'s 69,
  and a `yeet`ed binary that runs. 27 tests in `tests/native/test_native_cli.py`. Full suite 1305
  passed; MSVC `/W4 /WX` clean, with `run`/`build`/`xray`/`fmt`/`test`/`vibe`/`yeet` all exercised on
  Windows, including a yeeted `.exe` that runs.
  **N8 acceptance:** `run/build/xray/fmt/test/vibe/yeet` all work with the C binary and match the
  Python CLI. `bootstrap` is N9 task 2 and reports itself as not yet wired up rather than pretending.
- **N9 task 2 · `funny bootstrap --verify` on the C VM.** `selfhost/bootstrap.funny` (the library)
  plus `selfhost/bootstrapcli.funny` (argv), the same split `fmt.funny`/`fmtcli.funny` has. Stage 2
  is the toolchain linked from `selfhost/` by this build, stage 3 is what stage 2 produces from the
  same sources, stage 4 is what stage 3 produces; byte-equal stage 3 and 4 is the fixed point. It
  reaches one: **255,687 bytes, identical.**
  **DECISION · the native bootstrap stages the linker, not the compiler.** `cli.py`'s stages
  `funnyc.funny`, whose stage 3 and 4 are bare `.funnyc` files — and a bare `.funnyc` has no bundle,
  so its own `gimme { ... } from "compiler.funny"` has nothing to resolve against. The Python VM gets
  away with that by pointing a `ModuleResolver` at `selfhost/` and *compiling those imports on the
  fly*, which requires a compiler inside the running VM. The native runtime has none by design: a
  file import is resolved at **link** time. So every native stage is a self-contained bundle, which
  means staging `linker.funny` rather than `funnyc.funny`. That is strictly stronger — linking a
  bundle exercises lexer, parser, resolver, compiler, emitter *and* bundler, where compiling one file
  skips the last — and it is the only shape that works without a run-time compiler. The two byte
  counts therefore differ by design (255,687 vs 1,009); every other line of output is identical,
  thousands separators included, and a test asserts exactly that.
  **`--diff` is tested through a driver**, because it is unreachable through the CLI: the bootstrap
  reaches a fixed point, so the stages never differ, so the one code path whose whole job is
  explaining a *failure* would otherwise ship untested. `proto_differs` was widened to compare every
  field of a proto, matching `cli.py`'s whole-`FunctionProto` comparison, so both implementations
  agree on *which* proto is the first divergent one — and the case where two protos are equal but the
  bundles still differ (constants live in the unit's pool, not the proto) is reported as exactly that
  rather than pointed at an innocent proto.
  **Found · a bundle cannot address a module outside the entry's directory.** A `.funnypak` keys its
  modules by path relative to the entry's directory, so an import that escapes it
  (`../bootstrap.funny` from `selfhost/_drivers/`) gets an absolute key the runtime loader can never
  resolve back — `gimme` at run time normalises to `../bootstrap.funny`, which is not that key.
  **Both implementations share this**, since `funnylang/modules.py`'s `_canonical_name` and
  `make_pak_module_loader` do the same thing. Not fixed here: the fix is to root canonical names at
  the common ancestor of all modules, which changes every bundle key and would break byte-identity
  with the Python bundler mid-milestone. Logged so it is decided deliberately; today it only bites a
  test driver, and the workaround (put the driver beside what it imports) is one line.
  Verified: 8 tests in `tests/native/test_native_bootstrap.py` — the fixed point, output shape
  against the Python CLI, `--verify` being required, `--keep` leaving all three stages behind and its
  absence leaving nothing, and three shapes of `--diff` report. Full suite 1313 passed.
- **N9 tasks 3-7 · the no-Python proof, the CI matrix, the docs, and 2.0.0.**
  **Task 3 — the job the plan calls "the single most important test in the document."** A new CI
  job runs in a `debian:12` container with `gcc` and nothing else: it *asserts* no `python`,
  `python3` or `pip` is on PATH, then **deletes every `.py` file and `funnylang/` from the
  checkout**, then builds `native/` and exercises the entire toolchain — `--version`, a source file,
  a multi-module program, `build` then run the bundle, `test` over `tests/lang` and `examples`,
  `xray`, `fmt --check`, `yeet` and run the result, the REPL, and `bootstrap --verify`. Then it does
  the load-bearing subset again under `env -i` with **no PATH at all**, which also rules out shelling
  out to anything.
  Proven locally first (`build/n4/no_python_check.sh`, same shape in a sandbox with no `.py` in it):
  every step passes, `bootstrap --verify` included. The container job itself is written but
  unverified locally — there is no Docker on this machine — so it is CI that will confirm the
  `apt-get`/`checkout` half. Said plainly rather than claimed.
  **Task 4 — the compiler dimension.** The `native` job built with whatever `cc` resolved to, which
  meant clang-on-Linux was never exercised, and that is exactly where a `-Werror` difference shows
  up. The matrix now names the compiler: `{windows, msvc}`, `{ubuntu, gcc}`, `{ubuntu, clang}`,
  `{macos, clang}`. No `macos`+`gcc` entry, because on a GitHub macOS runner `gcc` is a shim for
  Apple clang and would build the same code with the same compiler under a different name — said in
  a comment rather than left as a silently redundant matrix row. Verified locally: `CC=clang
  ./build.sh` is clean and all 248 native tests pass under it.
  **Task 5 — docs.** New `docs/NATIVE.md`: building, the two binaries and what ships beside them,
  the `native/` layout, **the platform boundary** (`platform.c` is the only file allowed
  `#ifdef _WIN32`, and why that is a rule and not a preference), **the GC contract** as four
  numbered rules including the one this milestone learned the hard way — *a heap's roots are the job
  of the collector that owns the heap* — how to test, how to port, and the debugging switches.
  `README.md`'s install section is now the C build. `docs/BYTECODE.md`'s yeet section said "frozen
  Python runtime stub" and had the trailer bytes in the wrong order; it now describes `funnyrt` and
  puts the magic first, matching the code. `docs/LANGUAGE.md`'s self-hosting section said only the
  compiler was self-hosted; the whole toolchain is.
  **Task 6 — 2.0.0.** Bumped in the four places that carry it (`funnylang/__init__.py`,
  `native/main.c`, `selfhost/cli.funny`, `selfhost/vibe.funny`) plus `pyproject.toml`, and the test
  that pinned a literal version string now reads it from `__init__.py` instead — a test that has to
  be edited on every bump is a fifth copy of the version. `CHANGELOG.md` gets a full 2.0.0 section:
  the runtime, the self-hosted toolchain, every language and stdlib addition, the six bugs fixed
  along the way, and a compatibility note (bytecode format unchanged; `.funnyc`/`.funnypak` from
  1.1.0 run unmodified; the binary now needs its two bundles beside it, and `--version` deliberately
  works without them).
  **Task 7 — the honesty pass.** `README.md`'s "what this isn't" said the VM is Python and is
  frozen into every yeeted binary. That was true when written and is now false. Replaced with what
  *is* true: the VM is not self-hosted and **cannot** be, because something has to execute bytecode
  and a FunnyLang-hosted VM would need a VM to run it — the regress never bottoms out. What changed
  in 2.0.0 is that the C runtime replaced the Python one, and a yeeted program went from 8 MB of
  bundled CPython to a couple of hundred KB. The `funnylang/` package is described as what it now
  is: a reference oracle on its way out, with a pointer to N11.
  Verified: full suite 1313 passed at 2.0.0; every subcommand still byte-identical to the Python
  CLI (0 mismatches); the no-Python sandbox green end to end.
- **N10 task 1 · the toolchain is compiled into the binary.** `native/toolchain_blob.c` — the linked
  `selfhost/` bundle as a `const unsigned char[]` — is generated by `tools/bin2c.funny` and checked
  in, following `unicode_tbl.c`'s pattern. `funny` is now the entire install: no `bootstrap/`, no
  `selfhost/`, nothing beside it. Verified in a sandbox holding only the binary and some `.funny`
  files, with `env -i` and no PATH: `run`, `build`, `xray`, `fmt`, `test`, `yeet` and `vibe` all
  work.
  **The generator is FunnyLang**, per the amendment made in N8: `tools/bin2c.funny`, run by `funny`
  itself, because N11 removes every `.py` including dev tooling. So the toolchain generates the
  artifact that *is* the toolchain — `funny build selfhost/cli.funny` then `funny tools/bin2c.funny`
  then `./build.sh`, and the binary has linked and embedded its own successor with no Python in the
  loop.
  **Prerequisite · every tool became a library.** `cli.funny` had been *compiling*
  `selfhost/xray.funny` and friends from source on every invocation, which meant a shipped binary
  still needed `selfhost/` on disk. Each is now a library plus a thin `*cli.funny` wrapper — the
  shape `fmt.funny`/`fmtcli.funny` already had — so `cli.funny` imports them and they end up inside
  the bundle. Side benefit: `funny xray` no longer recompiles the disassembler every time it runs.
  **`bootstrap/`'s two checked-in bundles are gone.** The blob supersedes both: it is what breaks the
  compiler's circular dependency *and* what makes the artifact a single file, so keeping a second
  copy of the same bytes on disk was redundant. `bootstrap/STAGE0.md` is rewritten around the blob,
  and the staleness test now relinks `selfhost/`, parses the byte array back out of the generated
  `.c`, and compares — it caught a stale blob the first time it ran, which is the entire point.
  **`FUNNY_CLI` replaced the sidecar search**, deliberately. It is an explicit override for the
  development loop (edit `selfhost/`, relink, run, without regenerating the blob and recompiling C).
  What it is *not* is a filesystem search: a shipped binary's behaviour should never depend on what
  happens to be sitting next to it. `FUNNY_TOOLCHAIN` is gone entirely, along with the dead lookup
  in `cli.funny`. `FUNNY_STUB` stays, because `funny yeet` copies `funnyrt` off disk and a binary
  cannot contain a copy of a binary that contains it.
  **The blob is linked into `funny` only.** `funnyrt` is a VM with no compiler in it, and 372KB it
  never references would land directly in the size of every yeeted program. Both build scripts and
  both test fixtures exclude it from the stub.
  **Two self-inflicted bugs worth recording**, both from patching files with a Python script:
  `native\toolchain_blob.c` written in a non-raw string put a literal **tab** into `build.bat`
  (`cl` then reported `Cannot open source file: 'oolchain_blob.c'`), and adding `-I native` so a
  generated file in a temp directory could find its header made `#include <string.h>` resolve to
  FunnyLang's own `native/string.h`, failing across half the runtime.
  **The second is fixed at the source rather than documented, at the project owner's suggestion:
  `native/string.{c,h}` is now `native/da_string.{c,h}`.** The first instinct was to write down a
  rule — *never put `native/` on the include path* — but a rule that exists because a filename is
  a trap is worse than not having the trap. `deadass` is already the language's own word, so
  `da_string` is short, cannot collide with anything in the C standard library, and the header
  says why it is called that so nobody tidies the name back. Verified by compiling the whole
  runtime with `-I native` — the exact command that failed before, now clean. `native/error.h` is
  the only other name shadowing anything (glibc's `<error.h>`), and that same build shows it is
  harmless in practice: nothing in the runtime, or in any system header it pulls in, includes it.
  Sizes: `funny` 624KB (600KB stripped), `funnyrt` 251KB on Linux/gcc; 808KB and 446KB with MSVC.
  All comfortably inside task 4's 2MB ceiling. Verified: every subcommand still byte-identical to
  the Python CLI (0 mismatches), the no-Python sandbox green end to end including
  `bootstrap --verify`, and the single-file sandbox green on both Linux and Windows.
- **N10 tasks 2-9 · the release pipeline.** New `.github/workflows/release.yml`, and the PyInstaller
  `release-build`/`release-publish` jobs deleted from `ci.yml` — freezing CPython to ship a binary
  stopped making sense the moment the runtime stopped being Python.
  **Task 2 · five artifacts**, plus the `funnyrt` stub for each: `linux-x86_64` and `linux-aarch64`
  (both on `ubuntu-22.04`), `macos-x86_64` (`macos-13`), `macos-aarch64` (`macos-14`),
  `windows-x86_64` (MSVC). No `zig cc` cross-compilation was needed after all — GitHub now has a
  native ARM Linux runner, and a native build is worth more than a cross-compiled one.
  **Task 3 · the glibc floor, asserted rather than assumed.** Linux builds happen on `ubuntu-22.04`
  (glibc 2.35) so the artifact runs on Debian 12 and anything newer, and a step reads the symbol
  versions back out with `objdump -T` and *fails the build* if anything above 2.35 crept in. Then
  `verify-published` runs the published binary inside an actual `debian:12` container, because the
  inference and the fact are different things.
  **Task 4 · `strip -s` and a 2MB ceiling**, checked per artifact. Current sizes: `funny` 600KB
  stripped (624KB unstripped) and `funnyrt` 251KB on Linux/gcc; 808KB and 446KB with MSVC.
  **Task 6 · two triggers, and the nightly gates on green.** A `v*` tag publishes a permanent
  release; a push to `master` moves the rolling `nightly` prerelease. The nightly hangs off
  `workflow_run` rather than `push` **on purpose**: the plan says "every *green* push", and a plain
  `push:` trigger would happily ship a nightly built from a master whose suite had just gone red.
  The release body is the matching `CHANGELOG.md` section, extracted with an `index()`-based `awk`
  (a regex built from an awk string literal needs escapes that do not survive YAML inside shell —
  found by running it, not by reading it) and the publish job *fails* if there is no section for
  the version being tagged.
  **Task 7 · `install.sh` and `install.ps1`.** Both verify the download's SHA-256 against the
  release's own `SHA256SUMS` and refuse to install on a mismatch; there is deliberately no `--force`,
  because a corrupted or substituted download is the exact thing a checksum exists to catch. The
  `funnyrt` stub is treated as optional — only `funny yeet` needs it, so failing to fetch it is a
  warning, not a failed install. `install.sh` grew a `FUNNY_BASE_URL` seam so it can be pointed at a
  local directory and **actually tested**: all four paths (happy, corrupted binary, asset missing
  from `SHA256SUMS`, missing stub) are exercised in `build/n4/install_check.sh`. A release script
  only ever run during a release is a bad place to discover a quoting bug — and the first version of
  that check reported a false pass, because `if cmd | tail; then` takes `tail`'s exit status.
  **Task 9 · `verify-published`.** Downloads from the *published release* rather than the build job's
  artifacts — those are different things and only one of them is what a user gets — verifies the
  checksum, and runs `--version`, `run`, `test tests/lang` and `bootstrap --verify` on Linux, macOS
  and Windows. It deliberately builds nothing, or it would stop testing what was published.
  **Verified locally**: the installer's four paths, and the release-notes extraction against the real
  `CHANGELOG.md` (74 lines for 2.0.0, empty for a version with no section so the guard fires). The
  workflow itself is written but unrun — it needs a tag and a GitHub runner matrix — so CI is what
  will confirm the rest. Said plainly rather than claimed.


- **N11 task 2 · the pytest suite, test by test, with a disposition.** Task 2 says "no silent
  deletions", so before a single `.py` is removed here is what is in `tests/` and what replaces it.
  The suite is **469 test functions**, which `pytest` expands to **1,313 collected tests** through
  `@parametrize`; the table counts *functions*, because that is the unit a replacement is written
  against. Classifier: `build/n4/survey_pytest.py`, an AST walk that folds local string constants
  before deciding, since most of `test_stdlib.py` hoists its source into a `src = (...)` local and a
  naive reading calls that "not mechanical". **131 tests (27%) are already `.funny`/`.expected`
  goldens wearing a `pytest` disguise** — `assert run_funny("...") == "..."`, with expected output a
  person wrote as a specification, which is exactly the distinction drawn when "grow the goldens"
  was chosen over "freeze today's output".

  | file | fns | disposition |
  |---|---:|---|
  | `test_stdlib.py` | 82 | → `tests/lang/` goldens. 72 convert as-is; the rest need argv or an exit code, so they wait on the `.expected` directives below. |
  | `test_vm.py` | 53 | → `tests/lang/` goldens; 40 convert as-is, the rest are error identity → `!ERROR <Flavor>`, which the runner already understands. |
  | `test_squads.py` | 26 | → goldens; 18 as-is, 8 assert on emitted op sequences → `funny xray` disassembly goldens. |
  | `test_lexer.py` | 46 | → `funny xray --tokens` goldens. It prints `Token(YO, 'yo', 1:1)` per line — the same kind/lexeme/value/position tuple the `lex`/`lex_kinds` helpers assert on. |
  | `test_parser.py` | 81 | → `funny xray --ast` goldens. `--ast` prints the s-expression `dump_ast` produces (`(program (yo x (+ 1 2)) (yap x))`), so the 19 explicit `dump_ast` comparisons are a copy-paste and the `isinstance` ones become the shape of that s-expression. |
  | `test_compiler.py` | 40 | → `funny xray` disassembly goldens; const-pool tag assertions become the `; 14` comments the disassembler already emits. |
  | `test_resolver.py` | 30 | → `!ERROR` goldens (`WhoDis`, `ImmutableVibes`, `ParserHadAStroke` are all observable from outside) plus scope/shadowing behaviour goldens. |
  | `test_serializer.py` | 12 | → a round-trip test written in FunnyLang against `selfhost/`'s own emitter, plus `funny xray --pak`. |
  | `test_errors.py` | 10 | → diagnostic goldens; needs the runner to compare **stderr**, since a rendered diagnostic is the thing under test. |
  | `test_modules.py` | 16 | → multi-file goldens under `tests/lang/modules/`; `funny test` already links a test through `build_bundle`, so a `gimme "sibling.funny"` works today. `FUNNYPATH` resolution needs `computer.env`, added in N8 task 6. |
  | `test_hardening.py` | 10 | → goldens for the cycle-printing and Unicode cases; the five jump-widening tests become disassembly goldens; the "50k statements under 10s" budget becomes a `.funny` test using `clock`. |
  | `test_cli.py` | 34 | → `.funny` tests driving the real binary through `sus.run_program`. Already mirrored by `tests/native/test_native_cli.py`, which is itself `pytest` and dies in task 5 — the port is from the *native* file, not the Python one. |
  | `test_packager.py` | 8 | → `funny yeet` tests, ported from `tests/native/test_native_yeet.py` for the same reason. |
  | `test_bootstrap.py` | 3 | → `funny bootstrap --verify`, already an N9 CI step and a strictly stronger check than two of these three. |
  | `test_fuzz.py` | 2 | → `tests/fuzz.funny`, a seeded generator using `rizz`. A fuzzer that is not reproducible is a bug report you cannot act on, so the seed is fixed and printed. |
  | `test_selfhost_*.py` (7 files) | 16 | **Dropped, and this is the one real loss.** Every one compares the Python implementation against `selfhost/` — the same source through two front ends, diffed. Delete Python and there is no second side to diff against; nothing can "replace" a differential test whose other half is gone. What remains is the bootstrap fixed point (`stage3` and `stage4` byte-identical), which checks the same compiler against itself rather than against an oracle. Weaker in kind, and worth saying so plainly rather than filing it under "replaced by". |

  **Two mechanism gaps this survey exposed**, both prerequisites for task 1 rather than task 1 itself:

  1. **`.expected` needs directives.** Today a golden is stdout-only, and the runner passes `[]` for
     argv and ignores the exit code, so a whole class of test cannot be expressed. `!ERROR <Flavor>`
     already establishes the pattern of a first-line directive; it generalises to `!ARGS`, `!EXIT`,
     `!STDERR` and `!XRAY <flags>`. The primitives are all present — `sus.run_bytecode` has taken an
     argv stash and returned a `code` since N8 — it is the runner that does not use them.
  2. **A test cannot import a module above it.** A `.funnypak`'s keys are relative to the entry's
     directory, so `tests/lang/foo.funny` cannot `gimme "../../selfhost/lexer.funny"` (logged
     earlier in this section, in both implementations). That blocks the tidiest form of the lexer and
     parser goldens — importing the front end and printing its output — which is why they go through
     `funny xray` instead. That is not a workaround so much as the better test: it asserts on the
     shipped command's output rather than on an internal call.

- **N11 task 1 · `.expected` directives, and the first goldens that need them.** The survey above
  said a golden could only ever be "stdout, byte for byte", plus the one special case of a first line
  reading `!ERROR <Flavor>`. That is enough for a language test and nothing else: a program that
  needs argv, or whose exit code *is* the assertion, or that is testing how a diagnostic **renders**
  rather than which flavor it is, could not be written down at all. With a 1,313-test suite being
  replaced by this corpus, "cannot be written down" means "coverage quietly lost", so the `!ERROR`
  idea was generalised instead of special-cased further: any run of leading `!` lines in a
  `.expected` is a directive block, and the body is what follows.

  `!ARGS a b "two words"` · `!EXIT 3` · `!ERROR SkillIssue` · `!DIAG [serious]` · `!XRAY <flags>` ·
  `!STDOUT`. An unrecognised directive **fails the test** rather than being read as body text —
  quietly comparing against a line the author meant as an instruction turns a typo into a passing
  test, which is the worst outcome on offer.

  Three things had to exist under it:

  1. **`sus.run_bytecode` returns `diag`** — the rendered §4.2 diagnostic as *text*, produced into a
     `tmpfile()` by `diag_render_error` instead of printed. Colour is forced off so a golden cannot
     depend on whether the machine running it has a terminal; `serious` is a caller option rather
     than the process default, because the two renderings are different outputs and both need
     testing. Third argument is an options groupchat, so the surface grew by one optional parameter.
  2. **`DiagOptions.sourceRoot`** — without it a `!DIAG` golden loses its caret line, which is
     exactly what `tests/test_errors.py::test_caret_points_at_correct_column` is *for*. A bundle
     stores each module under a key relative to the entry's directory, so an error in
     `tests/lang/errors/x.funny` names itself `x.funny` and cannot be found from the repository root.
     The path is tried as given first, so `funny run` is unchanged, and only then rooted.
  3. **`selfhost/xray.funny` returns its report instead of printing it** — `xray_lines`/`xray_text`
     build the text, `xray` prints it. A golden needs the report as a value it can compare. This is
     what carries the lexer, parser and compiler tests across: `funny test` imports `xray_text`
     directly (both files live in `selfhost/`, so the bundle-key limitation does not bite).

  **The differential against the Python `funny test` had to be narrowed, and that is not a dodge.**
  `cli.py`'s runner does not understand the new directives and will not learn them — it is deleted in
  task 5. Comparing the two over a corpus containing them compares a feature against its absence, so
  `test_native_test_runner.py` now stages a copy of `tests/lang` and `examples` with the native-only
  *goldens* removed and runs both over that. It copies the trees whole and deletes `.expected` files,
  rather than picking pairs: a `.funny` with no golden is ignored by both runners but is still there
  to be imported, and `examples/modules/main.funny` needs `mathstuff.funny`, which has no golden of
  its own. The `test_native_cli.py` subcommand case moved to `examples` for the same reason.

  **First goldens: 14, all verified against the Python oracle rather than captured.** Seven under
  `tests/lang/lexer/` (`!XRAY --tokens`) and five under `tests/lang/parser/` (`!XRAY --ast`) were
  diffed line by line against `funnylang.lexer` and `funnylang.ast_nodes.dump_ast` — 0 differences.
  That check is only possible *now*, while the oracle still exists, which is the whole reason N11 is
  ordered last. They cover every keyword (all 46), every operator including both maximal-munch
  cases, all five number bases and float forms, string escapes down to the NUL, comment and newline
  collapsing, CRLF folding, and the full precedence, associativity, pointer, slice, postfix,
  assignment-target, literal and lambda tables. One `.gitattributes` exception was needed:
  `tests/lang/lexer/crlf_newline.funny -text`, because `*.funny text eol=lf` would rewrite the file
  whose CRLF *is* the test.

  **Two product findings fell out, neither introduced here.**
  - **`yapper.is_letter` is ASCII-only natively**, so `yapper.is_letter("中")` is `cap` where Python
    says `fax`, and therefore `yo 変数 = 1` is a `LexerSaidNah` natively and lexes fine in Python.
    This is the N4 AGENT CHOICE logged in `native/yapper.h` coming due: `native/unicode_tbl.c` and
    its generator were deferred N4 → N5 → still absent, and `tools/` today holds only `bin2c.funny`.
    **Nothing in the 1,313-test pytest suite catches it** — that suite runs the *Python* VM — and no
    program in `tests/native/programs/` classifies a non-ASCII character either. Written up in
    `tests/lang/lexer/README.md` with the goldens that go in when the tables land. This makes N11
    task 3 bigger than "port a generator": the generator does not exist and never did.
  - **`funny xray` exits 0 on a lexer error**, and the diagnostic labels the file `cli.funny` rather
    than the file being x-rayed. Found while generating goldens; not fixed here.

- **N11 task 1 (cont.) · a Windows bundler bug, found by running the new goldens on Windows.** The
  first `!DIAG` golden passed on Linux and failed on MSVC, and the difference was the path inside the
  diagnostic: `caret_points_at_column.funny` against
  `F:/my_program_lang/tests/lang/errors/caret_points_at_column.funny`. Not a diagnostics bug —
  **every module in every bundle built on Windows was keyed by its absolute path.**

  `selfhost/bundler.funny`'s `canonical_name` strips the entry directory with a
  `starts_with(entry_dir + "/")` test. On Windows `filez.abs_path` returns forward slashes (a
  deliberate normalisation in `platform_abs_path`, with a comment saying the rest of the runtime
  stays POSIX-style) while `filez.dir_of` and `filez.join_path` return **backslashes** — they were
  changed to the native separator during N9 to match what `pathlib` renders. Each change is
  defensible alone; together the prefix never matches and the strip silently does nothing.

  Three consequences, none of which announce themselves: a `.funnypak` built on Windows is not
  byte-identical to the same bundle built anywhere else; a `yeet`ed executable carries the builder's
  directory layout into every diagnostic it ever prints; and the keys stop matching
  `funnylang/modules.py`, which has always used `Path.relative_to(...).as_posix()`. Fixed by
  normalising both sides before the comparison and emitting `/` — the reference's own format.
  Verified by building the same file on both platforms: identical SHA-256, and
  `bootstrap --verify` now reports the same 256,003 bytes on Windows as on Linux.

  **Why it survived: `tests/native/` never runs on Windows.** `tests/conftest.py`'s `native_binary`
  fixture builds with gcc-style flags, so `CC: msvc` makes it raise and every test skip; ci.yml's
  differential steps are `if: runner.os != 'Windows'` for that reason. The Windows job proved the
  code compiles and the binary starts, and nothing else. Rather than teach a fixture that gets
  deleted in task 5 how to drive `cl`, the Windows job now runs `funny.exe test tests/lang` and
  `funny.exe bootstrap --verify` — no Python, catches this class of bug, and is the shape *every*
  CI step takes after N11 anyway.

- **N11 task 1 (cont.) · seven compiler goldens, and what a disassembly golden cannot see.**
  `tests/lang/compiler/` covers the opcode-level assertions of `tests/test_compiler.py`: the full
  binary-operator table, the unary ops, all four short-circuit forms with their distinct keep-jumps
  (`&&`→JUMP_IF_FALSE_KEEP, `||`→JUMP_IF_TRUE_KEEP, `??`→JUMP_IF_GHOST_KEEP, `?.`→GET_PROP_SAFE),
  if/elif/else, both loop forms, the iterator protocol, break/continue, CALL vs INVOKE, upvalue
  capture, the default-parameter ghost prologue, and `regardless` being compiled twice. All seven
  diffed clean against `funnylang.disasm.disassemble`.

  A golden of this shape asserts *more* than the pytest it replaces, which mostly checked
  `"OPCODE" in ops`; the golden pins the offsets, the operands and the constant-pool comments too.
  What it cannot see is proto **metadata** that the disassembler does not print: `is_variadic`,
  `default_count`, `upvalue_count`. Those are covered behaviourally instead — `tests/lang/variadics.funny`
  and the closure goldens exercise them — which is weaker than a direct structural assertion and is
  recorded here rather than papered over. Printing them would change `funny xray`'s output format and
  break its byte-identity with the Python disassembler, which is not a trade worth making for this.

- **N11 task 1 (cont.) · the corpus goes from 76 goldens to 268.** `test_stdlib.py`, `test_vm.py`
  and `test_squads.py` were 161 tests of the shape `assert run_funny(SRC) == EXPECTED`, with both
  halves literals a person wrote as a specification. That is a golden pair already, so converting
  them is **transcription, not capture** — `build/n4/convert_goldens.py` lifts the two literals out
  of the AST and writes the files. A test with several such asserts becomes several files, so a
  failure names one behaviour instead of a bundle of them. 144 came across mechanically; the rest
  were written by hand from the same assertions.

  **All 268 were then run through the *Python* VM as an oracle** — `stdlib` 86/86, `vm` 48/48,
  `squads` 20/21, `errors` 14/15, the two "failures" being the `!XRAY` and `!DIAG` directives that
  the Python runner does not know. That matters most for the goldens that were *not* transcribed:
  `filez`, `clock`, `computer`, `rizz` and the squad display cases were written fresh and had no
  reference behind them until this run.

  Three of the 144 disagreed with the native VM, and each is recorded rather than adjusted:
  - **`yapper.is_letter` and `yapper.is_alnum`** — the ASCII-only gap above. The goldens are right
    and the implementation is wrong, so they are parked as `.pending` files rather than deleted or
    weakened: `funny test` pairs `*.funny` with `*.expected`, so a `.pending` suffix leaves them
    inert with their expected output intact and activating them is dropping the suffix.
  - **`internet.is_it_up` with `FUNNY_NO_NET=1`** — dropped. The pytest sets the variable with
    `monkeypatch`; a golden cannot set an environment variable for the program it runs, and §5
    already puts network behaviour in the "tested on properties, not goldens" bucket. The
    alternatives were an `!ENV` directive that mutates the runner's own process environment, or a
    hostname chosen to fail DNS — a test that hangs on a slow resolver.

  **Tests that asserted on a property rather than a value convert fine, once the property moves
  inside the language.** `test_clock_now_is_numeric` asserted `isinstance`-ish things about a
  timestamp in Python; the golden is `yap what_is_it(clock.now())` against `numba`. Same for
  `computer.ram()`, `rizz.uuid()`'s length, and the `mafs` constants. `test_filez_roundtrip` used
  pytest's `tmp_path`; the golden asks the language for `filez.temp_file()` and removes it again, so
  it carries no absolute path and leaves nothing behind.

  **Eleven `isinstance(err, Flavor)` tests in `test_vm.py` needed nothing at all** — `err_div_zero`,
  `err_type_mismatch`, `err_wrong_arity_too_few` and the rest already exist in `tests/lang/`, one
  per flavor. Checked name by name rather than assumed.

- **N11 task 3 · `tools/gen_unicode.funny` and `native/unicode_tbl.c`, written rather than ported.**
  The task said "port `tools/gen_unicode.py`". There was no such file, and no `unicode_tbl.c` either:
  §4's tree lists both, `native/yapper.h` cites the table as the reason its classification is
  ASCII-only, and the work was deferred N4 → N5 → never. `tools/` held exactly one file,
  `bin2c.funny`. So this is a new generator and a new table, not a translation.

  **What it cost, in visible behaviour:** `yapper.is_letter("中")` answered `cap` where the
  reference answers `fax`; `yapper.SCREAM("héllo")` returned `héllo` unchanged; and `yo 変数 = 1`
  was a `LexerSaidNah`, because `selfhost/lexer.funny`'s identifier rule goes through
  `yapper.is_letter`. FunnyLang accepted CJK identifiers under Python and rejected them natively.
  **Nothing in the 1,313-test suite caught any of it** — that suite runs the Python implementation —
  and nothing in `tests/native/programs/` classified a non-ASCII character either. It surfaced only
  when a golden asked.

  **The generator downloads UnicodeData.txt over HTTPS** and emits the tables. That is legitimate for
  a generator: it runs on a maintainer's machine, its output is checked in, and a build still needs
  nothing but a C compiler. It accepts a local path too, and records the canonical URL in the
  generated header either way rather than the path of somebody's scratch file.

  **Scope, chosen deliberately.** `unicode_is_letter` is general category L*, which is exactly what
  `str.isalpha()` accepts; `unicode_is_alnum` adds Nd/Nl/No, exactly `str.isalnum()`. Case mapping is
  the **simple** one-code-point-to-one mapping from fields 12 and 13. Full case mapping
  (SpecialCasing.txt, where `ß` uppercases to `SS`) is **not** represented: it cannot be a code-point
  map, nothing in the corpus needs it, and a partial version that quietly differs from the reference
  is worse than a stated absence. Written down in `native/unicode_tbl.h`, not just here.

  **Shape:** sorted range arrays, binary-searched — 684 letter ranges, 146 numeric, 694 upper runs,
  678 lower. A flat `0x110000`-entry table would be 1.1 MB against a 2 MB binary ceiling; Unicode
  assigns categories in blocks, so ranges hold the same information in 54 KB of source and **33 KB of
  binary** (657 KB total, from 624 KB). The lookups live in a separate hand-written `native/unicode.c`
  because `unicode_tbl.c` gets overwritten whole and nothing hand-written may live in it.

  **Two bugs found while building it, both mine, both worth the note:**
  - `parse_hex("")` returned `0` rather than `ghost`, so the ~39,000 characters with *no* case
    mapping each got a "mapping to U+0000" with a unique delta. Run compression collapsed to nothing:
    39,724 runs instead of 694. The symptom was a suspiciously large table, not a wrong answer.
  - `utf8_decode_cp`'s second parameter is the **sequence** length, not the buffer length. Passing
    `bi + seqLen` made it read a one-byte `i` as the lead of a two-byte sequence, so `SCREAM("hi")`
    returned `HⱿ`. It failed loudly and immediately, which is the good case; the same mistake in a
    bounds check would not have.

  `yapper.h`'s AGENT CHOICE is rewritten rather than deleted — the shape of a gap that no test could
  see is worth keeping on the page. Verified: 273/273 goldens on Linux and Windows, all 90
  `tests/lang/stdlib/` goldens **also pass on the Python VM**, `tests/native` 248 passed, bootstrap
  fixed point byte-identical at the same size on both platforms, MSVC `/W4 /WX` clean.

- **N11 · source paths through the front end, and compile-time diagnostics that match the reference.**
  Chosen by the project owner from three candidate mechanisms. The symptom was small — a `!DIAG`
  golden for a parse error rendered as coming from `cli.funny` — and the cause was structural:
  `parse_source(src)` took text and no path, and `oops()` had no file argument, so a self-hosted
  front-end error carried **no source at all**. The renderer fell back to whatever unit was running
  (the toolchain) and dropped the caret line entirely, because it had no file to quote. `funny run`
  papered over it by printing its own `couldn't compile <path>: <message>` summary, which was the
  honest thing to print while that was true and is strictly less than what is now known.

  A path now threads through `tokenize` → `parse_program` → `parse_source` → `resolve_program` →
  `compile_source`, and through `build_bundle` and `fmt`. The bundler keeps **two** names per module,
  which is the part worth remembering: the compiled *unit* is keyed by its canonical name, because
  that is what the loader resolves a quoted `gimme` against once there is no filesystem left; a
  front-end *error* gets a real path, because the renderer opens it to show the caret and
  `err.funny` does not resolve from wherever `funny` was run.

  **`oops`'s fifth argument became an extras groupchat rather than three more positional
  parameters** — `{"file", "roast", "hint"}`. A sixth, seventh and eighth argument would have been
  unreadable at the call site, and this leaves room for the rest of §3.9's field set. `roast` had to
  exist because **in funny mode the rendered body is the roast, not the message**: every self-hosted
  compile error was rendering with its flavor's generic default, so an unterminated string reported
  "what even IS that character. i'm not doing this." `funnylang/lexer.py`, `parser.py` and
  `resolver.py` override the roast on eleven errors between them, and all eleven are now copied
  verbatim.

  **The self-hosted resolver had no "did you mean" at all.** `suggest_name` and `levenshtein` did
  not exist on this side, so a `WhoDis` never suggested the name you probably meant — and
  `tests/test_resolver.py::test_whodis_suggests_close_match` had no native counterpart to fail.
  Ported including the tie-break: `<` not `<=`, so the *first* candidate at the best distance wins,
  which makes the order the reference walks scopes in part of the observable behaviour.

  **Two things fell out that were not the point but are worth recording:**
  - `exc.roast` raises `WhoDis`. §3.9's readable field set is flavor/message/line/col/file/trace/
    payload — `roast` and `hint` are not in it. The parser's error-recovery list now holds the error
    *objects* rather than copies of their fields, and a single parse error is **re-raised
    unchanged**, so its roast, hint and position survive exactly as the site that raised them wrote
    them. Several errors still get a joined message with the first's flavor and position, the way
    `ParseErrorBundle` exposes `errors[0]`.
  - Diagnostic paths are now separator-normalised to `/` on every platform, the same as bundle keys
    already were. Without it, `tests\lang\x.funny` on Windows against `tests/lang/x.funny`
    everywhere else made every `!DIAG` golden platform-specific. Applied to the runtime path too
    (`compile_to_blob`'s unit name), because the two halves of one program's error reporting
    disagreeing about how to spell a path would be worse than either choice.

  **Verified by comparison, not by taste:** `build/n4/diagcmp.sh` runs the same file through
  `./funny run` and `python -m funnylang run` and the stderr is byte-identical for the parse, lexer
  and resolver cases — caret line, roast, hint and all. `tests/native/test_native_selfhost.py`'s
  syntax-error test now asserts that equality directly instead of grepping for a summary line whose
  premise has changed. 275/275 goldens on Linux and Windows, `tests/native` 248 passed, bootstrap
  fixed point byte-identical on both.

- **N11 · CLI goldens and a seeded fuzzer, the other two mechanisms the owner chose.** Both needed
  the same thing first, and it turned out to be a real bug rather than test plumbing.

  **`sus.toolchain()`.** `tests/test_cli.py` drove the CLI as a subprocess 34 times. There is no
  process-spawn primitive in FunnyLang, and adding one to test the CLI would be a large new
  capability bought for a small reason — so the goldens go the other way. `sus.run_bytecode` already
  runs a bundle in an isolated VM with argv, captured stdout and an exit code, which *is* a CLI
  invocation; the only missing piece was getting hold of the toolchain's bytes. `main.c` registers
  them (`sus_set_toolchain`) rather than sus.c referencing `toolchain_blob` directly, because
  `stub_main.c` links neither — a `yeet`ed executable carries a program and no compiler, and
  `sus.toolchain()` is `ghost` there. The goldens therefore exercise **the shipped dispatch**, not a
  re-linked copy of it.

  **Runs nest, and both streams leaked.** `sus.run_program` sent the user program's output to the
  real stdout unconditionally, so a CLI driven from inside a captured child VM sprayed it into the
  test runner's own output. Same for `yell` and the uncaught-error diagnostic, which went to the real
  stderr. `RunnerOptions` now carries `out` and `err`, the VM carries an `err` stream beside its
  `out`, and `sus.run_bytecode` captures both and returns the second as `err`. At the top level those
  *are* stdout and stderr, so nothing changed for anyone running a program normally — but a child's
  diagnostics no longer escape the thing that is supposed to be isolating it. Found because the fuzz
  golden printed 300 diagnostics into `funny test`'s output and was unreadable.

  **The fuzzer's seed is fixed and printed.** A fuzzer you cannot re-run is a bug report you cannot
  act on. `rizz` is xoshiro256** seeded through splitmix64 — deterministic and identical on every
  platform — so this is a golden rather than a coin flip. 200 token soups and 100 grammar-aware
  programs, about 2.4 s, against the pytest version's 2000 and 500 through an in-process compiler:
  each case here goes through the real command line at ~6 ms, and that is what sets the counts. The
  assertion is the pytest one restated — a clean rejection is fine, garbage *should* be rejected, and
  anything else is the bug — which concretely means exit 0 or 1 with no error escaping the CLI. The
  offending source is printed on failure, so a failure is actionable without re-running anything.

  **`!NATIVE`, a seventh directive.** These three goldens cannot pass under the Python runner: they
  ask for an embedded toolchain the reference has no equivalent of. Marking them at the golden is
  better than a directory exclusion in the differential fixture — the reason lives where the reason
  applies. The runner records nothing for it; `tests/native/test_native_test_runner.py` reads it when
  staging the shared corpus.

  Verified: 278/278 on Linux and Windows, `tests/native` 248 passed, bootstrap fixed point
  byte-identical at the same size on both, MSVC `/W4 /WX` clean.

- **N11 · module goldens, and a segfault.** `tests/test_modules.py`'s 16 tests become one directory
  per case under `tests/lang/modules/`, so two cases can both have a `lib.funny`. The imported files
  have no golden of their own, so they are never run as tests but are still there to be imported —
  the behaviour `test_a_funny_file_with_no_golden_is_ignored` already pins.

  **`funny test` on a circular import died with SIGSEGV.** A `.funnypak` module was added to the
  loader's cache only once it had *finished*, so a module reached again while still running
  re-entered `vm_run_module` forever and took the C stack with it. `gimme "main.funny"` from
  `main.funny` — the smallest cycle there is — was enough. `funnylang/modules.py` has always kept a
  stack of modules currently loading and reported the cycle from it; the native loader now does too,
  with the entry module on the stack as well (Python calls `enter()` for the entry script for exactly
  this reason) and the same `circular import: a → b → a` message built from basenames.

  Worth noting *why* it had gone unseen: `funny run` on a single file emits a `.funnyc`, not a pak,
  and a lone `.funnyc` has no module loader at all — so the same source reports a `WhoDis` there. It
  is only the bundled path that recursed, and only `funny test` always bundles.

  **A missing import was a `SkillIssue`.** `selfhost/bundler.funny` reported it with
  `chuck "<string>"`, which can only ever be a `SkillIssue`; the reference raises `ImportSkillIssue`.
  Third instance of this same mistake after the parser's two, and the reason is always the same:
  spelling a flavor into a message is not the same as having that flavor.

  **One case is parked as `.pending`: `funny_modules/` found by walking up.** A `.funnypak`'s keys
  are relative to the *entry*'s directory, so a module found above it has no expressible key — the
  bundler resolves it, keys it by absolute path because the prefix does not match, and the runtime
  loader then looks for `shared.funny` and does not find it. Logged earlier in this section as a
  shared limitation; the equivalent pytest passes only because Python's runtime loader goes back to
  the filesystem, which a bundle by definition cannot. The fix — rooting keys at the common ancestor
  of every module rather than at the entry — changes every key in every bundle, and is deferred while
  byte-identity against the Python bundler is still one of the checks holding this work together.
  `FUNNYPATH` resolution is not converted at all: it needs an environment variable set for the
  program under test, which a golden cannot do, and the bundler documents not implementing it.

  **291 goldens**, 13/13 of the module ones passing on the Python VM too. `tests/native` 248 passed,
  full suite 1313 passed, bootstrap fixed point byte-identical at the same size on Linux and Windows.

- **N11 · the hardening conversion, and three product defects it uncovered.** `tests/test_hardening.py`
  is the file that tests things *at size*, and converting it found more than the rest of the suite put
  together. In order of discovery:

  **1. `groupchat` was an association list.** `groupchat_find` was a linear scan, and `groupchat_set`
  calls it, so building a dictionary was O(n²) — and FunnyLang's `groupchat` is the language's
  dictionary type. Measured before the fix: 2,000 inserts 10.5 ms, 32,000 inserts **2,935 ms** — 16×
  the input for 278× the time. That is what made the self-hosted compiler quadratic, because its
  constant pool is a groupchat keyed by `"tag:value"`:

  | statements | native before | native after | Python |
  |---:|---:|---:|---:|
  | 2,500 | 0.24 s | 0.14 s | 0.26 s |
  | 5,000 | 0.66 s | 0.29 s | 0.35 s |
  | 10,000 | 2.11 s | 0.65 s | 0.50 s |
  | 20,000 | 6.02 s | 1.37 s | 0.85 s |
  | 40,000 | **35.83 s** | **3.02 s** | 1.56 s |

  Python was linear all along; this was not. Fixed with an open-addressed hash index over `entries`,
  which stays the ordered array everything else reads — insertion order, `keys()`, display and
  serialisation are untouched. **Only string keys are indexed, deliberately**: `value_equal_narrow`
  gives numbers cross-type equality (`1 == 1.0 ==` a bignum holding 1), so a numeric hash would have
  to agree across three representations and getting that subtly wrong means a key that is present but
  cannot be found — a correctness bug, not a slow one. A string can only ever equal another string,
  by inspection of that same function. Below 12 entries there is no index at all, because a linear
  scan wins there and allocates nothing.

  **2. The self-hosted compiler had no jump-relaxation pass.** `chunk_patch_jump` chucked *"your
  function is too long. seek help."* once an offset passed `0xFFFF`, so the shipped toolchain could
  not compile a function body over 64 KB of bytecode — while the reference could, and after N11 there
  would be no reference to fall back to. Ported from `funnylang/chunk.py`: unconditional `JUMP`/`LOOP`
  become `JUMP_LONG`/`LOOP_LONG` with u32 offsets, and a conditional keeps its opcode (a wide version
  would be a different instruction with different pop/keep semantics) and gets a trampoline —
  `COND +3 / JUMP +5 / JUMP_LONG <target>`. The marking loop iterates to a fixed point because
  widening one jump moves every later one. `JUMP_LONG`/`LOOP_LONG` were absent from the self-hosted
  opcode table entirely, with a comment saying they were unnecessary *because* there was no such pass.

  Verified the only way worth verifying it: `build/n4/relax_diff.sh` and `relax_nested.sh` compile
  five shapes with both compilers and compare **bytes** — a taken conditional, an untaken one, a long
  loop body, a `bail` out of one, and four large bodies in a row so that widening an early jump pushes
  a later one over and the fixed point runs more than once. All five byte-identical, and the outputs
  identical too.

  **3. Windows could not open a file whose name is not ANSI-representable.** `filez.list_dir` returned
  `h?llo_??_??.funny` for `héllo_🎉_中文.funny`, and `exists`/`slurp` on the real name failed with
  "No such file or directory". Every filesystem call went through an `A` entry point —
  `GetFileAttributesA`, `FindFirstFileA`, `fopen`, `_mkdir`, `_fullpath` — which interprets UTF-8
  bytes in the active ANSI codepage and therefore asks the OS about a different, non-existent file.
  Python's `pathlib` uses the wide APIs, which is why the equivalent pytest passed and nothing
  noticed. Fixed by converting UTF-8 → UTF-16 once at the boundary in `platform.c` and calling the
  `W` entry points; paths stay UTF-8 `char *` everywhere else, since that is the only string
  representation the language has.

  **`tests/slow/`, a second corpus directory.** The jump-widening golden generates and compiles
  12,000-statement programs and costs about 5 seconds, so it lives apart from `tests/lang/` and CI
  runs `funny test tests/slow` as its own step. `tests/test_hardening.py` marked its equivalents
  `@pytest.mark.slow` for the same reason. Not optional: relaxation only happens at this size.

  The timing assertions themselves (*"50k statements compiles under 10 s"*) are **not** converted —
  a wall-clock threshold is a property of the machine, not of the program, and the scaling table
  above is a better record of the same concern than a test that goes red on a busy runner.

  293 goldens on Linux and Windows, `tests/native` 248 passed, full suite 1313 passed, bootstrap fixed
  point byte-identical at the same size on both, MSVC `/W4 /WX` clean.
