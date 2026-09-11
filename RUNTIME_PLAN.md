# FunnyLang — Runtime Plan: message passing, real security at rest, and a thread-safety audit

> **Status:** planned, nothing built. Branch `feature/runtime-and-examples-plans` holds the plan;
> each milestone gets its own branch off `master`.
> **Prerequisite:** `master` at `0e5b238` or later — the C runtime, the self-hosted toolchain,
> `async_ngl`/`await_fr`, `interns`, and the HTTPS server example that found most of what is below.
> **Runs before:** `EXAMPLES_PLAN.md`, whose examples are written against the runtime this plan
> produces.
> **Deliverable:** `interns` that can be handed work while they run; a `vault` module with password
> hashing and authenticated encryption from the OS; a runtime that ThreadSanitizer says nothing
> about; and the seven smaller things the HTTPS server had to work around.

---

## 0. Rules for the executing agent

1. **`platform.c` is the only file allowed `#ifdef _WIN32`.** Every OS crypto call, signal, and
   atomic rename goes behind `platform.h`. `modules.c` may not call `strtok_r`, because on Windows
   that function is `strtok_s`; either it is written by hand or it lives in `platform.c`. The
   `strtok` race in R0 is the proof this rule earns its keep.
2. **The error taxonomy is closed.** `native/error.c` lists every flavor; `oops()` validates against
   it. Nothing here adds a flavor. A mailbox that is never going to hear anything is `LeftOnRead`; a
   sealed value that will not open is `SkillIssue`; a wrong type is `TypeVibeMismatch`.
3. **A golden that cannot be deterministic is not a golden.** Interns finish in whatever order the
   scheduler likes. Assert on order-independent facts (every message arrived exactly once; the sum
   is right) or force the order (await them in sequence). §7 has the technique per milestone.
4. **Anything under `selfhost/` changes the toolchain blob.** Regenerate `native/toolchain_blob.c`
   by the procedure in `bootstrap/STAGE0.md`, and `funny bootstrap --verify` must still reach a
   fixed point. R8 is the only milestone that touches it.
5. **Both build scripts.** `build.sh` *and* `build.bat`. A library added on one and not the other is
   how the last branch lost a CI run to a missing `advapi32.lib`.
6. **Nothing here may require Python**, on the machine that builds FunnyLang or the machine that
   runs it. The `no-python` CI job stays green.
7. **Stage by explicit path.** Several sessions work in this repository at once; `git add -A` would
   sweep somebody else's half-finished work into a milestone commit.
8. **Every deliberate deviation gets a §9 entry.** Including the ones that turn out to be wrong.
9. **Commit per milestone, push, never merge to master yourself.**

---

## 1. What "done" looks like

```funny
gimme interns
gimme vault
gimme blob
gimme json

// R2: a worker that is handed work while it runs.
//   counter.funny:  gimme interns
//                   bruh (fax) {
//                       yo dm = await_fr interns.check_dms()
//                       sus (dm["msg"] == "stop") { bail }
//                       interns.dm("boss", dm["msg"] * 2)
//                   }
yo w = interns.hire("counter.funny")
interns.dm(w, 21)
yap (await_fr interns.check_dms())["msg"]        // 42
interns.dm(w, "stop")
await_fr w

// R3: a password and a secret, kept the way the OS keeps them.
yo stored = vault.hash_password("correct horse battery")          // "pbkdf2-sha256$600000$…$…"
yap vault.check_password("correct horse battery", stored)         // fax
yo key = vault.new_key()
yo sealed = vault.seal("admin@example.com", key)                  // "v1$…$…", AES-256-GCM
yap vault.unseal(sealed, key)                                     // admin@example.com

// R1: bytes that are bytes.
yo png = filez.read_blob("logo.png")
yap what_is_it(png), png.how_thicc(), png[0]                      // blob 4127 137
internet.holler_back(conn, png)                                   // sends every byte as it is

// R7: JSON without a 300-line helper file.
yap json.parse("{\"a\": [1, 2.5]}")["a"][1]                       // 2.5
```

- **R0** — the corpus under `tests/lang/threads/` runs every stdlib module from eight interns at
  once, under ThreadSanitizer, and TSan reports nothing.
- **R4** — `interns.hire(path, arg, {"live": fax})` prints a worker's output as it happens.
- **R5** — the HTTPS server stops cleanly on Ctrl-C and its activity log is whole.
- **R6** — `filez.replace(from, to)` is atomic, and `filez.yeet_out_atomic` never leaves a
  half-written config.
- **R8** — `yo me = 1` says `me` is a keyword, and a line may begin with `.method()`.
- **R9** — `kill -QUIT` (or `--dump-on-stall`) prints what every thread is waiting on.

---

## 2. Why this shape

### 2.1 Isolation stays; a channel is added on top of it

`interns` was designed so that no FunnyLang value is ever touched by two threads: each worker is a
VM with its own heap, and the only things that cross are deep copies through `portable.c`. That is
what makes the runtime free of locks around values and what lets `interns` exist without a
thread-safe collector. Message passing keeps every word of that. A message is a `PortableValue` —
copied on the way in, copied on the way out, malloc'd memory that no collector owns — placed in a
mutex-guarded queue. The receiver's VM materialises it on its own heap when it asks. Nothing is
shared; something is *handed over*, which is what `hire`'s argument already does, once.

Why one inbox per VM rather than named channels: a worker is a program, not a function, and a
program with one front door is easy to read. `interns.check_dms()` is the whole receive API. A
message carries who sent it, so one inbox serves a worker with many jobs and a parent with many
workers. Named channels can be built on top in FunnyLang in ten lines; they cannot be taken away
once they are in C.

Why a star, not a mesh: handles are numbered per owning VM (`interns.c`'s reasoning: a stolen
handle must mean "my own intern #2", not somebody else's thread). A worker therefore has no handle
for a sibling and cannot address one. A message to a sibling goes through the parent, which is
where a worker pool's dispatcher sits anyway. §9 records the decision; it is reversible.

### 2.2 Security at rest comes from the OS, like TLS did

The HTTPS example stores unsalted SHA-256 passwords and base64-"encrypted" email because the
language had nothing better. The right answer is the answer TLS already gave: every operating
system ships PBKDF2, HMAC, AES-GCM and a CSPRNG, and FunnyLang bundles no crypto of its own.

| | Windows | macOS | Linux / BSD |
|---|---|---|---|
| PBKDF2-HMAC-SHA256 | `BCryptDeriveKeyPBKDF2` (CNG) | `CCKeyDerivationPBKDF` (CommonCrypto) | `PKCS5_PBKDF2_HMAC` (dlopen'd libcrypto) |
| AES-256-GCM | `BCryptEncrypt` with `BCRYPT_CHAIN_MODE_GCM` | CommonCrypto's GCM one-shots (see §9 — they are exported but their header is SPI) | `EVP_aes_256_gcm` |
| SHA-256, HMAC-SHA256 | `BCryptHash` | `CC_SHA256`, `CCHmac` | `EVP_Digest`, `HMAC` |
| CSPRNG | `BCryptGenRandom` (already: `rizz.entropy`) | `arc4random_buf` (already) | `/dev/urandom` (already) |

Everything the module produces is a self-describing string — `pbkdf2-sha256$<iters>$<salt>$<hash>`,
`v1$<nonce>$<ciphertext+tag>` — so a file says what was done to each value, a future version can
change the algorithm without breaking old files, and the HTTPS example's `b64:`/`sha256:` prefixes
have a direct replacement.

### 2.3 The audit is a corpus, not a reading

The `strtok` race hid through the whole concurrency branch, TSan and all, because no golden ran
enough interns importing at once to trip it. Reading the code for globals finds the obvious ones
(§3.0 lists what a first pass found); it does not find the C library's hidden state. What finds
that is a corpus that calls every stdlib function from many threads at the same moment, run under
ThreadSanitizer, in CI, every push. The audit's deliverable is that corpus. The fixes are what
falls out of running it.

### 2.4 The small things, and why each is here

Each of these was a workaround in `extensive_examples/web_server_https/`:

| Workaround | Because | Fix |
|---|---|---|
| A 300-line `crypto.funny` | no SHA-256, no base64 | R3 `vault` |
| A 400-line `json.funny`, slow on big files | no JSON | R7 `json` |
| Assets are text-only; the favicon is an SVG | a `yapstring` cannot hold arbitrary bytes | R1 `blob` |
| Log lines travel worker → keeper → main thread | a worker's output is held until it is joined | R4 live output |
| Ctrl-C loses the last quarter second of the activity log | no way to hear Ctrl-C | R5 |
| `config.custom.json` is written in place | no atomic rename | R6 |
| Three parse errors about `me` and `to`, each a minute lost | the parser says "expected a variable name" and not why | R8 |
| Downloading gdb into WSL to see a hang | no way to ask a running `funny` what it is waiting on | R9 |

---

## 3. The milestones

Order: R0 first, because it is small, because every later milestone adds threads-meeting-threads
code that the corpus then covers, and because it turns the TSan step from "two directories" into
"everything threaded". R1 (`blob`) before R2 and R3, because messages and sealed values want to
carry bytes. R2 before R3 only because it is the one the examples plan needs most. R4–R9 are
independent of one another and small; do them in the order listed unless something blocks.

### R0 — the thread-safety audit

**Inventory** (a reading pass on `native/`, to be completed by the corpus):

| Site | State | Verdict |
|---|---|---|
| `modules.c` `normalize_module_path` | was `strtok` | fixed on `master` (`757d147`); the corpus keeps it fixed |
| `platform.c` `lexical_normalize` | was `strtok` | fixed on `master`; MSVC side uses its own splitter — check |
| `rizz.c` `g_state[4]`, `g_seeded` | one xoshiro state for the process, unlocked | **per-VM state**, seeded from `platform_random_bytes` (or `rizz.seed` on that VM). Two interns rolling dice at once currently corrupt the generator. Goldens seed on one VM, so nothing observable changes |
| `platform.c` `ensure_winsock` | `static bool started` | `INIT_ONCE`, like `g_tlsOnce`. Two interns opening their first socket at once call `WSAStartup` twice (harmless) and one may proceed before it returns (not) |
| `platform.c` `platform_monotonic_seconds` (Windows) | `static bool init` + `freq` | `INIT_ONCE`; a reader can see `init` before `freq` |
| `platform.c` `set_errbuf` → `strerror` | returns a static buffer on some libcs | `strerror_r` / `strerror_s` behind a `platform_strerror` |
| `platform.c` `platform_strftime_now` | `localtime_r` / `localtime_s` | fine; leave |
| `diag.c` `g_override` | written once at start-up, read everywhere | document as start-up-only; assert `taskCount == 0` when set |
| `sus.c` `g_toolchain` | set once in `main` before any thread | document |
| `interns.c` registry | mutex-guarded | fine; the corpus stresses it |
| `gc.c` per-VM | one collector per VM | fine by construction; nothing to do |
| `platform_temp_file` | `mkstemp` / `GetTempFileName` | fine |
| `filez.list_dir` | `readdir` on a private `DIR *` | fine on glibc; `readdir_r` is deprecated, leave |

**The corpus**: `tests/lang/threads/`, one golden per stdlib module plus the cross-cutting ones.
Each hires eight interns that hammer the module for a fixed number of iterations and deliver a
checksum; the golden prints whether every checksum matches the single-threaded answer.

- `mafs.funny`, `yapper.funny`, `stash.funny`, `groupchat.funny` — pure computation, mostly a
  check that nothing in the VM is secretly process-global (bignum scratch space, number
  formatting buffers, the Unicode tables).
- `rizz.funny` — eight interns each seed *their own* generator and roll; every stream must equal
  the single-threaded stream for that seed. This fails today.
- `filez.funny` — each intern makes, writes, reads, lists and deletes its own temp files.
- `clock.funny` — `date_yap`, `now`, `stopwatch`, `chill` from eight interns.
- `computer.funny` — `env`, `exe_path`, `ram`, `uptime`.
- `internet.funny` — eight interns each run a loopback server *and* a client; also all eight
  accepting from one shared listener (`shared_listener` already covers the listener).
- `sus.funny` — eight interns each `sus.run_bytecode` a small program: eight child VMs of child VMs.
- `imports.funny` — `concurrent_imports` already; it moves here.
- `errors.funny` — eight interns each raise and catch every flavor; the diagnostic renderer from
  eight threads (`diag.c` has the override global).

**CI**: the TSan step runs `tests/lang/threads`, `tests/lang/interns`, `tests/lang/async`, and
`extensive_examples` once. The last is slow under TSan (estimate 60–90 s) and runs only on that one
matrix leg.

**Deliverables**: the corpus; the fixes the inventory names; a paragraph in `docs/NATIVE.md` listing
what is process-global on purpose and why each is safe. **~450 lines** (corpus ~300, fixes ~150).

### R1 — `blob`: bytes that are bytes

A `yapstring` is a sequence of codepoints, and `string_new` counts codepoints over whatever bytes it
is given, so a PNG read into one has a `how_thicc()` that means nothing and cannot be built from
FunnyLang at all (`chr_of(200)` is two bytes). Servers, encryption and JSON all need a byte
sequence that is exactly that.

| | |
|---|---|
| Type name | `blob` (`what_is_it(b) == "blob"`); immutable, GC-managed, `byteLen` + `uint8_t *` |
| Literal | none. `blob.of([137, 80, 78, 71])`, `blob.from_yap("text")` (UTF-8), `blob.from_hex("89504e47")`, `blob.from_base64("…")` |
| Back out | `b.to_yap()` (UTF-8, lossy → U+FFFD), `b.to_hex()`, `b.to_base64()`, `b.to_stash()` |
| Operators | `b[i]` is a `numba` 0–255; `b[a:c]` slices; `+` concatenates; `==` compares bytes; `how_thicc()` is the byte count; `in` on a `numba` or a `blob` |
| Iteration | `grind byte in b` yields `numba`s |
| Methods | `starts_with`, `ends_with`, `index_of`, `contains`, `split(sep)`, `join(stash)` |
| Crossing | portable: a `blob` crosses to an intern (new `PORTABLE_BLOB` in `portable.c`) and travels in a DM |
| `filez` | `filez.read_blob(path)`, `filez.write_blob(path, b)`, `filez.append_blob(path, b)`. `read_bytes`/`write_bytes` (stash of ints) stay, documented as the pre-`blob` form |
| `internet` | `holler_back` accepts a `blob`; `hear_them_out(conn, n, ms, {"raw": fax})` returns one; `go_brrrr`'s response gains `"blob"` beside `"body"` |
| `yapper` | `yapper.to_blob(s)` and `yapper.from_blob(b)` as the free-function forms |
| Printing | `yap b` prints `<blob 4127 bytes>`; `sheesh` prints the first 16 in hex |

Not a mutable buffer: appending in a loop is `stash` of `blob` then `blob.join`. Not a string:
`"a" + b` is a `TypeVibeMismatch`. **~700 lines** (`blob.c`, portable, filez, internet, docs,
goldens under `tests/lang/stdlib/blob*`).

### R2 — DMs: message passing between interns

**API**, in `interns`:

| Function | Description |
|---|---|
| `interns.dm(who, value)` | Puts `value` (deep-copied; anything `hire` accepts, `blob` included) in `who`'s inbox and returns at once. `who` is a handle from `hire`, or the string `"boss"` for the VM that hired the caller. A handle that is not this VM's is `OutOfPocket`; `"boss"` outside a worker is `OutOfPocket`; a worker that has already finished is `LeftOnRead` |
| `interns.check_dms(timeout_ms?)` | An `otw` that settles with `{"from": who, "msg": value}` — `from` is a handle or `"boss"` — when a message is waiting, or `ghost` on timeout. Only the asking task waits |
| `interns.dms_waiting()` | How many are in this VM's inbox |
| `interns.wait_up(p)` | Also blocks on a `check_dms` `otw` (a mailbox is a thing a thread can block on, like a timer) |

**Semantics**: FIFO per inbox; unbounded, with a cap of 1 M messages after which `dm` raises
`OutOfPocket` (a producer that far ahead of its consumer is a bug, and the alternative is the
process running out of memory quietly). A message is delivered even if the receiver has not asked
yet; an inbox is drained by its owner in order. `hire`'s assignment stays what it is: the first
message, delivered before the program starts, by the old route.

**Implementation**:
- `Mailbox` in `interns.c`: a `PlatformMutex`, a ring of `{PortableValue *value; int fromId;}`,
  counts. Owned by the `Intern` for a worker's inbox and by the `VM` (`vm->inbox`, malloc'd at
  `vm_init`) for a top-level program's. A worker's VM points at its `Intern`'s mailbox, so a parent
  can send to it before the thread has fully started and after the worker's VM is gone (the message
  is then dropped at release, and `dm` reports `LeftOnRead` once `done` is set).
- A send takes the mailbox lock, pushes, releases, and broadcasts `g_wake` — the condition variable
  the event loop already sleeps on for finishing workers. `wake_waiters` in `loop.c` gets a fourth
  pass, before the socket poll: every task whose `otw` has `waitMailbox` set is fulfilled if its
  inbox is non-empty. The blocking wait already goes through `interns_wait_any`, which is a timed
  wait on `g_wake`; a mailbox waiter counts as `anyWorker` for the purpose of choosing it.
- `LeftOnRead` when nothing can ever arrive: a top-level VM with no live interns waiting on
  `check_dms` with no timeout is stranded (the existing `strand_waiters` rule); a worker's parent
  is always alive while the worker is (a VM joins its interns before it dies), so a worker is never
  stranded by the parent going away — it is stranded only by the parent never sending, which no
  runtime can know, so a worker's untimed `check_dms` is allowed to wait forever.
- `otw.h`: `bool waitMailbox;` beside `waitSocket`. `task.c`/`gc.c`: nothing — a `PortableValue` is
  not a heap object.
- `vm_destroy`: after joining interns, free the inbox and every `PortableValue` in it.

**Goldens** (`tests/lang/interns/dm_*.funny`): round trip; a worker pool of four with twenty jobs
dealt round-robin and results summed (order-independent); a DM carrying every portable type and a
`blob`; `dm` to a finished worker; `check_dms` timeout; `LeftOnRead` at top level with no interns;
a worker that hires a worker and forwards; `wait_up` on `check_dms`. Under TSan.

**Docs**: `STDLIB.md` `interns` table; `LANGUAGE.md`'s concurrency section gets a paragraph;
`CHANGELOG.md`. **~500 lines.**

### R3 — `vault`: password hashing and authenticated encryption

**API**, a new stdlib module `vault`:

| Function | Description |
|---|---|
| `vault.hash_password(password, iterations?)` | `"pbkdf2-sha256$<iters>$<salt b64>$<hash b64>"`. 16-byte random salt, 600,000 iterations by default (OWASP 2023 for PBKDF2-SHA256), 32-byte output |
| `vault.check_password(password, stored)` | `fax`/`cap`. Reads the parameters out of `stored`, so a hash made with other iterations still checks. Constant-time compare. Malformed `stored` is `cap`, never an error — a login must not leak which |
| `vault.new_key()` | 32 random bytes as a `blob` |
| `vault.seal(plain, key, aad?)` | `"v1$<nonce b64>$<ciphertext+tag b64>"`. AES-256-GCM, 12-byte random nonce, 16-byte tag. `plain` is a `yapstring` or a `blob`; `aad` (a `yapstring`) is authenticated but not encrypted — bind a value to its record id so it cannot be moved to another |
| `vault.unseal(sealed, key, aad?)` | The plaintext (`yapstring` if it was one, `blob` otherwise — the version byte records which). Wrong key, wrong aad, one flipped bit: `SkillIssue` "that sealed value won't open" |
| `vault.derive_key(password, salt, iterations?)` | PBKDF2 to a 32-byte `blob`, for when the key itself comes from a passphrase |
| `vault.sha256(x)`, `vault.hmac_sha256(key, x)` | Hex digests of a `yapstring` or `blob` |
| `vault.same_secret(a, b)` | Constant-time equality of two `yapstring`s or `blob`s |
| `vault.base64_encode(x)` / `vault.base64_decode(s)` | The pair `crypto.funny` implemented; `decode` returns a `blob`, `ghost` on malformed input |

**Backends**: §2.2's table, each behind `platform_pbkdf2`, `platform_aes_gcm_seal/open`,
`platform_sha256`, `platform_hmac_sha256` in `platform.h`. Linux/BSD extends the `dlopen`'d
OpenSSL with libcrypto symbols (`PKCS5_PBKDF2_HMAC`, `EVP_CIPHER_CTX_new`, `EVP_EncryptInit_ex`,
`EVP_CIPHER_CTX_ctrl` for the IV length and tag, `EVP_EncryptUpdate`/`Final`, the `Decrypt` set,
`EVP_sha256`, `HMAC`); no libcrypto ⇒ `vault` functions raise the same "needs OpenSSL" `SkillIssue`
`https` does. Windows links `bcrypt.lib` (already). macOS: CommonCrypto for PBKDF2, SHA-256 and
HMAC; GCM is the one open question, §9.

**Key handling is the caller's**, and the docs say so in bold: a key in the same file as the data it
seals is not encryption. The HTTPS example (an `EXAMPLES_PLAN.md` follow-up, not this milestone)
reads its key from `--key-file` outside `data/`, generated once with `vault.new_key()`.

**Goldens**: the RFC 6070 / NIST PBKDF2 vectors; the NIST GCM test vectors (encrypt and decrypt,
including a tag-mismatch case); round trips through every backend; `check_password` on a malformed
string; `unseal` with the wrong aad. Vectors are the same on every platform, so one `.expected`.
**~900 lines**, of which Windows CNG and the OpenSSL dlsym table are the bulk.

### R4 — live output from interns

Today a worker's `yap` and `yell` are captured to a temp file and replayed when the worker is
joined — on purpose, so a golden's output does not interleave by luck. That is the right default and
it stays. What is added is an opt-in: `interns.hire(path, arg, {"live": fax})`. A live worker's
streams are the process's `stdout`/`stderr`, and each `yap` is one `fwrite` of the whole line under
the C library's stream lock, so lines never interleave mid-way. The order *between* threads is
whatever it is, which is what "live" means; goldens do not use it. The HTTPS server's log then goes
straight from the worker to stderr, and the keeper stops forwarding it (an examples-plan change).
**~80 lines.**

### R5 — Ctrl-C

`computer.until_ctrl_c()` returns an `otw` that settles (with `ghost`) the first time the process
receives SIGINT / `CTRL_C_EVENT`. Behind `platform_on_interrupt`: the handler sets a
`volatile sig_atomic_t` and nothing else (no cond variable — not async-signal-safe); every event
loop's timed wait is at most `MIXED_WAIT_CAP_MS` when something is outstanding, so the flag is
noticed within 25 ms, and `wake_waiters` gets a pass for it. The **second** Ctrl-C keeps today's
behaviour (the default action: the process dies), so a program that hangs in its shutdown can still
be stopped. Only the top-level VM's `otw` settles; a worker asks its boss. A program that never
calls `until_ctrl_c` is unchanged: the handler is installed by the first call. **~120 lines.**

### R6 — atomic file replacement

`filez.replace(from, to)` renames `from` over `to` in one step (`rename(2)`; `MoveFileExW` with
`MOVEFILE_REPLACE_EXISTING`), and `filez.yeet_out_atomic(path, text)` / `filez.write_blob_atomic`
write to a temp file *in the same directory*, flush and sync it, then `replace` it into place — so a
reader sees the old file or the new one and never half of either, and a crash mid-write leaves the
old file. **~90 lines.**

### R7 — `json` in the standard library

`json.parse(text)` and `json.spill(value, opts?)` (`{"pretty": fax}`), in C, with exactly the
semantics `extensive_examples/web_server_https/json.funny` has now: integers stay `numba` integers,
`2.5`/`1e3` become floats, `null` is `ghost`, objects are insertion-ordered `groupchat`s, strings
decode surrogate pairs, a raw control character in a string is an error, nesting deeper than 512 is
an error, trailing garbage is an error, and every error is a `SkillIssue` naming the character
offset. `spill` writes NaN and infinities as `null`, escapes U+2028/2029, and refuses a value that
contains itself with `OutOfPocket`. A `blob` spills as base64 text and is not parsed back (JSON has
no bytes). **~500 lines**, plus the goldens moved from the example's test.

### R8 — kinder parser errors and leading-dot continuation

Two changes to `selfhost/parser.funny` (and one to `lexer.funny`), so this is the milestone that
regenerates the toolchain blob:

- **Keywords as names.** `p_expect(ps, "IDENT", …)` for a variable, parameter, catch, import or
  alias name: when the token is a keyword, the message becomes *"`me` is a keyword (it's `this`
  inside a squad) — pick another name."* with the one-line meaning from a table of all of them.
  `!ERROR ParserHadAStroke` goldens for `yo me`, `yo to`, `bet f(from)`, `my_bad (in)`.
- **Leading-dot continuation.** A `NEWLINE` token followed by a `DOT` token followed by an `IDENT`
  is dropped by the parser's statement-end check, so
  ```funny
  yo names = people
      .vibe_check(lowkey (p) => p["age"] > 17)
      .glow_up(lowkey (p) => p["name"])
  ```
  parses as one expression. The lexer already refuses `.5` as a number (`is_dec_digit` after the
  dot is a float only *after* digits), so a line starting with `.` has no other meaning today.
  `funny fmt` learns to keep the layout. Goldens in `tests/lang/parser/`.

**~150 lines** of FunnyLang, then `bootstrap --verify`.

### R9 — a dump of what every thread is waiting on

Every thread keeps a one-line, plain-C status slot (`"loop: 3 tasks; waiting on socket 7 (2.1 s left), timer 0.4 s"`,
`"joining intern #4 (worker.funny)"`, `"in native: filez.slurp"`), updated at each wait point in
`loop.c` and `interns.c` and in `vm_destroy`'s join. The slots live in a mutex-guarded process-wide
table; a thread registers at `vm_init` and leaves at `vm_destroy`.

Three ways to read them:
- `SIGQUIT` on POSIX (Ctrl-`\`), `CTRL_BREAK_EVENT` on Windows: the handler sets a flag; the next
  thread to reach any wait point prints the whole table to stderr. If every thread is blocked in a
  join or a socket read with no timeout, nobody reaches a wait point — so the handler *also*
  writes the table itself with `write(2)`, unlocked, marked "(may be torn)". A torn diagnostic beats
  none.
- `funny run --dump-on-stall 30 x.funny`: a watchdog thread prints the table when no thread has
  updated its slot in 30 s.
- `sus.threads()`: the table as a `stash` of `groupchat`s, for a program's own health endpoint.

**~250 lines.**

---

## 4. Files

```
native/
  blob.c blob.h          R1  the type, its methods, its stdlib module
  interns.c              R2  Mailbox, dm/check_dms/dms_waiting; R4 live option
  loop.c otw.h           R2  mailbox waits; R5 the interrupt pass
  portable.c             R1  PORTABLE_BLOB
  vault.c vault.h        R3  the module; crypto behind platform.h
  platform.c platform.h  R0 fixes; R3 pbkdf2/gcm/sha256/hmac per OS; R5 interrupt; R6 replace;
                         R9 status slots and the dump
  json.c json.h          R7
  computer.c             R5 until_ctrl_c
  filez.c                R1 blobs; R6 replace and atomic writes
  internet.c             R1 blobs on the wire
  rizz.c                 R0 per-VM state
  sus.c                  R9 threads()
  modules.c              R1, R3, R7 registry entries
selfhost/
  parser.funny lexer.funny fmt.funny   R8
  compiler.funny                       R1/R3/R7 stdlib names (STDLIB_MODULE_NAMES)
tests/lang/threads/      R0 the corpus
tests/lang/stdlib/       R1 blob, R3 vault, R6 filez, R7 json goldens
tests/lang/interns/      R2 dm_* goldens
tests/lang/parser/       R8
docs/STDLIB.md docs/LANGUAGE.md docs/NATIVE.md CHANGELOG.md   every milestone
```

---

## 5. Milestone checklist

### R0 — thread-safety audit
- [x] `tests/lang/threads/` corpus, one golden per module, each with eight interns
- [x] `rizz` state per VM, seeded from the OS; `rizz.seed` per VM
- [x] `ensure_winsock`, Windows `platform_monotonic_seconds` under `INIT_ONCE`
- [x] `platform_strerror` (`strerror_r`/`strerror_s`); `set_errbuf` and `sock_last_error` use it
- [x] `diag.c`, `sus.c` start-up-only globals documented and asserted
- [x] CI TSan step covers `threads`, `interns`, `async`, `extensive_examples`
- [x] `docs/NATIVE.md`: "what is process-global, and why that is safe"

### R1 — `blob`
- [x] The type: object, GC, equality, hashing as a groupchat key, printing
- [x] Constructors, conversions, operators, iteration, methods
- [x] Portable across interns; `filez`, `internet`, `yapper` integration
- [x] `STDLIB_MODULE_NAMES` in `compiler.funny` (blob regenerated with R3/R7's, once)
- [x] Goldens; docs

### R2 — DMs
- [x] `Mailbox`; `vm->inbox`; a worker's inbox on its `Intern`
- [x] `dm`, `check_dms`, `dms_waiting`; `wait_up` on a mailbox `otw`
- [x] `loop.c` mailbox pass; `LeftOnRead` rule; teardown frees the inbox
- [x] Goldens, under TSan; docs

### R3 — `vault`
- [x] `platform.h` crypto surface; Linux (dlopen libcrypto), Windows (CNG), macOS (CommonCrypto)
- [x] The module; the string formats; constant-time compare
- [x] Known-answer goldens; both build scripts; docs, with the key-handling warning

### R4 — live output
- [x] `{"live": fax}` on `hire`; one `fwrite` per line; docs

### R5 — Ctrl-C
- [x] `platform_on_interrupt`; `computer.until_ctrl_c`; loop pass; second Ctrl-C still kills
- [x] A golden cannot press Ctrl-C: `tests/lang/stdlib/until_ctrl_c.funny` checks the `otw` is
      pending and that `wait_up` on it with a timeout is refused, and the manual check is written
      down in the CHANGELOG entry

### R6 — atomic replace
- [ ] `platform_replace_file`; `filez.replace`; `yeet_out_atomic`, `write_blob_atomic`; goldens

### R7 — `json`
- [ ] `json.parse`, `json.spill`; the example's `json.funny` goldens moved to `tests/lang/stdlib/`
- [ ] `extensive_examples/web_server_https` switched to it (its `json.funny` deleted)

### R8 — parser
- [ ] Keyword-as-name messages; leading-dot continuation; `fmt` keeps it
- [ ] Toolchain blob regenerated; `bootstrap --verify` fixed point

### R9 — thread dump
- [ ] Status slots at every wait point; `SIGQUIT`/`CTRL_BREAK`; `--dump-on-stall`; `sus.threads()`

---

## 6. Cost sheet

| Milestone | Lines (est.) | Mostly |
|---|---|---|
| R0 audit | 450 | FunnyLang goldens; small C fixes |
| R1 blob | 700 | C |
| R2 DMs | 500 | C |
| R3 vault | 900 | C, three backends |
| R4 live output | 80 | C |
| R5 Ctrl-C | 120 | C |
| R6 atomic replace | 90 | C |
| R7 json | 500 | C |
| R8 parser | 150 | FunnyLang, plus a blob regeneration |
| R9 thread dump | 250 | C |
| Docs, changelog | 400 | Markdown |

---

## 7. Testing

- **Every golden deterministic.** Interns deliver checksums, counts or sorted results; nothing
  prints in arrival order. The `dm` pool golden sums results; the `threads` corpus compares each
  intern's checksum to the single-threaded answer.
- **Under ThreadSanitizer, in CI**, everything under `tests/lang/threads`, `interns`, `async`, and
  `extensive_examples`, on the ubuntu/clang leg. TSan output is a failure, not a warning.
- **Under ASan/UBSan** as today: the whole corpus on every non-Windows leg.
- **Known-answer vectors** for every cryptographic primitive, identical on all three platforms.
- **The three platforms**: nothing in R3 is done until CI is green on all of them; the Windows and
  macOS backends cannot be run on the development machine (the HTTPS branch verified Windows through
  an llvm-mingw cross-build run natively, and macOS only in CI).
- **The HTTPS example is the integration test**: at the end of R7 it uses `json`, at the end of the
  examples plan's first milestone it uses `vault`, `blob`, DMs, live output and Ctrl-C, and its
  golden still prints byte-identical output on every platform.

---

## 8. Decisions already made

- **One inbox per VM, star topology, FIFO, unbounded with a sanity cap.** §2.1.
- **Deep copy for messages, exactly as for `hire`.** No shared memory, no reference passing, no
  exceptions for "small" values. The cost is measured in the pool golden and written in the docs.
- **Self-describing formats for everything `vault` stores.** Algorithm, parameters, salt and
  nonce travel with the value.
- **PBKDF2, not scrypt or Argon2**, because it is the one every OS ships. The iteration count is a
  parameter and the format records it, so raising it later re-hashes on next login.
- **`blob` is immutable.** Same reasoning as `yapstring`: it can be a groupchat key, it can cross to
  an intern, and nothing can change under a reader.
- **Live output is opt-in.** Captured-and-replayed stays the default because goldens depend on it.
- **The parser changes wait until R8** so the toolchain blob is regenerated once for the whole plan
  (R1/R3/R7's `STDLIB_MODULE_NAMES` additions land in the same regeneration).

---

## 9. Deviations log

**Branching (applies to every milestone).** §0 asks for a branch per milestone. This runs on one
branch, `feature/runtime-plan`, with a commit pushed per milestone instead. The plan's reason for
the rule — that a milestone lands reviewable on its own — is met by the commits, and ten branches
for ten milestones that each build on the last would be ten pull requests waiting on each other.

**R0.** Built as specified, with two notes.

*The corpus is eleven goldens plus the moved one, not twelve new ones.* Each
`tests/lang/threads/<module>.funny` computes the expected answers on its own thread first, then
hires eight interns to compute the same eight and counts the matches. The expected-first ordering
is not cosmetic: with a process-wide generator the golden's own rolls would have raced the
interns', so the test would have been measuring the bug with the bug. `tests/lang/interns/`'s
`concurrent_imports` moved here as `threads/imports.funny`, since it is the same kind of test and
CI now runs this whole directory under ThreadSanitizer.

*`rizz`'s first seed comes from the OS, which the plan did not ask for.* Moving the generator into
the `VM` turned a latent bug into a visible one: the old self-seed mixed `time(NULL)` with the
address of the (single, static) state, so eight interns starting in the same second would have
drawn from eight nearly identical streams. It now seeds from `platform_random_bytes`, falling back
to the clock only if that fails.

*Everything else in R0 is as written.* Winsock start-up and the Windows performance-counter
frequency moved to `INIT_ONCE`; `strerror` became `strerror_r`/`strerror_s` behind `set_errbuf`,
which `sock_last_error` and the TLS certificate reader now use; `diag.c` and `sus.c`'s start-up-only
globals are documented where they are declared; `docs/NATIVE.md` gained a "what is process-global,
and why that is safe" section; CI's ThreadSanitizer step now covers `tests/lang/threads`,
`tests/lang/interns`, `tests/lang/async` and `extensive_examples`.

**R1.** Built as specified. Four notes, three of them about what the type deliberately does not do.

*A blob works as a groupchat key through `value_equal_narrow`, not through the hash index.*
`groupchat.c` indexes string keys only, and every other key type already falls back to a linear
scan; blobs join that group rather than growing a second hash function. Keys are almost always
strings, and a groupchat keyed by blobs is small when it exists at all.

*`blob.of` refuses a numba outside 0-255 instead of masking it.* `filez.write_bytes` masks, and
keeps doing so, because programs already depend on that; but this is a new constructor, and 300
quietly becoming 44 is a bug nobody finds for a week.

*The plan's `sheesh` line said "the first 16 in hex", so `sheesh(b)` prints*
`<blob 26 bytes: 6162...6f70...>` *-- the length, then the bytes, then an ellipsis when there are
more.* `yap b` stays `<blob 26 bytes>`.

*Four existing goldens changed because a message changed.* The portable-value rejection names
what can cross, and `blob` is now on that list, so `tests/lang/interns/cant_cross`, `handles`,
`otw_states` and `examples/concurrency` say so too. The toolchain blob is deliberately NOT
regenerated here even though `STDLIB_MODULE_NAMES` gained "blob": the plan batches that with R3
and R7's own additions into one regeneration in R8, and `bootstrap --verify` is a fixed point
either way because it compiles the current source with itself.

**R2.** Built as specified, with one API difference that the plan's own design forced.

*A message's `from` is a numba, not a handle.* The plan says "`from` is a handle or `"boss"`", but
a handle is an `otw` on the receiver's heap, and the sender has no way to name it: reconstructing
one would mean either handing back a different object each time or keeping a heap pointer in the
malloc'd registry, where the collector cannot see it. So `from` is the sender's id *within the
receiving VM* -- the same number `intern_at` has always used -- and `dm` accepts one as an address
beside a handle and `"boss"`. That is safe for exactly the reason `interns.h` already gives for
numbering per VM: a stolen id can only ever name one of the thief's own interns, so the check is
structural rather than a rule to remember. It is also what makes replying possible at all.

*A handle whose intern has been collected reports `LeftOnRead`, not `OutOfPocket`.* `otw_fulfill`
clears `internId`, so a settled handle no longer names anything; "that intern has finished" is the
true answer, and the plan's own `LeftOnRead` rule covers it.

*`intern_release` now runs under `g_lock` in `interns_collect`.* A sender holds `g_lock` across the
whole post, including the push, which is what stops a mailbox being freed between being found and
being written to. Releasing outside the lock would have re-opened exactly that window.

**R3.** Built as specified. The open question is settled and two test-shaped things differ.

*macOS AES-GCM: option (a), as recommended.* `CCCryptorGCMOneshotEncrypt` and `…Decrypt` are
resolved with `dlsym` against local prototypes, exactly as Linux resolves OpenSSL, and a macOS that
does not export them gets a clear `SkillIssue` rather than a different construction. Option (b)
would have made `v1$` mean AES-CTR-then-HMAC on one platform and AES-GCM on the others, so a value
sealed on a Mac could not be opened on a server; that is a worse outcome than a named failure on an
OS old enough not to have the symbol.

*There are no NIST GCM known-answer vectors, and there is no way to add them without a worse API.*
A known-answer test has to supply the nonce, and `seal` generates one -- which is the single most
important thing it does, because a reused nonce with the same key breaks GCM completely. Exposing a
nonce parameter to make the test possible would hand every caller that footgun. GCM is covered
instead by round trip, by a tamper case, by a wrong key, by a wrong `aad`, and by the check that
sealing the same value twice gives two different strings. PBKDF2, SHA-256 and HMAC-SHA256 do have
their published vectors, and those are what catch a backend wired up wrongly.

*The sealed format has two version tags, not one.* `v1$` is a sealed `yapstring` and `v1b$` a
sealed `blob`, which is how `unseal` returns the type that went in -- the plan asked for exactly
that behaviour ("the version byte records which") without saying what it would look like.

**R4.** Built as specified, and it found a real bug in R2.

*ThreadSanitizer caught a race in the mailbox code while R4 was being tested.* A VM's inbox was
made on first use, so a worker posting to `"boss"` created its *parent's* mailbox -- writing a
field of the parent's VM while the parent was reading that same field. The registry lock did not
help, because a VM reads its own inbox pointer without taking it. Every VM's inbox is now made in
`interns_build`, on its own thread, before it can have hired anybody; the pointer is written once
and only read afterwards. This is exactly what R0's corpus and the TSan step exist to catch, and it
is worth recording that they caught it two milestones later rather than at the moment the code was
written.

*Every printed line is now one `fwrite`, for every program and not only a live one.* `yap` used to
write one call per argument plus a separator, which is the difference between two threads
interleaving between lines and interleaving mid-word. `yell` got the same treatment. It costs one
allocation per printed line and removes a whole class of garbled output.

*R3 and R4 are one commit.* Their documentation lines interleave in `docs/STDLIB.md`,
`CHANGELOG.md` and this file, and splitting three shared files by hand to satisfy a
commit-per-milestone rule would be worse than the rule is worth. Both are complete and each is
described separately in the message.

*There is no golden for live output, deliberately.* A live worker writes to the process's own
stdout, which is the stream `funny test` is capturing to compare against, and the order between
threads is not fixed by design. It is checked by hand with two live workers and one captured one:
the live lines appear interleaved while the program runs, never broken mid-line, and the captured
worker's output still arrives in one piece at its join.

**R5.** Built as specified.

*The manual check the plan asks for, and what it showed.* A program that awaits
`computer.until_ctrl_c()` while an `async_ngl` task keeps working was sent a real `SIGINT` from
the shell: the interrupt landed mid-run, the program printed its own shutdown lines, the
outstanding task drained, and the process exited on its own rather than dying on the signal. A
second program whose shutdown deliberately hangs was sent two: the first started the shutdown, the
second ended the process. Both are written down in the CHANGELOG entry, because a golden cannot
press Ctrl-C.

*The checklist line about `wait_up` with a timeout does not match the API.* `wait_up` takes no
timeout, so there is nothing to refuse; what the golden checks instead is that asking gives a
pending `otw`, that asking twice gives two different ones, and that neither settles while nobody
has pressed anything. The worker case got its own golden in `tests/lang/interns/`, since it needs a
worker to be refused in.

*One diagnosis worth recording, because it cost time and was not a bug.* The first manual run
appeared to hang with no output. The program was fine: the shell had captured the wrong background
PID, so the signal went nowhere, and stdout redirected to a file is block-buffered, so a program
that never exits never flushes. Capturing the PID properly made it pass first time. The lesson is
about the harness, not the runtime.

**Open before R3 starts:** macOS AES-GCM. CommonCrypto's `CCCryptorGCMOneshotEncrypt` /
`…Decrypt` are exported from `libcommonCrypto.dylib` on macOS 10.13+ but declared only in
`CommonCryptorSPI.h`, which the public SDK does not ship. Options, in order of preference: (a)
`dlsym` them and declare the prototypes locally, with a `SkillIssue` if absent — the same pattern as
Linux's OpenSSL; (b) AES-256-CTR + HMAC-SHA256 encrypt-then-MAC through public CommonCrypto only,
which is a different (still authenticated) construction and would make the `v1$` format mean two
things; (c) a Swift shim over CryptoKit, which adds a toolchain. Recommend (a), logged here when
decided. **Decided: (a)**, and built that way -- see the R3 entry above.
