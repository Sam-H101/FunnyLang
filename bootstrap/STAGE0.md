# `bootstrap/funnyc.funnypak` — the checked-in compiler

## What it is

The FunnyLang compiler in `selfhost/` (lexer, parser, compiler, emitter — itself written in
FunnyLang), compiled to bytecode and linked into one PLAN.md §5.3 bundle.

It is checked in on purpose, and it is the only generated binary in the tree that is. Everything
else here is either source or reproducible from source by a C compiler alone.

## Why it has to exist

To compile a `.funny` file you need a compiler. The compiler is written in FunnyLang. Something has
to break that circle, and this file is it: with a C compiler you build `funny`, `funny` loads this
bundle, and from there FunnyLang can compile itself — no Python involved at any point.

Without it, a fresh clone could build the runtime but could not compile a single program with it.

## How to regenerate it

```sh
python3 -m funnylang build selfhost/funnyc.funny -o bootstrap/funnyc.funnypak
```

Regenerate whenever anything under `selfhost/` changes. Linking is deterministic — the same sources
produce byte-identical output — so a regeneration that changes nothing produces no diff.

> This step still uses the Python implementation, which is the last thing it is needed for. Once
> `selfhost/linker.funny` lands (N8 task 1) the bundle will be able to relink itself, and the
> instruction above becomes `funny build selfhost/funnyc.funny -o bootstrap/funnyc.funnypak`.

## Staleness policy

`tests/native/test_native_selfhost.py::test_checked_in_bootstrap_is_not_stale` relinks the bundle
from `selfhost/` and compares it byte for byte against this file. Editing `selfhost/` without
regenerating fails that test, so a stale bootstrap cannot reach `main` quietly.

That check is the whole policy. It is deliberately mechanical: a checked-in build artifact is only
safe while something automatically notices when it stops matching its sources.
