# FunnyLang language reference

FunnyLang is a dynamically typed, bytecode-compiled scripting language with comedic/slang syntax.
Files use the `.funny` extension; the compiler produces `.funnyc` (a single compiled module) or
`.funnypak` (a linked multi-module bundle). Source is UTF-8, and emoji are legal in strings,
comments, and identifiers.

## Comments

```funny
// single line
/* block
   comment, and /* nested */ blocks are fine too */
```

## Literals

| Kind | Syntax | Runtime type |
|---|---|---|
| Integer | `42`, `-7`, `1_000_000`, `0xFF`, `0b1010`, `0o755` | `numba` |
| Float | `3.14`, `1e9`, `2.5e-3` | `numba` |
| String | `"hi"`, `'hi'`, `"""multi\nline"""` | `yapstring` |
| Interpolated string | `` `yo {name}, you are {age}` `` | `yapstring` |
| `fax` | boolean true | `boolski` |
| `cap` | boolean false | `boolski` |
| `ghost` | null | `ghost` |
| Array | `[1, 2, 3]` | `stash` |
| Map | `{"a": 1, "b": 2}` | `groupchat` |
| Function | `bet f() {}` / `lowkey (x) => x + 1` | `bet` |

String escapes: `\n \t \r \\ \" \' \0 \u{1F480}`, plus `\{` to put a literal `{` inside an
interpolated string.

Integers are arbitrary precision. Floats are IEEE-754 doubles. Mixing int and float promotes to
float. `/` always produces a float; `\` is floor division (not `//` — that's the line-comment
marker):

```funny
yap 7 / 2      // 3.5
yap 7 \ 2      // 3
yap 7 % 2      // 1
```

## Keywords

| FunnyLang | Means | Notes |
|---|---|---|
| `yo` | mutable variable | `yo x = 5` |
| `deadass` | constant | reassignment is a compile error |
| `yap` / `yeet` | print with newline | both are canonical, `yap a, b, c` space-joins them |
| `mumble` | print without newline | |
| `bet` | function declaration | `bet add(a, b) { bounce a + b }` |
| `lowkey` | lambda | `lowkey (x) => x * 2` or `lowkey (x) { bounce x * 2 }` |
| `bounce` | return | bare `bounce` returns `ghost` |
| `sus` / `kinda_sus` / `nah` | if / else-if / else | `sus (x > 3) { }` |
| `bruh` | while | `bruh (x < 10) { }` |
| `grind` | for (range or for-each) | see below |
| `from`, `to`, `step`, `in` | loop clauses | `from`/`to`/`step` are contextual keywords |
| `bail` / `nvm` | break / continue | |
| `sketchy` / `my_bad` / `regardless` | try / catch / finally | `my_bad (e) { }` |
| `chuck` | throw | `chuck "you fumbled it"` |
| `gimme` / `as` / `flex` | import / alias / export | see [Modules](#modules) |
| `squad` / `inherits` / `me` / `og` / `spawn` | class / extends / this / super / constructor | see [Classes](#classes-squad) |
| `fax` / `cap` / `ghost` | true / false / null | |
| `fr` / `orr` / `aint` | `&&` / `\|\|` / `!` aliases | |
| `same_energy` / `diff_energy` | `==` / `!=` aliases | |
| `vibe` | no-op statement | compiles to nothing |
| `async_ngl` | async function declaration | `async_ngl bet f() { }`, `async_ngl lowkey (x) => x` — see [Concurrency](#concurrency) |
| `await_fr` | await an `otw` | `await_fr p` — unary precedence |

Reserved for future use (lexed as keywords, the parser rejects them with a "not yet, chief"
error): `vibin`, `yield_lol`, `match_this`, `when`.

Identifiers start with a letter, `_`, or emoji, and continue with letters, digits, `_`, or emoji.
Case sensitive.

## Operators and precedence

Lowest to highest. Every binary operator is left-associative except assignment, `? :`, and `**`.

| Level | Operators | Notes |
|---|---|---|
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `**=` `\|\|=` | assignment, right-assoc; target must be a name, index, or property |
| 2 | `?  :` | ternary, right-assoc |
| 3 | `\|>` | pipe: `x \|> f \|> g` is `g(f(x))` |
| 4 | `??` | ghost-coalesce: `a ?? b` is `b` only when `a` is `ghost` |
| 5 | `\|\|` `orr` | short-circuit or |
| 6 | `&&` `fr` | short-circuit and |
| 7 | `\|` | bitwise or |
| 8 | `^` | bitwise xor |
| 9 | `&` | bitwise and |
| 10 | `==` `!=` `same_energy` `diff_energy` | |
| 11 | `<` `<=` `>` `>=` `in` | `in` works on `stash`, `groupchat`, `yapstring`, `blob` |
| 12 | `<<` `>>` | bit shifts |
| 13 | `+` `-` | `+` also concatenates `yapstring`s, `stash`es and `blob`s |
| 14 | `*` `/` `\` `%` | `*` also repeats a `yapstring`/`stash`: `"ha" * 3` |
| 15 | `**` | right-assoc, and binds *looser* than unary — `-2 ** 2` is `(-2) ** 2` |
| 16 | unary `-` `!` `aint` `~` | |
| 17 | postfix `(...)` `[...]` `.name` `?.name` | call, index, member, safe member |

`?.` short-circuits the rest of the postfix chain to `ghost` when the receiver is `ghost`:
`a?.b.c()` never touches `.b`/`.c()` if `a` is `ghost`.

Indexing supports negative indices (`arr[-1]` is the last element) and slices — `arr[1:3]`,
`arr[:2]`, `arr[2:]`, `s[::-1]`.

**Truthiness:** `cap` and `ghost` are falsy, as are `0`, `0.0`, `""`, `[]`, and `{}`. Everything
else is truthy.

## Statements

```funny
yo x = 10
yo y            // defaults to ghost
deadass PI = 3.14159

yap "hello"
yeet "also hello"
yap x, y, "three things"
mumble "no newline"

sus (x > 100) {
    yap "big"
} kinda_sus (x > 10) {
    yap "medium"
} nah {
    yap "smol"
}

bruh (x > 0) {
    x -= 1
    sus (x same_energy 3) { bail }
}

// range loop, upper bound exclusive
grind i from 0 to 10 {
    yap i
}
grind i from 10 to 0 step -2 {
    yap i
}

// for-each over a stash, yapstring, blob, or groupchat (groupchat yields its keys)
grind item in ["a", "b", "c"] {
    sus (item same_energy "b") { nvm }
    yap item
}

bet add(a, b) {
    bounce a + b
}
bet greet(name, greeting = "yo") {   // default params, evaluated at call time
    bounce `{greeting} {name}`
}
bet sum_all(...rest) {               // variadic; rest is a stash, must be the last param
    yo total = 0
    grind n in rest { total += n }
    bounce total
}

sketchy {
    chuck "something went sideways"
} my_bad (e) {
    yap "caught:", e.message, "of type", e.flavor
} regardless {
    yap "cleanup always runs"
}
```

A statement ends at a newline or `;`. Blank lines and repeated terminators are skipped. Inside
`(...)`/`[...]`/`{...}` of an argument list or array/map literal, newlines are suppressed so
multi-line expressions read naturally; the `{}` braces of a statement block do not suppress them.

## Functions & closures

- Functions are first class. Closures capture enclosing locals by reference (upvalues), same model
  as Lua and the Crafting Interpreters book.
- Default parameters are evaluated left to right, at call time, only when the argument is `ghost`
  (i.e. omitted).
- A variadic `...rest` parameter must be last.
- Recursion is supported up to a 10,000-frame depth limit, past which a `TooDeepBro` error fires.
- `|>` pipes the left value in as the callee's *first* argument:
  `[1, 2, 3] |> stash.squish(lowkey (a, b) => a + b, 0)`.
- `combo(f, g, h)` returns a new function equivalent to `lowkey (x) => h(g(f(x)))` — left to right.

```funny
bet counter() {
    yo n = 0
    bounce lowkey () => { n += 1; bounce n }
}
yo c = counter()
yap c(), c(), c()   // 1 2 3
```

## Pointers (`pointa`)

A `pointa` is a safe reference to a *place*, never a raw machine address — FunnyLang is
garbage-collected, so handing out real addresses isn't an option. It keeps its target alive and
always knows what it points *at*. Unary `&` takes one, produced from exactly five forms: a local
(`&x`), a global (`&g`), a captured upvalue, a stash element or groupchat key (`&arr[i]`,
`&m["k"]`), or a squad instance's field (`&obj.field`). `&` on anything else (`&42`, `&f()`,
`&(a + b)`) is a syntax error — none of those name a place.

```funny
bet swap(a, b) {
    yo t = *a
    *a = *b
    *b = t
}
yo x = 1
yo y = 2
swap(&x, &y)
yap x, y   // 2 1
```

`*p` reads, `*p = v` writes, and compound assignment (`*p += 1`) works, evaluating `p` exactly
once. Taking `&x` boxes `x` using the identical mechanism closures already use for captured
locals, so a closure capturing `x` and an `&x` taken in the same scope alias each other correctly,
automatically. A stash pointer supports the C idioms — `p + n`, `p - n`, `p - q` (distance), and
`< <= > >=` — since a stash index is a genuine ordinal; arithmetic on any other kind of pointer is
a type error. `ghost` still means "nothing" (`*ghost` raises `GhostError`); there's no separate
null pointer. Bounds and liveness are checked on every *read*, not on construction, so forming
`&arr[99]` is fine and only errors if you actually dereference it. `p == q` compares place
identity (the same box, or the same container and key) — use `*p == *q` to compare values. A
pointer to a pointer needs no special support: `&p` and `**pp` just work by composition.

## Classes (`squad`)

```funny
squad Animal {
    spawn(name) {
        me.name = name
    }
    bet speak() {
        yap me.name, "makes a noise"
    }
    bet to_yap() {                 // magic method, used by yap and string conversion
        bounce `Animal({me.name})`
    }
}

squad Dog inherits Animal {
    spawn(name, breed) {
        og.spawn(name)             // og == super
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

The VM recognizes four magic methods: `to_yap()` (stringify), `how_thicc()` (length),
`get_it(key)`/`set_it(key, val)` (indexing with `[]`), and `same_energy(other)` (`==`).

Single inheritance only — no interfaces, no static members. Fields come into existence by
assignment inside `spawn` (or any method); there's no separate field-declaration syntax.

## Modules

```funny
// mathstuff.funny
flex deadass TAU = 6.28318
flex bet double(x) { bounce x * 2 }
bet secret() { }                 // not flex'd, so not exported

// main.funny
gimme "mathstuff.funny"                      // binds the module object as `mathstuff`
gimme "./util/helpers.funny" as helpers      // explicit alias
gimme { double, TAU } from "mathstuff.funny" // named import
gimme mafs                                   // stdlib module — no quotes, no extension
gimme rizz as luck

yap mathstuff.double(21)
yap mafs.sqrt(16)
```

A quoted import path resolves, in order:
1. relative to the importing file's own directory,
2. relative to each entry in the `FUNNYPATH` environment variable (`;`-separated on Windows, `:`
   elsewhere),
3. `./funny_modules/`, walking up from the importing file.

A bare identifier (`gimme mafs`) only ever resolves to a stdlib module. Modules execute once —
results are cached by canonical absolute path — and a circular import raises `ImportSkillIssue`
naming the whole cycle.

## Runtime types & their methods

Instance methods are called as `value.method(args)`; the matching stdlib module also exposes most
of them as free functions (`stash.sort_by(arr, ...)` next to `arr.sort_by(...)`). Full stdlib
reference: [STDLIB.md](STDLIB.md).

**`yapstring`** (immutable) — `how_thicc()`, `SCREAM()`, `whisper()`, `trim()`, `split(sep)`,
`contains(s)`, `starts_with(s)`, `ends_with(s)`, `replace(a, b)`, `index_of(s)`, `slice(a, b)`,
`reverse()`, `to_numba()`, `chars()`, `at(i)`, `code_at(i)`, `repeat(n)`, `pad_left(n, c)`,
`pad_right(n, c)`.

**`blob`** (immutable, bytes rather than codepoints) — `to_yap()`, `to_hex()`, `to_base64()`,
`to_stash()`, `how_thicc()`, `starts_with(b)`, `ends_with(b)`, `index_of(b)`, `contains(b)`,
`split(sep)`, `join(stash)`. Built with `blob.of(stash)`, `blob.from_yap(s)`, `blob.from_hex(s)`
or `blob.from_base64(s)`. `b[i]` is a numba `0`–`255`, `b[a:c]` slices bytes, and assigning to
`b[i]` raises `ImmutableVibes`. A `yapstring` is text; a `blob` is a PNG, a request body or a key.

**`stash`** (mutable, reference semantics) — `how_thicc()`, `yeet_in(x)` (push), `yoink()` (pop),
`yoink_at(i)`, `insert(i, x)`, `contains(x)`, `index_of(x)`, `slice(a, b)`, `reverse()`,
`sort(cmp?)`, `join(sep)`, `glow_up(fn)` (map), `vibe_check(fn)` (filter), `squish(fn, init)`
(reduce), `any(fn)`, `all(fn)`, `first()`, `last()`, `clone()`, `clear()`, `extend(other)`.

**`groupchat`** (mutable, reference semantics, insertion-ordered) — `how_thicc()`, `keys()`,
`values()`, `pairs()`, `has(k)`, `get(k, default?)`, `set(k, v)`, `remove(k)`, `merge(other)`,
`clone()`, `clear()`. Keys may be a `yapstring`, `numba`, `boolski`, or `blob`.

**`numba`** — `to_yap()`, `abs()`, `floor()`, `ceil()`, `round(digits?)`, `is_whole()`.

**`pointa`** (a safe reference to a place — see [Pointers](#pointers-pointa) below) —
`deref()`/`set(v)` (method forms of `*p`/`*p = v`), `valid()` (would a read succeed?), `where()`
(the index/key, or the variable name for a local/global/upvalue pointer).

**`bet`** (function) — `arity()`, `name()`, `call(...args)`.

**`otw`** (a value that is on the way — see [Concurrency](#concurrency)) — pending, fulfilled or
rejected. Produced by `async_ngl bet` calls, `interns.hire` and `clock.chill`; consumed by
`await_fr` and `interns.wait_up`.

**`error`** (a caught value, bound by `my_bad (e)`) — `.flavor` (the error class name, a
`yapstring`), `.message`, `.line`, `.col`, `.file`, `.trace` (a `stash` of `yapstring`s),
`.payload` (whatever was `chuck`ed, if it wasn't a plain string).

`chuck "some text"` always raises a `SkillIssue`. To raise a specific flavor, build the error value
first with `oops(flavor, message?, line?, col?)` and `chuck` that — `chuck oops("WhoDis", "no such
name")`. The flavor must be one of `PLAN.md` §4.1's error names; the taxonomy is closed, so an
invented one is a `TypeVibeMismatch`. Chucking a caught error re-raises it with its flavor, message
and original position intact.

## Concurrency

Two things, and they are different. **Tasks** are lines of execution inside one thread, scheduled by
an event loop. **Interns** are real OS threads, each in its own VM with its own heap. Async is what
makes waiting for interns ergonomic; interns are what give async something worth waiting for.

### `async_ngl` and `await_fr`

```funny
async_ngl bet fetch(id) {
    bounce await_fr slow_lookup(id)
}

yo p = fetch(7)            // nothing has run yet: p is an otw
yap what_is_it(p)          // otw
yap await_fr p             // the task runs, and this is its answer
```

Calling an `async_ngl bet` does **not** run its body. It makes a task, hands back an `otw`, and
carries on; the body runs when the loop next gets a turn — which is the next time anything awaits,
or after the entry program finishes. `funny run` drains outstanding work before exiting.

`await_fr` sits at unary precedence, so `await_fr a + await_fr b` is `(await_fr a) + (await_fr b)`
and `await_fr f()` awaits what the call returned. Awaiting something that is **not** an `otw` is
that thing — there is nothing to wait for, and it keeps a helper that might or might not be
asynchronous from forcing its callers to know which.

The lambda form is `async_ngl lowkey (x) => ...`. A squad's *methods* cannot be `async_ngl` yet;
wrap one in an `async_ngl bet` outside the squad.

### `otw`

"On the way" — a value that does not have its answer yet. `what_is_it(p)` is `"otw"`. Three states,
and once it leaves the first it never changes again:

```funny
yap clock.chill(5)         // <otw pending>
yap await_fr fetch(7)      // ... and afterwards p prints as <otw done 7>
                           // or <otw rejected KeyGhosted>
```

An async function that raises does not raise at the call — the call only made a task. The error is
carried by the `otw` and re-raised wherever it is awaited, **with its own original flavor** and the
awaiting site's position. Awaiting a rejected `otw` raises every time, not just the first.

### Where a task cannot pause

The VM keeps its own stack and frames rather than using the C stack, which is what makes a task
suspendable: its whole state is a slice of two arrays. But a *native* function that calls back into
your code — `glow_up`, `vibe_check`, `squish`, `sort`, `combo`, a squad's `to_yap` — re-enters the
interpreter on the **C** stack, and that frame cannot be saved. `await_fr` on something pending in
there is a `CantWaitRightNow` naming the callback:

```funny
[1, 2, 3].glow_up(lowkey (x) => await_fr later(x))
// CantWaitRightNow: can't 'await_fr' inside 'glow_up' -- it called back into
// your code from the runtime, and that can't be paused.
```

Await before the call or after it. Awaiting an `otw` that has *already* settled inside a callback is
fine (nothing needs pausing), and so is *starting* an async function in one (making a task suspends
nothing).

### `LeftOnRead`

Work does not vanish quietly. A task waiting on an `otw` that nothing can ever settle — a deadlock —
is told so at its own `await_fr`. An `async_ngl bet` that **rejected** and that nobody ever awaited
is reported when the program exits, and sets the exit code: that is an error thrown into the void. A
*fulfilled* `otw` nobody awaited says nothing; starting work you do not need the answer to is a
legitimate thing to do.

### `interns` — real threads

```funny
gimme interns

// double.funny:  gimme interns
//                interns.deliver(interns.assignment() * 2)
yo a = interns.hire("double.funny", 21)
yo b = interns.hire("double.funny", 100)
yap interns.everybody([a, b])        // [42, 200]
```

Each hire is an OS thread running a whole program in its own VM, with its own collector. **Nothing
is shared**, which is why nothing needs a lock: arguments are deep-copied in and results
deep-copied out. `ghost`, `boolski`, `numba`, `yapstring`, `blob` and `stash`/`groupchat` of those can
cross; a `bet`, a squad instance, a `pointa` or an `otw` cannot, because each references a heap and
there is no second copy of that heap on the other side. A structure containing itself is refused
too. Every refusal names the type.

`interns.wait_up(x)` blocks — it is the non-async way to wait, for an intern or a `clock.chill`. For
an `otw` an `async_ngl bet` will settle, use `await_fr`: the task needs the interpreter that
`wait_up` would be holding. Full reference: [STDLIB.md](STDLIB.md#interns--workers-on-real-os-threads).

### Messages between interns

A worker does not have to be a function call. `interns.dm(who, value)` posts a message and returns
at once; `interns.check_dms(ms?)` is an `otw` that settles with `{"from", "msg"}` when one arrives,
or `ghost` if the deadline passes first. Messages are FIFO per inbox and deep-copied exactly like
an assignment, so the two threads still share nothing.

```funny
gimme interns

yo w = interns.hire("worker.funny", ghost)
interns.dm(w, "start")
yo reply = interns.wait_up(interns.check_dms())
yap reply["from"]        // which intern it came from
yap reply["msg"]
```

Inside a worker, `"boss"` addresses whoever hired it, and the `from` of a message is an address you
can post straight back to. The shape is a star rather than a mesh: you can talk to an intern you
hired and to whoever hired you, and a worker that needs to reach a sibling forwards through the one
they have in common. That is what keeps a handle from ever naming another VM's thread.

A `check_dms` with no deadline, in a program with no interns running and nobody above it, is
`LeftOnRead`: nothing can arrive, so the runtime says so instead of hanging.

### Timers

`clock.chill(ms)` is an `otw` that settles after a delay. `clock.touch_grass(seconds)` blocks the
whole program, every task included; `chill` yields, so only the awaiting task waits. The pair reads
correctly and cannot be confused for one another.

## Errors

Errors are a first-class, comedic feature — see the taxonomy and rendered diagnostic format in
`PLAN.md` §4. Set `FUNNY_SERIOUS=1` for plain, roast-free diagnostics (useful for CI logs); set
`FUNNY_NO_COLOR=1` to strip ANSI color regardless of TTY detection.

## The toolchain

```
funny run file.funny            # compile and run
funny build file.funny -o out.funnyc     # compile to bytecode (auto-bundles to .funnypak
                                          # if the entry imports other local files)
funny run out.funnyc            # run compiled bytecode
funny yeet file.funny           # freeze into a standalone native executable
funny xray file.funny           # disassemble / dump tokens / dump AST
funny fmt file.funny            # reformat in place (--check to just verify)
funny test some_dir/            # run *.funny + *.expected golden pairs under some_dir/
funny vibe                      # the REPL
funny bootstrap --verify        # self-hosting fixed-point check (see below)
```

Global flags (before or after the subcommand): `--serious`, `--no-color`, `--time`, `--vibes`.

## Self-hosting

The whole FunnyLang *toolchain* is self-hosted. `selfhost/` holds the lexer, parser, resolver,
compiler and `.funnyc` emitter, the `.funnypak` linker, the disassembler behind `funny xray`, the
formatter, the test runner, the REPL, and the command line itself — all written in FunnyLang.
`funny bootstrap --verify` links that toolchain, uses the result to link it again, and again, and
checks the last two generations are byte-for-byte identical: the classic "does the compiler
compile itself" fixed-point test.

The FunnyLang *virtual machine* is not self-hosted, and cannot be: something has to actually
execute bytecode, and that something is `native/`, a C program. A FunnyLang-hosted VM would need a
VM to run it, and the regress never bottoms out.

See `selfhost/` and the self-hosting subset described in `PLAN.md` §8 for exactly which language
features the self-hosted toolchain's own source is restricted to (it can still *compile* the full
language — squads, templates, everything — the restriction is only on how it's *written*).
