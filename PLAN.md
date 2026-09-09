# FunnyLang — Build Plan

> **Audience:** an executing coding agent (Sonnet) working autonomously in `f:\my_program_lang`.
> **Deliverable:** a complete, self-hosting, bytecode-compiled programming language called **FunnyLang**,
> shipped as a native executable toolchain, where `hello.funny` compiles to `hello.exe` (or `hello` on Linux/macOS).
> **Host language for stage 1:** Python 3.12+.

---

## 0. Rules for the executing agent

Read these before touching a file. They are not optional.

1. **The spec in §3–§6 is frozen.** Keyword names, opcode numbers, and file formats are decided.
   Do not rename `yap` to `speak` because it reads better. Consistency across 14 milestones matters
   more than your taste. If you find a genuine contradiction in the spec, fix it in this file *first*,
   note it under §16 Change Log, then implement.
2. **Work milestone by milestone, in order.** Each milestone (M0…M14) has an **Acceptance** block with
   literal shell commands and expected output. A milestone is not done until those commands pass on
   Windows (`powershell`) *and* the pytest suite is green.
3. **Never skip tests.** Every milestone adds tests. `pytest -q` must pass at the end of every milestone.
   If you break a previous milestone's test, fix it before moving on — do not delete or `xfail` it.
4. **Commit after each milestone.** `git commit -m "M<n>: <milestone title>"`. Repo is not yet
   initialized — M0 does that.
5. **The comedy is a hard requirement, not decoration.** Error messages, CLI output, and stdlib names
   are part of the product. A technically correct compiler with boring error messages fails acceptance.
   But: *funny must never cost correctness*. Errors must still report exact file, line, column, and cause.
6. **No half-features.** If a milestone says arrays support `slice`, ship `slice` with tests, negative
   indices, and an out-of-range error. Do not stub.
7. **Ask nothing, assume nothing silently.** Where this plan leaves a genuine choice (marked
   `AGENT CHOICE`), pick the simplest option that satisfies the acceptance criteria and write one line
   in §16 recording what you picked.
8. **Windows is the primary dev platform.** Paths use `\`, but all code must use `pathlib` / `os.path`
   and work on POSIX. Line endings in generated `.funny` files: `\n`.

---

## 1. What "done" looks like

At the end of M13, all of the following work from a clean checkout:

```powershell
# install the toolchain in dev mode
pip install -e .

# 1. run a source file directly (compile in memory, execute on the VM)
funny run examples/hello.funny

# 2. compile to portable bytecode
funny build examples/hello.funny -o hello.funnyc
funny run hello.funnyc

# 3. compile to a REAL native executable, no Python required on the target machine
funny yeet examples/hello.funny -o hello.exe
.\hello.exe
# => yo sup world

# 4. REPL
funny vibe

# 5. inspect the bytecode
funny xray examples/hello.funny

# 6. THE BIG ONE — the compiler compiles itself
funny bootstrap --verify
# => stage2 and stage3 bytecode are byte-identical. we have achieved self-hosting. sheeeesh.
```

And `hello.funny` is:

```funny
// hello.funny
yo greeting = "yo sup world"
yap greeting
```

---

## 2. Repository layout

Create exactly this. Files marked `(Mn)` are created in that milestone.

```
f:\my_program_lang\
├── PLAN.md                          # this file
├── README.md                        # (M13) the funny public-facing readme
├── CHANGELOG.md                     # (M0)
├── pyproject.toml                   # (M0) packaging, entry point `funny`
├── .gitignore                       # (M0)
├── pytest.ini                       # (M0)
│
├── funnylang/                       # the stage-1 toolchain, written in Python
│   ├── __init__.py                  # (M0) __version__
│   ├── __main__.py                  # (M0) `python -m funnylang`
│   ├── cli.py                       # (M8) argparse CLI: run/build/yeet/vibe/xray/fmt/bootstrap
│   ├── source.py                    # (M1) SourceFile, Span, line/col mapping, snippet rendering
│   ├── tokens.py                    # (M1) TokenKind enum, Token dataclass, KEYWORDS table
│   ├── lexer.py                     # (M1)
│   ├── ast_nodes.py                 # (M2) all AST node dataclasses
│   ├── parser.py                    # (M2) recursive descent + Pratt expression parser
│   ├── resolver.py                  # (M3) scope resolution, upvalue capture, const enforcement
│   ├── opcodes.py                   # (M4) Op enum + operand-width table + disassembler names
│   ├── compiler.py                  # (M4) AST -> Chunk
│   ├── chunk.py                     # (M4) Chunk, ConstPool, LineTable, FunctionProto
│   ├── serializer.py                # (M4) .funnyc read/write, .funnypak read/write
│   ├── disasm.py                    # (M4) human-readable bytecode dump
│   ├── values.py                    # (M5) runtime value types: FunnyString, Stash, GroupChat, Closure…
│   ├── vm.py                        # (M5) the stack VM
│   ├── errors.py                    # (M6) FunnyError hierarchy + diagnostic renderer
│   ├── modules.py                   # (M7) module resolution, load cache, circular-import detection
│   ├── stub_main.py                 # (M10) entry point baked into the frozen runtime stub
│   ├── packager.py                  # (M10) stub + payload -> native exe
│   └── stdlib/                      # (M6)
│       ├── __init__.py              # registry: NAME -> module builder
│       ├── builtins.py              # globals: yap, how_thicc, what_is_it, no_cap, ask, …
│       ├── mafs.py
│       ├── yapper.py                # string utils
│       ├── stash.py                 # array utils
│       ├── groupchat.py             # map utils
│       ├── rizz.py                  # randomness
│       ├── filez.py
│       ├── clock.py
│       ├── computer.py              # computer.explode() etc.
│       ├── internet.py              # internet.go_brrrr() etc.
│       └── sus.py                   # reflection / debug helpers
│
├── selfhost/                        # the stage-2 compiler, written in FunnyLang
│   ├── funnyc.funny                 # (M12) entry point
│   ├── lexer.funny                  # (M12)
│   ├── parser.funny                 # (M12)
│   ├── compiler.funny               # (M12)
│   ├── emitter.funny                # (M12) .funnyc byte emission
│   └── prelude.funny                # (M12) shared helpers
│
├── examples/                        # (M1 onward — add as features land)
│   ├── hello.funny
│   ├── fizzbuzz.funny
│   ├── fib.funny
│   ├── arrays.funny
│   ├── closures.funny
│   ├── errors.funny
│   ├── modules/
│   │   ├── main.funny
│   │   └── mathstuff.funny
│   └── chaos.funny                  # showcases computer.explode(), internet.go_brrrr()
│
├── tests/
│   ├── test_lexer.py                # (M1)
│   ├── test_parser.py               # (M2)
│   ├── test_resolver.py             # (M3)
│   ├── test_compiler.py             # (M4)
│   ├── test_vm.py                   # (M5)
│   ├── test_stdlib.py               # (M6)
│   ├── test_errors.py               # (M6)
│   ├── test_modules.py              # (M7)
│   ├── test_cli.py                  # (M8)
│   ├── test_serializer.py           # (M4)
│   ├── test_packager.py             # (M10)
│   ├── test_bootstrap.py            # (M12)
│   ├── conftest.py                  # (M1) helpers: run_source(), expect_error()
│   └── lang/                        # golden end-to-end tests
│       ├── <name>.funny
│       └── <name>.expected          # exact stdout, or `!ERROR <ErrorName>` on line 1
│
├── docs/                            # (M13)
│   ├── LANGUAGE.md                  # full user-facing language reference
│   ├── BYTECODE.md                  # opcode reference
│   └── STDLIB.md
│
└── build/                           # gitignored
    └── stub/                        # the frozen runtime stub binary lives here
```

---

## 3. Language specification (FROZEN)

File extension: `.funny`. Compiled bytecode: `.funnyc`. Linked bundle: `.funnypak`.
Encoding: UTF-8. Emoji are legal in strings, comments, and — yes — identifiers.

### 3.1 Comments

```funny
// single line
/* block
   comment */
```

### 3.2 Literals

| Kind | Syntax | Runtime type name |
|---|---|---|
| Integer | `42`, `-7`, `1_000_000`, `0xFF`, `0b1010`, `0o755` | `numba` |
| Float | `3.14`, `1e9`, `2.5e-3` | `numba` |
| String | `"hi"`, `'hi'`, `"""multi\nline"""` | `yapstring` |
| Interpolated string | `` `yo {name}, you are {age}` `` | `yapstring` |
| Boolean true | `fax` | `boolski` |
| Boolean false | `cap` | `boolski` |
| Null | `ghost` | `ghost` |
| Array | `[1, 2, 3]` | `stash` |
| Map | `{"a": 1, "b": 2}` | `groupchat` |
| Function | `bet f() {}` / `lowkey (x) => x + 1` | `bet` |

String escapes: `\n \t \r \\ \" \' \0 \u{1F480}` and `\{` to escape a `{` in an interpolated string.

Integers are arbitrary precision (Python `int`). Floats are IEEE-754 doubles.
Mixed int/float arithmetic promotes to float. `/` always produces a float; `//` is floor division.

### 3.3 Keywords (FROZEN — this is the whole list)

| FunnyLang | Means | Notes |
|---|---|---|
| `yo` | mutable variable declaration | `yo x = 5` |
| `deadass` | constant declaration | reassignment is a compile error |
| `yap` | print with newline | `yap a, b, c` → space-separated |
| `yeet` | print with newline | **alias of `yap`**, both are canonical |
| `mumble` | print without newline | |
| `bet` | function declaration | `bet add(a, b) { bounce a + b }` |
| `lowkey` | anonymous function / lambda | `lowkey (x) => x * 2` or `lowkey (x) { bounce x*2 }` |
| `bounce` | return | bare `bounce` returns `ghost` |
| `sus` | if | `sus (x > 3) { }` |
| `kinda_sus` | else-if | |
| `nah` | else | |
| `bruh` | while | `bruh (x < 10) { }` |
| `grind` | for | see §3.5 |
| `from` | range-loop lower bound | contextual keyword |
| `to` | range-loop upper bound (exclusive) | contextual keyword |
| `step` | range-loop stride | contextual keyword |
| `in` | for-each / membership operator | |
| `bail` | break | |
| `nvm` | continue | |
| `sketchy` | try | |
| `my_bad` | catch | `my_bad (e) { }` |
| `regardless` | finally | |
| `chuck` | throw | `chuck "you fumbled it"` |
| `gimme` | import | see §3.8 |
| `as` | import alias | contextual |
| `flex` | export | `flex bet add(a,b) {}` |
| `squad` | class declaration | see §3.7 |
| `inherits` | superclass | `squad Dog inherits Animal {}` |
| `me` | `this` / `self` | |
| `og` | `super` | `og.speak()` |
| `spawn` | constructor method name | `spawn(name) { me.name = name }` |
| `fax` | `true` | |
| `cap` | `false` | |
| `ghost` | `null` | |
| `fr` | logical AND | alias of `&&` |
| `orr` | logical OR | alias of `\|\|` |
| `aint` | logical NOT | alias of `!` |
| `same_energy` | `==` | alias |
| `diff_energy` | `!=` | alias |
| `vibe` | no-op statement / pass | `vibe` compiles to nothing |

Reserved for future use (lexed as keywords, parser rejects with a "not yet, chief" error):
`vibin`, `async_ngl`, `await_fr`, `yield_lol`, `match_this`, `when`.

Identifiers: `[A-Za-z_\p{Emoji}][A-Za-z0-9_\p{Emoji}]*`. Case sensitive.

### 3.4 Operators and precedence

Lowest to highest binding. All binary operators are left-associative except where noted.

| Lvl | Operators | Notes |
|---|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `**=` `\|\|=` | assignment, **right-assoc**, target must be ident / index / property |
| 2 | `? :` | ternary, right-assoc |
| 3 | `\|>` | pipe: `x \|> f \|> g` == `g(f(x))` |
| 4 | `??` | ghost-coalesce: `a ?? b` yields `b` only if `a` is `ghost` |
| 5 | `\|\|` `orr` | short-circuit |
| 6 | `&&` `fr` | short-circuit |
| 7 | `\|` | bitwise or |
| 8 | `^` | bitwise xor |
| 9 | `&` | bitwise and |
| 10 | `==` `!=` `same_energy` `diff_energy` | |
| 11 | `<` `<=` `>` `>=` `in` | `in` works on stash, groupchat, yapstring |
| 12 | `<<` `>>` | bit shifts |
| 13 | `+` `-` | `+` also concatenates yapstrings and stashes |
| 14 | `*` `/` `//` `%` | `*` also repeats yapstring/stash: `"ha" * 3` |
| 15 | `**` | **right-assoc** |
| 16 | unary `-` `!` `aint` `~` | |
| 17 | postfix `(...)` `[...]` `.name` `?.name` | call, index, member, safe-member |
| 18 | primary | literal, ident, `(expr)`, `me`, `og` |

`?.` short-circuits the whole postfix chain to `ghost` if the receiver is `ghost`.

Indexing supports negative indices (`arr[-1]` is last) and slices: `arr[1:3]`, `arr[:2]`, `arr[2:]`, `s[::-1]`.

**Truthiness:** `cap` and `ghost` are falsy. `0`, `0.0`, `""`, `[]`, `{}` are falsy.
Everything else is truthy. (Documented as: "empty things have no rizz.")

### 3.5 Statements

```funny
// declarations
yo x = 10
yo y            // defaults to ghost
deadass PI = 3.14159

// print
yap "hello"
yeet "also hello"
yap x, y, "three things"
mumble "no newline"

// conditionals
sus (x > 100) {
    yap "big"
} kinda_sus (x > 10) {
    yap "medium"
} nah {
    yap "smol"
}

// while
bruh (x > 0) {
    x -= 1
    sus (x same_energy 3) { bail }
}

// for over a range (upper bound exclusive)
grind i from 0 to 10 {
    yap i
}
grind i from 10 to 0 step -2 {
    yap i
}

// for-each over stash / yapstring / groupchat (groupchat yields keys)
grind item in ["a", "b", "c"] {
    sus (item same_energy "b") { nvm }
    yap item
}

// functions
bet add(a, b) {
    bounce a + b
}
bet greet(name, greeting = "yo") {   // default params
    bounce `{greeting} {name}`
}
bet sum_all(...rest) {               // variadic; rest is a stash
    yo total = 0
    grind n in rest { total += n }
    bounce total
}

// closures
bet counter() {
    yo n = 0
    bounce lowkey () => { n += 1; bounce n }
}

// errors
sketchy {
    chuck "something went sideways"
} my_bad (e) {
    yap "caught:", e.message, "of type", e.flavor
} regardless {
    yap "cleanup always runs"
}
```

**Statement termination:** the lexer emits `NEWLINE` tokens. A statement ends at a `NEWLINE` or `;`.
Blank lines and consecutive terminators are skipped. Inside `(...)`, `[...]`, and `{...}` *of a
map/array literal or argument list*, NEWLINE tokens are suppressed (paren-depth tracking in the lexer),
so multi-line expressions work naturally. Block braces `{}` of statements do NOT suppress newlines.

### 3.6 Functions & closures

- First class, closures capture by reference via upvalues (Lua/Crafting-Interpreters style).
- Default parameters evaluated at call time, left to right.
- Variadic `...rest` must be the last parameter.
- Recursion supported; stack depth limit 10_000 frames → `TooDeepBro` error.
- `|>` pipe passes the left value as the **first** argument: `[1,2,3] |> stash.squish(lowkey (a,b)=>a+b, 0)`.
- Composition builtin: `combo(f, g, h)` returns `lowkey (x) => h(g(f(x)))` (left-to-right).

### 3.7 Classes (`squad`)

```funny
squad Animal {
    spawn(name) {
        me.name = name
    }
    bet speak() {
        yap me.name, "makes a noise"
    }
    bet to_yap() {                 // magic method used by yap/string conversion
        bounce `Animal({me.name})`
    }
}

squad Dog inherits Animal {
    spawn(name, breed) {
        og.spawn(name)
        me.breed = breed
    }
    bet speak() {
        og.speak()
        yap me.name, "says bark. skrrrt."
    }
}

yo d = Dog("rex", "corgi")
d.speak()
```

Magic methods recognized by the VM: `to_yap()` (stringify), `how_thicc()` (length),
`get_it(key)` / `set_it(key, val)` (index), `same_energy(other)` (equality).

Single inheritance only. No interfaces. No static members. Fields are created by assignment in `spawn`.

### 3.8 Modules

```funny
// mathstuff.funny
flex deadass TAU = 6.28318
flex bet double(x) { bounce x * 2 }
bet secret() { }                 // not exported

// main.funny
gimme "mathstuff.funny"                    // binds module object as `mathstuff`
gimme "./util/helpers.funny" as helpers    // explicit alias
gimme { double, TAU } from "mathstuff.funny"   // named import
gimme mafs                                 // stdlib module (no quotes, no extension)
gimme rizz as luck

yap mathstuff.double(21)
yap mafs.sqrt(16)
```

Resolution order for a quoted path:
1. Relative to the importing file's directory.
2. Relative to each entry in the `FUNNYPATH` env var (`;`-separated on Windows, `:` elsewhere).
3. `./funny_modules/` walking up from the importing file.

Bare identifiers (`gimme mafs`) resolve **only** to stdlib modules.
Modules execute once; results are cached by canonical absolute path.
Circular imports raise `ImportSkillIssue` naming the full cycle.

### 3.9 Runtime types & their methods

Method calls are `value.method(args)`. Free functions live in stdlib modules (§7).

**`yapstring`** — `how_thicc()`, `SCREAM()`, `whisper()`, `trim()`, `split(sep)`, `contains(s)`,
`starts_with(s)`, `ends_with(s)`, `replace(a,b)`, `index_of(s)`, `slice(a,b)`, `reverse()`,
`to_numba()`, `chars()`, `at(i)`, `code_at(i)`, `repeat(n)`, `pad_left(n,c)`, `pad_right(n,c)`.
Immutable.

**`stash`** — `how_thicc()`, `yeet_in(x)` (push), `yoink()` (pop), `yoink_at(i)`, `insert(i,x)`,
`contains(x)`, `index_of(x)`, `slice(a,b)`, `reverse()`, `sort(cmp?)`, `join(sep)`,
`glow_up(fn)` (map), `vibe_check(fn)` (filter), `squish(fn, init)` (reduce), `any(fn)`, `all(fn)`,
`first()`, `last()`, `clone()`, `clear()`, `extend(other)`. Mutable, reference semantics.

**`groupchat`** — `how_thicc()`, `keys()`, `values()`, `pairs()`, `has(k)`, `get(k, default?)`,
`set(k,v)`, `remove(k)`, `merge(other)`, `clone()`, `clear()`. Keys may be `yapstring`, `numba`,
`boolski`. Insertion ordered. Mutable, reference semantics.

**`numba`** — `to_yap()`, `abs()`, `floor()`, `ceil()`, `round(digits?)`, `is_whole()`.

**`bet`** (function) — `arity()`, `name()`, `call(...args)`.

**`error`** — fields `.flavor` (yapstring, the error class name), `.message`, `.line`, `.col`,
`.file`, `.trace` (stash of yapstrings), `.payload` (whatever was `chuck`ed).

---

## 4. The error system (FROZEN)

This is a headline feature. Budget real time for it.

### 4.1 Error taxonomy

| Class | Raised when | Sample roast |
|---|---|---|
| `LexerSaidNah` | unrecognized character / unterminated string | "what even IS that character. i'm not doing this." |
| `ParserHadAStroke` | syntax error | "i read this three times. it's still not code." |
| `WhoDis` | undefined variable | "`{name}` who? never heard of them." |
| `TypeVibeMismatch` | wrong operand types | "a numba and a yapstring do NOT have the same energy." |
| `MathAintMathin` | div by zero, bad domain | "you divided by zero. the universe said no." |
| `OutOfPocket` | index out of range | "index {i} on a stash of {n}. that's straight up out of pocket." |
| `KeyGhosted` | missing map key | "key `{k}` left the group chat." |
| `GhostError` | member access / call on `ghost` | "you're talking to a ghost, king." |
| `NotACallableRizz` | calling a non-function | "that thing has no call rizz whatsoever." |
| `WrongNumberOfHomies` | arity mismatch | "`{fn}` wanted {want} args. you brought {got}. awkward." |
| `TooDeepBro` | stack overflow | "you recursed {n} deep. touch grass." |
| `ImportSkillIssue` | module not found / circular | "can't find `{m}`. did you make it up?" |
| `ImmutableVibes` | reassigning a `deadass` | "`{name}` is deadass. it doesn't change. like your ex's opinion of you." |
| `SkillIssue` | user `chuck` with a non-error value, generic base | "skill issue." |
| `ComputerExploded` | `computer.explode()` | see §7.9 |

All of these are Python classes in `funnylang/errors.py` inheriting `FunnyError`.
`FunnyError` carries `flavor`, `message`, `roast`, `hint`, `span`, `frames`.

### 4.2 Diagnostic rendering

Every user-visible error prints in this exact shape (colors via ANSI when stdout is a TTY, plain otherwise):

```
💀💀💀 FUNNYLANG MOMENT 💀💀💀

  TypeVibeMismatch  ──  examples/oops.funny:7:12

     5 │ bet main() {
     6 │     yo name = "sam"
     7 │     yap name + 5
       │                ^ this right here
     8 │ }

  bro. you tried to add a numba to a yapstring.
  those two do NOT have the same energy.

  💡 skill issue fix:  yap name + to_yap(5)

  🥞 stack of shame:
       at main()          examples/oops.funny:7
       at <the big one>   examples/oops.funny:10
```

Requirements:
- The caret must point at the correct **column** of the offending token, using the span from the line table.
- Show 2 lines of context before and 1 after, clamped to the file.
- `💡 skill issue fix:` line is optional — emit it only when `hint` is set. Add hints for the top 10
  most common mistakes (type mismatch on `+`, missing `bounce`, `=` vs `==`, calling `yap` with parens
  when not needed, undefined var with a close match via Levenshtein distance ≤ 2 → "did you mean `{x}`?").
- Compile-time errors (`LexerSaidNah`, `ParserHadAStroke`, `WhoDis`, `ImmutableVibes`) use the same
  renderer with no stack of shame.
- **Parser error recovery:** on a syntax error, synchronize to the next statement boundary and keep
  parsing, so a single run reports up to 5 syntax errors, then `... and {n} more. i'll stop.`
- Exit code on uncaught error: `1`. Exit code for `computer.explode()`: `69`.
- Env var `FUNNY_SERIOUS=1` swaps all roasts for plain professional text (same structure, no emoji).
  Implement this in the renderer only; tests use it to assert on stable strings.

---

## 5. Bytecode specification (FROZEN)

Stack-based VM. All jump offsets are **unsigned 16-bit, big-endian, relative**.
Constant-pool indices are **unsigned 16-bit big-endian** unless noted. Local/upvalue slots are **u8**.

### 5.1 Opcode table

| # | Name | Operands | Stack effect |
|---|---|---|---|
| 0 | `NOP` | — | — |
| 1 | `CONST` | u16 idx | → value |
| 2 | `GHOST` | — | → ghost |
| 3 | `FAX` | — | → true |
| 4 | `CAP` | — | → false |
| 5 | `POP` | — | value → |
| 6 | `DUP` | — | a → a a |
| 7 | `SWAP` | — | a b → b a |
| 8 | `GET_LOCAL` | u8 slot | → value |
| 9 | `SET_LOCAL` | u8 slot | value → value |
| 10 | `GET_GLOBAL` | u16 nameIdx | → value |
| 11 | `SET_GLOBAL` | u16 nameIdx | value → value |
| 12 | `DEF_GLOBAL` | u16 nameIdx | value → |
| 13 | `GET_UPVAL` | u8 idx | → value |
| 14 | `SET_UPVAL` | u8 idx | value → value |
| 15 | `CLOSE_UPVAL` | — | value → |
| 16 | `GET_PROP` | u16 nameIdx | obj → value |
| 17 | `SET_PROP` | u16 nameIdx | obj value → value |
| 18 | `GET_PROP_SAFE` | u16 nameIdx | obj → value \| ghost |
| 19 | `GET_INDEX` | — | obj key → value |
| 20 | `SET_INDEX` | — | obj key value → value |
| 21 | `GET_SLICE` | — | obj start stop stepv → value |
| 22 | `ADD` | — | a b → c |
| 23 | `SUB` | — | a b → c |
| 24 | `MUL` | — | a b → c |
| 25 | `DIV` | — | a b → c |
| 26 | `IDIV` | — | a b → c |
| 27 | `MOD` | — | a b → c |
| 28 | `POW` | — | a b → c |
| 29 | `NEG` | — | a → -a |
| 30 | `NOT` | — | a → boolski |
| 31 | `BNOT` | — | a → ~a |
| 32 | `BAND` | — | a b → c |
| 33 | `BOR` | — | a b → c |
| 34 | `BXOR` | — | a b → c |
| 35 | `SHL` | — | a b → c |
| 36 | `SHR` | — | a b → c |
| 37 | `EQ` | — | a b → boolski |
| 38 | `NEQ` | — | a b → boolski |
| 39 | `LT` | — | a b → boolski |
| 40 | `LE` | — | a b → boolski |
| 41 | `GT` | — | a b → boolski |
| 42 | `GE` | — | a b → boolski |
| 43 | `IN` | — | a b → boolski |
| 44 | `JUMP` | u16 off | — (ip += off) |
| 45 | `JUMP_IF_FALSE` | u16 off | cond → |
| 46 | `JUMP_IF_TRUE` | u16 off | cond → |
| 47 | `JUMP_IF_FALSE_KEEP` | u16 off | cond → cond (for `&&`) |
| 48 | `JUMP_IF_TRUE_KEEP` | u16 off | cond → cond (for `\|\|`) |
| 49 | `JUMP_IF_GHOST_KEEP` | u16 off | a → a (for `??` and `?.`) |
| 50 | `LOOP` | u16 back | — (ip -= back) |
| 51 | `CALL` | u8 argc | fn a1..an → result |
| 52 | `INVOKE` | u16 nameIdx, u8 argc | obj a1..an → result |
| 53 | `INVOKE_OG` | u16 nameIdx, u8 argc | obj a1..an → result |
| 54 | `CLOSURE` | u16 protoIdx, then n×(u8 isLocal, u8 index) | → closure |
| 55 | `RETURN` | — | value → (pops frame) |
| 56 | `BUILD_STASH` | u16 n | n values → stash |
| 57 | `BUILD_GROUPCHAT` | u16 n | 2n values → groupchat |
| 58 | `BUILD_STRING` | u16 n | n values → yapstring (interpolation) |
| 59 | `YAP` | u8 argc, u8 newline | n values → |
| 60 | `CHUCK` | — | value → (unwinds) |
| 61 | `TRY_PUSH` | u16 handlerOff, u16 finallyOff | — |
| 62 | `TRY_POP` | — | — |
| 63 | `SQUAD` | u16 nameIdx | → class |
| 64 | `METHOD` | u16 nameIdx | class closure → class |
| 65 | `INHERIT` | — | super class → super class |
| 66 | `IMPORT` | u16 pathIdx, u8 mode | → module \| bindings |
| 67 | `EXPORT` | u16 nameIdx | value → value |
| 68 | `ITER_NEW` | — | iterable → iterator |
| 69 | `ITER_NEXT` | u16 doneOff | iterator → iterator value |
| 70 | `HALT` | — | — |

Reserve 71–99 for future opcodes. **Never renumber.** Bump `BYTECODE_VERSION` if you must.

### 5.2 `.funnyc` file format

All multi-byte integers big-endian. `str` = u32 length + UTF-8 bytes.

```
magic          6 bytes   b"FUNNY\x00"
version        u16       BYTECODE_VERSION (starts at 1)
flags          u16       bit0 = has_debug_info
source_name    str
const_count    u32
consts         const_count × { tag u8, payload }
                 tag 0 = ghost        (no payload)
                 tag 1 = boolski      (u8)
                 tag 2 = int          (u8 sign, u32 byte_len, big-endian magnitude bytes)
                 tag 3 = float        (8 bytes, IEEE-754 big-endian)
                 tag 4 = yapstring    (str)
                 tag 5 = proto_ref    (u32 index into proto table)
proto_count    u32
protos         proto_count × {
                 name str
                 arity u8
                 default_count u8
                 is_variadic u8
                 upvalue_count u8
                 max_stack u16
                 local_count u8
                 code u32 len + bytes
                 line_count u32
                 lines line_count × { code_offset u32, line u32, col u32 }
               }
entry_proto    u32
```

### 5.3 `.funnypak` (linked bundle) format

```
magic          9 bytes   b"FUNNYPAK\x00"
version        u16
module_count   u32
modules        module_count × { logical_name str, funnyc u32 len + bytes }
entry_name     str
```

### 5.4 Native executable layout

```
[ frozen runtime stub binary bytes ][ .funnypak bytes ][ payload_len u64 ][ b"FUNNYYEET" 9 bytes ]
```
Trailer is exactly 17 bytes. The stub reads its own file (`sys.executable` when frozen), seeks to
`filesize - 17`, validates the magic, reads `payload_len`, seeks to `filesize - 17 - payload_len`,
loads the `.funnypak`, and runs it. No temp files, no extraction.

---

## 6. Milestones

Each milestone: **Goal → Tasks → Acceptance**. Do not proceed until Acceptance passes.

---

### M0 — Scaffolding

**Goal:** an installable, importable, testable empty project.

**Tasks**
1. `git init`; create `.gitignore` (`__pycache__/`, `*.pyc`, `build/`, `dist/`, `*.egg-info/`, `.pytest_cache/`, `*.funnyc`, `*.funnypak`, `*.exe`).
2. `pyproject.toml`: name `funnylang`, version `0.1.0`, requires-python `>=3.10`,
   `[project.scripts] funny = "funnylang.cli:main"`, dev extras `pytest`, `pyinstaller`.
   Zero runtime dependencies for the core (stdlib `internet` module may use `urllib` only — no `requests`).
3. `funnylang/__init__.py` with `__version__ = "0.1.0"` and `BYTECODE_VERSION = 1`.
4. `funnylang/__main__.py` → `from .cli import main; main()`.
5. Temporary `cli.py` that prints the banner and exits 0.
6. `pytest.ini` with `testpaths = tests`.
7. `CHANGELOG.md`.
8. The banner (used by `funny` with no args and by `funny vibe`):

```
   ███████╗██╗   ██╗███╗   ██╗███╗   ██╗██╗   ██╗
   ██╔════╝██║   ██║████╗  ██║████╗  ██║╚██╗ ██╔╝
   █████╗  ██║   ██║██╔██╗ ██║██╔██╗ ██║ ╚████╔╝
   ██╔══╝  ██║   ██║██║╚██╗██║██║╚██╗██║  ╚██╔╝
   ██║     ╚██████╔╝██║ ╚████║██║ ╚████║   ██║
   ╚═╝      ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═══╝   ╚═╝
        FunnyLang v0.1.0 — it compiles. somehow.
```

**Acceptance**
```powershell
pip install -e .
funny            # prints banner, exit 0
pytest -q        # 0 tests, exit 0
```

---

### M1 — Lexer

**Goal:** `.funny` source → token stream, with precise spans and funny lex errors.

**Tasks**
1. `source.py`: `SourceFile(path, text)` with `line_starts`, `line_col(offset)`, `line_text(n)`,
   and `Span(start, end, line, col)`.
2. `tokens.py`: `TokenKind` enum covering every literal, operator, keyword, `NEWLINE`, `EOF`.
   `KEYWORDS: dict[str, TokenKind]` populated from §3.3 — all of them, including the reserved list.
3. `lexer.py`:
   - Number literals incl. `_` separators, hex/binary/octal, floats, exponents.
   - Strings: `"`, `'`, `"""`, escapes, `\u{...}`.
   - Interpolated strings with backticks: emit `TEMPLATE_START`, then alternating `TEMPLATE_STRING`
     and a nested token run for each `{...}` expression, then `TEMPLATE_END`.
     **Implementation note:** lex the whole template into a list of `(literal | sub-token-list)` parts
     and attach that to a single `TEMPLATE` token. Simpler than re-entrant lexing and the parser can
     recursively parse each part's token list.
   - Comments `//` and nested `/* */`.
   - Paren-depth counter: while `depth > 0` (from `(`, `[`, and `{` **only when the `{` opens a map
     literal or is inside an expression context** — track this by a stack pushed by the parser? No.
     **Decision:** the lexer emits NEWLINE always; the *parser* skips NEWLINE tokens whenever it is
     inside an expression, an argument list, or a bracketed literal. This keeps the lexer
     context-free. Implement `Parser.skip_newlines()` and call it at the right points.
   - Emit `NEWLINE` for `\n` (and `\r\n`); never two in a row.
   - On a bad character: raise `LexerSaidNah` with the span.
4. `tests/test_lexer.py` and `tests/conftest.py` (`lex(src)` helper).

**Acceptance**
- ≥ 25 lexer tests covering every token kind, every numeric form, every escape, nested block
  comments, template strings, and 4 error cases.
- `pytest -q` green.

---

### M2 — AST + Parser

**Goal:** tokens → AST for the entire grammar of §3, with multi-error recovery.

**Tasks**
1. `ast_nodes.py` — frozen dataclasses, every node carries `span`:
   - Expressions: `Literal`, `TemplateString`, `Identifier`, `Unary`, `Binary`, `Logical`, `Assign`,
     `Ternary`, `Call`, `Index`, `Slice`, `Get`, `SafeGet`, `Set`, `SetIndex`, `StashLit`,
     `GroupChatLit`, `Lambda`, `Me`, `Og`, `Pipe`, `Coalesce`.
   - Statements: `VarDecl`, `ConstDecl`, `FuncDecl`, `SquadDecl`, `Block`, `If`, `While`, `ForRange`,
     `ForEach`, `Return`, `Break`, `Continue`, `Try`, `Chuck`, `Import`, `Export`, `Yap`, `ExprStmt`,
     `Vibe` (no-op), `Program`.
2. `parser.py`:
   - Recursive descent for statements, **Pratt/precedence-climbing for expressions** using the table
     in §3.4. Encode precedence as a dict `TokenKind -> (lbp, rbp, is_right_assoc)`.
   - Assignment targets validated after parsing (`Identifier`, `Index`, `Get` only) — anything else
     is `ParserHadAStroke` "you can't assign to that. that's not a place."
   - Default params, variadics, trailing commas allowed everywhere.
   - Error recovery: `synchronize()` skips to the next NEWLINE/`;`/statement-keyword. Collect up to
     5 errors, then raise a `ParseErrorBundle`.
   - Reserved-word check: a reserved-for-future keyword produces
     `ParserHadAStroke: "\`{kw}\` isn't a thing yet. i put it in the lexer to be aspirational."`
3. `tests/test_parser.py` — assert on a compact AST repr. Add a `dump_ast(node) -> str` S-expression
   printer in `ast_nodes.py` and golden-test against it. ≥ 35 tests including precedence
   (`2 + 3 * 4 ** 2`), right-assoc `**` and `=`, `?.` chains, pipes, slices, all statement forms,
   and 6 error-recovery cases.

**Acceptance**
```powershell
pytest -q tests/test_parser.py
```
plus: `dump_ast(parse("1 + 2 * 3"))` == `(+ 1 (* 2 3))` and `dump_ast(parse("2 ** 3 ** 2"))` == `(** 2 (** 3 2))`.

---

### M3 — Resolver

**Goal:** static scope analysis before codegen. Catches name errors at compile time.

**Tasks**
1. `resolver.py` — a visitor producing, for each `Identifier`, a resolution:
   `LOCAL(slot)`, `UPVALUE(idx)`, `GLOBAL(name)`, or error.
2. Track: function scopes, block scopes, loop depth (for `bail`/`nvm` validation), `squad` depth
   (for `me`/`og` validation), const-ness.
3. Compile-time errors emitted here:
   - `WhoDis` for an unknown identifier **only when it isn't a known global/builtin/stdlib name** —
     otherwise defer to runtime (globals can be defined by imports). Include Levenshtein "did you mean".
   - `ImmutableVibes` on assigning a `deadass`.
   - `ParserHadAStroke` for `bail`/`nvm` outside a loop ("bail from what, exactly?"),
     `bounce` at top level ("bounce to where? this is the top."), `me` outside a squad.
   - Shadowing a variable in the same scope: `ParserHadAStroke` "you already declared `{x}` right there. scroll up."
4. Upvalue capture: mark captured locals so the compiler emits `CLOSE_UPVAL` at scope exit.
5. `tests/test_resolver.py` — ≥ 18 tests.

**Acceptance:** resolver tests green; all M1/M2 tests still green.

---

### M4 — Bytecode compiler + serializer + disassembler

**Goal:** AST → `Chunk` → `.funnyc` bytes → readable disassembly.

**Tasks**
1. `chunk.py`: `ConstPool` (dedupes by `(type, value)`), `FunctionProto`, `LineTable`,
   `Chunk.write(op, *operands, span)`.
2. `opcodes.py`: `class Op(IntEnum)` exactly matching §5.1, plus `OPERANDS: dict[Op, tuple[int,...]]`
   giving operand byte widths, used by both the disassembler and the VM's decoder. **Single source of
   truth — the VM must not hardcode widths.**
3. `compiler.py`:
   - One `FunctionCompiler` per function, with a parent chain for upvalues.
   - Jump patching helpers: `emit_jump(op) -> patch_site`, `patch(site)`, `emit_loop(start)`.
     If a jump distance exceeds 65535, raise an internal error `"your function is too long. seek help."`
   - `&&` / `||` via `JUMP_IF_FALSE_KEEP` / `JUMP_IF_TRUE_KEEP` + `POP`.
   - `??` and `?.` via `JUMP_IF_GHOST_KEEP`.
   - `grind ... from ... to ... step` desugars to a hidden local counter + `LOOP`; the loop variable
     is a fresh local per iteration **only if captured** (else reuse the slot).
   - `grind ... in ...` uses `ITER_NEW` / `ITER_NEXT`.
   - `sketchy/my_bad/regardless` → `TRY_PUSH handler, finally` + `TRY_POP`. The VM maintains a
     per-frame handler stack (§M5).
   - `bail` / `nvm` record patch sites per loop, patched at loop end.
   - Compile-time constant folding for literal arithmetic (`2 + 3` → `CONST 5`). Keep it simple:
     fold only when both operands are numeric literals and the op can't throw.
4. `serializer.py`: `dump_funnyc(chunk) -> bytes`, `load_funnyc(bytes) -> Chunk`,
   `dump_funnypak(modules, entry)`, `load_funnypak(bytes)`. Exactly §5.2/§5.3. Validate magic and
   version on load; version mismatch → `"this bytecode is from a different era. recompile it."`
5. `disasm.py`: output like
   ```
   == bet main (arity 0) ==
   0000  line 1  CONST        0    ; "yo sup world"
   0003          DEF_GLOBAL   1    ; greeting
   0006  line 2  GET_GLOBAL   1    ; greeting
   0009          YAP          1 1
   0012          GHOST
   0013          RETURN
   ```
6. `tests/test_compiler.py` (assert opcode sequences for ~20 snippets),
   `tests/test_serializer.py` (round-trip every const tag, incl. a 500-digit int and `float('inf')`;
   assert `load(dump(c))` produces an identical disassembly).

**Acceptance:** disassembly of `examples/hello.funny` matches a golden file; round-trip tests green.

---

### M5 — The VM

**Goal:** execute bytecode. This is the heart.

**Tasks**
1. `values.py`:
   - `FunnyValue` union: Python `int`/`float` for `numba`, `str` for `yapstring`, `bool` for `boolski`,
     a singleton `GHOST` for `ghost`, `Stash` (wraps `list`), `GroupChat` (wraps `dict`),
     `Closure`, `NativeFn`, `Squad`, `Instance`, `BoundMethod`, `Module`, `Iterator`.
   - `type_name(v) -> str` returning the funny names (`numba`, `yapstring`, …).
   - `is_truthy(v)`, `funny_eq(a, b)`, `to_display(v)` (what `yap` prints), `to_repr(v)` (what `sheesh`
     prints — quotes strings, shows types).
     `to_display` of a `Stash` → `[1, 2, 3]`; of a `GroupChat` → `{"a": 1}`; of an `Instance` → calls
     `to_yap()` if defined else `<Dog instance>`; of a `bet` → `<bet add/2>`; of `ghost` → `ghost`.
2. `vm.py`:
   - `Frame(closure, ip, slot_base, handlers)`. `VM.stack: list`, `VM.frames: list[Frame]`.
   - Main dispatch loop: `while True:` reading `op = code[ip]` then a dict/`match` dispatch to methods.
     **Performance target:** naive fib(25) in under 3 seconds. If you exceed it, switch dispatch to a
     precomputed list-indexed jump table of bound methods and inline the hot arithmetic ops.
   - Open-upvalue list per VM, closed on `CLOSE_UPVAL` and on `RETURN` (Crafting Interpreters §25.4).
   - `CALL` handles `Closure`, `NativeFn`, `Squad` (construction), `BoundMethod`. Anything else →
     `NotACallableRizz`.
   - Arity check with defaults and variadics → `WrongNumberOfHomies`.
   - Frame limit 10_000 → `TooDeepBro`.
   - **Exception model:** `TRY_PUSH` pushes `(handler_ip, finally_ip, stack_depth, frame_index)` onto
     the current frame's handler stack. A thrown `FunnyError` unwinds frames until a handler is found,
     truncating the value stack to the recorded depth and binding the error object to the catch slot.
     `regardless` blocks run on both the normal and the exceptional path — compile the finally body
     **twice** (once inline on the normal path, once on the unwind path) or use a `finally_ip` return
     address; pick one, document it in §16. Simplest correct approach: duplicate the finally body.
   - `ITER_NEW` produces a Python generator wrapped in `Iterator`; `ITER_NEXT` advances or jumps.
   - `yap`/`yeet` write to `vm.stdout` (injectable — tests capture it).
3. `tests/test_vm.py` + the golden `tests/lang/` runner in `conftest.py`:
   `run_funny(src) -> str` compiles and runs in-process, returning captured stdout.
   The golden runner discovers every `tests/lang/*.funny`, runs it, and compares against
   `<name>.expected` (exact match; if the expected file's first line is `!ERROR <Flavor>`, assert the
   run raised that error flavor).
4. Write at least 30 `tests/lang/` programs covering: arithmetic and precedence, string ops,
   truthiness, all control flow, closures and counters, recursion (fib, ackermann small),
   arrays and maps, slicing, ternary, pipes, `??`, `?.`, try/catch/finally ordering, break/continue
   in nested loops, shadowing, variadics, defaults.

**Acceptance**
```powershell
pytest -q                       # everything green
python -c "from tests.conftest import run_funny; print(run_funny(open('examples/fib.funny').read()))"
```
and `funny run examples/fizzbuzz.funny` produces correct FizzBuzz 1..100 (wire a minimal `run` in
`cli.py` now; full CLI lands in M8).

---

### M6 — Stdlib + the error system

**Goal:** the language becomes usable and the errors become the reason people share it.

**Tasks**
1. `errors.py`: the full class hierarchy from §4.1 + `render_diagnostic(err, source_registry) -> str`
   implementing the §4.2 layout exactly, with `FUNNY_SERIOUS=1` support and TTY color detection.
2. Wire every raise site in lexer/parser/resolver/vm to a specific flavor with a `roast` and, where
   listed in §4.2, a `hint`. Levenshtein suggestion helper in `errors.py`.
3. `stdlib/builtins.py` — always-in-scope globals:
   `how_thicc(x)`, `what_is_it(x)`, `to_yap(x)`, `to_numba(x)`, `to_int(x)`, `sheesh(x)` (debug repr →
   stdout, returns x), `no_cap(cond, msg?)` (assert → `SkillIssue`), `ask(prompt?)` (stdin),
   `dip(code?)` (exit), `the_args()` (argv as a stash), `combo(...fns)`, `identity(x)`,
   `range_stash(a, b, step?)`, `zip_em(a, b)`, `enumerate_em(a)`, `deep_clone(x)`.
4. Stdlib modules (each is a `Module` of `NativeFn`s; every function validates arg types and raises
   `TypeVibeMismatch` with a roast naming the function):
   - **`mafs`** — `sqrt abs floor ceil round min max pow log log2 log10 exp sin cos tan atan2 hypot
     clamp sign gcd lcm is_prime factorial`; consts `skibidi_pi` (π), `e`, `phi`, `infinity`, `nan`.
   - **`yapper`** — `split join SCREAM whisper trim ltrim rtrim replace contains starts_with ends_with
     index_of slice reverse repeat pad_left pad_right chars ord_of chr_of format is_numba
     lines words title_case sarcasm_case` (`sarcasm_case("hello") == "hElLo"`).
   - **`stash`** — free-function forms of every `stash` method plus `sort_by(arr, keyfn)`,
     `group_by`, `unique`, `flatten`, `chunk(arr, n)`, `sum_up`, `shuffle_it`.
   - **`groupchat`** — `keys values pairs has get set remove merge invert from_pairs`.
   - **`rizz`** — `roll(a,b)` int inclusive, `float_roll()`, `pick(stash)`, `shuffle(stash)`,
     `coinflip()`, `seed(n)`, `uuid()`, `gamble(odds)` (true with probability `odds`).
   - **`filez`** — `slurp(path)`, `yeet_out(path, text)`, `append_to(path, text)`, `exists`,
     `delete` (`obliterate`), `list_dir`, `mkdir`, `read_bytes` (→ stash of ints), `write_bytes`,
     `abs_path`, `join_path`, `dir_of`, `base_of`, `ext_of`.
     **Required by M12 self-hosting — do not stub these.**
   - **`clock`** — `now()` epoch float, `now_ms()`, `touch_grass(seconds)` (sleep),
     `stopwatch()` → closure returning elapsed, `date_yap(fmt?)`.
   - **`sus`** (reflection) — `type_of(x)`, `fields_of(instance)`, `is_a(x, "yapstring")`,
     `stack_trace()`, `dump(x)`.
   - **`computer`** — see §7.9 below.
   - **`internet`** — see §7.10 below.
5. `stdlib/__init__.py`: `STDLIB: dict[str, Callable[[], Module]]`, lazily built.
6. `tests/test_stdlib.py` (≥ 60 assertions), `tests/test_errors.py` (assert the exact rendered
   diagnostic for 12 error kinds under `FUNNY_SERIOUS=1`, and assert emoji/roast presence without it).

**Acceptance:** all tests green; `funny run examples/errors.funny` prints a correctly-pointed
diagnostic with a caret under the right column.

---

### M7 — Modules

**Goal:** `gimme` works for user files and stdlib, with caching and cycle detection.

**Tasks**
1. `modules.py`: `ModuleResolver` implementing §3.8 resolution order, a `loaded: dict[str, Module]`
   cache keyed by `Path.resolve()`, and a `loading: list[str]` stack for cycle detection.
2. `IMPORT` opcode modes: `0` = whole module bound to a name, `1` = named bindings
   (the compiler emits a `BUILD_STASH` of names first), `2` = stdlib bare import.
3. Module top-level code runs once in its own global namespace; `flex`ed names become module members.
   Non-`flex`ed names are private.
4. Circular import → `ImportSkillIssue` listing the cycle: `a.funny → b.funny → a.funny`.
5. `examples/modules/` demo. `tests/test_modules.py` (≥ 12 tests incl. cycle, missing file,
   named import of a non-exported name → "`{n}` isn't flexed. it's shy.", stdlib import, alias, `FUNNYPATH`).

**Acceptance:** `funny run examples/modules/main.funny` works; module tests green.

---

### M8 — The CLI

**Goal:** the `funny` command, complete.

**Tasks** — implement these subcommands in `cli.py`:

| Command | Behavior |
|---|---|
| `funny run <file>` | Accepts `.funny` (compile in memory) or `.funnyc`/`.funnypak`. Extra argv after `--` goes to `the_args()`. |
| `funny build <file> -o <out>` | Compile + link all imported modules into a `.funnypak` (or `.funnyc` if single-module and `-o` ends in `.funnyc`). |
| `funny yeet <file> -o <out>` | Native executable. See M10. |
| `funny vibe` | REPL. See below. |
| `funny xray <file>` | Disassemble. `--tokens` dumps tokens, `--ast` dumps the AST, `--pak` inspects a bundle. |
| `funny fmt <file>` | Canonical formatter: 4-space indent, `{` on the same line, spaces around binary ops, `--check` exits 1 if unformatted. Implement as an AST pretty-printer. |
| `funny test <dir>` | Runs every `*.funny` in a dir, compares to `*.expected` — the language's own test runner. |
| `funny bootstrap` | See M12. |

Global flags: `--version`, `--serious` (sets `FUNNY_SERIOUS`), `--no-color`, `--time` (prints
`compiled in 4ms, ran in 12ms. blazingly fast (probably)`), `--vibes` (verbose: prints a random
loading quip per phase from a fixed list of ~15).

**REPL (`funny vibe`) requirements:** persistent global scope across inputs; multi-line continuation
when braces/parens are unbalanced; last expression value auto-printed with `to_repr`; `.help`,
`.exit`, `.clear`, `.xray <expr>`, `.time <expr>` meta-commands; history via `readline` when
available; errors print the diagnostic and keep the session alive. Prompt: `funny> ` / `.....> `.

`tests/test_cli.py`: invoke via `subprocess` for at least `run`, `build`, `xray`, `fmt --check`,
`--version`, and a failing program's exit code.

**Acceptance:** every command in the table runs; `funny fmt examples/fizzbuzz.funny --check` exits 0
after formatting the examples.

---

### M9 — Classes (`squad`)

**Goal:** §3.7 fully working.

**Tasks**
1. Parser: `SquadDecl` with `spawn`, methods, `inherits`.
2. Resolver: `me`/`og` binding, method scopes.
3. Compiler: `SQUAD`, `METHOD`, `INHERIT`, `INVOKE`, `INVOKE_OG`.
4. VM: `Squad`, `Instance` (fields in a dict), `BoundMethod`, method lookup walking the superclass
   chain, magic methods (`to_yap`, `how_thicc`, `get_it`, `set_it`, `same_energy`).
5. Errors: calling an undefined method → `WhoDis` "`{Class}` doesn't do `{m}`. that's not its thing."
   Accessing an unset field → `GhostError`? **No** — return `ghost`, but *setting* a field on a
   non-instance → `TypeVibeMismatch`.
6. `examples/squads.funny` + ≥ 12 `tests/lang/` programs.

**Acceptance:** inheritance, `og.spawn(...)`, method override, magic `to_yap` all pass.
**Constraint:** the M12 self-hosted compiler must NOT use `squad` — it stays on the subset in §8.

---

### M10 — Native executable packaging

**Goal:** `funny yeet hello.funny -o hello.exe` → a standalone binary.

**Tasks**
1. `stub_main.py`: reads its own trailing payload (§5.4), loads the `.funnypak`, runs the VM,
   renders diagnostics, exits with the right code. If no payload is found, it prints
   `"this stub is naked. it has no program. sad."` and exits 2.
2. Build the stub once, cached in `build/stub/`:
   ```powershell
   pip install pyinstaller
   pyinstaller --onefile --name funnyrt --distpath build/stub --workpath build/_work `
       --specpath build/_spec --console `
       --hidden-import funnylang.stdlib.mafs `
       # ...one --hidden-import per stdlib module (they're loaded dynamically)
       funnylang/stub_main.py
   ```
   Wrap this in `packager.py::ensure_stub()` which builds on first use and caches by a hash of
   `funnylang/**/*.py` + Python version, so the stub rebuilds only when the runtime changes.
3. `packager.py::yeet(pak_bytes, out_path)`: copy the stub, append payload + trailer, `chmod +x` on POSIX.
4. `funny yeet` flags: `--icon <path>` (Windows, via PyInstaller `--icon` on stub rebuild),
   `--console/--no-console`, `--keep-stub`, `--rebuild-stub`.
5. Size expectation: ~10–15 MB. Print, after a successful yeet:
   `yeeted 12.4 MB of pure comedy into hello.exe. it runs anywhere. no python. no cap.`
6. `tests/test_packager.py`: mark `@pytest.mark.slow`; builds the stub once (session fixture),
   yeets `hello.funny`, runs the exe via `subprocess`, asserts stdout. Skip if PyInstaller is absent.

**Acceptance**
```powershell
funny yeet examples/hello.funny -o hello.exe
.\hello.exe                     # => yo sup world
```
and the exe still works after moving it to another directory.

---

### M11 — Hardening pass

**Goal:** stop the fun from crashing.

**Tasks**
1. **No Python traceback may ever escape to the user.** Wrap the CLI top level: any unexpected
   `Exception` prints
   ```
   ☠️  COMPILER SKILL ISSUE  ☠️
   the compiler itself broke. that's on us, not you.
   please open an issue with this file and the goofy details below.
   ```
   followed by the traceback, and exits 70.
2. Recursion: the VM's Python-level recursion (native fns calling back into the VM) must not blow
   Python's stack. Set `sys.setrecursionlimit(20000)` and cap VM frames at 10_000.
3. Fuzz: `tests/test_fuzz.py` — 2000 random token soups and 500 random ASTs through the parser and
   compiler; assert only `FunnyError` subclasses escape, never `AssertionError`/`IndexError`/etc.
4. Deep structures: `to_display` of a self-referential stash must print `[...]`, not recurse forever.
5. Unicode: identifiers, strings, and file paths with emoji and CJK all work end to end.
6. Large programs: generate a 50k-line `.funny` file, compile it, assert < 10 s and no jump-offset
   overflow (this will find your `>65535` jump bugs — fix by adding `JUMP_LONG`/`LOOP_LONG` at
   opcodes 71/72 if needed, and record it in §16).
7. `--time` numbers are real, not fabricated.

**Acceptance:** fuzz suite green; `pytest -q` fully green including slow tests.

---

### M12 — Self-hosting (THE BIG ONE)

**Goal:** a FunnyLang compiler written in FunnyLang, compiled by the Python compiler, that reproduces
itself byte for byte.

**Read this carefully:** the *compiler* becomes self-hosted. The *VM* does not and cannot — something
must execute bytecode natively, and that stays Python (frozen into the stub). That is the correct,
honest architecture; do not attempt a FunnyLang VM. State this plainly in the README.

**Tasks**
1. **Freeze the self-hosting subset.** `selfhost/*.funny` may use only:
   `yo`, `deadass`, `bet`, `lowkey`, `bounce`, `sus`/`kinda_sus`/`nah`, `bruh`, `grind` (both forms),
   `bail`, `nvm`, `sketchy`/`my_bad`, `chuck`, `gimme`/`flex`, `yap`, all operators, `stash`,
   `groupchat`, `yapstring`, `numba`, `boolski`, `ghost`, and the stdlib modules
   `yapper`, `stash`, `groupchat`, `mafs`, `filez`, `sus`.
   **No `squad`. No `regardless`. No template strings inside the emitter's hot loops** (keep it boring
   so bugs are findable).
2. Port, in this order, each file passing its own `.funny` tests before the next:
   - `selfhost/prelude.funny` — assert helpers, byte-buffer helpers (a `stash` of ints 0–255),
     `u8/u16/u32/u64` big-endian writers, varint-free int encoding matching §5.2 tag 2.
   - `selfhost/lexer.funny` — port of `lexer.py`. Tokens are `groupchat` records:
     `{"kind": "IDENT", "text": "x", "line": 3, "col": 5, "start": 40}`.
   - `selfhost/parser.funny` — port of `parser.py`. AST nodes are `groupchat` records with a `"node"` key.
   - `selfhost/compiler.funny` — resolver + codegen merged (the resolver's info can be computed inline;
     keep the two-pass structure if it's clearer).
   - `selfhost/emitter.funny` — writes `.funnyc` bytes per §5.2 via `filez.write_bytes`.
   - `selfhost/funnyc.funny` — CLI: `the_args()` → in-path, out-path, compile, write.
3. **Byte-exactness discipline.** For stage2 output to equal stage3 output, the two compilers must
   agree on every detail. The dangerous ones — enforce each explicitly and test each:
   - **Constant pool ordering:** first-use order, dedupe on `(tag, value)`. `1` (int) and `1.0`
     (float) are *distinct* constants. Never dedupe across tags.
   - **Dict/map iteration order:** insertion order in both (`groupchat` is insertion-ordered by spec).
   - **Float formatting:** never round-trip floats through strings; write raw IEEE-754 bytes.
   - **Big ints:** identical sign/length/magnitude encoding.
   - **Line table:** identical `(offset, line, col)` triples — so both compilers must attribute spans
     to the *same* token. Port span assignment literally, node by node.
   - **Constant folding:** the folding rules in M4 must be reproduced exactly, or disabled in both.
     **Decision: keep folding, port it exactly, and add a `--no-fold` flag to both for debugging.**
   - **Proto ordering:** functions are emitted in source order, depth-first.
4. `funny bootstrap` subcommand:
   ```
   stage1 = the Python compiler
   stage2.funnyc  = stage1.compile(selfhost/funnyc.funny)      # a FunnyLang compiler, in bytecode
   stage3.funnyc  = run(stage2.funnyc, args=[selfhost/funnyc.funny])   # it compiles itself
   stage4.funnyc  = run(stage3.funnyc, args=[selfhost/funnyc.funny])   # and again
   assert stage3 == stage4   bytes-identical  ->  FIXED POINT REACHED
   ```
   (stage2 ≠ stage3 is *expected and fine* — different compilers, same language. The fixed point is
   stage3 == stage4. Do not chase stage2 == stage3.)
   Flags: `--verify` (the above, exit 1 on mismatch), `--keep` (write artifacts to `build/bootstrap/`),
   `--diff` (on mismatch, disassemble both and print the first divergent instruction with context —
   you WILL need this; build it before you need it).
5. Cross-validation: run the entire `tests/lang/` suite through the **stage3** compiler and assert
   identical stdout to the stage1 compiler. This is the real proof it works.
6. Success output:
   ```
   🥁 stage 2... compiled.
   🥁 stage 3... compiled by stage 2.
   🥁 stage 4... compiled by stage 3.

   stage3 and stage4 are byte-identical (41,208 bytes).
   FunnyLang now compiles FunnyLang. we are so back. 🏆
   ```
7. `tests/test_bootstrap.py` — `@pytest.mark.slow`, runs `--verify` and the cross-validation.

**Expect this milestone to take as long as M1–M8 combined. Budget accordingly. When stage3 ≠ stage4,
`--diff` tells you exactly which instruction diverged — start there, every time.**

**Acceptance**
```powershell
funny bootstrap --verify        # exit 0, fixed point reached
pytest -q -m slow               # green
```

---

### M13 — Polish, docs, examples, release

**Tasks**
1. `docs/LANGUAGE.md` — the complete user reference, generated by hand from §3, with runnable examples.
2. `docs/BYTECODE.md` — §5 plus a worked disassembly walkthrough.
3. `docs/STDLIB.md` — every module, every function, signature + one-line description + example.
   Generate the skeleton from the `NativeFn` registry so it can't drift.
4. `README.md` — the funny front door: the banner, a 10-line taste of the language, install,
   the bootstrap flex, an honest "what this is / what this isn't" section.
5. Examples: all files listed in §2, each with a matching `.expected`, each passing `funny test examples/`.
6. `examples/chaos.funny` must exercise `computer.explode()`, `internet.go_brrrr()`,
   `rizz.gamble()`, `yapper.sarcasm_case()`, and a deliberate caught error.
7. Version → `1.0.0`. Update `CHANGELOG.md`.
8. `.github/workflows/ci.yml`: matrix over `windows-latest`, `ubuntu-latest`, `macos-latest` ×
   Python 3.10/3.11/3.12 — `pytest -q`, then `funny bootstrap --verify`, then `funny yeet` + run the
   exe, then upload the exe as an artifact.

**Acceptance:** `funny test examples/` passes; CI green on all three OSes; README renders correctly.

---

### M14 — Stretch (only after M13 is green)

Pick from, in this order: `match_this` pattern matching · a bytecode optimizer pass
(peephole: `CONST/POP` elimination, jump threading, `GET_LOCAL x; GET_LOCAL x` → `GET_LOCAL x; DUP`) ·
`vibin` generators via `yield_lol` · a `.funnyc` → C transpiler for real native speed ·
a VS Code extension with syntax highlighting · a package registry called **the group chat**
(`funny gimme-from-the-internet <pkg>`).

---

## 7. Signature stdlib bits (must match exactly)

### 7.9 `computer` module

```funny
gimme computer
computer.explode()          // ASCII mushroom cloud, "🍄 kernel panic: user was cringe", exit 69
computer.flex()             // prints OS, CPU count, RAM, python version — "your rig: mid"
computer.ram()              // → numba, bytes of RAM
computer.yeet_to_void(x)    // accepts anything, returns ghost, does nothing. the /dev/null of funcs
computer.beep()             // terminal bell
computer.clear()            // clears the screen
computer.uptime()           // → numba seconds
computer.blue_screen()      // full-screen blue ANSI + "FUNNYLANG_FAULT_NOT_HANDLED", exit 1
```

`explode()` prints (then exits 69):
```
                  _.-^^---....,,--
              _--                  --_
             <                        >)
             |                         |
              \._                   _./
                 ```--. . , ; .--'''
                       | |   |
                    .-=||  | |=-.
                    `-=#$%&%$#=-'
                       | ;  :|
              _____.,-#%&$@%#&#~,._____

  🍄 KERNEL PANIC: user was cringe
  computer.explode() called at chaos.funny:12
  it's over. exit code 69.
```

`blue_screen()` and `explode()` must be genuinely harmless: no subprocess, no filesystem writes,
no real system calls beyond printing and `sys.exit`.

### 7.10 `internet` module

```funny
gimme internet
internet.go_brrrr(url)         // GET, returns a groupchat {"status":200,"body":"...","headers":{...}}
internet.go_brrrr(url, opts)   // opts: {"method":"POST","body":"...","headers":{...},"timeout":10}
internet.is_it_up(url)         // → boolski
internet.download(url, path)   // saves to disk, returns bytes written
internet.speed_test()          // downloads a small file, reports "your internet: {n} mbps. mid."
internet.ping(host)            // → numba ms (TCP connect time to :80, no raw ICMP)
```

Implementation: `urllib.request` only, 10 s default timeout, no third-party deps.
Network failures raise `SkillIssue` with flavor text `"the internet said no. 🚫"` — never a Python
traceback. All network functions must be skippable in tests (`FUNNY_NO_NET=1` makes them raise a
clean `SkillIssue`, and CI sets it).

---

## 8. The self-hosting subset (quick reference for M12)

Legal in `selfhost/`: `yo deadass bet lowkey bounce sus kinda_sus nah bruh grind bail nvm sketchy
my_bad chuck gimme flex yap` · all operators · `stash groupchat yapstring numba boolski ghost` ·
stdlib `yapper stash groupchat mafs filez sus`.

Illegal in `selfhost/`: `squad inherits me og spawn regardless` · template strings · `?.` · `|>` ·
slices with steps · stdlib `rizz clock computer internet`.

Reason: every feature the self-hosted compiler uses is a feature that must be *bug-free* for
bootstrapping to work at all. Keep the surface small.

---

## 9. Testing strategy

| Layer | Location | What it proves |
|---|---|---|
| Unit | `tests/test_*.py` | each phase in isolation |
| Golden E2E | `tests/lang/*.funny` + `.expected` | the whole pipeline, per feature |
| Error goldens | `tests/lang/err_*.funny` with `!ERROR <Flavor>` | diagnostics fire correctly |
| Fuzz | `tests/test_fuzz.py` | nothing but `FunnyError` escapes |
| Packaging | `tests/test_packager.py` (slow) | the exe actually runs |
| Bootstrap | `tests/test_bootstrap.py` (slow) | self-hosting fixed point |
| Cross-compiler | in `test_bootstrap.py` | stage1 and stage3 agree on all of `tests/lang/` |

Mark slow tests `@pytest.mark.slow`; register the marker in `pytest.ini`. Default `pytest -q` runs
everything; CI runs everything.

Target: **≥ 90% line coverage** on `funnylang/` excluding `stub_main.py`. Add `pytest-cov` in dev extras.

---

## 10. Performance targets (soft, but check them)

- `fib(25)` recursive: < 3 s.
- Compile 10k lines: < 2 s.
- `funny run hello.funny` cold: < 300 ms.
- `hello.exe` startup: < 500 ms.

If the VM misses these, the fix is dispatch-table dispatch, avoiding attribute lookups in the loop,
and hoisting `code`/`consts` into locals — not a rewrite.

---

## 11. Non-goals (do not build these)

- A FunnyLang-hosted VM (see M12 note).
- Threads, async, or a GC (Python's refcounting is the GC).
- A type checker. FunnyLang is dynamically typed and proud.
- Native code generation (that's M14 stretch territory).
- Anything requiring a third-party runtime dependency.

---

## 12. Order of operations — the short version

```
M0 scaffold → M1 lexer → M2 parser → M3 resolver → M4 compiler/serializer/disasm
   → M5 VM → M6 stdlib+errors → M7 modules → M8 CLI → M9 classes
   → M10 native exe → M11 hardening → M12 self-hosting → M13 docs/release → M14 stretch
```

Do not start M12 before M11 is green. Self-hosting on a buggy foundation is a two-week debugging
sentence.

---

## 13. First file to write (so M1 has a target)

`examples/hello.funny`:
```funny
// hello.funny — the first program
yo greeting = "yo sup world"
yap greeting
```
`examples/hello.expected`:
```
yo sup world
```

## 14. A larger taste (write this as `examples/chaos.funny` in M13)

```funny
gimme computer
gimme rizz
gimme yapper
gimme mafs

deadass VERSION = "1.0.0"

squad Vibe {
    spawn(name, level) {
        me.name = name
        me.level = level
    }
    bet check() {
        sus (me.level > 9000) {
            bounce `{me.name} is ASTRONOMICAL`
        } kinda_sus (me.level > 50) {
            bounce `{me.name} is solid`
        } nah {
            bounce `{me.name} is, respectfully, mid`
        }
    }
    bet to_yap() { bounce `Vibe({me.name}, {me.level})` }
}

bet main() {
    yap "FunnyLang", VERSION, "— booting up"

    yo squad_list = [
        Vibe("gary", 9001),
        Vibe("dave", 72),
        Vibe("chad", 3)
    ]

    grind v in squad_list {
        yap "  ", v.check()
    }

    yo nums = range_stash(1, 21)
    yo evens = nums |> stash.vibe_check(lowkey (n) => n % 2 same_energy 0)
    yap "evens:", evens.join(", ")
    yap "sum:", evens.squish(lowkey (a, b) => a + b, 0)
    yap "sqrt of 144 is", mafs.sqrt(144), "obviously"

    sketchy {
        yo x = 1 / 0
    } my_bad (e) {
        yap "caught a", e.flavor, "->", e.message
    } regardless {
        yap "we move."
    }

    yap yapper.sarcasm_case("this language is very serious")

    sus (rizz.gamble(0.1)) {
        computer.explode()
    } nah {
        yap "you survived. this time."
    }
}

main()
```

---

## 15. Definition of Done (the whole project)

- [ ] `pytest -q` green on Windows, Linux, macOS × Python 3.10–3.12
- [ ] ≥ 90% coverage on `funnylang/`
- [ ] `funny bootstrap --verify` reaches a fixed point
- [ ] All of `tests/lang/` produces identical output under the stage1 and stage3 compilers
- [ ] `funny yeet examples/chaos.funny -o chaos.exe` → a working standalone binary
- [ ] No Python traceback is reachable from any user input
- [ ] Every error in §4.1 has a golden test asserting its rendered diagnostic
- [ ] `docs/LANGUAGE.md`, `docs/BYTECODE.md`, `docs/STDLIB.md` complete and accurate
- [ ] README explains honestly that the compiler self-hosts and the VM does not
- [ ] Reading the error messages out loud makes someone laugh

---

## 16. Change log (executing agent: append here)

Record every deviation from this spec, every `AGENT CHOICE` you resolved, and every opcode you added.
Format: `- [Mn] <what changed> — <why>`.

- (empty)
