# FunnyLang standard library reference

Every function below is generated from the actual `NativeFn` registry (`funnylang/stdlib/`), so
the signatures can't drift from the real implementation. Argument counts are `(min..max)`; `inf`
means variadic. Stdlib modules are imported with `gimme <name>` (no quotes, no file extension) —
see [LANGUAGE.md](LANGUAGE.md#modules). Most `stash`/`groupchat`/`yapstring` functions are also
callable as instance methods (`arr.sort()` as well as `stash.sort(arr)`); that's noted per module.

## Always-in-scope builtins

No `gimme` needed — these are available everywhere.

| Function | Description |
|---|---|
| `how_thicc(x)` | Length of a `stash`, `groupchat`, or `yapstring` (or a squad instance's own `how_thicc()` magic method). |
| `what_is_it(x)` | The runtime type name of `x`, as a `yapstring` (`"numba"`, `"yapstring"`, `"stash"`, ...). |
| `to_yap(x)` | `x` converted to its display string (what `yap` would print). |
| `to_numba(x)` | `x` converted to a `numba` — parses a `yapstring`, passes a `numba` through, `1`/`0` for a `boolski`. |
| `to_int(x)` | Like `to_numba`, but always truncates to an integer. |
| `sheesh(x)` | Prints `x`'s `repr`-style form (quoted strings, etc.) with a newline; returns `x` unchanged, so it composes inline. |
| `no_cap(cond, msg?)` | Assert: raises `SkillIssue` with `msg` (default `"assertion failed. couldn't be you."`) if `cond` is falsy. |
| `ask(prompt?)` | Prints `prompt` (no newline) if given, reads and returns one line of stdin. |
| `yell(...)` | Like `yap`, but to stderr: values space-separated, newline-terminated. The only way to write to stderr. |
| `oops(flavor, message?, line?, col?)` | Builds an `error` value with a chosen flavor, ready to `chuck`. `flavor` must be one of `PLAN.md` §4.1's error names — the taxonomy is closed. Without a `line`, the `chuck` site is used. |
| `dip(code?)` | Exits the process immediately with `code` (default 0). |
| `the_args()` | The program's own extra CLI arguments (after `--`), as a `stash` of `yapstring`s. |
| `combo(...fns)` | Composes functions left to right: `combo(f, g, h)` is `lowkey (x) => h(g(f(x)))`. |
| `identity(x)` | Returns `x` unchanged. |
| `range_stash(a, b?, step?)` | A `stash` of numbers from `a` (or `0`) to `b` (exclusive), stepping by `step` (default `1`). |
| `zip_em(a, b)` | Pairs up two `stash`es element-wise into a `stash` of 2-element `stash`es. |
| `enumerate_em(a)` | A `stash` of `[index, value]` pairs for `a`. |
| `deep_clone(x)` | A recursive copy of a `stash`/`groupchat` (nested ones included); other values pass through unchanged. |

## `pointa` — pointers

Not a `gimme`-able module — a `pointa` (produced by unary `&`, see
[LANGUAGE.md](LANGUAGE.md#pointers-pointa)) is a runtime type with only these instance methods, no
free-function form.

| Method | Description |
|---|---|
| `p.deref()` | The method form of `*p` — reads through the pointer. |
| `p.set(v)` | The method form of `*p = v` — writes through the pointer, returns `v`. |
| `p.valid()` | `boolski`: would a read through `p` succeed right now? |
| `p.where()` | The stash index / groupchat key / property name `p` addresses, or the variable name for a local, global, or upvalue pointer. |

## `mafs` — math

| Function | Description |
|---|---|
| `mafs.sqrt(x)` | Square root. |
| `mafs.abs(x)` | Absolute value. |
| `mafs.floor(x)` | Round down to the nearest integer. |
| `mafs.ceil(x)` | Round up to the nearest integer. |
| `mafs.round(x, digits?)` | Round to `digits` decimal places (default `0`, returning an integer). |
| `mafs.min(...xs)` / `mafs.max(...xs)` | Smallest / largest of the given numbers. |
| `mafs.pow(x, y)` | `x` to the power of `y`. |
| `mafs.log(x, base?)` | Logarithm of `x` (default base `e`). |
| `mafs.log2(x)` / `mafs.log10(x)` | Base-2 / base-10 logarithm. |
| `mafs.exp(x)` | `e` to the power of `x`. |
| `mafs.sin(x)` / `mafs.cos(x)` / `mafs.tan(x)` | Trig functions (radians). |
| `mafs.atan2(y, x)` | Two-argument arctangent. |
| `mafs.hypot(...xs)` | Euclidean norm (`sqrt(sum of squares)`). |
| `mafs.clamp(x, lo, hi)` | `x` restricted to `[lo, hi]`. |
| `mafs.sign(x)` | `-1`, `0`, or `1`. |
| `mafs.gcd(a, b)` / `mafs.lcm(a, b)` | Greatest common divisor / least common multiple. |
| `mafs.is_prime(n)` | Whether `n` is prime. |
| `mafs.factorial(n)` | `n!`. |
| `mafs.is_float(x)` | Whether `x` is specifically a float value (as opposed to an int with the same magnitude — `what_is_it` calls both `"numba"`). |
| `mafs.float_to_bits(x)` | The IEEE-754 binary64 bit pattern of `x`, as an unsigned integer. |
| `mafs.bits_to_float(bits)` | The inverse: the float an unsigned 64-bit IEEE-754 pattern encodes. |
| `mafs.skibidi_pi`, `mafs.e`, `mafs.phi`, `mafs.infinity`, `mafs.nan` | Constants: π, *e*, the golden ratio, `+inf`, `NaN`. |

Also exposed as `numba` instance methods: `to_yap()`, `abs()`, `floor()`, `ceil()`, `round(digits?)`,
`is_whole()`.

## `yapper` — strings

Free functions below; most are also `yapstring` instance methods (see
[LANGUAGE.md](LANGUAGE.md#runtime-types--their-methods)).

| Function | Description |
|---|---|
| `yapper.split(s, sep?)` | Splits on `sep` (default: any whitespace run), returns a `stash`. |
| `yapper.join(sep, stash)` | Joins a `stash` of values with `sep` between them. |
| `yapper.SCREAM(s)` / `yapper.whisper(s)` | Uppercase / lowercase. |
| `yapper.trim(s)` / `yapper.ltrim(s)` / `yapper.rtrim(s)` | Strip whitespace from both ends / the left / the right. |
| `yapper.replace(s, old, new)` | Replaces every occurrence of `old` with `new`. |
| `yapper.contains(s, sub)` | Whether `sub` appears in `s`. |
| `yapper.starts_with(s, prefix)` / `yapper.ends_with(s, suffix)` | Prefix / suffix check. |
| `yapper.index_of(s, sub)` | Index of the first occurrence of `sub`, or `-1`. |
| `yapper.slice(s, start?, stop?)` | Substring `[start, stop)`. |
| `yapper.reverse(s)` | `s` reversed. |
| `yapper.repeat(s, n)` | `s` repeated `n` times. |
| `yapper.pad_left(s, n, c?)` / `yapper.pad_right(s, n, c?)` | Pads to length `n` with `c` (default space). |
| `yapper.chars(s)` | `s` as a `stash` of one-character strings. |
| `yapper.ord_of(s)` | The Unicode code point of a single-character string. |
| `yapper.chr_of(n)` | The one-character string for code point `n`. |
| `yapper.format(s, ...args)` | Python-`str.format`-style `{}` substitution. |
| `yapper.is_numba(s)` | Whether the whole string parses as a number. |
| `yapper.is_letter(s)` | Whether every character in `s` is a Unicode letter (`str.isalpha`). |
| `yapper.is_alnum(s)` | Whether every character in `s` is a Unicode letter or digit (`str.isalnum`). |
| `yapper.lines(s)` | Splits on newlines into a `stash`. |
| `yapper.words(s)` | Splits on whitespace into a `stash`. |
| `yapper.title_case(s)` | Title Cases Every Word. |
| `yapper.sarcasm_case(s)` | AlTeRnAtInG cAsE. |

Additional `yapstring` instance methods not exposed as free functions: `how_thicc()`, `at(i)`,
`code_at(i)`, `to_numba()`.

## `stash` — arrays

Free functions below; most are also `stash` instance methods.

| Function | Description |
|---|---|
| `stash.how_thicc(a)` | Length. |
| `stash.yeet_in(a, x)` | Appends `x`, mutating `a` in place; returns `a`. |
| `stash.yoink(a)` | Pops and returns the last element. |
| `stash.yoink_at(a, i)` | Pops and returns the element at index `i`. |
| `stash.insert(a, i, x)` | Inserts `x` at index `i`. |
| `stash.contains(a, x)` | Membership test. |
| `stash.index_of(a, x)` | Index of the first match, or `-1`. |
| `stash.slice(a, start?, stop?)` | A sub-`stash` `[start, stop)`. |
| `stash.reverse(a)` | Reversed copy. |
| `stash.sort(a, cmp?)` | Sorted copy, optionally with a `(a, b) -> numba` comparator. |
| `stash.sort_by(a, keyfn)` | Sorted copy, ordered by `keyfn(x)`. |
| `stash.join(a, sep)` | Joins elements (stringified) with `sep`. |
| `stash.glow_up(a, fn)` | Maps `fn` over every element. |
| `stash.vibe_check(a, fn)` | Filters to elements where `fn(x)` is truthy. |
| `stash.squish(a, fn, init)` | Reduces `a` with `fn(acc, x)`, starting from `init`. |
| `stash.any(a, fn)` / `stash.all(a, fn)` | Whether `fn` is truthy for any / every element. |
| `stash.first(a)` / `stash.last(a)` | First / last element. |
| `stash.clone(a)` | Shallow copy. |
| `stash.clear(a)` | Empties `a` in place. |
| `stash.extend(a, other)` | Appends every element of `other` to `a` in place. |
| `stash.group_by(a, keyfn)` | Groups elements into a `groupchat` keyed by `keyfn(x)`. |
| `stash.unique(a)` | Deduplicated copy, first occurrence kept. |
| `stash.flatten(a)` | Recursively flattens nested `stash`es into one. |
| `stash.chunk(a, n)` | Splits into consecutive sub-`stash`es of length `n` (the last may be shorter). |
| `stash.sum_up(a)` | Sum of all elements (must all be numbers). |
| `stash.shuffle_it(a)` | A randomly-shuffled *copy* (compare `rizz.shuffle`, which shuffles in place). |

## `groupchat` — maps

Free functions below; most are also `groupchat` instance methods.

| Function | Description |
|---|---|
| `groupchat.how_thicc(m)` | Number of entries. |
| `groupchat.keys(m)` / `groupchat.values(m)` / `groupchat.pairs(m)` | Entries as `stash`es — keys, values, or `[key, value]` pairs, in insertion order. |
| `groupchat.has(m, k)` | Whether `k` is a key. |
| `groupchat.get(m, k, default?)` | The value for `k`, or `default` (`ghost` if omitted) if absent. |
| `groupchat.set(m, k, v)` | Sets `m[k] = v`, mutating in place; returns `m`. |
| `groupchat.remove(m, k)` | Deletes key `k` (errors if absent). |
| `groupchat.merge(m, other)` | Merges `other`'s entries into `m` in place, `other` winning on conflicts. |
| `groupchat.clone(m)` | Shallow copy. |
| `groupchat.clear(m)` | Empties `m` in place. |
| `groupchat.invert(m)` | A new `groupchat` with keys and values swapped. |
| `groupchat.from_pairs(pairs)` | Builds a `groupchat` from a `stash` of `[key, value]` 2-element `stash`es. |

## `rizz` — randomness

| Function | Description |
|---|---|
| `rizz.roll(lo, hi)` | A random integer in `[lo, hi]` inclusive (a dice roll). |
| `rizz.float_roll()` | A random float in `[0, 1)`. |
| `rizz.pick(a)` | A random element from `stash` `a`. |
| `rizz.shuffle(a)` | Shuffles `a` **in place** and returns it (compare `stash.shuffle_it`, which returns a copy). |
| `rizz.coinflip()` | A random `boolski`, 50/50. |
| `rizz.seed(x?)` | Seeds the RNG (for reproducible randomness); reseeds from system entropy if omitted. |
| `rizz.uuid()` | A random UUID4 string. |
| `rizz.gamble(odds)` | `fax` with probability `odds` (a number in `[0, 1]`), `cap` otherwise. |

## `filez` — file I/O

| Function | Description |
|---|---|
| `filez.slurp(path)` | Reads a whole text file as a `yapstring`. Text mode: every `\r\n` and lone `\r` arrives as `\n`, on every platform. Use `read_bytes` for the bytes as they are. |
| `filez.yeet_out(path, text)` | Writes `text` to `path`, overwriting it; returns the byte count. Writes exactly the bytes given — a `\n` stays a `\n` on every platform. |
| `filez.append_to(path, text)` | Appends `text` to `path`; returns the byte count written. |
| `filez.exists(path)` | Whether `path` exists. |
| `filez.is_dir(path)` / `filez.is_file(path)` | Whether `path` is a directory / a regular file. Both are `cap` for a path that doesn't exist. |
| `filez.obliterate(path)` | Deletes a file, or an empty directory. |
| `filez.list_dir(path)` | Directory entries as a sorted `stash` of names. |
| `filez.mkdir(path)` | Creates a directory (and any missing parents). |
| `filez.read_bytes(path)` | Reads a whole file as a `stash` of ints `0`–`255`. |
| `filez.write_bytes(path, bytes)` | Writes a `stash` of ints `0`–`255` to `path`; returns the byte count. |
| `filez.append_bytes(path, bytes)` | Appends a `stash` of ints `0`–`255` to `path`; returns the byte count. |
| `filez.make_executable(path)` | Marks `path` runnable (the executable bits on POSIX; a no-op on Windows). |
| `filez.temp_file(prefix?)` | Creates an empty file in the OS temp directory and returns its path. Yours to `obliterate`. |
| `filez.abs_path(path)` | The absolute, resolved form of `path`. |
| `filez.join_path(...parts)` | Joins path components with the OS separator. |
| `filez.dir_of(path)` / `filez.base_of(path)` / `filez.ext_of(path)` | The parent directory / filename / extension of `path`. |

## `clock` — time

| Function | Description |
|---|---|
| `clock.now()` | The current Unix timestamp, as a float number of seconds. |
| `clock.now_ms()` | The current Unix timestamp in whole milliseconds. |
| `clock.touch_grass(seconds?)` | Sleeps for `seconds` (default `0`). Blocks the **whole program** — no other task runs either. |
| `clock.chill(ms?)` | An `otw` that settles after `ms` milliseconds (default `0`). Yields instead of blocking: `await_fr clock.chill(50)` pauses only the awaiting task. |
| `clock.stopwatch()` | Starts a stopwatch and returns a zero-argument function that, each time it's called, returns the elapsed seconds since `stopwatch()` was called. |
| `clock.date_yap(fmt?)` | The current local time formatted with a `strftime`-style pattern (default `"%Y-%m-%d %H:%M:%S"`). |

## `interns` — workers on real OS threads

Each hire runs a `.funny` program in its **own VM, on its own thread, with its own heap**. Nothing
is shared, so nothing needs a lock: arguments are deep-copied in and results deep-copied out. What
can cross is `ghost`, `boolski`, `numba` (bignums included), `yapstring`, and `stash`/`groupchat`
recursively containing those. A `bet`, a squad instance, a `pointa`, an `otw` — anything that
references a heap — is refused with a `TypeVibeMismatch` that names the type, because there is no
second copy of that heap to reference. A structure containing itself is refused too.

An error raised inside a worker is re-raised in the parent **with its own original flavor**.
Whatever the worker printed is replayed at the point you waited for it, so two workers' output
never interleaves by luck.

| Function | Description |
|---|---|
| `interns.hire(path, arg?)` | Runs `path` on a new OS thread with `arg` as its assignment; returns an `otw`. `path` may be `.funny` source (compiled first, and cached) or an already-compiled `.funnyc`/`.funnypak`. |
| `interns.wait_up(x)` | Blocks until `x` settles and is its value, re-raising its error if it rejected. Anything that is not an `otw` is itself. Blocks on an intern or a `clock.chill`; for an `otw` an `async_ngl bet` will settle, use `await_fr`. |
| `interns.everybody(stash)` | Waits on a `stash` of `otw`s in the order given and returns a `stash` of their values. |
| `interns.headcount()` | How many interns are worth hiring: the machine's logical CPU count. Not a limit. |
| `interns.assignment()` | **Inside a worker:** what `hire` was given. |
| `interns.deliver(v)` | **Inside a worker:** what `wait_up` gets back. Last delivery wins. |

A worker is a *program*, not a function — a top-level script has no `bounce`, so it reads its input
and returns its answer through the two calls above:

```funny
// double.funny
gimme interns
interns.deliver(interns.assignment() * 2)
```

```funny
gimme interns
yap interns.wait_up(interns.hire("double.funny", 21))   // 42
```

`interns.assignment()` and `interns.deliver()` outside a worker are an `OutOfPocket`. A worker may
hire interns of its own; its handles are its own, and anything it leaves unwaited is joined when it
finishes. Nothing is ever killed — `funny` waits for its workers at exit, because stopping a thread
mid-allocation leaves a heap nothing can safely free.

## `computer` — the joke module

No subprocess calls, no filesystem writes — everything here is either printing or reading basic
platform info.

| Function | Description |
|---|---|
| `computer.explode()` | Prints an ASCII mushroom cloud and raises `ComputerExploded` (exit code 69 when uncaught). |
| `computer.flex()` | Prints a summary of the OS, CPU count, RAM, and Python version. |
| `computer.ram()` | Total system RAM, in bytes. |
| `computer.yeet_to_void(x?)` | Accepts anything, does nothing, returns `ghost` — the `/dev/null` of functions. |
| `computer.env(name, default?)` | An environment variable's value, or `default` (`ghost` if not given) when it is unset. |
| `computer.exe_path()` | The path of the running executable — how `funny` finds its own sidecars. `ghost` if the OS won't say. |
| `computer.readline(prompt?)` | Prints `prompt` (no newline) if given, then reads one line of stdin. Returns `ghost` at end of input — unlike `ask()`, which returns `""` for both that and an empty line. |
| `computer.beep()` | Rings the terminal bell (`\a`). |
| `computer.clear()` | Clears the terminal screen. |
| `computer.uptime()` | Seconds since the FunnyLang process itself started. |
| `computer.blue_screen()` | A full-screen blue-ANSI "fatal error" screen, then raises an error (exit code 1 when uncaught). |

## `internet` — networking

Built on `urllib` only — no third-party dependencies. Every function here honors
`FUNNY_NO_NET=1` (set by CI), which makes it raise a clean `SkillIssue` instead of touching the
network at all.

| Function | Description |
|---|---|
| `internet.go_brrrr(url, opts?)` | An HTTP request. `opts` is a `groupchat` with optional `method`, `body`, `headers`, `timeout` keys. Returns a `groupchat` with `status`, `body`, `headers`. |
| `internet.is_it_up(url)` | Whether a `GET` to `url` succeeds (`cap` on any failure, never raises). |
| `internet.download(url, path)` | Downloads `url` to a local file at `path`; returns the byte count. |
| `internet.speed_test()` | Times a download from `https://example.com/` and returns a description of the throughput in Mbps. |
| `internet.ping(host)` | Opens a TCP connection to `host:80` and returns the round-trip time in milliseconds. |

## `sus` — reflection / debugging

| Function | Description |
|---|---|
| `sus.type_of(x)` | Same as the builtin `what_is_it(x)`. |
| `sus.fields_of(x)` | A squad instance's own fields as a `groupchat` (empty for anything else). |
| `sus.is_a(x, type_name)` | Whether `what_is_it(x) == type_name`. |
| `sus.stack_trace()` | The current call stack as a `stash` of `yapstring`s, from wherever it's called. |
| `sus.run_bytecode(bytes, args?)` | Runs a compiled program in a fresh, isolated VM with stdout captured. Returns `{out, flavor, message, code}`. |
| `sus.run_program(bytes, args?, label?)` | Runs a compiled program the way the top level does: output straight through, a full diagnostic on error. Returns `{code, ms}`. |
| `sus.new_session()` / `sus.run_in(id, bytes, args?)` / `sus.close_session(id)` | A VM kept alive between runs — a REPL. `run_in` returns `{repr, flavor, message, code}`; `repr` is the last expression's rendering. |
| `sus.dump(x)` | Prints `x`'s `repr`-style form and returns it unchanged (like `sheesh`, under a different name for reflection-flavored code). |

Note: `sus` is also the `if` keyword. As a bare `gimme sus`, it's unambiguously the stdlib module
(see [LANGUAGE.md](LANGUAGE.md#modules)); `sus (cond) { }` at a statement's start is always the
keyword. See `PLAN.md` §16 for the full disambiguation rule.
