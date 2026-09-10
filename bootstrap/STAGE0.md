# `native/toolchain_blob.c` — the checked-in toolchain

## What it is

The whole FunnyLang toolchain — lexer, parser, resolver, compiler, `.funnyc` emitter, `.funnypak`
linker, disassembler, formatter, test runner, REPL and command line, all of it written in FunnyLang
and living in `selfhost/` — compiled to bytecode, linked into one PLAN.md §5.3 bundle, and turned
into a C byte array by `tools/bin2c.funny`.

It is the only generated artifact checked into this repository. Everything else here is either
source or reproducible from source by a C compiler alone.

## Why it has to exist

Two reasons, and they are different.

**The circle.** To compile a `.funny` file you need a compiler, and the compiler is written in
FunnyLang. Something has to break that circle. This file is it: with a C compiler you build
`funny`, and `funny` already contains the compiler, so from there FunnyLang compiles itself with no
Python involved at any point. Without it a fresh clone could build the runtime but could not
compile a single program with it.

**The single file.** `native/main.c` is a loader: it sets up the console, reads the two diagnostic
flags, and hands the embedded bundle argv. Every subcommand — `run`, `build`, `yeet`, `xray`,
`fmt`, `test`, `vibe`, `bootstrap` — is FunnyLang. Compiling the bundle *into* the binary is what
makes the shipped artifact one downloadable file with nothing beside it, which is NATIVE_PLAN.md
N10's whole point. It also means the toolchain can never get out of step with the executable that
runs it.

`funny --version` is answered by the C loader itself, so the one command someone runs when an
install looks broken works even if everything above it is broken.

## How to regenerate it

Two steps: link the bundle, then embed it. Both use `funny` itself.

```console
$ funny build selfhost/cli.funny -o bootstrap/cli.funnypak
$ funny tools/bin2c.funny -- bootstrap/cli.funnypak native/toolchain_blob.c toolchain_blob
$ ./build.sh
```

`bootstrap/cli.funnypak` is an intermediate and is deliberately *not* checked in — it is
regenerated on the way through and gitignored.

Regenerate whenever anything under `selfhost/` changes. Linking is deterministic — the same sources
produce byte-identical output — so a regeneration that changes nothing produces no diff.

> The `funny` doing the regenerating is the one you built from the *previous* blob. That is the
> normal way a self-hosted toolchain updates itself, and `funny bootstrap --verify` is what proves
> the result is a fixed point rather than drifting.

While the Python implementation still exists, the equivalent first step is
`python3 -m funnylang build selfhost/cli.funny -o bootstrap/cli.funnypak`. That is the only thing
it is still needed for here, and NATIVE_PLAN.md N11 removes it.

## Iterating without regenerating

Setting `FUNNY_CLI` to a `.funnypak` replaces the embedded toolchain for that run:

```console
$ funny build selfhost/cli.funny -o /tmp/cli.funnypak
$ FUNNY_CLI=/tmp/cli.funnypak funny run examples/hello.funny
```

That is the development loop — no `bin2c`, no C recompile. It is deliberately an explicit
environment variable and *not* a search of the filesystem: a shipped binary's behaviour should
never depend on what happens to be sitting next to it.

## Staleness policy

`tests/native/test_native_selfhost.py::test_checked_in_toolchain_blob_is_not_stale` relinks
`selfhost/`, parses the byte array back out of `native/toolchain_blob.c`, and compares them byte
for byte. Editing `selfhost/` without regenerating fails that test, so a stale toolchain cannot
reach `master` quietly.

That check is the whole policy. It is deliberately mechanical: a checked-in build artifact is only
safe while something automatically notices when it stops matching its sources.
