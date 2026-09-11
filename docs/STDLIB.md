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
| `the_script()` | The running program's own absolute path, or `ghost` when it arrived as bytes with no path. Inside an `interns` worker, the path it was hired from; in a yeeted binary, the executable. For finding the files that live beside a program whatever directory it was started from — `filez.dir_of(the_script())`. |
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

A `yapstring` is indexed and measured in **codepoints**, which is what you want almost
everywhere. `byte_len` is for the exception: anything speaking a wire protocol counts bytes, and
an HTTP `Content-Length` taken from `how_thicc` would be short for any response with a single
non-ASCII character in it.

| Function | Description |
|---|---|
| `yapper.byte_len(s)` | Length in **bytes** (`how_thicc` is length in codepoints). |
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
| `yapper.to_blob(s)` | The string's UTF-8 bytes, as a `blob`. Also an instance method (`s.to_blob()`). |
| `yapper.from_blob(b)` | A `blob` decoded as UTF-8, with `U+FFFD` for anything invalid. |

Additional `yapstring` instance methods not exposed as free functions: `how_thicc()`, `at(i)`,
`code_at(i)`, `to_numba()`.

## `blob` — bytes

A `yapstring` is a sequence of codepoints; a `blob` is a sequence of **bytes**. Anything that is
not text — a PNG, a request body, a key, a hash — is a `blob`, because `how_thicc` on a string
counts characters and `chr_of(200)` is two bytes rather than the byte `200`.

A `blob` is **immutable**, like a `yapstring` and for the same reasons: it travels to `interns`,
it can be a `groupchat` key, and neither is safe if it can change underneath. Build one up with a
`stash` of blobs and `join`, not in place. It is also not a string with different accessors:
`"a" + b` is a `TypeVibeMismatch`, and `b.to_yap()` is the explicit (and lossy, `U+FFFD`) way
across.

```funny
gimme blob

yo png = blob.of([137, 80, 78, 71])
yap png.to_hex()          // 89504e47
yap png[0]                // 137   -- a numba, not a one-byte blob
yap how_thicc(png)        // 4     -- bytes
yap png                   // <blob 4 bytes>
```

| Function | Description |
|---|---|
| `blob.of(stash)` | A blob from a `stash` of numbas `0`–`255`. Anything outside that range is a `TypeVibeMismatch` rather than a silent `& 0xFF`. |
| `blob.from_yap(s)` | The UTF-8 bytes of a `yapstring`. |
| `blob.from_hex(s)` / `blob.from_base64(s)` | Decodes hex / base64. Both are strict: bad padding or a character outside the alphabet raises `SkillIssue`. |
| `b.to_yap()` | The bytes decoded as UTF-8, with `U+FFFD` for anything invalid. |
| `b.to_hex()` / `b.to_base64()` | Encodes to a `yapstring`. |
| `b.to_stash()` | The bytes as a `stash` of numbas. |
| `b.starts_with(other)` / `b.ends_with(other)` | Prefix / suffix check, against another blob. |
| `b.index_of(other)` | Byte index of the first occurrence, or `-1`. |
| `b.contains(other)` | Whether `other` appears in `b`. |
| `b.split(sep)` | Splits on a non-empty separator blob, returns a `stash` of blobs. |
| `sep.join(stash)` | Joins a `stash` of blobs with `sep` between them — the receiver is the separator, matching `yapper.join`. |

Every method above is also a free function with the blob as its first argument
(`blob.to_hex(b)`), the same arrangement `stash` and `yapper` have.

Operators: `b[i]` is a numba `0`–`255` (negative indexes count from the end); `b[a:c:step]`
slices bytes; `+` concatenates; `==` compares contents; `how_thicc(b)` is the byte count;
`grind byte in b` yields numbas; `n in b` asks whether that byte value is present and
`other in b` whether that run of bytes is. Assigning to `b[i]` raises `ImmutableVibes`.

Elsewhere: `filez.read_blob` / `write_blob` / `append_blob`, `internet.holler_back` (which
accepts one), `internet.hear_them_out(conn, n, ms, {"raw": fax})`, `go_brrrr`'s `blob` response
key, and `yapper.to_blob` / `yapper.from_blob`. A blob is a portable value, so it can be handed
to an `interns` worker and come back.

## `vault` — passwords and secrets

Password hashing and authenticated encryption, from the operating system's own crypto library:
the `dlopen`'d OpenSSL that `https` already uses on Linux/BSD, CNG on Windows, CommonCrypto on
macOS. Nothing in FunnyLang implements a cipher or a hash, deliberately — every platform ships a
reviewed one. On a machine with no OpenSSL at all, every function here raises the same
`SkillIssue` `https` does.

**A key kept next to the data it seals is not encryption.** `seal` protects data against somebody
who gets the file; that only means anything if they do not also get the key. Read it from
somewhere else — a path given on the command line, an environment variable, an OS keychain — and
never from the directory you are sealing into.

Passwords are different: `hash_password` is **one-way**, and there is deliberately no function
here that turns one back. Store the string it gives you and compare with `check_password`.

```funny
gimme vault

yo stored = vault.hash_password("hunter2")      // pbkdf2-sha256$600000$...$...
yap vault.check_password("hunter2", stored)     // fax

yo key = vault.new_key()                        // a 32-byte blob, kept elsewhere
yo sealed = vault.seal("balance: 100", key, "account-7")
yap vault.unseal(sealed, key, "account-7")      // balance: 100
```

| Function | Description |
|---|---|
| `vault.hash_password(password, iterations?)` | `"pbkdf2-sha256$<iters>$<salt>$<hash>"` — PBKDF2-HMAC-SHA256, a fresh 16-byte salt, 600,000 iterations by default. |
| `vault.check_password(password, stored)` | `fax`/`cap`, in constant time. The parameters come out of `stored`, so raising the default later leaves existing passwords checkable. Anything malformed is `cap`, never an error: a login must not leak which part was wrong. |
| `vault.new_key()` | 32 random bytes from the OS, as a `blob`. |
| `vault.derive_key(password, salt, iterations?)` | PBKDF2 to a 32-byte `blob`, for when the key comes from a passphrase. |
| `vault.seal(plain, key, aad?)` | `"v1$<nonce>$<ciphertext+tag>"` — AES-256-GCM, a fresh 12-byte nonce every time. `plain` is a `yapstring` or a `blob`; `aad` is authenticated but not encrypted, so a sealed value can be bound to the record it belongs to and not be movable to another. |
| `vault.unseal(sealed, key, aad?)` | The plaintext, as the same type that went in. A wrong key, a wrong `aad` and a single changed byte all raise the same `SkillIssue`: telling them apart is what turns decryption into an oracle. |
| `vault.sha256(x)` / `vault.hmac_sha256(key, x)` | Hex digests of a `yapstring` or `blob`. |
| `vault.same_secret(a, b)` | Constant-time equality — every byte is looked at every time, so the comparison does not say how much of a guess was right. |
| `vault.base64_encode(x)` / `vault.base64_decode(s)` | `decode` returns a `blob`, or `ghost` if the text is not valid base64. |

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
| `rizz.entropy(n?)` | `n` bytes (default 32, at most 1024) from the **operating system's CSPRNG**, as `2n` lowercase hex digits. Everything else in `rizz` is a clock-seeded generator — fine for dice, guessable as a token. Session ids, CSRF tokens and anything else secret come from here. |

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
| `filez.read_blob(path)` | Reads a whole file as a `blob` — one allocation rather than one per byte. |
| `filez.write_blob(path, b)` | Writes a `blob` to `path`; returns the byte count. |
| `filez.append_blob(path, b)` | Appends a `blob` to `path`; returns the byte count. |
| `filez.replace(from, to)` | Moves `from` over `to` in one step. A reader sees the old file or the new one, never half of either. Both must be on the same filesystem. |
| `filez.yeet_out_atomic(path, text)` | Writes `text` beside `path`, flushes it to the disk, then `replace`s it into place; returns the codepoint count. A crash mid-write leaves the old file. |
| `filez.write_blob_atomic(path, b)` | The same for a `blob`; returns the byte count. |
| `filez.make_executable(path)` | Marks `path` runnable (the executable bits on POSIX; a no-op on Windows). |
| `filez.private(path)` | Makes `path` owner-only — `chmod 0600` on POSIX, where that is the difference between a key file and a published one. A no-op on Windows, whose equivalent is an ACL rewrite rather than a mode bit. |
| `filez.temp_file(prefix?)` | Creates an empty file in the OS temp directory and returns its path. Yours to `obliterate`. |
| `filez.abs_path(path)` | The absolute, resolved form of `path`. |
| `filez.join_path(...parts)` | Joins path components with the OS separator. |
| `filez.dir_of(path)` / `filez.base_of(path)` / `filez.ext_of(path)` | The parent directory / filename / extension of `path`. |

## `json` — JSON, both ways

```funny
gimme json

yo doc = json.parse("{\"name\": \"sam\", \"tags\": [1, 2]}")
yap doc["name"]                        // sam
yap json.spill(doc)                    // {"name":"sam","tags":[1,2]}
yap json.spill(doc, {"pretty": fax})   // indented, one key per line
```

The mapping is the whole specification: `null` is `ghost`, `true`/`false` are `boolski`, an object
is an insertion-ordered `groupchat`, an array is a `stash`, and **an integer stays an integer** —
a `numba` is arbitrary-precision, so a document full of large ids round-trips exactly instead of
losing its low digits to a double. `2.5` and `1e3` are floats.

The parser assumes hostile input, because a request body is written by whoever is on the other end
of the socket. It never nests deeper than 512, never hands back a half-built value, and reports
every failure as one `SkillIssue` naming the character it gave up at — a string that was never
closed, a raw control character inside one, an escape JSON does not have, or anything at all after
the end of the value.

| Function | Description |
|---|---|
| `json.parse(text)` | The whole of `text` as one value. Trailing text after a valid value is an error, not ignored. |
| `json.spill(value, opts?)` | `value` as JSON. `{"pretty": fax}` indents two spaces, puts one key per line, and ends with a newline — the form meant for a file a person opens. |

Going out: NaN and infinity are written as `null`, because JSON has neither and the alternative is
emitting something nothing can read back. `U+2028` and `U+2029` are escaped, since they are legal
JSON but end a line in JavaScript and a browser is usually the other reader. A `blob` becomes
base64 text — JSON has no bytes — and does not come back as a blob, because nothing in the text
says it was one. A value that contains itself is `OutOfPocket`.

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
| `interns.hire(path, arg?, opts?)` | Runs `path` on a new OS thread with `arg` as its assignment; returns an `otw`. `path` may be `.funny` source (compiled first, and cached) or an already-compiled `.funnyc`/`.funnypak`. `opts` is a `groupchat`: `{"live": fax}` sends the worker's `yap` and `yell` straight to this process's own stdout and stderr as they happen, instead of holding them and replaying them when it is joined. |
| `interns.wait_up(x)` | Blocks until `x` settles and is its value, re-raising its error if it rejected. Anything that is not an `otw` is itself. Blocks on an intern or a `clock.chill`; for an `otw` an `async_ngl bet` will settle, use `await_fr`. |
| `interns.everybody(stash)` | Waits on a `stash` of `otw`s in the order given and returns a `stash` of their values. |
| `interns.dm(who, value)` | Posts `value` to an inbox and returns at once. `who` is a handle from `hire`, the `from` of a message you were sent, or the string `"boss"` for whoever hired you. Deep-copied like an assignment. A worker that has finished is `LeftOnRead`; anyone else's intern is `OutOfPocket`. |
| `interns.check_dms(timeout_ms?)` | An `otw` settling with `{"from", "msg"}` when a message is waiting, or `ghost` if `timeout_ms` passes first. Without a timeout it waits as long as it takes. Only the asking task waits. |
| `interns.dms_waiting()` | How many messages are in this VM's inbox. |
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
| `computer.until_ctrl_c()` | An `otw` that settles the first time somebody presses Ctrl-C, so a long-running program can shut down tidily instead of being killed mid-write. Pressing Ctrl-C a second time ends the process immediately, so a shutdown that hangs is not a trap. Only the program that owns the terminal may ask: inside an `interns` worker this is `OutOfPocket`, and the boss dms it instead. |
| `computer.blue_screen()` | A full-screen blue-ANSI "fatal error" screen, then raises an error (exit code 1 when uncaught). |

## `internet` — networking

Built on `urllib` only — no third-party dependencies. Every function here honors
`FUNNY_NO_NET=1` (set by CI), which makes it raise a clean `SkillIssue` instead of touching the
network at all.

| Function | Description |
|---|---|
| `internet.go_brrrr(url, opts?)` | An HTTP request. `opts` is a `groupchat` with optional `method`, `body`, `headers`, `timeout` keys. Returns a `groupchat` with `status`, `body`, `headers`, and `blob` (the body exactly as it arrived). |
| `internet.is_it_up(url)` | Whether a `GET` to `url` succeeds (`cap` on any failure, never raises). |
| `internet.download(url, path)` | Downloads `url` to a local file at `path`; returns the byte count. |
| `internet.speed_test()` | Times a download from `https://example.com/` and returns a description of the throughput in Mbps. |
| `internet.ping(host)` | Opens a TCP connection to `host:80` and returns the round-trip time in milliseconds. |

Everything above dials *out*. These are the other direction — enough to be the
thing on the other end of somebody else's request, and to answer several of them at once on one
thread (`hold_up` plus `async_ngl` — see
[LANGUAGE.md](LANGUAGE.md#concurrency)). Listeners and connections are `numba` handles
rather than objects, so they stay out of the collector and can cross to an `interns` worker (which
an object could not). `extensive_examples/web_server/` is a whole HTTP server built on them.

| Function | Description |
|---|---|
| `internet.open_shop(port, host?)` | Binds `port` and starts listening; returns a listener handle. `host` defaults to every interface; `port` 0 means "pick a free one". `SO_REUSEADDR` is set, so a restarted server can rebind its own port. |
| `internet.shop_port(listener)` | Which port it actually got — only interesting after `open_shop(0)`. |
| `internet.next_customer(listener, timeout_ms?)` | Waits for a connection. Returns `{conn, peer}`, or **`ghost`** if nobody arrived in time (a timeout is not an error — a server loop wants a turn between callers). |
| `internet.hold_up(handle, timeout_ms?)` | An `otw` that settles `fax` when that socket has something to read (for a listener: somebody waiting to be accepted), or `cap` on timeout. **This is what lets one thread serve several callers at once** — `await_fr` it and only the asking task waits, while the event loop polls every socket anybody is parked on in a single call. A plain blocking read would stop the whole thread, every other task included. |
| `internet.slide_into(host, port, timeout_ms?)` | Dials out: a raw TCP connection, none of the HTTP above it. Returns a connection handle. |
| `internet.hear_them_out(conn, max_bytes?, timeout_ms?)` | One read. `""` means the other end closed; **`ghost`** means it said nothing in time. With `{"raw": fax}` as a fourth argument the bytes come back as a `blob` instead of being decoded as UTF-8, which is what a body that is not text needs. |
| `internet.holler_back(conn, text)` | Writes all of it (looping over short writes) and returns the byte count. Accepts a `blob` as well as a `yapstring`. |
| `internet.kick_out(conn)` / `internet.close_shop(listener)` | Close one connection / stop listening. The same call under two names, because they are different acts and a program reads better saying which it meant. |

A listener is non-blocking, so several `interns` can accept from the same one: a thread that loses
the race for a connection gets `ghost` from `next_customer(listener, 0)` and goes back to waiting.

**TLS.** The same handles, encrypted. The operating system's own TLS does the work — OpenSSL
(`dlopen`'d) on Linux/BSD, Schannel on Windows, Secure Transport on macOS — and once a connection is
handshaken, `hear_them_out` reads plaintext, `holler_back` writes it and `hold_up` knows about bytes
the TLS library is already holding. `extensive_examples/web_server_https/` is a whole HTTPS site built
on them.

| Function | Description |
|---|---|
| `internet.open_secure_shop(port, host, opts)` | Like `open_shop`, with a server identity from a **PKCS#12** file: `opts` is `{"pfx": path, "password": ...}`. TLS 1.2 is the minimum and only forward-secret AEAD suites are offered; renegotiation and compression are off. Every connection accepted from it carries a TLS session that has not handshaken yet. |
| `internet.handshake(conn)` | One step of the server-side handshake, without blocking: `fax` when done, `cap` when it needs to hear from the client — `await_fr hold_up(conn)` and call again. A refusal (a TLS 1.0 client, plain HTTP sent to the TLS port) is a `SkillIssue` saying why. `fax` at once for a plain connection. |
| `internet.tls_info(conn)` | `{"version", "cipher"}` for a handshaken TLS connection, `ghost` otherwise. |
| `internet.slide_into(host, port, opts)` | With a groupchat as the third argument — `{"timeout_ms", "tls": fax, "server_name", "ca"}` — dials out over TLS and comes back handshaken and **verified**: chain and host name, against the system trust store, or against exactly the one CA in the `ca` PEM file. There is no way to turn verification off. |

Next to `next_customer`'s `{conn, peer}` is `secure`, which says whether the connection came from a
secure listener. On a TLS connection `hear_them_out(conn, n, 0)` can return `ghost` right after
`hold_up` said "readable": what arrived was part of a record. Treat it as "nothing yet", which a loop
built on `ghost` already does.

`FUNNY_NO_NET=1` does **not** block these, with one exception: `slide_into` to a non-loopback host.
The flag exists so a test runner does not *reach the network*, and a server binding its own
loopback port sends no packet anywhere — refusing it would make a server untestable in exactly the
environment that most needs its tests to run.

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
| `sus.threads()` | What every thread in this process is doing, as a `stash` of `groupchat`s with `id`, `doing` and `intern`. The same table `SIGQUIT` (Ctrl-`\`) on POSIX, or Ctrl-Break on Windows, prints to stderr — for a program that would rather answer the question over its own health endpoint. |
| `sus.dump(x)` | Prints `x`'s `repr`-style form and returns it unchanged (like `sheesh`, under a different name for reflection-flavored code). |

Note: `sus` is also the `if` keyword. As a bare `gimme sus`, it's unambiguously the stdlib module
(see [LANGUAGE.md](LANGUAGE.md#modules)); `sus (cond) { }` at a statement's start is always the
keyword. See `PLAN.md` §16 for the full disambiguation rule.
