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

### M8 — The CLI
- `funny run/build/xray/fmt/test/vibe` all fully implemented, plus global flags (`--version
  --serious --no-color --time --vibes`) usable before or after the subcommand. `build` links a
  whole dependency tree into a self-contained `.funnypak` that resolves its own internal imports
  with no filesystem access at run time. `vibe` is a real REPL: persistent global scope, multi-line
  continuation, last-expression auto-print, `.help/.exit/.clear/.xray/.time`. `yeet`/`bootstrap`
  cleanly report "lands in M10/M12" until those milestones. `fmt` is an AST pretty-printer (drops
  comments, a documented limitation — see PLAN.md §16).
