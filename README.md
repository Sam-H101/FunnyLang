# FunnyLang

A bytecode-compiled programming language that refuses to take itself seriously.

FunnyLang has a hand-written lexer, a recursive-descent parser, a scope resolver, a
bytecode compiler, and a stack VM with closures, upvalues, exceptions, and modules —
all in pure Python, no third-party runtime dependencies. The keywords just happen to
be brainrot.

```funny
yo greeting = "yo sup world"
yap greeting
```

```console
$ funny run examples/hello.funny
yo sup world
```

## Install

Requires Python 3.10 or newer.

```console
$ pip install -e .
```

That puts a `funny` command on your PATH. You can also skip the install entirely and
run the package directly from a checkout with `python -m funnylang`, which is what the
test suite does.

## A taste

Control flow (`examples/fizzbuzz.funny`):

```funny
grind n from 1 to 101 {
    sus (n % 15 == 0) {
        yap "FizzBuzz"
    } kinda_sus (n % 3 == 0) {
        yap "Fizz"
    } kinda_sus (n % 5 == 0) {
        yap "Buzz"
    } nah {
        yap n
    }
}
```

Functions and closures (`examples/closures.funny`):

```funny
bet counter() {
    yo n = 0
    bounce lowkey () {
        n += 1
        bounce n
    }
}
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

Rough decoder ring: `yo` declares, `yap` prints, `bet` defines a function, `bounce`
returns, `lowkey` is a lambda, `sus`/`kinda_sus`/`nah` are if/elif/else, `grind` loops,
`sketchy`/`my_bad`/`regardless` are try/catch/finally, and `gimme` imports.

## The CLI

```
usage: funny [-h] [--serious] [--no-color] [--time] [--vibes] [--version]
             {run,build,yeet,vibe,xray,fmt,test,bootstrap} ...

    run         run a .funny/.funnyc/.funnypak file
    build       compile (+ link) to .funnyc/.funnypak
    yeet        compile to a native executable (M10)
    vibe        the REPL
    xray        disassemble/inspect a file
    fmt         canonical formatter
    test        run *.funny/*.expected pairs in a directory
    bootstrap   self-host verification (M12)
```

`build` links a whole dependency tree into a self-contained `.funnypak` that resolves
its own imports at run time with no filesystem access. `vibe` is a real REPL with a
persistent global scope, multi-line continuation, and last-expression auto-print.
`yeet` and `bootstrap` are not implemented yet and say so when you call them.

Global flags work before or after the subcommand. `--serious` swaps the roasts for
plain professional error text, `--no-color` drops ANSI codes, `--time` reports how
long compiling and running took, and `--vibes` narrates each phase.

## Errors

Diagnostics are a feature, not an afterthought — every error renders the offending
source line with a caret, a plain-English explanation, a fix hint, and a stack trace:

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

Ten modules, imported with `gimme`: `mafs`, `yapper`, `stash`, `groupchat`, `rizz`,
`filez`, `clock`, `sus`, `computer`, and `internet`. A set of builtins is always in
scope without an import.

## Development

The test suite is pure `pytest` and needs nothing but `pytest` itself:

```console
$ python -m pytest
```

Use `python -m pytest` rather than bare `pytest` — the CLI tests shell out to
`python -m funnylang`, and the `-m` form is what puts the repo root on `sys.path`.

Every push and pull request runs the suite on Linux and Windows across Python
3.10–3.13; see [.github/workflows/tests.yml](.github/workflows/tests.yml).

`PLAN.md` is the language specification and the milestone roadmap. `CHANGELOG.md`
tracks what has actually landed.
