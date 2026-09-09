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
