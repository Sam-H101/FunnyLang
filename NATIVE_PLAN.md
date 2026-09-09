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
  main.c            CLI entry, argument parsing
  value.h/.c        tagged-union Value, type predicates, equality, truthiness
  object.h/.c       Obj header, allocation, the all-objects list
  gc.c              mark-sweep, gray stack, temp roots, stress mode
  bignum.h/.c       arbitrary-precision integers + fixnum promotion
  numfmt.c          Ryū float formatting (shortest round-trip)
  string.c          UTF-8 yapstring, interning, codepoint indexing
  unicode_tbl.c     GENERATED — do not hand-edit (see tools/gen_unicode.py)
  toolchain_blob.c  GENERATED — the linked toolchain .funnypak as a byte array (N10)
  table.c           insertion-ordered hash table (groupchat + globals + interning)
  stash.c           dynamic array
  chunk.c           .funnyc / .funnypak loaders (PLAN.md §5.2/§5.3)
  vm.c              the interpreter loop, all 80 opcodes (0-79, PLAN.md §5.1)
  frames.c          call frames, closures, upvalues, try/catch/finally unwinding
  squad.c           classes, instances, methods, inheritance, the 4 magic methods
  diag.c            the PLAN.md §4.2 diagnostic renderer
  platform.c        the ONLY file with #ifdef _WIN32 — fs, time, tty, sockets, dlopen
  stub_main.c       the yeet runtime stub (PLAN.md §5.4)
  stdlib/
    mafs.c yapper.c stash_mod.c groupchat_mod.c rizz.c filez.c
    clock.c sus.c computer.c internet.c builtins.c
tools/
  gen_unicode.py    regenerates unicode_tbl.c   (dev-time only, needs Python)
  bin2c.py          regenerates toolchain_blob.c (dev-time only, needs Python)
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

1. **Differential golden testing (the backbone).** A new `tests/test_native_differential.py` runs
   every file in `tests/lang/` and `examples/` through both VMs and asserts identical stdout, stderr,
   and exit code. This is the acceptance gate for N2 through N6, with the passing subset growing each
   milestone.
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
6. **Interim binary release (do this first — it delivers value on day one).** The v1.0.0 PyInstaller
   toolchain already works; users just have no way to get it without a Python install. Add a
   `release` job to `.github/workflows/ci.yml` that runs on tags, `funny yeet`s the toolchain on each
   of the three OSes, and publishes the results to **GitHub Releases**. Cut `v1.0.1` from it. These
   binaries are ~8 MB and carry CPython inside — that is exactly what N1–N9 fixes — but they let
   someone start writing FunnyLang today, which is the point. N10 replaces this job wholesale.

**Acceptance:** `build.sh` and `build.bat` both produce a binary that prints the banner, on all three
platforms in CI, with `-Werror` clean. A `v1.0.1` GitHub Release exists carrying downloadable
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
2. `unicode_tbl.c` generated by `tools/gen_unicode.py`: `isalpha`, `isalnum`, upper/lower mappings.
   Checked in so the build never needs Python.
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
   with no sidecar. `tools/bin2c.py` (dev-time) plus a checked-in generated blob, following the same
   pattern as `unicode_tbl.c`, so the build still needs nothing but a C compiler.
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

## 7. Order of operations

```
PLAN.md M15 pointers (Python + selfhost, the differential oracle)
N0 scaffold → N1 values/GC/bignum → N2 loader+core loop → N3 functions/closures/errors
   → N4 strings/collections/classes → N5 stdlib+modules → N5b HTTPS/TLS
   → N6 diagnostics → N7 native CLI+yeet → N8 toolchain in FunnyLang → N9 bootstrap
   → N10 prebuilt binary distribution
```

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
- **N0 · complete (tasks 1-5 · task 6 pending confirmation).** `build.sh`/`build.bat`, `native/
  main.c` (a plain-ASCII toolchain smoke test — the real Unicode banner needs `platform.c`'s
  UTF-8 console setup, which doesn't exist yet), `native/ARCHITECTURE.md`, and a `native` CI job
  (Windows/Linux/macOS, release + debug/sanitizer builds) are all in place; YAML and shell syntax
  checked locally, full verification pending CI's actual compilers, since this environment has none
  installed. Task 6 (cutting a real `v1.0.1` tag and publishing GitHub Releases under the project's
  real name) was deliberately **not** done without the user's explicit go-ahead first — publishing a
  release is a public, hard-to-reverse action, unlike everything else in N0.
  **AGENT CHOICE · Windows C compiler for CI.** MSVC via `ilammy/msvc-dev-cmd@v1` (provisions
  `cl.exe`'s environment on `windows-latest`), not MinGW — matches N10's own release-matrix choice
  (`NATIVE_PLAN.md` §6.N10) and needs no extra toolchain install on the runner. `build.bat` targets
  `cl.exe` accordingly; `PLAN.md`'s "MSVC + MinGW" requirement (§2.4) means MinGW must also keep
  working, but CI only needs to prove *one* Windows path continuously — MinGW gets exercised by
  `zig cc` compatibility testing later, not by a second parallel CI job now.
- **N10 · ADDITION · prebuilt binary distribution.** Not in the original plan; added at the user's
  request so that using FunnyLang never requires building it. GitHub Releases on
  `github.com/Sam-H101/FunnyLang`, five platform artifacts per tag plus a rolling `nightly`, checksums,
  install scripts, and a `verify-published` job that re-downloads each artifact and runs it. An
  interim job in N0 publishes the *existing* PyInstaller binaries immediately, so downloadable builds
  exist long before the native runtime is finished.
- **N5 · AGENT CHOICE · PRNG.** `rizz` uses xoshiro256\*\* rather than reproducing CPython's Mersenne
  Twister stream. `rizz.seed(n)` is reproducible within the C VM; cross-VM stream equality was never
  specified and no golden file depends on it.
