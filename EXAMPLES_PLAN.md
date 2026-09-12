# FunnyLang — Examples Plan: six more `extensive_examples/`

> **Status:** planned, nothing built.
> **Prerequisite:** `RUNTIME_PLAN.md`, complete. Each example below says which of its milestones it
> leans on; three of the six could be written against today's runtime, but the plan is sequenced
> to run after it so that none of them has to be rewritten when it lands.
> **Deliverable:** six programs under `extensive_examples/`, each with a README, each with a golden
> that `funny test extensive_examples` runs on every platform, and none of them a toy — each one
> is the kind of thing a person would write to find out whether a language can carry it.

---

## 0. Rules for the executing agent

1. **An example is FunnyLang.** If it needs something the runtime does not have, that is a
   `RUNTIME_PLAN.md` §9 entry and a small, general primitive — never a special case for one
   example. `web_server/` got `open_shop` that way; `web_server_https/` got `open_secure_shop`.
2. **Every example has a golden**, and the golden is deterministic: it drives the program over
   loopback, from a temp directory, on a port the OS picks, and asserts on facts that are true in
   every interleaving. `web_server_https/test_server.funny` is the model.
3. **Every example has a README** that says what it is, how to run it, what it measured, and —
   in a section titled exactly that — what it is not. An example that claims more than it does is
   worse than one that does less.
4. **`the_script()` for every path.** An example runs from any working directory.
5. **The first thing an example does is fail safely.** `web_server_https/server.funny`'s "failing
   safely" note: a program that hires interns must wind every one of them down before it raises,
   or it hangs in its own teardown.
6. **Stage by explicit path; commit per example; push; never merge to master yourself.**

---

## 1. The six, in order

| # | Example | Directory | Shows | Runtime it leans on |
|---|---|---|---|---|
| E1 | Parallel word count | `extensive_examples/word_count/` | real threads, a worker pool, map-reduce | R2 DMs, R4 live output |
| E2 | Static site generator | `extensive_examples/site_gen/` | text processing; the beginner's example | nothing new |
| E3 | A Lisp | `extensive_examples/lisp/` | the language carrying a language | nothing new; R8 for readability |
| E4 | Key-value database | `extensive_examples/kv/` | persistence, a wire protocol, a CLI client | R1 blob, R6 atomic replace, R5 Ctrl-C, R7 json |
| E5 | Chat over WebSockets | `extensive_examples/chat/` | DMs at their best; binary framing; broadcast | R1, R2, R3, R4, R5, R7 |
| E6 | Reverse proxy | `extensive_examples/proxy/` | the TLS client with a pinned CA; streaming bytes | R1, R2 |

E1–E3 first because they are small and independent. E4 and E6 are medium. E5 is the big one and
comes after E4, whose framing and client code it reuses.

And one **E0**, first of all: bring `web_server_https/` onto the new runtime, since it was the
reason for most of it — `vault` for passwords and sealing (with a `--key-file`), `blob` so it can
serve a PNG, DMs so the keeper is a function on the main thread rather than a socket protocol, live
output for the log, `until_ctrl_c` for shutdown, `json` instead of `json.funny`, and
`filez.yeet_out_atomic` for the config. Its README's "protected at rest, honestly" section then
says the honest thing for real. That is the integration test for `RUNTIME_PLAN.md`.

---

## 2. Each example

### E0 — `web_server_https/` on the new runtime

- Delete `crypto.funny` and `json.funny`. `store.funny`'s `seal`/`unseal`/`hash_password` become
  `vault` calls; the `b64:`/`sha256:` prefixes become `v1$`/`pbkdf2-sha256$`; a config in the old
  format is migrated on load (re-hashing needs the password, so old password hashes are kept and
  verified with a fallback until the user changes them — the README says so).
- `--key-file PATH`, default `<data>/../vault.key`, generated on first run if absent, mode 0600
  on POSIX. Never inside `data/`.
- The keeper becomes `keeper.funny` *as a module on the main thread*: workers `dm("boss", …)`,
  the main thread's event loop answers by `dm(worker, …)`. Same protocol, no loopback sockets, no
  shared secret, no connection pool. Measure before and after: the README's numbers table gains a
  column.
- Workers hired `{"live": fax}`; the log goes straight to stderr; the `subscribe` op goes away.
- `await_fr computer.until_ctrl_c()` then `stop_server`; the golden checks the activity file is
  whole after a stop.
- `web/` gains `logo.png` served as a `blob`; the path guard allows `png`, `jpg`, `ico`, `woff2`.
- Its golden still prints byte-identical output on every platform.

**~600 lines changed**, most of it deletion.

### E1 — Parallel word count (`word_count/`)

```console
$ funny run extensive_examples/word_count/count.funny -- ./corpus --workers 8 --top 20
counted 2,418,113 words in 312 files with 8 workers in 1.84 s (1 worker: 11.2 s)
the        141,208
of          88,101
…
```

- **Shape**: the main thread lists the files and hires `--workers` interns from `worker.funny`.
  Each worker loops on `check_dms()`: a message is a file path; it reads it, counts words into a
  `groupchat`, and `dm("boss", counts)`; `"stop"` ends it. The main thread deals paths as workers
  come free (a job per free worker, not a static split — the point `web_server/`'s `/primes` made
  about balancing), merges the counts, and prints the top N.
- **Two other strategies, for the README**: a static split (`--split static`) and one intern per
  file (`--split file`), timed against the pool. The lesson is that the pool wins and why.
- **Tokenizing**: lowercase, Unicode letters via `yapper.is_letter`, apostrophes inside words kept.
- **Golden**: a generated corpus of known composition (so the counts are known), 4 workers; asserts
  the totals and the top 5, which are true in any order. `--workers 1` versus 4 produce identical
  output.
- **README** measures 1/2/4/8 workers on the same corpus. **~350 lines.**

### E2 — Static site generator (`site_gen/`)

```console
$ funny run extensive_examples/site_gen/build.funny -- ./site ./out
built 14 pages, 3 posts, 1 feed in 0.21 s → ./out
```

- **Input**: a directory of Markdown files with a front-matter block (`title`, `date`, `tags`,
  `draft`), a `layout.html` with `{{ title }}`-style holes, `static/` copied through.
- **Markdown**: headings, paragraphs, emphasis, links, images, code spans, fenced code blocks,
  ordered and unordered lists, block quotes, horizontal rules — CommonMark's everyday subset, in
  `markdown.funny`, with the spec's examples for those constructs as the golden. Not tables, not
  footnotes, not raw HTML pass-through (escaped instead — it is a site generator, not a sanitizer,
  but the README says which).
- **Output**: one HTML file per page, an index of posts newest first, a tag page per tag, an Atom
  feed, and a `sitemap.txt`. Dates via `clock.date_yap`-style formatting of the front matter's
  ISO date, done in FunnyLang.
- **Golden**: a small site under `site_gen/example/`; the build's output compared to a committed
  `expected/` tree file by file (paths sorted, so it is deterministic).
- **Why it is the beginner's example**: no threads, no sockets, no tricks; a reader who knows
  Markdown can follow every line. The README is written for that reader. **~700 lines.**

### E3 — A Lisp (`lisp/`)

```console
$ funny run extensive_examples/lisp/lisp.funny
> (define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
> (fib 20)
6765
$ funny run extensive_examples/lisp/lisp.funny -- examples/queens.lisp
```

- **A Scheme-ish Lisp**: integers (arbitrary precision for free — `numba`), floats, strings,
  symbols, lists, booleans, nil; `define`, `lambda` with closures, `if`, `cond`, `let`, `quote`,
  `quasiquote`/`unquote`, `set!`, `begin`, `and`/`or`; proper tail calls (the evaluator is a
  trampoline, so `(loop 1000000)` does not hit `TooDeepBro`); `define-macro` with the classic
  examples (`when`, `unless`, `my-or`); a REPL with history via `ask`; error messages with the
  offending form.
- **Built-ins**: arithmetic, comparison, `car`/`cdr`/`cons`/`list`/`append`/`length`/`map`/
  `filter`/`reduce`, string ops, `display`/`newline`, `load`.
- **Golden**: `lisp/examples/*.lisp` each with an `.expected`: fib, queens, a meta-circular
  evaluator (`eval` written in the Lisp, evaluating fib), tail-recursion depth, macros.
- **Why it is here**: it is the classic "can the language carry a language" showcase, and the
  first example with no networking, no threads and no files that is still not small. With R8's
  leading-dot continuation the evaluator's dispatch reads as a chain. **~900 lines.**

### E4 — Key-value database (`kv/`)

```console
$ funny run extensive_examples/kv/server.funny -- --data ./kvdata
kv listening on 127.0.0.1:6380 — ctrl-c to stop
$ funny run extensive_examples/kv/cli.funny -- set greeting "yo"
OK
$ funny run extensive_examples/kv/cli.funny -- get greeting
yo
```

- **Storage**: an append-only log of operations (`set`, `del`, `expire`) as one `json` line each,
  written with `filez.append_to`; a full snapshot (`json.spill`, pretty) written atomically with
  `filez.yeet_out_atomic` every N operations or on shutdown; on start, the snapshot is loaded and
  the log since it replayed. Values are `yapstring` or `blob` (a `blob` stored as `b64:`). Keys
  expire (`expire key seconds`), swept by a timer task.
- **Protocol**: RESP-like, line-based and easy to type over `nc`: `*3\r\n$3\r\nSET\r\n…`
  — or, simpler and still honest about it, a length-prefixed text protocol of this example's own:
  `SET <key> <len>\r\n<bytes>\r\n`. Decision at build time, §9.
- **Commands**: `GET SET DEL EXISTS KEYS <glob> EXPIRE TTL INCR MGET MSET FLUSH INFO PING`.
- **Concurrency**: one thread, one event loop, tasks per connection, exactly `web_server/`'s
  shape; a single-threaded store needs no keeper. The README says why this is the right shape for
  a store (every command touches the same map) where it was the wrong shape for a web server.
- **Shutdown**: `until_ctrl_c` → snapshot → exit; the golden kills a server with a stop command and
  restarts it to prove the data survived.
- **Client**: `cli.funny` speaks the protocol; `kvclient.funny` is a library other examples import
  (E5 uses it for presence).
- **Golden**: server on port 0, client intern drives every command, restart, expiry (with a short
  TTL and `clock.chill`), a `blob` round trip. **~800 lines.**

### E5 — Chat over WebSockets (`chat/`)

```console
$ funny run extensive_examples/chat/serve.funny -- 8443
chat at https://localhost:8443 on 8 threads — ctrl-c to stop
```

- **Built on `web_server_https/`**: the same TLS listener, worker threads, static `web/`, sessions
  and sign-in (imported from `../web_server_https/`, which is why E0 must land first and why that
  example's modules must stay importable). New: `ws.funny`, RFC 6455 — the `Upgrade` handshake
  (`Sec-WebSocket-Accept` is SHA-1, so `vault.sha1` is one more small primitive, logged in
  `RUNTIME_PLAN.md` §9), framing with `blob` (masking is an XOR over bytes; text, binary, ping,
  pong, close frames; fragmentation), and a per-connection task that reads frames and awaits DMs at
  the same time.
- **Broadcast is DMs**: a worker that receives a chat message `dm("boss", …)`; the main thread
  (the "room") `dm`s it to every worker; each worker writes it to the connections it holds. That is
  the design the keeper could not express, and the README says so with numbers: messages per
  second, one worker versus eight.
- **The page**: rooms, who is online (presence in E4's `kv`, optional — `--kv host:port`), message
  history (last 100, in memory), typing indicators, reconnect with backoff, all in `web/js/chat.js`
  with no framework and a CSP of `default-src 'self'; connect-src 'self'`.
- **Golden**: a WebSocket client in FunnyLang (`ws_client.funny`, the other half of `ws.funny`) as
  interns: three clients in one room, each sends a known set, each receives everybody's; asserted
  as sets. Ping/pong, a close handshake, a masked frame, a 2 MB message fragmented.
- **~1,400 lines.**

### E6 — Reverse proxy (`proxy/`)

```console
$ funny run extensive_examples/proxy/proxy.funny -- 8443 --to localhost:9001,localhost:9002 --ca certs/ca.pem
```

- **Shape**: TLS in front (`open_secure_shop`), N backends behind, each an instance of
  `web_server_https/` started by the golden (or `--start N` for the demo). Every request is a new
  `slide_into(backend, {"tls": fax, "ca": …, "server_name": "localhost"})` — the pinned-CA client
  doing real work — with the request forwarded byte for byte as a `blob`, and the response streamed
  back as it arrives (`hear_them_out` raw → `holler_back`), so a large response never sits in
  memory whole.
- **Balancing**: round-robin by default; `--least-connections`; a backend that refuses a connection
  is marked down for 5 s and retried. `X-Forwarded-For` and `X-Forwarded-Proto` added;
  `Connection` and hop-by-hop headers stripped; `Host` rewritten.
- **Threads**: acceptor workers as in `web_server_https/`; the balancer's state (which backend is
  next, which are down) is on the main thread and asked with DMs — a small, cheap message per
  request, which is a good measurement of what a DM costs.
- **Golden**: two backends on ports the OS picks, the proxy in front, a client intern sends 40
  requests and checks each answered 200 and that both backends saw some (via `/api/health`'s
  thread/backend id); one backend stopped mid-run and every request still answered.
- **README**: requests per second through the proxy versus direct, on one and on eight threads.
  **~600 lines.**

---

## 3. Milestones

### E0 — `web_server_https/` on the new runtime
- [x] `vault` for passwords and sealing; `--key-file`; old-format migration
- [x] Keeper as a main-thread module over DMs; loopback protocol and secret gone
- [x] Live worker output; `until_ctrl_c`; `json`; atomic config writes; a PNG served
- [x] Golden byte-identical on all platforms; README numbers updated (keeper before/after)

### E1 — word count
- [x] Pool over DMs; static and per-file strategies for comparison; generated-corpus golden; README with timings

### E2 — site generator
- [ ] `markdown.funny` with CommonMark examples as goldens; front matter; layouts; index, tags, feed, sitemap; `example/` and `expected/`

### E3 — Lisp
- [ ] Reader, evaluator (trampolined), environments, macros, REPL, `load`; `examples/*.lisp` goldens

### E4 — key-value database
- [ ] Log + snapshot storage with atomic writes; protocol (decision logged); commands; expiry; CLI and client library; restart golden

### E5 — chat
- [ ] `ws.funny`, `ws_client.funny`; rooms over DMs; the page; presence via E4 (optional); golden with three clients
- [ ] `vault.sha1` in `RUNTIME_PLAN.md` §9

### E6 — reverse proxy
- [ ] Pinned-CA client per request; streaming; balancing; health; golden with two backends and a failure

---

## 4. Cost sheet

| Example | Lines (est.) |
|---|---|
| E0 | 600 changed |
| E1 | 350 |
| E2 | 700 |
| E3 | 900 |
| E4 | 800 |
| E5 | 1,400 |
| E6 | 600 |
| READMEs | 900 |

---

## 5. Testing

- `funny test extensive_examples` runs every golden on every CI platform, and the TSan leg runs it
  too (`RUNTIME_PLAN.md` R0).
- Every golden binds port 0, uses a temp directory, drives itself with interns, and asserts
  order-independent facts.
- E0's golden is the integration test for the whole runtime plan.
- Measurements in READMEs are made on one machine, say which, and compare one thread to many on
  the same machine — a number without its baseline is not a measurement.

---

## 6. Decisions already made

- **E0 first.** Making the runtime plan's changes real in the program that motivated them is the
  cheapest way to find out whether they are the right changes.
- **E5 imports from E0** rather than copying the HTTPS server. That means `web_server_https/`'s
  modules are a library other examples depend on, and its README says so.
- **E4 is single-threaded.** The right shape for a store, and the contrast with E5 is the lesson.
- **No new runtime primitives are planned here.** `vault.sha1` for WebSockets is the one known
  exception and is logged in advance; anything else found along the way is a `RUNTIME_PLAN.md` §9
  entry first.

---

## 7. Deviations log

*(empty — nothing built yet)*
