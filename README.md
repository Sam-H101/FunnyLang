# FunnyLang

```
   ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗
   ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝
   █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝
   ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝
   ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║
   ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝
        FunnyLang v1.0.0 — it compiles. somehow.
```

A bytecode-compiled programming language that refuses to take itself seriously — and, as of
v1.0.0, compiles itself.

```funny
bet fizzbuzz(n) {
    grind i from 1 to n + 1 {
        sus (i % 15 == 0) {
            yap "FizzBuzz"
        } kinda_sus (i % 3 == 0) {
            yap "Fizz"
        } kinda_sus (i % 5 == 0) {
            yap "Buzz"
        } nah {
            yap i
        }
    }
}
fizzbuzz(20)
```

```console
$ funny run examples/fizzbuzz.funny
1
2
Fizz
4
Buzz
...
```

## Install

Requires Python 3.10 or newer, and nothing else — FunnyLang's runtime has zero third-party
dependencies.

```console
$ pip install -e .
```

That puts a `funny` command on your PATH. You can also skip the install and run the package
directly from a checkout with `python -m funnylang`, which is what the test suite does.

## What this is

- A complete, hand-written toolchain: lexer → parser → resolver → bytecode compiler → stack VM,
  all built from scratch, no parser-generator, no bytecode-generator library.
- A real, if comedic, general-purpose language: closures with proper upvalue capture, single
  inheritance classes, exceptions with try/catch/finally, a module system, arbitrary-precision
  integers, string interpolation, slicing — see [docs/LANGUAGE.md](docs/LANGUAGE.md) for the full
  reference.
- Genuinely self-hosting: the *compiler* — lexer, parser, resolver, codegen, and `.funnyc` emitter
  — has a second, independent implementation, written in FunnyLang itself, living in `selfhost/`.
  `funny bootstrap --verify` compiles it with the Python compiler, then uses that output to compile
  itself twice more, and checks that the third and fourth generations are byte-for-byte identical:

  ```console
  $ funny bootstrap --verify
  🥁 stage 2... compiled.
  🥁 stage 3... compiled by stage 2.
  🥁 stage 4... compiled by stage 3.

  stage3 and stage4 are byte-identical (1,005 bytes).
  FunnyLang now compiles FunnyLang. we are so back. 🏆
  ```

  It's cross-validated against the whole test suite too: the self-hosted compiler (running as
  bytecode the Python compiler produced) compiles every program in `tests/lang/`, and its output
  runs identically to what the Python compiler itself would have run.
- Shippable: `funny yeet` freezes a program into a standalone native executable — no Python
  installation required on the machine that runs it.

## What this isn't

- **The virtual machine is not self-hosted, and was never meant to be.** Something has to actually
  execute bytecode, and that's Python, frozen into every `funny yeet` binary via PyInstaller. Only
  the compiler — the part that turns `.funny` source into bytecode — has a FunnyLang
  implementation. Claiming otherwise would be a real, not a funny, lie.
- Not statically typed, not performance-tuned beyond "reasonable for a tree-walking-adjacent
  bytecode VM," and not aiming to replace a real production language. It's a from-scratch language
  implementation built as a complete, working system — a teaching-and-tinkering project, not a
  pitch for your next backend.
- No package manager, no LSP, no debugger beyond what `funny xray` and the diagnostic renderer
  give you. See `PLAN.md`'s stretch-goal list for what might come next.

## A taste

Closures (`examples/closures.funny`):

```funny
bet counter() {
    yo n = 0
    bounce lowkey () => { n += 1; bounce n }
}
yo c = counter()
yap c(), c(), c()   // 1 2 3
```

Classes (`examples/squads.funny`):

```funny
squad Animal {
    spawn(name) { me.name = name }
    bet speak() { yap me.name, "makes a noise" }
}
squad Dog inherits Animal {
    bet speak() {
        og.speak()
        yap me.name, "says bark. skrrrt."
    }
}
Dog("rex", "corgi").speak()
```

Exceptions (`examples/errors.funny`):

```funny
sketchy {
    yap divide(10, 0)
} my_bad (e) {
    yap "caught:", e.flavor
} regardless {
    yap "cleanup always runs"
}
```

Rough decoder ring: `yo` declares (mutable), `deadass` declares a constant, `yap`/`yeet` print,
`bet` defines a function, `bounce` returns, `lowkey` is a lambda, `sus`/`kinda_sus`/`nah` are
if/elif/else, `bruh` is while, `grind` loops (range or for-each), `bail`/`nvm` are break/continue,
`sketchy`/`my_bad`/`regardless` are try/catch/finally, `chuck` throws, `gimme`/`flex` are
import/export, and `squad`/`inherits`/`me`/`og`/`spawn` are class/extends/this/super/constructor.
Full keyword table: [docs/LANGUAGE.md](docs/LANGUAGE.md#keywords).

## The CLI

```
usage: funny [-h] [--serious] [--no-color] [--time] [--vibes] [--version]
             {run,build,yeet,vibe,xray,fmt,test,bootstrap} ...

    run         run a .funny/.funnyc/.funnypak file
    build       compile (+ link) to .funnyc/.funnypak
    yeet        compile to a standalone native executable
    vibe        the REPL
    xray        disassemble/inspect a file
    fmt         canonical formatter
    test        run *.funny/*.expected pairs in a directory
    bootstrap   self-hosting fixed-point verification
```

`build` links a whole dependency tree into a self-contained `.funnypak` that resolves its own
imports at run time with no filesystem access, whenever the entry file imports anything local; a
dependency-free entry compiles to a plain `.funnyc` instead. `vibe` is a real REPL with a
persistent global scope, multi-line continuation, and last-expression auto-print. `bootstrap
--verify` runs the self-hosting fixed-point check described above (`--keep` writes the
intermediate stage artifacts to `build/bootstrap/`; `--diff` disassembles the first divergent
instruction on a mismatch).

Global flags work before or after the subcommand. `--serious` swaps the roasts for plain
professional error text, `--no-color` drops ANSI codes, `--time` reports how long compiling and
running took, and `--vibes` narrates each phase.

## Errors

Diagnostics are a feature, not an afterthought — every error renders the offending source line
with a caret, a plain-English explanation, a fix hint, and a stack trace:

```
💀💀💀 FUNNYLANG MOMENT 💀💀💀

  TypeVibeMismatch  ──  examples/errors.funny:16:9

     14 │ bet main() {
     15 │     yo name = "sam"
     16 │     yap name + 5
        │         ^ this right here
     17 │ }

  a numba and a yapstring do NOT have the same energy.

  💡 skill issue fix:  wrap the numba in to_yap(...) first, so both sides are yapstrings

  🥞 stack of shame:
       at main()  examples/errors.funny:16
       at <the big one>  examples/errors.funny:18
```

Pass `--serious` if you are, in fact, being serious.

## Standard library

Ten modules, imported with `gimme`: `mafs`, `yapper`, `stash`, `groupchat`, `rizz`, `filez`,
`clock`, `sus`, `computer`, and `internet`, plus a set of builtins always in scope with no import.
Full reference, generated from the actual function registry so it can't drift:
[docs/STDLIB.md](docs/STDLIB.md).

## Documentation

- [docs/LANGUAGE.md](docs/LANGUAGE.md) — the full language reference.
- [docs/BYTECODE.md](docs/BYTECODE.md) — the opcode table, file formats, and a worked disassembly
  walkthrough.
- [docs/STDLIB.md](docs/STDLIB.md) — every stdlib function.
- [PLAN.md](PLAN.md) — the original specification and milestone-by-milestone build log, including
  every deviation from spec and why (§16).
- [CHANGELOG.md](CHANGELOG.md) — what's actually landed, milestone by milestone.

## Development

The test suite is pure `pytest` and needs nothing but `pytest` itself (plus `pyinstaller` for the
packaging and self-hosting tests, which skip cleanly without it):

```console
$ python -m pytest -q
```

Use `python -m pytest` rather than bare `pytest` — several tests shell out to `python -m
funnylang`, and the `-m` form is what puts the repo root on `sys.path`.

Every push and pull request runs the full suite across Windows, Linux, and macOS on Python
3.10–3.12, verifies the self-hosting bootstrap, and freezes + runs a real executable — see
[.github/workflows/ci.yml](.github/workflows/ci.yml).
