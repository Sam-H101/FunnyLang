# `bootstrap/` — the two checked-in bundles

## What they are

- **`funnyc.funnypak`** — the FunnyLang compiler in `selfhost/` (lexer, parser, compiler, emitter,
  itself written in FunnyLang), compiled to bytecode and linked into one PLAN.md §5.3 bundle.
- **`cli.funnypak`** — the FunnyLang command line in `selfhost/cli.funny` (argument parsing and
  every subcommand), linked the same way.

They are checked in on purpose, and they are the only generated binaries in the tree that are.
Everything else here is either source or reproducible from source by a C compiler alone.

## Why they have to exist

**`funnyc.funnypak`**: to compile a `.funny` file you need a compiler. The compiler is written in
FunnyLang. Something has to break that circle, and this file is it: with a C compiler you build
`funny`, `funny` loads this bundle, and from there FunnyLang can compile itself — no Python involved
at any point. Without it, a fresh clone could build the runtime but could not compile a single
program with it.

**`cli.funnypak`**: since NATIVE_PLAN.md N8 task 6, `native/main.c` is a *loader* — it sets up the
console, reads the two diagnostic flags, and hands argv to this bundle. Every subcommand (`run`,
`build`, `yeet`, `xray`, `fmt`, `test`, `vibe`) is FunnyLang, which is the point of the milestone.
The consequence is that the binary does nothing without this file, so it has to ship with it.
`funny --version` is deliberately answered by the C loader itself, so the one command someone runs
when their install is broken works even when this bundle is missing.

N10 embeds both into the binary as a byte array (`native/toolchain_blob.c`), at which point they
stop being sidecars — but they stay checked in, because that generated array is built from them.

## How to regenerate them

```sh
python3 -m funnylang build selfhost/funnyc.funny -o bootstrap/funnyc.funnypak
python3 -m funnylang build selfhost/cli.funny    -o bootstrap/cli.funnypak
```

Regenerate whenever anything under `selfhost/` changes. Linking is deterministic — the same sources
produce byte-identical output — so a regeneration that changes nothing produces no diff.

> These steps still use the Python implementation, which is close to the last thing it is needed
> for. `selfhost/linker.funny` (N8 task 1) already relinks both correctly, so once the checked-in
> bundles are known-good the instruction becomes
> `funny build selfhost/cli.funny -o bootstrap/cli.funnypak` and Python drops out entirely.

## Staleness policy

`tests/native/test_native_selfhost.py::test_checked_in_bootstrap_is_not_stale` and
`::test_checked_in_cli_is_not_stale` relink each bundle from `selfhost/` and compare it byte for
byte against the checked-in file. Editing `selfhost/` without regenerating fails those tests, so a
stale bootstrap cannot reach `main` quietly.

That check is the whole policy. It is deliberately mechanical: a checked-in build artifact is only
safe while something automatically notices when it stops matching its sources.
