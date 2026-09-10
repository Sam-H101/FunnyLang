# FunnyLang

```
   ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗
   ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝
   █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝
   ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝
   ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║
   ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝
        FunnyLang v2.0.0 — it compiles. somehow.
```

A bytecode-compiled programming language that refuses to take itself seriously — and, as of
v2.0.0, compiles itself, with a runtime that needs nothing installed.

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

### Download

One file. Nothing to install alongside it, no runtime, no interpreter.

```console
$ curl -fsSL https://raw.githubusercontent.com/Sam-H101/FunnyLang/master/install.sh | sh
```

```powershell
> irm https://raw.githubusercontent.com/Sam-H101/FunnyLang/master/install.ps1 | iex
```

Both scripts verify the download's SHA-256 against the release's own `SHA256SUMS` and refuse to
install on a mismatch. Or grab a binary yourself from
[Releases](https://github.com/Sam-H101/FunnyLang/releases):

| Platform | File |
|---|---|
| Linux x86-64 | `funny-linux-x86_64` |
| Linux ARM64 | `funny-linux-aarch64` |
| macOS Apple Silicon | `funny-macos-aarch64` |
| macOS Intel | `funny-macos-x86_64` |
| Windows x86-64 | `funny-windows-x86_64.exe` |

`chmod +x` it on Linux/macOS and you're done. Each release also ships `funnyrt-*`, the runtime stub
— you only need it if you want `funny yeet`. The Linux builds target glibc 2.35, so they run on
Debian 12 and anything newer; CI checks that on an actual Debian 12 container before a release is
allowed to stand.

### Build from source

For contributors, or any platform without a prebuilt binary. You need a C compiler and nothing else
— no Python, no build system, no package manager, no third-party library.

```console
$ git clone https://github.com/Sam-H101/FunnyLang
$ cd FunnyLang
$ ./build.sh                      # or build.bat on Windows, with MSVC
$ ./funny run examples/hello.funny
yo sup world
```

That produces two binaries: `funny` (the command line) and `funnyrt` (the runtime stub `funny
yeet` copies to make standalone programs). `build.sh` honours `CC`, so `CC=clang ./build.sh` works.

`funny` is self-contained — the entire toolchain, compiler included, is compiled into it. Nothing
needs to ship beside it. Keep `funnyrt` around only if you want `funny yeet`.

See [docs/NATIVE.md](docs/NATIVE.md) for the runtime's layout, the platform boundary, and the GC
contract.

## What this is

- A complete, hand-written toolchain: lexer → parser → resolver → bytecode compiler → stack VM,
  all built from scratch, no parser-generator, no bytecode-generator library.
- A real, if comedic, general-purpose language: closures with proper upvalue capture, single
  inheritance classes, exceptions with try/catch/finally, a module system, arbitrary-precision
  integers, string interpolation, slicing — see [docs/LANGUAGE.md](docs/LANGUAGE.md) for the full
  reference.
- Genuinely self-hosting, and not only the compiler. Everything in `selfhost/` is FunnyLang:
  lexer, parser, resolver, codegen, `.funnyc` emitter, the `.funnypak` linker, the disassembler,
  the formatter, the test runner, the REPL, and the command line itself. `native/main.c` is a
  loader — it finds the CLI bundle and hands it your arguments. `funny bootstrap --verify` links
  that toolchain, uses the result to link it again, and again, and checks the last two generations
  are byte-for-byte identical:

  ```console
  $ funny bootstrap --verify
  🥁 stage 2... compiled.
  🥁 stage 3... compiled by stage 2.
  🥁 stage 4... compiled by stage 3.

  stage3 and stage4 are byte-identical (255,687 bytes).
  FunnyLang now compiles FunnyLang. we are so back. 🏆
  ```
- Shippable: `funny yeet` turns a program into a standalone executable by copying the runtime stub
  and appending the compiled bytecode. A couple of hundred KB, nothing to install to run it.

## What this isn't

- **The virtual machine is not self-hosted, and cannot be.** Something has to actually execute
  bytecode, and that something is `native/` — a C program. Everything *above* the VM is FunnyLang;
  the VM itself is C and will stay C, because a FunnyLang-hosted VM would need a VM to run it and
  the regress never bottoms out. Claiming otherwise would be a real, not a funny, lie.

  What did change in v2.0.0: that C runtime replaced the Python one. Earlier versions ran on
  CPython and `funny yeet` bundled an entire Python interpreter to produce an 8 MB executable.
  Now the runtime is a few hundred KB and needs nothing installed.
- Not statically typed, not performance-tuned beyond "reasonable for a straightforward bytecode
  VM," and not aiming to replace a real production language. It's a from-scratch language
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
- [docs/NATIVE.md](docs/NATIVE.md) — building the C runtime, the platform boundary, the GC
  contract, and how to port it.
- [docs/BYTECODE.md](docs/BYTECODE.md) — the opcode table, file formats, and a worked disassembly
  walkthrough.
- [docs/STDLIB.md](docs/STDLIB.md) — every stdlib function.
- [PLAN.md](PLAN.md) — the original specification and milestone-by-milestone build log, including
  every deviation from spec and why (§16).
- [NATIVE_PLAN.md](NATIVE_PLAN.md) — the plan for the C runtime and the self-hosted toolchain, with
  the same kind of build log in §9.
- [CHANGELOG.md](CHANGELOG.md) — what's actually landed, milestone by milestone.

## Development

There are two test suites, and they are different in kind.

**The golden corpus** needs nothing but the binary you just built:

```console
$ ./funny test tests/lang
$ ./funny test examples
$ ./funny bootstrap --verify
```

**The differential suite** compiles each program with both the C and the Python implementations,
runs it through both VMs, and diffs stdout byte for byte. It needs Python and `pytest`:

```console
$ python -m pytest -q
$ FUNNY_GC_STRESS=1 python -m pytest tests/native/ -q
```

Use `python -m pytest` rather than bare `pytest` — several tests shell out to `python -m
funnylang`, and the `-m` form is what puts the repo root on `sys.path`.

The Python implementation in `funnylang/` is a reference oracle, not the product. It is what the C
runtime is checked against, and it is on its way out: see NATIVE_PLAN.md's N11.

Every push and pull request builds `native/` with gcc, clang and MSVC across Windows, Linux and
macOS, runs the differential suite (plain and under `FUNNY_GC_STRESS`), and — the job that matters
most — builds and exercises the whole toolchain in a container with **no Python installed and every
`.py` file deleted from the checkout**. See
[.github/workflows/ci.yml](.github/workflows/ci.yml).
