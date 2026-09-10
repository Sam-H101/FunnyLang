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

Reserved for future use (lexed as keywords, the parser rejects them with a "not yet, chief"
error): `vibin`, `async_ngl`, `await_fr`, `yield_lol`, `match_this`, `when`.

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
| 11 | `<` `<=` `>` `>=` `in` | `in` works on `stash`, `groupchat`, `yapstring` |
| 12 | `<<` `>>` | bit shifts |
| 13 | `+` `-` | `+` also concatenates `yapstring`s and `stash`es |
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

// for-each over a stash, yapstring, or groupchat (groupchat yields its keys)
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

**`stash`** (mutable, reference semantics) — `how_thicc()`, `yeet_in(x)` (push), `yoink()` (pop),
`yoink_at(i)`, `insert(i, x)`, `contains(x)`, `index_of(x)`, `slice(a, b)`, `reverse()`,
`sort(cmp?)`, `join(sep)`, `glow_up(fn)` (map), `vibe_check(fn)` (filter), `squish(fn, init)`
(reduce), `any(fn)`, `all(fn)`, `first()`, `last()`, `clone()`, `clear()`, `extend(other)`.

**`groupchat`** (mutable, reference semantics, insertion-ordered) — `how_thicc()`, `keys()`,
`values()`, `pairs()`, `has(k)`, `get(k, default?)`, `set(k, v)`, `remove(k)`, `merge(other)`,
`clone()`, `clear()`. Keys may be a `yapstring`, `numba`, or `boolski`.

**`numba`** — `to_yap()`, `abs()`, `floor()`, `ceil()`, `round(digits?)`, `is_whole()`.

**`pointa`** (a safe reference to a place — see [Pointers](#pointers-pointa) below) —
`deref()`/`set(v)` (method forms of `*p`/`*p = v`), `valid()` (would a read succeed?), `where()`
(the index/key, or the variable name for a local/global/upvalue pointer).

**`bet`** (function) — `arity()`, `name()`, `call(...args)`.

**`error`** (a caught value, bound by `my_bad (e)`) — `.flavor` (the error class name, a
`yapstring`), `.message`, `.line`, `.col`, `.file`, `.trace` (a `stash` of `yapstring`s),
`.payload` (whatever was `chuck`ed, if it wasn't a plain string).

`chuck "some text"` always raises a `SkillIssue`. To raise a specific flavor, build the error value
first with `oops(flavor, message?, line?, col?)` and `chuck` that — `chuck oops("WhoDis", "no such
name")`. The flavor must be one of `PLAN.md` §4.1's error names; the taxonomy is closed, so an
invented one is a `TypeVibeMismatch`. Chucking a caught error re-raises it with its flavor, message
and original position intact.

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

The FunnyLang *compiler* is self-hosted: a second, independent implementation of the lexer,
parser, resolver, compiler, and `.funnyc` emitter, written in FunnyLang itself, lives in
`selfhost/`. `funny bootstrap --verify` compiles that implementation with the Python compiler,
then uses the result to compile itself twice more, and checks that the third and fourth
generations are byte-for-byte identical — the classic "does the compiler compile itself"
fixed-point test. The FunnyLang *virtual machine* is not self-hosted, and isn't meant to be:
something has to actually execute bytecode, and that's Python, frozen into every `funny yeet`
executable. See `selfhost/` and the self-hosting subset described in `PLAN.md` §8 for exactly
which language features the self-hosted compiler's own source is restricted to (it can still
*compile* the full language — squads, templates, everything — the restriction is only on how it's
*written*).
