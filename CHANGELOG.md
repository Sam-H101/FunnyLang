# Changelog

All notable changes to FunnyLang are documented here.

## [Unreleased] — the runtime under threads

`gimme interns` gave FunnyLang real OS threads; this is the pass over `native/` that makes every
stdlib module safe on them, and the corpus that proves it. Plan and build log:
[RUNTIME_PLAN.md](RUNTIME_PLAN.md).

### Added
- **`tests/lang/threads/`** — one golden per stdlib module. Each runs the same work on this thread
  and then on eight interns at once, and passes only if all eight answers match the single-threaded
  one. CI also runs the whole directory under ThreadSanitizer, along with `extensive_examples`.

### Fixed
- **`rizz` kept one generator for the whole process**, unlocked. Two interns rolling dice at the
  same moment was a data race, and a seeded stream came out different depending on what the other
  threads happened to be doing. The state now lives in the `VM`, so `rizz.seed(n)` means the same
  stream wherever it runs. `stash.shuffle_it`, which draws from the same generator, is fixed with
  it.
- **First use of randomness now seeds from the operating system** rather than the wall clock. Two
  interns hired in the same second no longer roll the same dice.
- **Winsock start-up and the Windows performance-counter frequency** are done under `INIT_ONCE`. A
  plain `static bool` let one thread see the flag set before the thing it guarded was.
- **Error text is built with `strerror_r`/`strerror_s`**. Plain `strerror` may hand back a buffer
  shared by every thread, so two threads failing at once could read each other's message.

## [Unreleased] — HTTPS

A FunnyLang program can now be an HTTPS server, and `extensive_examples/web_server_https/` is one: a
small site with sign-in, settings and an activity log, served over TLS 1.2+ by 24 acceptor threads,
with everything above the TLS record layer written in FunnyLang. Design and deviations:
[extensive_examples/web_server_https/PLAN.md](extensive_examples/web_server_https/PLAN.md).

### Added
- **`internet.open_secure_shop(port, host, {"pfx", "password"})`** — a listener with a TLS identity
  from a PKCS#12 file. TLS 1.2 minimum, forward-secret AEAD suites only, no renegotiation or
  compression. OpenSSL (`dlopen`'d) on Linux/BSD, Schannel on Windows, Secure Transport on macOS.
- **`internet.handshake(conn)`** — the server-side handshake one step at a time, so a slow client
  costs its own task and nobody else's; **`internet.tls_info(conn)`** — the negotiated version and
  cipher.
- **`internet.slide_into(host, port, {"tls", "server_name", "ca", "timeout_ms"})`** — a verified TLS
  client, optionally trusting exactly one CA. A bare timeout as the third argument still works.
- TLS connections are ordinary handles: `hear_them_out`, `holler_back`, `kick_out` and `hold_up`
  work on them unchanged, and `hold_up` knows about plaintext the TLS library is already holding.
- **`rizz.entropy(n)`** — bytes from the operating system's CSPRNG, as hex.
- **`the_script()`** — the running program's own absolute path (inside an intern, its hire path), so
  a program can find the files beside it from any working directory. The command line and
  `funny test` pass it down, which regenerated `native/toolchain_blob.c`.

### Changed
- **Listeners are non-blocking**, so several interns can accept from one: a thread that loses the
  race gets `ghost` from `next_customer(listener, 0)` instead of sitting in `accept()` with its tasks
  frozen until the next connection. `tests/lang/interns/shared_listener` is the golden.
- **SIGPIPE is ignored** on POSIX. A write to a connection the peer has dropped now fails with a
  `SkillIssue` instead of killing the process.
- OpenSSL is loaded under `pthread_once`: two threads reaching TLS for the first time at the same
  moment could previously both run the lazy `dlopen`.

### Fixed
- **Interns importing modules at the same moment could fail** with "'x.funny' isn't in this bundle"
  for a module that was in the bundle. The loader normalized import paths with `strtok`, whose
  cursor is shared by every thread in the process, so concurrent imports walked through each
  other's paths. Rare with a few interns, reliable enough to find with two dozen starting at once.
  `tests/lang/interns/concurrent_imports` is the golden.

## [Unreleased] — concurrency

`async_ngl` and `await_fr` are real, and `gimme interns` runs work on real OS threads. Two of
`PLAN.md` §3.3's six reserved keywords now mean what they are named. Design, cost sheet and build
log: [ASYNC_PLAN.md](ASYNC_PLAN.md).

```funny
gimme interns
gimme clock

async_ngl bet fetch_both() {
    // Two interns, two OS threads, one wait.
    yo a = interns.hire("slow_job.funny", 21)
    yo b = interns.hire("slow_job.funny", 21)
    bounce await_fr a + await_fr b
}

yap await_fr fetch_both()      // 42
```

### Added
- **`interns`** — an eleventh stdlib module. `interns.hire(path, arg)` runs a `.funny` program on a
  real OS thread in its own VM with its own heap and hands back an `otw`; `wait_up`, `everybody`
  and `headcount` collect. Inside a worker, `interns.assignment()` and `interns.deliver(v)` are the
  protocol — a worker is a *program*, not a function, because a top-level script has no `bounce`.
- **`async_ngl bet` and `await_fr`**, with a real event loop. Calling an `async_ngl bet` makes a
  task and returns an `otw` without running the body; `await_fr` suspends the task until it
  settles. `async_ngl lowkey (x) => ...` works too. `funny run` drains outstanding work before
  exiting.
- **`otw`** — a new runtime type, `what_is_it(p) == "otw"`. Pending, fulfilled or rejected, and
  final once it leaves pending. Prints as `<otw pending>` / `<otw done 42>` /
  `<otw rejected KeyGhosted>`.
- **`clock.chill(ms)`** — an `otw` that settles after a delay. Its pair `clock.touch_grass(s)`
  blocks the whole program; `chill` yields, so only the awaiting task waits.
- **`LeftOnRead` and `CantWaitRightNow`** join `PLAN.md` §4.1's error taxonomy, and are the only
  two additions. The taxonomy is closed — `oops(flavor, ...)` validates against it — so this is a
  spec change, made deliberately.
- **`AWAIT` (opcode 80)**. A proto's `is_variadic` byte became a flags byte (bit 0 variadic, bit 1
  async). Not a bytecode version bump: every value any earlier toolchain wrote is 0 or 1 and reads
  back identically, which is what keeps the self-hosting bootstrap from needing a special dance.

### Also added
- **Listening sockets.** Everything in `internet` dialled *out*; these are the other direction.
  `internet.open_shop(port, host?)` binds and listens, `next_customer(listener, timeout?)` accepts
  (or is `ghost` if nobody arrived), `hear_them_out` / `holler_back` read and write,
  `kick_out` / `close_shop` close, `shop_port` tells you which port `open_shop(0)` picked, and
  `slide_into(host, port)` dials the other end. Listeners and connections are `numba` handles, so
  they stay out of the collector and can cross to an `interns` worker.
- **`internet.hold_up(handle, timeout_ms?)`** — an `otw` that settles when a socket has something to
  read. This is what lets one thread serve several callers at once: `await_fr` it and only the
  asking task waits, while the event loop polls every socket anybody is parked on in a single call.
  The loop learned to wait on sockets, timers and worker threads together.
- **`yapper.byte_len(s)`** — length in bytes, where `how_thicc` is length in codepoints. An HTTP
  `Content-Length` taken from the latter is short for any response with a non-ASCII character in it.
- **`extensive_examples/web_server/`** — a real HTTP/1.1 server in FunnyLang: a socket loop, a
  request parser, routes, and a golden that drives the whole thing over a loopback socket with an
  `interns` worker as the client, so `funny test extensive_examples` keeps it honest.

### Notes on the design
- **Nothing is shared between threads, so nothing needs a lock.** Each intern gets its own VM and
  its own collector; arguments are deep-copied in and results deep-copied out. `ghost`, `boolski`,
  `numba`, `yapstring` and `stash`/`groupchat` of those can cross. A `bet`, a squad instance, a
  `pointa` or an `otw` cannot — each references a heap, and there is no second copy of that heap on
  the other side. Every refusal names the type. This is a real ergonomic cost and it is the price
  of not having a thread-safe collector.
- **A task cannot suspend inside a native callback**, because that C frame cannot be saved. `await_fr`
  under `glow_up`, `squish`, `sort`, `combo` or a squad's `to_yap` raises `CantWaitRightNow`
  *naming the callback* rather than silently doing the wrong thing.
- **An error raised inside a worker, or inside an async task, keeps its own flavor.** A worker's
  `MathAintMathin` is re-raised in the parent as a `MathAintMathin`, at the line that waited.
- **Nothing is ever killed.** `funny` joins its workers at exit: stopping a thread mid-allocation
  leaves a heap nothing can safely free.
- **The scheduler is deterministic** — tasks run in creation order and never preempt — so a golden
  can assert the language's own ordering without asserting on timing.

### Tested
- 392 goldens, including `tests/lang/async/` and `tests/lang/interns/`, green on Linux and Windows;
  all of them also under `FUNNY_GC_STRESS=50` and under ASan+UBSan.
- `tests/lang/async` and `tests/lang/interns` additionally under **ThreadSanitizer**.
- `funny bootstrap --verify` still reaches a byte-identical fixed point.

## [2.0.0] — 2026-09-10

The runtime is no longer Python. `native/` is a complete bytecode VM written in C — values,
garbage collector, arbitrary-precision integers, diagnostics, the whole standard library — and the
entire toolchain above it is FunnyLang. Building FunnyLang needs a C compiler and nothing else.

### The native runtime (`native/`)
- A from-scratch bytecode VM in C11: tagged values, a mark-and-sweep GC (`FUNNY_GC_STRESS=1`
  collects on every allocation), sign-magnitude arbitrary-precision integers, closures with real
  upvalue capture, single-inheritance squads, try/catch/finally, pointers, and every one of the ten
  stdlib modules. Float formatting matches Python's `repr` exactly, digit for digit.
- **`platform.c` is the only file allowed `#ifdef _WIN32`.** Filesystem, timing, TTY, console,
  sockets, TLS, `dlopen`, the executable's own path and temp files all route through
  `platform.h` — so a port to a new OS is one file, not a hunt through twenty.
- HTTPS uses the TLS each OS already ships: WinHTTP on Windows, `Security.framework` on macOS,
  OpenSSL `dlopen`'d at run time on Linux/BSD, so the binary still builds and runs where OpenSSL
  is absent. Certificate verification is mandatory and has no opt-out.
- `PLAN.md` §4.2's diagnostics — source snippet, caret, roast, hint, stack trace — reimplemented in
  C, byte-identical to the Python renderer.
- Two binaries: `funny` (the command line) and `funnyrt` (a VM with no compiler in it). `funny
  yeet` copies the stub, appends your compiled program and a 17-byte trailer, and marks it
  executable — a couple of hundred KB, where the PyInstaller path produced 8 MB.

### The toolchain is FunnyLang (`selfhost/`)
- `bundler.funny`/`linker.funny` (the `.funnypak` linker), `loader.funny` (its inverse),
  `disasm.funny` + `astdump.funny` + `xray.funny` (`funny xray`), `fmt.funny` + `fmtcli.funny`
  (`funny fmt`), `test.funny` (`funny test`), `vibe.funny` (the REPL), `bootstrap.funny`
  (`funny bootstrap --verify`), and `cli.funny` — argument parsing and every subcommand.
- `native/main.c` is a **loader**: it sets up the console, reads `--serious`/`--no-color` (they
  configure the runtime's own renderer, so they cannot live in the FunnyLang half), finds the CLI
  bundle and hands it argv. 118 lines, about a third of them explaining why.
- Every subcommand's output is byte-identical to the previous Python CLI's, checked by diffing
  both over the whole corpus.

### Language and standard library
- `oops(flavor, message?, line?, col?)` builds an `error` value with a chosen flavor, so `chuck`
  can raise something other than `SkillIssue`. The flavor set stays closed to `PLAN.md` §4.1's
  taxonomy.
- `yell(...)` — `yap` to stderr. The only way to write there.
- `mafs.bits_to_float(bits)` — the inverse of `float_to_bits`.
- `filez.is_dir` / `is_file` / `append_bytes` / `make_executable` / `temp_file`.
- `computer.readline(prompt?)` (returns `ghost` at end of input, unlike `ask()`),
  `computer.env(name, default?)`, `computer.exe_path()`.
- `sus.run_bytecode` (a fresh isolated VM, stdout captured), `sus.run_program` (the way the top
  level runs one), and `sus.new_session`/`run_in`/`close_session` (a VM kept alive between runs —
  a REPL).
- `filez.slurp` now does universal-newline translation on every platform, matching the reference;
  `filez.yeet_out`/`append_to` write exactly the bytes given on every platform, matching nothing
  but common sense.

### Fixed
- **NUL bytes were being truncated out of strings.** A FunnyLang string may contain them —
  `selfhost/lexer.funny`'s own escape table holds one — but every display path measured the result
  with `strlen()`, silently cutting the value short. Affected `yap`, template interpolation,
  `to_yap`, `stash.join`, `yapper.join`, `yapper.format` and `ask`'s prompt.
- **`filez.join_path` and `dir_of` were POSIX-shaped on Windows**, and `join_path` concatenated
  where `pathlib` normalizes.
- **Empty `stash`/`groupchat` were truthy**, where Python treats `[]`/`{}` as falsy.
- **`what_is_it(e)` on a caught error** raised an uncatchable Python `TypeError`; it answers
  `"error"` now, as the C VM always did.
- **`funny test` was killed by a test that called `dip()`** — `SystemExit` propagated out of the
  in-process runner and took the whole run with it.
- **The self-hosted parser lost its error flavor** on a multi-error parse failure, and its
  resolver could not raise `WhoDis`/`ImmutableVibes` at all.
- **A circular import segfaulted.** A `.funnypak` module was added to the loader's cache only once
  it had *finished*, so a module reached again while still running re-entered the runner forever and
  took the C stack with it. `gimme "main.funny"` from `main.funny` was enough.
- **`groupchat` was an association list.** Lookup was a linear scan, and setting a key calls lookup,
  so building FunnyLang's dictionary type was O(n²) — 32,000 inserts took 2.9 seconds. It is a hash
  index over string keys now: the same insert takes 17 ms, and a 40,000-statement program compiles
  in 3.0 seconds instead of 35.8.
- **The compiler could not compile a function body over 64 KB of bytecode.** There was no jump
  relaxation pass, so an over-long jump raised *"your function is too long. seek help."* rather than
  widening to `JUMP_LONG`/`LOOP_LONG`.
- **`yapper.is_letter` and case conversion were ASCII-only**, so `yapper.is_letter("中")` answered
  `cap`, `yapper.SCREAM("héllo")` did nothing, and `yo 変数 = 1` would not lex. `native/unicode_tbl.c`
  (generated by `tools/gen_unicode.funny`) fixes all three; full case mapping — `ß` → `SS` — is
  still deliberately absent.
- **On Windows:** bundles keyed every module by its *absolute path*, so they were not reproducible
  across platforms and a `yeet`ed executable leaked the builder's directory layout into every
  diagnostic; a file whose name is not representable in the active ANSI codepage could not be opened
  or listed; and `filez.mkdir` could not create an absolute path on a drive other than the current
  one.
- **A parse that found several errors reported one.** PLAN.md §4.2 asks for up to five and a summary
  line.
- **Eleven error roasts were missing**, so an unterminated string reported *"what even IS that
  character"* — the flavor's generic default — rather than its own wording.
- **A run nested inside another leaked both streams**: a program run from inside a captured child VM
  sent its output and its diagnostics to the real stdout and stderr.

### Removed
- **`funnylang/` — the Python implementation — and with it `pyproject.toml`, the 1,313-test `pytest`
  suite, and every `.py` file in the repository.** `find . -name '*.py'` returns nothing, and CI
  proves it in a container with a C compiler and no interpreter of any kind.
- What replaced the suite is the golden corpus: 376 `.funny`/`.expected` pairs run by `funny test`,
  which is itself FunnyLang. It grew from 76 pairs *while the Python implementation was still there
  to check each one against* — every plain golden also passes on the Python VM, and every token,
  AST and bytecode dump was diffed byte for byte against `funnylang.lexer`, `dump_ast` and
  `disasm.disassemble`. Nothing in it was captured from the C runtime and blessed.

### Compatibility
- Bytecode format unchanged: `BYTECODE_VERSION` is still 2, and `.funnyc`/`.funnypak` files from
  1.1.0 run unmodified.
- `funny <file>` with no subcommand is a new shorthand for `funny run <file>`.
- `funny` is self-contained: the whole toolchain is compiled into it as a byte array, so there is
  nothing to ship beside the binary. `FUNNY_CLI` points at a `.funnypak` to use instead, which is
  the development loop. `funny yeet` still needs `funnyrt` on disk, since it copies it.
- **There is no Python package any more.** `funnylang/`, `pyproject.toml` and the `pytest` suite are
  gone, so `pip install` has nothing to install. Get the binary from a release, `install.sh` /
  `install.ps1`, or `./build.sh`.

## [1.1.0] — 2026-09-09

### M15 — Pointers (`pointa`)
- `&x` (local), `&g` (global), a captured upvalue, `&arr[i]`/`&m["k"]` (a stash element or
  groupchat key), and `&obj.field` (a squad instance field) all produce a `pointa` — a safe
  reference to a *place*, never a raw address, since a GC'd VM can't hand out real pointers
  without breaking every other safety guarantee in the language. `*p` reads, `*p = v` writes
  (compound assignment included, evaluating `p` exactly once), and taking `&x` boxes the local
  using the identical `Upvalue` mechanism closures already use for captures — so a closure
  capturing `x` and an `&x` taken in the same scope alias each other automatically, for free.
- Pointer arithmetic (`p + n`, `p - n`, `p - q`, `< <= > >=`) works only on a pointer into a
  `stash`, since that's the only kind with a genuine ordinal. Bounds and liveness are checked on
  every *read*, never on construction, so `&arr[99]` is legal to form and only errors if
  dereferenced — no pointer operation anywhere in FunnyLang can crash the VM. Every failure mode
  reuses an existing §4.1 error flavor (`OutOfPocket`, `KeyGhosted`, `GhostError`,
  `TypeVibeMismatch`); no new error class was needed. `&` of a `deadass` constant is a resolve-time
  `ImmutableVibes`, matching direct reassignment.
- New opcodes 73–79 (`PTR_LOCAL`, `PTR_GLOBAL`, `PTR_UPVAL`, `PTR_INDEX`, `PTR_PROP`, `DEREF`,
  `SET_DEREF`); `BYTECODE_VERSION` → 2. `selfhost/parser.funny` and `selfhost/compiler.funny` both
  learn to *compile* pointer syntax while `selfhost/` itself keeps not using any (§8) — verified
  byte-for-byte identical against the Python compiler's output for the whole new `tests/lang/ptr_*`
  corpus, and `funny bootstrap --verify` still reaches its fixed point. See `PLAN.md` §16 for the
  full design log, including why this reverses M13's earlier removal of a dead `pointa` spec row.

## [1.0.0] — 2026-09-09

### M0 — Scaffolding
- Repository initialized. Package skeleton, packaging metadata, and test harness in place.

### M1 — Lexer
- Full tokenizer: every literal form, comments, templates, NEWLINE collapsing.

### M2 — AST + Parser
- Recursive-descent + precedence-climbing parser for the whole grammar (except `squad`, which
  lands in M9). Multi-error recovery.

### M3 — Resolver
- Local/upvalue/global scope resolution with the Lua/Crafting-Interpreters closure model.

### M4 — Bytecode compiler, serializer, disassembler
- AST -> bytecode, `.funnyc` read/write, human-readable disassembly.

### M5 — The VM
- The stack VM: closures, upvalues, control flow, try/catch/finally, iteration, runtime errors.
  `funny run <file.funny>` works end to end.

### M6 — Stdlib + the error system
- The full §4.2 diagnostic renderer (funny/serious modes, source snippet with caret, hints,
  stack traces) wired into `funny run`. All 10 stdlib modules (`mafs yapper stash groupchat rizz
  filez clock sus computer internet`) plus the always-in-scope builtins. Stash/groupchat/yapstring/
  numba instance methods share their implementation with the matching free-function module.

### M7 — Modules
- `gimme` works for both user files (with §3.8's full resolution order, caching, and cycle
  detection) and stdlib modules. Each module gets its own isolated global namespace — a real
  architectural change (constant pools and globals moved from the VM onto each Closure) documented
  in PLAN.md §16.

### M10 — Native executable packaging
- `funny yeet` produces a genuinely standalone `.exe`/binary (~8 MB): a PyInstaller-frozen runtime
  stub, built once and cached by a hash of the whole `funnylang` package, with a linked `.funnypak`
  appended plus a 17-byte trailer. Verified end to end: hello world, a multi-module bundle, an
  uncaught-error exit code, a naked stub, and running after being moved to a different directory.

### M12 — Self-hosting
- `selfhost/prelude.funny`: assert helpers and `.funnyc`-format byte-buffer writers (u8/u16/u32/u64
  big-endian, a hand-rolled UTF-8 encoder, and the arbitrary-precision int-magnitude encoding for
  §5.2's tag-2 constants), cross-checked byte-for-byte against Python's own `struct`-based encoding.
- `selfhost/lexer.funny`: a full port of `lexer.py`/`tokens.py` — numbers (decimal/hex/binary/octal,
  underscores, floats, exponents), strings (escapes, `\u{...}`, triple-quotes), templates, unicode
  identifiers, comments, and NEWLINE collapsing. Cross-checked against the Python lexer's token
  kind/line/col across the entire `tests/lang/` + `examples/` corpus, plus targeted value and
  template-structure checks. Needed a small, deliberate stdlib addition — `yapper.is_letter`/
  `yapper.is_alnum` — since nothing in the self-hosting subset previously exposed Unicode character
  classification; see PLAN.md §16.
- `selfhost/parser.funny`: a full port of `parser.py` (recursive-descent statements +
  precedence-climbing expressions, the whole §3 grammar including `squad`). AST nodes are
  groupchat records with a `"node"` key. Cross-checked against the Python parser's `dump_ast()`
  S-expression output across the entire `tests/lang/` + `examples/` corpus (72 files) — exact
  match on every one, including every squad/template/closure/module test.
- `selfhost/compiler.funny`: resolver + codegen merged into one file (per the plan's own
  suggestion) — a full port of resolver.py + compiler.py + chunk.py's const-pool/bytecode-buffer
  bookkeeping. AST nodes being mutable groupchats let the resolver write results directly onto the
  tree instead of needing Python's identity-keyed side tables. Cross-checked against
  `funnylang.compiler`'s `CompiledUnit` directly (not through the disassembler) across the entire
  `tests/lang/` + `examples/` corpus — byte-identical bytecode, proto metadata, line tables, and
  constant pools on every file, plus matching resolve-time rejections for the two golden files that
  are supposed to fail (undefined variable, const reassignment). Needed one more small stdlib
  addition, `mafs.is_float`, and along the way found and fixed a real pre-existing bug: `<<`/`>>`
  with a negative shift amount raised a raw Python `ValueError` instead of a `FunnyError`, both at
  runtime and when constant-folded — see PLAN.md §16.
- `selfhost/emitter.funny`: writes the compiled-unit structure out as real `.funnyc` bytes per
  §5.2, via `prelude.funny`'s byte-buffer writers. Needed one more stdlib addition,
  `mafs.float_to_bits`, for bit-exact IEEE-754 float encoding. Verified two ways across the whole
  `tests/lang/` + `examples/` corpus: the emitted bytes are byte-for-byte identical to
  `funnylang.serializer.dump_funnyc()`'s output, and — separately — every emitted `.funnyc` file
  actually loads and runs correctly through the real Python VM, producing identical stdout to
  running the original source directly.
- `selfhost/funnyc.funny`: the sixth and last file — the CLI. `funny bootstrap --verify` (a new
  Python CLI subcommand) is real: stage1 (Python) bundles `selfhost/funnyc.funny` into a
  self-contained stage2 `.funnypak`; running stage2 (compiling `funnyc.funny` using the self-hosted
  compiler's logic for the first time) produces stage3; running stage3 produces stage4. **stage3
  and stage4 are byte-identical — the self-hosting fixed point is reached.** `--keep` writes all
  three stage artifacts to `build/bootstrap/`; `--diff` disassembles the first divergent proto on a
  mismatch. Cross-validated per §M12 task 5: stage3 compiles the entire `tests/lang/` corpus (not
  just `funnyc.funny`) with stdout identical to stage1 on every file, including every squad,
  closure, template, and try/catch/finally test — the real proof self-hosting works.

  Self-hosting is complete: `funnylang/` (the Python implementation) is still what ships and what
  the VM runs — the VM itself never becomes self-hosted, by design — but the compiler now has a
  second, independent implementation, written in FunnyLang, that reproduces itself byte for byte
  and agrees with the Python one on every test in the repo.

### M11 — Hardening pass
- No Python traceback ever escapes the CLI (a clean "COMPILER SKILL ISSUE" message + exit 70
  instead), recursion is bounded (`sys.setrecursionlimit(20_000)`, 10,000-frame VM cap), a 2000-
  random-token-soup + 500-random-AST fuzz suite (`tests/test_fuzz.py`) never lets anything but a
  `FunnyError` escape, self-referential `stash`/`groupchat` values print `[...]`/`{...}` instead of
  recursing forever, and unicode identifiers/strings/file paths work end to end. Added `JUMP_LONG`/
  `LOOP_LONG` (opcodes 71/72) and a `Chunk.finish()`-time relaxation pass so functions whose
  bytecode exceeds a 16-bit jump offset (the M11 acceptance test: a single 50,000-statement `sus`
  body) compile correctly instead of raising `OverflowError` — documented in detail in PLAN.md §16.

### M9 — Classes (`squad`)
- Full `squad`/`inherits`/`spawn`/`me`/`og` support with the 4 magic methods (`to_yap`, `how_thicc`,
  `get_it`/`set_it`, `same_energy`). Fixed a real infinite-recursion bug in 3+ level `og` super-calls
  and a missing-inherited-`spawn` construction bug — both documented in PLAN.md §16.

### M8 — The CLI
- `funny run/build/xray/fmt/test/vibe` all fully implemented, plus global flags (`--version
  --serious --no-color --time --vibes`) usable before or after the subcommand. `build` links a
  whole dependency tree into a self-contained `.funnypak` that resolves its own internal imports
  with no filesystem access at run time. `vibe` is a real REPL: persistent global scope, multi-line
  continuation, last-expression auto-print, `.help/.exit/.clear/.xray/.time`. `yeet`/`bootstrap`
  cleanly report "lands in M10/M12" until those milestones. `fmt` is an AST pretty-printer (drops
  comments, a documented limitation — see PLAN.md §16).

### M13 — Polish, docs, examples, release
- `docs/LANGUAGE.md`, `docs/BYTECODE.md` (with a real, verified worked disassembly and file-format
  walkthrough), and `docs/STDLIB.md` (its function signatures generated directly from the
  `NativeFn` registry, so they can't drift from the real implementation).
- README rewritten: the real banner, an honest "what this is / what this isn't" section (the
  compiler self-hosts, the VM never was meant to), and the self-hosting bootstrap shown as the
  headline feature it is.
- `examples/chaos.funny` exercises `computer.explode()`, `internet.go_brrrr()` (wrapped in a real
  `sketchy`/`my_bad`/`regardless`, so it's deterministic under CI's `FUNNY_NO_NET=1` regardless of
  whether the network call itself succeeds or fails), `rizz.gamble()`, and `yapper.sarcasm_case()`.
  Every `examples/*.funny` now has a matching `.expected` and passes `funny test examples/`.
- `.github/workflows/ci.yml` replaces the earlier `tests.yml`: the full matrix (Windows, Linux,
  macOS × Python 3.10/3.11/3.12) runs `pytest -q`, then `funny bootstrap --verify`, then freezes
  and runs a real `funny yeet` executable and uploads it as an artifact — one consolidated workflow
  instead of two overlapping ones.
- Version bumped to `1.0.0`.
- Found and fixed two more real bugs while writing this milestone's docs and CI, both logged in
  PLAN.md §16: §3.2's frozen literals table listed a "Pointer" type (`pointa`, `*`/`&` syntax) that
  was never implemented anywhere in the project — a dead spec row, removed rather than built, since
  no milestone ever actually planned pointers. And `funny yeet file.funny -o some/new/dir/name`
  crashed with a raw, uncaught `FileNotFoundError` whenever `-o`'s parent directory didn't already
  exist (every existing packager test happened to pre-create it by hand, which is exactly why this
  was never caught) — fixed by creating the parent directory, like any ordinary CLI tool would.
