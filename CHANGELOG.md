# Changelog

All notable changes to FunnyLang are documented here.

## [Unreleased]

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

### M12 — Self-hosting (in progress)
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
