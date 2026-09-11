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
- Concurrent, in both of the ways that word means. `async_ngl bet` and `await_fr` are a real event
  loop over tasks that each own their stack and frames; `gimme interns` runs work on **real OS
  threads**, each in its own VM with its own heap:

  ```funny
  gimme interns
  gimme clock

  async_ngl bet fetch_both() {
      // Two interns, two OS threads, one wait.
      yo a = interns.hire("slow_job.funny", 21)
      yo b = interns.hire("slow_job.funny", 21)
      bounce await_fr a + await_fr b
  }

  yap await_fr fetch_both()                // 42
  yap what_is_it(fetch_both())             // otw
  await_fr clock.chill(50)                 // yields; touch_grass blocks
  ```

  Nothing is shared between threads, so nothing needs a lock: values are deep-copied across the
  worker boundary, and anything that cannot cross coherently is refused with an error that says
  which argument and why. `examples/concurrency.funny` is a tour of both halves; see
  [ASYNC_PLAN.md](ASYNC_PLAN.md) for the design and its cost sheet.

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

Eleven modules, imported with `gimme`: `mafs`, `yapper`, `stash`, `groupchat`, `rizz`, `filez`,
`clock`, `sus`, `computer`, `internet`, and `interns`, plus a set of builtins always in scope with
no import.
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
- [ASYNC_PLAN.md](ASYNC_PLAN.md) — the plan for `async_ngl`/`await_fr` and `interns`: why isolated
  workers rather than shared-memory threads, what the deep copy costs, and the same build log.
- [CHANGELOG.md](CHANGELOG.md) — what's actually landed, milestone by milestone.

## Development

The tests need nothing but the binary you just built.

```console
$ ./funny test tests/lang     # the golden corpus
$ ./funny test tests/slow     # the same, for the ones that take seconds each
$ ./funny test examples
$ ./funny bootstrap --verify  # the compiler reproduces itself, byte for byte
```

A test is a `.funny` file and a `.expected` file beside it. `funny test` compiles and runs each one
in an isolated VM and compares stdout byte for byte. A `.expected` may open with directives when
plain stdout is not the point:

| directive | what it asserts |
|---|---|
| `!ERROR SkillIssue` | that flavor escapes, at compile time or run time |
| `!ARGS a b "two words"` | argv for the program |
| `!EXIT 3` | the exit code |
| `!DIAG` / `!DIAG serious` | the rendered diagnostic, caret line and all |
| `!XRAY --tokens` | the token dump; the file is analysed, not run |
| `!XRAY --ast` | the parse tree |
| `!XRAY` | the disassembly |

There used to be a second suite: a 1,313-test `pytest` run that compiled every program with both the
C runtime and a Python reference implementation and diffed the results. That reference is gone, and
so is the suite — v2.0.0's whole point is that this repository contains no Python at all. What the
corpus lost in having an oracle it gained in coverage: it grew from 76 pairs to 324 while the port
was still there to check each one against.

Every push and pull request builds `native/` with gcc, clang and MSVC across Windows, Linux and
macOS and runs all of the above on each, plus the corpus under ASan/UBSan and under
`FUNNY_GC_STRESS`. One job does it in a container with a C compiler and **nothing else installed —
no Python, no pip, no interpreter of any kind** — which is what makes "needs nothing installed" a
fact about the artifact rather than a claim in a README. See
[.github/workflows/ci.yml](.github/workflows/ci.yml).
