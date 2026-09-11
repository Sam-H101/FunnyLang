# FunnyLang — HTTPS Web Server Plan (`extensive_examples/web_server_https/`)

> **Status:** built. H0–H4 and H6 done and verified on Linux (WSL2) and Windows 11; the macOS
> backend (H5) is written but has not been compiled or run — there is no Mac here, and CI builds
> macOS when the branch is pushed. Branch `feature/https-web-server`, cut from
> `feature/async-threading`. §5 has the state of each milestone, §9 every deviation.
> **Prerequisite:** `ASYNC_PLAN.md` complete (it is — A0–A7), and `extensive_examples/web_server/`
> in the tree, since this example is that one grown up: TLS on the wire, many threads behind the
> door, and a real site instead of a route list.
> **Deliverable:** `funny run extensive_examples/web_server_https/serve.funny` serves a small, modern
> site over HTTPS (TLS 1.2 minimum, TLS 1.3 where the backend has it), secure by default, on
> **24 OS threads** each running its own event loop — and everything above the TLS record layer is
> FunnyLang.

### Amendments from the project owner (2026-09-10)

These override anything below that disagrees with them, and each has a §9 entry.

1. **PKCS#12 everywhere.** The server identity is one `.p12` file on every platform, not PEM. That
   is the one format all three OS TLS stacks import natively, so it "just works" everywhere.
2. **The front end lives in its own `web/` subfolder** of plain HTML, JS and CSS, served from disk.
   It replaces the embedded-strings `assets.funny` and server-rendered `pages.funny`.
3. **More than a demo site.** Simple but modern: a **landing page**, a **login page**, a **user
   settings page**, and an **activity page** that shows everything users have done.
4. **Data is JSON on disk.** A committed default config is used unless a *customization* file
   exists. The customization file is written the first time a user changes their password, their
   profile, or their defaults. On load, customization is tried first, the default second.
5. **The activity log is its own JSON file.**
6. **Sensitive data is protected at rest.** Passwords are a one-way hash (unsalted SHA-256 — it is a
   demo). Everything else sensitive is base64 — it is a proof of concept.

---

## 0. Rules for the executing agent

1. **Everything above the socket is FunnyLang.** HTTP/1.1, routing, sessions, CSRF, rate limiting,
   the thread pool, the shared-state service, JSON parsing, SHA-256, base64, the site's back end:
   all `.funny`. The C runtime gains *transport* primitives only (§3), the same way `web_server/`
   got `open_shop` and friends. If you find yourself writing an HTTP parser or a router in C, stop.
2. **TLS itself is not written in FunnyLang, and this is a decision, not a shortcut.** §2.1 says why.
   `NATIVE_PLAN.md` §3.1 already rules that hand-rolling TLS "is not on the table at any scope"; that
   rule applies to the server side exactly as it did to the client side. Server-side TLS is added to
   `platform.c` behind `platform.h`, using the TLS each OS already ships (§3.2).
3. **`platform.c` is still the only file allowed `#ifdef _WIN32`.** Three TLS backends live there
   and nowhere else. `internet.c` never learns which backend it got.
4. **Certificate verification stays on, and nothing here adds a way to turn it off.** The one new
   knob (§3.1, `slide_into`'s `ca` option) *narrows* what is trusted to one named root; it never
   widens it. "Silently accepting bad certificates is the kind of joke that stops being funny."
5. **Nothing blocks — including the handshake.** `web_server/` earned its concurrency by never
   calling a blocking read. A TLS handshake is several round trips, and a blocking one would park a
   worker's every task for a WAN RTT. §2.4 gives the shape; it is not optional.
6. **A golden that cannot be deterministic is not a golden.** 24 threads accepting from one socket is
   the definition of nondeterministic. §6 says what to assert on instead.
7. **Every deliberate deviation gets a §9 entry**, same convention as `NATIVE_PLAN.md` and
   `ASYNC_PLAN.md`. Including the ones that turn out to be wrong.
8. **Commit per milestone on `feature/https-web-server`, stage by explicit path, never merge to
   master.** Other sessions work on this repository at the same time.

---

## 1. What "done" looks like

```console
$ funny run extensive_examples/web_server_https/serve.funny -- 8443
serving https://127.0.0.1:8443 on 24 threads — ctrl-c to stop
redirecting http://127.0.0.1:8080 to https
config: data/config.default.json (no customization yet)
21:04:11  #07  127.0.0.1:51150  TLSv1.3  GET /  -> 200  2841b
21:04:11  #13  127.0.0.1:51154  TLSv1.3  GET /css/site.css  -> 200  1907b
21:04:12  #02  127.0.0.1:51160  TLSv1.2  POST /api/login  -> 200  88b  admin

$ curl -s --cacert extensive_examples/web_server_https/certs/ca.pem https://localhost:8443/api/health
{"ok":true,"tls":"TLSv1.3","thread":7,"threads":24}

$ curl -sI --cacert certs/ca.pem https://localhost:8443/ | grep -i strict
strict-transport-security: max-age=31536000; includeSubDomains

$ curl -s --tls-max 1.1 --cacert certs/ca.pem https://localhost:8443/
curl: (35) ... protocol version                      # 1.0 and 1.1 are refused at the handshake

$ curl -si http://localhost:8080/settings | head -2
HTTP/1.1 301 Moved Permanently                        # plain http only ever redirects
```

- `funny test extensive_examples` runs `test_server.funny`, which starts the real server — keeper,
  workers, redirect listener — and drives it over real TLS on loopback with a committed test CA.
- `--threads N` picks the pool size (default 24, clamp 1..64). The `#07` in the log is which thread
  answered.
- The site (§2.6): a landing page, a login page, a settings page (profile, defaults, password), and
  an activity page listing every action, filterable, paginated by the user's own default page size.
  Changing anything on the settings page writes the customization file; every action is appended to
  the activity file.

---

## 2. Why this shape

### 2.1 TLS is a transport, and it is not written in FunnyLang

The ask is "written solely in FunnyLang". Above the record layer, it is. The record layer is not,
for reasons that are about *security*, not effort:

| | TLS in FunnyLang | TLS from the OS |
|---|---|---|
| Constant-time crypto | impossible — the VM's `numba` ops, `stash` indexing and string compare all branch on data | yes |
| Handshake cost | RSA-2048 / P-256 on an interpreted bignum: seconds per handshake | ~1 ms |
| Trust store, revocation, updates | none | the OS's, kept current by the OS |
| Bugs found by | us | everyone |

"Secure by default" and "hand-rolled AES-GCM in an interpreter" cannot both be true. So TLS is added
where the client-side TLS already lives — `platform.c`, behind `platform.h` — and shows up in
FunnyLang as **the same socket handles doing the same calls**: `hear_them_out` reads plaintext,
`holler_back` writes plaintext, `hold_up` still means "something to read". `http.funny` and
`routes.funny` never learn whether a connection is encrypted. That mirrors exactly how the client
side was done (`HttpConn { socket, optional TLS session }` — `NATIVE_PLAN.md` §9, N5b).

SHA-256 for passwords is a different case: it is a pure function over bytes the server already
has, it runs once per login or password change, and nothing about it needs to be constant-time
beyond the final comparison, which `security.funny` does in constant time. So it is FunnyLang
(`crypto.funny`), like everything else above the socket.

### 2.2 Twenty-four threads: acceptors, not a queue

`interns` gives real OS threads, but a worker is a *program* with its own VM and its own heap, hired
once with one argument. There is no channel to hand a running worker a new connection. So the shape
that fits is the classic **pre-threaded server**:

- `serve.funny` (main thread) opens the secure listener once. A listener is a `numba`, so it crosses
  to a worker.
- It hires **N acceptor workers** (`worker.funny`), each given the listener handle. Each runs the
  `web_server/` accept loop unchanged in spirit: `await_fr internet.hold_up(listener)`, then
  `next_customer(listener, 0)`, then one `async_ngl` task per connection.
- Every worker is *also* an event loop (a worker VM runs `vm_run`, which spawns the entry task and
  runs the loop like the top level does), so it is 24 threads × as many in-flight connections as
  each wants. The pool being larger than the core count (24 on an 8-core machine) is deliberate and
  harmless: a thread parked in `poll` costs nothing, and the surplus is what keeps a thread that is
  busy rendering a page from being the only one that could accept.
- Two more interns: the **keeper** (§2.3) and the **redirect** listener. With the main thread that
  is 27 threads at the default setting.

The kernel hands each new connection to exactly one of the threads polling the listener. Two things
have to be true for that to be safe, and one of them is not true today:

1. `next_customer(listener, 0)` must return `ghost` — not block — when another thread won the race.
   Today the listener socket is *blocking* (`platform_tcp_listen` never sets `FIONBIO`/`O_NONBLOCK`,
   and `platform_tcp_accept` polls and then calls a blocking `accept()`), so a thread that polled
   "readable" and lost the race sits in `accept()` until the *next* connection arrives, with all of
   its own tasks frozen. §3.1 makes the listener non-blocking. This is a ~30-line change and a
   prerequisite for everything.
2. The thundering herd (24 threads wake, one wins) is real but cheap at this scale. `SO_REUSEPORT`
   with one socket per thread would remove it, but it is Linux-only in the useful form, and
   correctness first.

### 2.3 Shared state without a shared heap: the keeper

The config, sessions, the activity log, rate-limit counters and hit counts must be one thing, and
24 heaps cannot share it. The runtime's rule is "one thread owns one heap"; the honest answer is not
to fight it but to do what the project already does for two ends of a conversation: **talk over a
socket.**

`keeper.funny` is one more intern. It owns *all* mutable state and is the **only thread that ever
touches `data/`**, so two threads can never write the same JSON file at once. It binds
`127.0.0.1:0` and speaks one JSON object per line in each direction. Workers keep a small pool of
persistent loopback connections to it — a task borrows one, asks, gives it back; tasks on one
thread only switch at an `await_fr`, so the pool needs no lock. A random 32-byte secret (§3.3) is
handed to every worker at hire time and must be the first line on each keeper connection, so
another process on the same machine cannot talk to it.

```
{"op":"begin","ip":"127.0.0.1","sid":"…"}         -> {"ok":true,"allowed":true,"user":{…}|null,"csrf":"…"}
{"op":"login","user":"admin","hash":"…","ip":…}   -> {"ok":true,"sid":"…","csrf":"…"} | {"ok":false,"error":…}
{"op":"logout","sid":"…"}                         -> {"ok":true}
{"op":"profile","sid":"…","changes":{…}}          -> {"ok":true,"user":{…}}      (writes customization)
{"op":"password","sid":"…","old":"…","new":"…"}   -> {"ok":true}                 (writes customization)
{"op":"activity","sid":"…","filter":{…}}          -> {"ok":true,"entries":[…],"total":n}
{"op":"done","thread":7,"line":"…","event":{…}}   -> {"ok":true,"stop":false}    (log line + activity)
{"op":"subscribe"}                                 -> a stream of log lines, one per request (main thread)
{"op":"stop"}                                      -> {"ok":true}
```

**Why log lines go through the keeper.** A worker's `yap` *and* `yell` are captured and replayed
only when somebody waits for that worker (`interns.c`: "the worker's `yap` and `yell` are held
rather than printed"). For a server, "when somebody waits" is shutdown — so a log written from a
worker would appear all at once, at the end. The request log therefore travels with the `done`
message the worker already sends; the keeper forwards each line to the main thread, which
`subscribe`d at start-up and prints them with `yell`. That needs no new runtime surface, and the
log still comes out in the order requests finished.

If the keeper turns out to be the bottleneck, the alternative is a native shared key-value store in
`interns` (`interns.whiteboard`, mutex-guarded, portable values only). That is a §9 decision to make
*with numbers*, not up front.

### 2.4 Nothing blocks, handshake included

A TLS handshake is 1–2 round trips. On loopback that is nothing; on a real link it is 20–200 ms, and
a blocking `SSL_accept` would freeze every task on that worker for it. So the handshake is driven
the same way reads already are:

```funny
// after next_customer on a secure listener:
bruh (!internet.handshake(conn)) {                       // fax = done, cap = needs more bytes
    sus (!await_fr internet.hold_up(conn, HANDSHAKE_TIMEOUT_MS)) {
        bounce hang_up(conn)                              // a client that never finishes gets dropped
    }
}
```

`handshake` runs one step of the handshake on a non-blocking socket and returns whether it
finished. A failed handshake raises `SkillIssue` (a client offering only TLS 1.0, a bad
ClientHello, plain HTTP sent to the TLS port) and the task logs one line and closes. Two
consequences the runtime must get right:

- **`hold_up` on a TLS connection settles immediately if the session has buffered plaintext**
  (`SSL_pending() > 0`, or Schannel/Secure Transport's equivalent), because the socket may have
  nothing left to read while a whole request already sits decrypted in the library. Polling the fd
  alone would hang that task forever.
- **`hear_them_out(conn, n, 0)` may return `ghost` on a TLS connection even right after `hold_up`
  said readable** — the bytes were a partial record. The existing loop already treats `ghost` as
  "wait again", so no FunnyLang changes, but it must be documented.

### 2.5 Secure by default, itemised

Every one of these is on without a flag. There is no `--insecure`.

| Layer | Default |
|---|---|
| Protocol | TLS 1.2 minimum; 1.3 negotiated when the client and backend can (Secure Transport tops out at 1.2) |
| Ciphers (1.2) | ECDHE only, AEAD only: `ECDHE-ECDSA-AES128-GCM-SHA256 ECDHE-RSA-AES128-GCM-SHA256 ECDHE-ECDSA-AES256-GCM-SHA384 ECDHE-RSA-AES256-GCM-SHA384 ECDHE-ECDSA-CHACHA20-POLY1305 ECDHE-RSA-CHACHA20-POLY1305`; server preference |
| Ciphers (1.3) | library defaults (all AEAD) |
| Renegotiation, compression | off |
| Plain HTTP | a second listener that answers **only** `301 → https://…` and never serves content |
| Bind | `127.0.0.1` unless told otherwise |
| Headers | `Strict-Transport-Security: max-age=31536000; includeSubDomains`, `Content-Security-Policy: default-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'`, `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: strict-origin-when-cross-origin`, `Permissions-Policy: camera=(), microphone=(), geolocation=()`, `Cache-Control: no-store` on every API response |
| Cookies | `__Host-sid=…; Secure; HttpOnly; SameSite=Strict; Path=/`; session id is 32 bytes from the OS CSPRNG (§3.3), never `rizz.roll` |
| CSRF | a per-session token, sent by the page in `X-CSRF-Token` on every state-changing request, compared in constant time |
| Login | same answer and same work for "no such user" and "wrong password"; 5 failures per IP per 5 minutes → `429` |
| Limits | request line 8 KiB, headers 16 KiB / 64 headers, body 64 KiB, header-read deadline 5 s, handshake deadline 5 s, per-thread in-flight cap 256 |
| Rate limit | per-IP window in the keeper: 300 requests / 10 s → `429` |
| Static files | only from `web/`, only an allow-listed set of extensions, no `..`, no dot-files, no directory listings, no symlink tricks (the path is rebuilt from validated segments) |
| At rest | password: unsalted SHA-256 (owner's call, §9); email, display name, IPs, user agents, session details: base64, behind one `seal`/`unseal` pair so a real cipher is a one-function change |
| Output | the front end sets `textContent`, never `innerHTML`, with data; JSON via the writer in `http.funny` |
| Errors | `500` shows a flavor and a message, never a stack trace |
| Logging | to stderr, with the thread number; never a body, a cookie, or a password |

### 2.6 The site and its data

**Front end — `web/`.** Four pages sharing one stylesheet and one small script library:

| Page | File | What it does |
|---|---|---|
| Landing | `web/index.html` | What this is, a live "threads / TLS version / uptime" strip from `/api/health`, sign-in call to action |
| Login | `web/login.html` | Username + password, errors inline, redirects to settings on success |
| Settings | `web/settings.html` | Profile (display name, email), defaults (theme, activity page size, landing page after login), password change; banner while the default password is still in use |
| Activity | `web/activity.html` | Every action, newest first: time, user, action, detail, IP, thread; filter by user and by action; pagination; admins see everybody, other users see themselves |

Plus `web/css/site.css`, `web/js/api.js` (fetch + CSRF + errors), one script per page, and
`web/favicon.svg`. No inline script or style anywhere, which is what lets the CSP be
`default-src 'self'`. Light and dark themes follow the user's saved default, falling back to
`prefers-color-scheme`.

**API — `/api/*`, JSON in and out.** `GET /api/health`, `POST /api/login`, `POST /api/logout`,
`GET /api/me`, `POST /api/profile`, `POST /api/defaults`, `POST /api/password`,
`GET /api/activity?user=&action=&page=`. Pages are reachable without their `.html` (`/login`,
`/settings`, `/activity`); `/settings` and `/activity` redirect to `/login` without a session.

**Data — `data/`.**

| File | Written by | When |
|---|---|---|
| `config.default.json` | nobody — committed | never |
| `config.custom.json` | the keeper | the first time any user changes their password, profile, or defaults, and on every change after |
| `activity.json` | the keeper | at most once a second while there is anything new, and at shutdown; capped at the newest 5,000 entries |

On start the keeper loads `config.custom.json` if it exists and parses, otherwise
`config.default.json`, and logs which. A customization file that exists but will not parse is a
refusal to start, not a silent fall-back to defaults — falling back would quietly undo a password
change. `--data DIR` moves the two written files elsewhere (the test uses a temp directory); the
default config is always read from the example's own `data/`.

Protected fields are stored as `"b64:…"` for base64 and `"sha256:…"` for the password hash, so a
file says which treatment each value got. Users in the default config: `admin` (role `admin`) and
`demo` (role `user`), both with the password `funnylang` — which the settings page nags about until
it is changed.

---

## 3. Runtime extensions (C) — the exact surface

Everything below is a `platform.h` + `internet.c` (or `rizz.c` / `builtins.c`) addition.
`NATIVE.md`, `STDLIB.md` and `CHANGELOG.md` get the corresponding lines in the same milestone.

### 3.1 `internet` — the secure half of listening

| Function | Description |
|---|---|
| `internet.open_secure_shop(port, host?, opts)` | Like `open_shop`, plus a TLS context from `opts`: `{"pfx": "certs/site.p12", "password": "…"}` — PKCS#12 on every platform. Refuses to start without a readable identity. Returns a listener handle. |
| `internet.next_customer(listener, timeout_ms?)` | Unchanged signature. On a secure listener the returned groupchat gains `"secure": fax` and the connection has a TLS session attached but **not yet handshaken**. |
| `internet.handshake(conn)` | Drive the handshake one step. `fax` when done, `cap` when it needs the socket (caller awaits `hold_up` and calls again). Raises `SkillIssue` on failure. On a plain connection: `fax`, immediately. |
| `internet.hear_them_out` / `holler_back` / `kick_out` | Unchanged. Plaintext in, plaintext out; `kick_out` sends `close_notify` first. |
| `internet.hold_up(handle, timeout_ms?)` | Unchanged signature. Settles `fax` at once if the TLS session holds buffered plaintext (§2.4). |
| `internet.tls_info(conn)` | `{"version": "TLSv1.3", "cipher": "TLS_AES_128_GCM_SHA256"}` for the log and `/api/health`; `ghost` on a plain connection. |
| `internet.slide_into(host, port, opts?)` | Third argument may now be a groupchat: `{"timeout_ms": n, "tls": fax, "server_name": "localhost", "ca": "certs/ca.pem"}`. With `tls`, the returned connection is handshaken (blocking is fine here — this is a client) and verified: chain **and** hostname, against the system store, or against exactly the given `ca` file when one is named. A bare `numba` third argument still means a timeout, so existing callers do not change. |

The listener made by `open_shop` / `open_secure_shop` is **non-blocking**, and `platform_tcp_accept`
maps `EAGAIN` / `WSAEWOULDBLOCK` to `PLATFORM_SOCKET_TIMEOUT`. Accepted sockets are explicitly set
back to blocking (Windows and the BSDs inherit the flag; Linux does not; do not rely on either).

Implementation shape, in `platform.c`: a mutex-guarded side table `socket handle → TlsSession *`.
`platform_socket_recv/send/close` and `platform_poll_sockets` consult it, so `internet.c`'s existing
functions do not change. This is the client-side `HttpConn` idea, made process-wide because 24
threads will be reading it. (`PlatformMutex` already exists — ASYNC_PLAN A0.)

### 3.2 Backends — the TLS each OS already ships, server side, PKCS#12 in

| Platform | Backend | Work |
|---|---|---|
| Linux / BSD | OpenSSL via the existing `dlopen` (`libssl.so.3` → `.1.1` → `.so`, plus the matching `libcrypto`) | New symbols: `TLS_server_method`, `d2i_PKCS12_fp` / `PKCS12_parse` / `PKCS12_free` (libcrypto), `SSL_CTX_use_certificate`, `SSL_CTX_use_PrivateKey`, `SSL_CTX_check_private_key`, `SSL_CTX_ctrl` (min proto `0x0303`, extra chain certs), `SSL_CTX_set_options`, `SSL_CTX_set_cipher_list`, `SSL_set_accept_state`, `SSL_do_handshake`, `SSL_get_error`, `SSL_pending`, `SSL_get_version`, `SSL_get_current_cipher` + `SSL_CIPHER_get_name`, `SSL_CTX_load_verify_locations`. Absent library ⇒ `open_secure_shop` raises the existing "https needs OpenSSL…" `SkillIssue`. |
| Windows | **Schannel** (SSPI: `AcquireCredentialsHandle` / `AcceptSecurityContext` / `InitializeSecurityContext` / `EncryptMessage` / `DecryptMessage`), `secur32` + `crypt32` via `#pragma comment(lib, …)` beside the `ws2_32` / `winhttp` pragmas already at the top of `platform.c` | WinHTTP is client-only, so this is new. The identity comes in through `PFXImportCertStore` — PKCS#12 is Windows' native import format. TLS 1.3 needs `SCH_CREDENTIALS` (Windows 10 1809+); older Windows falls back to `SCHANNEL_CRED` with 1.2 only. Schannel's record framing means the side table keeps an in/out byte buffer per session. Client side too (for `slide_into` with `tls`), with `CertGetCertificateChain` against an exclusive root store when `ca` is given. The big one. |
| macOS | Secure Transport, server side (`SSLCreateContext(kSSLServerSide)`, `SSLSetCertificate` with the identity from `SecPKCS12Import`) | PKCS#12 is also macOS' native import format, which is why the owner picked it. Client side with `ca`: break on server auth, then evaluate the chain with `SecTrustSetAnchorCertificates` + `SecTrustSetAnchorCertificatesOnly` and an SSL policy naming the host — narrowing trust, never skipping it. |

The committed `certs/site.p12` is encrypted with PBE-SHA1-3DES and a SHA-1 MAC: the one PKCS#12
encoding OpenSSL 3, Schannel and Secure Transport all read without extra providers (§9).

Build order is Linux first — it is the backend that can be tested in the Ubuntu WSL2 distro on this
machine (`libssl.so.3`, `gcc` and `curl` are all present there), and the one with the least new C.

### 3.3 `rizz.entropy(n)` — bytes from the OS CSPRNG

`rizz` is a time-seeded generator (`rizz.c` seeds from `time(NULL)` xor a stack address); it is a
dice roller, not a token generator, and a session id from it is guessable. `rizz.entropy(n)`
returns `n` bytes as a lowercase hex `yapstring`, from `BCryptGenRandom` / `getrandom(2)` (falling
back to `/dev/urandom`) / `SecRandomCopyBytes`, behind `platform_random_bytes(buf, n)`.

### 3.4 `the_script()` — where am I

`interns.hire(path)` resolves against the working directory, and `web_server/` worked around it by
writing its worker to a temp file. That will not do here: `worker.funny` imports `http.funny`
beside it, and a quoted import resolves relative to the *importing file*, so the worker has to be
hired from where it lives. `the_script()` (an always-in-scope builtin, beside `the_args()`) returns
the running program's absolute path — inside a worker, its hire path. `serve.funny` then hires
`filez.join_path(filez.dir_of(the_script()), "worker.funny")` and the example runs from any
directory.

This is the one change that reaches into `selfhost/`. A caught error's `.file` is not a way round
it — it holds the bundle's *logical* name (`serve.funny`), not a path. The path is only known to the
command line, so `cli.funny` passes it to `sus.run_program`, `test.funny` passes it in
`sus.run_bytecode`'s options, `interns.hire` records its own, and the resolver learns the new
builtin's name — which means regenerating `native/toolchain_blob.c` by the procedure in
`bootstrap/STAGE0.md`, and `funny bootstrap --verify` proving the result is a fixed point.

### 3.5 Not added, on purpose

- **Binary static files.** `web/` is served with `filez.slurp`, which is text. Everything in `web/`
  is text — HTML, CSS, JS and an SVG favicon — so nothing here needs more. A `holler_bytes(conn,
  stash)` is a small addition if a later example wants to serve a PNG.
- **Keep-alive, HTTP/2, chunked bodies.** Still `Connection: close`, still honest about it.
- **Any way to turn verification off.** See rule 4.

---

## 4. The files

```
extensive_examples/web_server_https/
  PLAN.md             this document
  README.md           the tour, the numbers, the "what it is not"
  serve.funny         the command line: parses args, calls server.funny, prints the log
  server.funny        start/stop: listeners, hires the keeper, the redirect and N workers
  worker.funny        one acceptor thread: accept loop + handshake + routing
  keeper.funny        the state service: config, sessions, activity, rate limits, stop flag
  keeper_client.funny the worker's side of the keeper protocol, with its connection pool
  redirect.funny      the plain-http listener that only ever says 301
  http.funny          web_server/http.funny + cookies, limits, 303/401/403/429/431, security headers
  routes.funny        the router: static files from web/, /api/* handlers
  static.funny        safe path resolution under web/, content types, a per-thread cache
  store.funny         config and activity files: load (custom → default), save, seal/unseal
  security.funny      constant-time compare, CSRF, session cookies, rate-window math
  crypto.funny        SHA-256 and base64, in FunnyLang
  json.funny          a JSON reader and writer (the writer moved here from http.funny, §9)
  data/
    config.default.json
  web/
    index.html  login.html  settings.html  activity.html  favicon.svg
    css/site.css
    js/api.js  js/landing.js  js/login.js  js/settings.js  js/activity.js
  certs/
    make_cert.sh      openssl CLI: a local CA + a PKCS#12 leaf for localhost/127.0.0.1/::1
    ca.pem, site.p12  committed test material, clearly labelled, loopback names only
  test_server.funny   the whole thing over real TLS on loopback, from one program
  test_server.expected
  test_client.funny   the golden's client, hired as interns (not a golden itself)
```

`http.funny` is copied from `web_server/`, not imported from it — the two examples should each read
whole, and the first one should stay the simple one.

---

## 5. Milestones

### H0 — the listener that does not block, and `the_script()`
- [x] Non-blocking listener in `platform_tcp_listen`; `EAGAIN` → `PLATFORM_SOCKET_TIMEOUT` in
      `platform_tcp_accept`; accepted fd set blocking. Existing `web_server/` test still green.
- [x] `the_script()` builtin, through `cli.funny` / `test.funny` / `interns.hire`; blob
      regenerated; `funny bootstrap --verify` green.
- [x] A many-thread accept test: `tests/lang/interns/shared_listener` — four workers, one listener,
      forty loopback connections, every one answered exactly once.

### H1 — TLS server on Linux (OpenSSL, `dlopen`)
- [x] `open_secure_shop` (PKCS#12), `handshake`, `tls_info`; side table; `hold_up` honours pending.
- [x] `slide_into` with `{"tls", "ca", "server_name"}`.
- [x] Cipher list, min version, options from §2.5.
- [x] `rizz.entropy`.
- [x] `certs/make_cert.sh` and the committed test material.
- [x] Verified in WSL with `curl --cacert`, `curl --tls-max 1.1` (refused), `openssl s_client`
      with `-tls1_2` and `-tls1_3` (both verify), and with a CBC suite and an RSA-key-exchange suite
      (both refused).

### H2 — the site's back end, and its front end
- [x] `json.funny`, `crypto.funny` (checked against the FIPS 180-4 and RFC 4648 vectors, and against
      `sha256sum`), `store.funny`, `security.funny`, `static.funny`, `http.funny`, `routes.funny`.
- [x] `web/` — the four pages, a 404, the stylesheet, the scripts (`node --check` clean).
- [x] `data/config.default.json`; customization written on change; activity file flushed.

### H3 — the keeper, and 24 threads
- [x] `keeper.funny`, `keeper_client.funny`, `worker.funny`, `redirect.funny`, `server.funny`,
      `serve.funny`.
- [x] `--threads N`, `--seconds N`, `--requests N`, `--data DIR`, `--host`, `--http-port`,
      `--no-http`, `--pfx`, `--pfx-password`, `--public-host`.
- [x] Shutdown: keeper says "stop", workers drain in-flight tasks (the `web_server/` 5 s grace),
      the keeper flushes the activity file, main awaits every worker.
- [x] Measured (README): 1 thread against 24 on bursts of 200, 1,000 and 400 requests, all
      answered; forty silent connections held open do not slow a normal request down.

### H4 — Windows (Schannel)
- [x] Server and client sides; PKCS#12 import; the same curl and `openssl s_client` checks from a
      Windows shell, against a build cross-compiled with llvm-mingw and run natively.
- [x] `test_server.funny` green on Windows.

### H5 — macOS (Secure Transport, server side)
- [x] Server side with `SecPKCS12Import` (into a temporary keychain); client side with an anchored
      trust evaluation. **Written, not compiled or run.**
- [ ] CI job green — needs the branch pushed.

### H6 — docs
- [x] README with the tour, the measurements, the "what it is not".
- [x] `STDLIB.md` (`internet` additions, `rizz.entropy`, `the_script`), `NATIVE.md` (the side table,
      the three backends), `CHANGELOG.md`.

---

## 6. Testing

`test_server.funny` is the golden, and it must be deterministic on a machine with many threads
racing for one socket. The `web_server/` test already shows the technique: assert on
order-independent facts, or force an order.

- **Pure functions, with no socket** — JSON round trips, SHA-256 and base64 against published test
  vectors, seal/unseal, cookies, CSRF, limits, the constant-time compare, and the static-path guard
  against every traversal spelling worth trying.
- **Over real TLS, on loopback, with the committed CA.** The client is an intern using
  `slide_into(…, {"tls": fax, "ca": "certs/ca.pem"})`. Asserts: the negotiated version is ≥ 1.2;
  a plain-TCP client that sends HTTP to the secure port gets no page; the redirect listener answers
  `301` with the right `Location`; every security header is present; login with a wrong password is
  `401` and the sixth is `429`; a `POST` without a CSRF token is `403`; a profile change writes
  `config.custom.json` with sealed fields and a hashed password; a restart reads it back; the
  activity file holds the login, the change and the logout.
- **Across threads:** with `--threads 4`, several client interns each doing a known number of
  requests; the activity log afterwards holds exactly that many entries — a fact true in every
  interleaving.
- **Under TSan**, the way `tests/lang/interns` already runs: the side table and the non-blocking
  accept are the two new places threads meet.
- **Not in the golden:** anything that depends on which thread won, timing, or cipher choice
  beyond "1.2 or newer".

---

## 7. Cost sheet

| Piece | Where | Lines (est.) |
|---|---|---|
| Non-blocking listener, `the_script()` | `platform.c`, `builtins.c`, `sus.c`, `interns.c`, `selfhost/` | 80 |
| OpenSSL server + client-with-CA, side table, `hold_up` pending | `platform.c`, `internet.c` | 500 |
| `rizz.entropy` | `platform.c`, `rizz.c` | 40 |
| Schannel server + client | `platform.c` | 900 |
| Secure Transport server + anchored client | `platform.c` | 300 |
| `server`, `worker`, `keeper`, `keeper_client`, `redirect`, `serve` | FunnyLang | 900 |
| `http`, `routes`, `static`, `store`, `security`, `crypto`, `json` | FunnyLang | 1,400 |
| `web/` | HTML, CSS, JS | 900 |
| `test_server.funny` | FunnyLang | 400 |
| Docs | Markdown | 300 |

---

## 8. Decisions already made

- **Pre-threaded acceptors over an acceptor-plus-queue**, because there is no way to hand a
  running worker a connection, and building one (a cross-VM channel) is a bigger runtime change
  than a non-blocking listener.
- **The keeper over a native shared store**, because it needs zero new C, zero locks, is the only
  writer of `data/`, and is the shape the project already uses for two threads that need to talk.
- **Committed test certificates**, because a golden that shells out to `openssl` on first run is
  not deterministic and not portable. They name only loopback, the CA key is destroyed after
  signing, and the README says in bold that they are not secrets.
- **PKCS#12 on every platform** (owner's call): one file, one format, native to all three stacks.
- **`web/` served from disk** (owner's call), text-only, through one path guard.
- **JSON parsing, SHA-256 and base64 in FunnyLang**, because they are ordinary computation and the
  example's point is that the language can do it.
- **`the_script()` rather than "run from the repo root"**, because an example that only works from
  one directory is a trap, and the primes worker in `web_server/` already paid for not having it.

---

## 9. Deviations log

- **Owner · PKCS#12 instead of PEM.** The first draft of this plan took PEM for OpenSSL and left
  macOS as an open question, because Secure Transport can only import an identity from PKCS#12.
  The owner resolved it the other way: PKCS#12 everywhere. That turned out simpler on all three —
  Schannel's `PFXImportCertStore` and macOS' `SecPKCS12Import` both take it directly, and OpenSSL
  reads it with `PKCS12_parse`.
- **Owner · the front end is a `web/` folder, and the site is real.** The first draft embedded the
  assets as strings and rendered pages server-side. The owner asked for HTML/JS/CSS in their own
  folder and for login, settings and activity pages backed by JSON files. The static-file path guard
  (§2.5) is new surface this creates, and is tested accordingly.
- **Owner · protection at rest is SHA-256 and base64.** Base64 is an *encoding*, not encryption:
  anyone with the file can read it. The owner chose it knowingly for a proof of concept. It is kept
  behind one `seal`/`unseal` pair in `store.funny`, every protected value carries a `b64:` prefix
  saying what was done to it, and the README says plainly what it does and does not protect
  against. Unsalted SHA-256 is likewise the owner's call for a demo; a real deployment wants a slow,
  salted password hash.
- **H0 · `the_script()` reaches into `selfhost/`.** The first draft costed it at ~20 lines of C. The
  running program's path is only known to the command line, which is FunnyLang, so the path has to
  be passed down and the toolchain blob regenerated (§3.4).
- **H3 · log lines travel through the keeper.** Discovered while reading `interns.c`: a worker's
  `yell` is captured until the worker is waited for, so a worker cannot log live. §2.3.
- **H1 · the test leaf certificate is valid for 825 days, not ten years.** Apple rejects a TLS
  server certificate issued after 2019 with a longer validity, private CAs included. The committed
  leaf therefore expires in December 2028, and `certs/make_cert.sh` regenerates it in one command.
- **H0 · ADDITION · SIGPIPE is ignored.** Found while designing the TLS writes: nothing handled it,
  so on Linux a client that hung up while a response was being written killed the whole process —
  `web_server/` included. Ignored once, under `pthread_once`, at the first socket operation; the write
  then fails with EPIPE, which `holler_back` already reports as a broken connection.
- **H1 · OpenSSL is loaded under `pthread_once`.** The client path's lazy `dlopen` was a race once
  interns existed. The server symbols are resolved from the same handle: `dlsym` on a `dlopen`ed
  library also searches the libraries it pulled in, which is how `PKCS12_parse` (libcrypto) is found
  through libssl.
- **H1 · a TLS session's socket is non-blocking for its whole life**, not just the handshake. Reads
  and writes loop over WANT_READ/WANT_WRITE themselves, so a plain socket's blocking semantics are
  kept at the API while a handshake step can return "not yet".
- **H2 · the JSON writer moved to `json.funny`**, beside the new parser, instead of staying in
  `http.funny`. The two directions belong together, and the keeper needs both without HTTP.
- **H2 · `read_request` is `async_ngl` and takes `read_more(ms)`**, where `web_server/`'s took a
  blocking callback. Waiting for the next chunk has to be an `await_fr` or a slow client holds the
  whole thread; the timeout is the time left before the request's own deadline.
- **H2 · the keeper records auth and settings events itself.** Only it knows whether a sign-in or a
  password change succeeded, so it writes those entries; a worker's `done` message carries only page
  views. The plan had every event going through `done`.
- **H2 · passwords are hashed on the worker**, never sent to the keeper: the keeper gets the hash.
  The one thread everybody shares should not be the one doing the arithmetic.
- **H3 · keeper connections are pooled per worker**, where the plan said one connection per query.
  Tasks on one thread only switch at an `await_fr`, so a pool needs no lock, and it roughly halves the
  per-request cost of talking to the keeper.
- **H3 · the activity file is flushed every quarter second when dirty**, not once a second, so
  Ctrl-C loses less. Each entry's JSON text is kept beside it, so a flush joins strings instead of
  re-serializing thousands of entries.
- **H3 · the golden's client is its own file**, `test_client.funny`, rather than a program in a string
  constant as `web_server/`'s test does. It is several hundred lines, and a program in a string is one
  nobody can read.
- **H4 · Schannel's private key is persisted while the listener is open.** `PKCS12_NO_PERSIST_KEY`
  would keep it in memory only, but Schannel does its private-key work in LSASS, which cannot see a
  key that lives only inside this process. The key is imported into this user's key store and
  deleted when the listener closes; a process killed mid-run leaves one behind.
- **H4 · verified with llvm-mingw, not MSVC.** There is no MSVC on this machine; the Windows build
  that was run is `x86_64-w64-mingw32-gcc -Wall -Wextra -Werror`, cross-compiled in WSL and run
  natively. CI's `build.bat` (MSVC `/W4 /WX`) compiles the same code and has not seen it yet.
- **H4 · on Windows and macOS a pinned-CA client verifies after the handshake**, where OpenSSL
  verifies during it. Schannel's manual validation and Secure Transport's break-on-server-auth both
  hand the chain back once the peer has shown it; no application data is sent before the check, so
  nothing is exposed, but the server sees a completed handshake followed by a hang-up.
- **H4 · Git's curl on Windows needs `--ssl-no-revoke`** against this server: it is built on Schannel,
  which checks revocation, and the test CA publishes no revocation list. Nothing here is wrong; the
  README says so.
- **H5 · Secure Transport imports the identity into a temporary keychain** with a random password,
  deleted when the listener closes, rather than the user's login keychain. Secure Transport tops out
  at TLS 1.2.
- **H4 · MSVC needed `advapi32.lib` named.** The first CI run compiled `platform.c` clean under
  `/W4 /WX` and then failed to link: `CryptAcquireContextW` (used to delete a legacy-CSP key when a
  listener closes) lives in advapi32, which llvm-mingw links by default and MSVC does not. Fixed with
  one more `#pragma comment(lib, …)` beside the others — the local cross-build could not have caught it.
- **Branch · master's squash of the concurrency work was merged in, not rebased onto.** `master`
  received `feature/async-threading` as one squash commit, whose tree is byte-identical to the
  commit this branch was cut from; every conflict was the same change arriving twice and was resolved
  to this branch's side, leaving the merged tree identical to the tested one. A merge kept the pushed
  history intact, where a rebase would have needed a force-push.
- **CI · the golden had a race of its own.** macOS and Windows CI failed intermittently on one line:
  the scripted client checked that its own actions were on page one of the activity log, but as an
  admin it sees everybody's, and the four crowd clients were adding forty anonymous page views at
  the same moment. On a busy runner enough landed in between to push its entries onto page two.
  The check now asks for `?user=admin`: seven entries, always on one page of ten. Reproduced locally
  by running four copies at once (4 failures in 20 on Windows before, 0 in 32 after).
- **Runtime · the module loader's `strtok` was a process-wide race.** Stressing further turned up a
  rarer failure: a worker dying at start-up with "'http.funny' isn't in this bundle" for a module
  that was in its bundle. `normalize_module_path` in `native/modules.c` split import paths with
  `strtok`, whose cursor is one hidden variable shared by every thread; with the keeper, the redirect
  and the workers all importing modules at the same moment, one thread's `strtok(NULL, …)` carried on
  through another thread's string. It predates this branch — any program hiring several interns that
  import modules could hit it — and the web server was simply the first thing to start enough of
  them at once. Replaced with a hand-written splitter (`modules.c` may not branch per platform, so
  not `strtok_r`/`strtok_s`); the same pattern in `platform.c`'s `lexical_normalize` became
  `strtok_r`. `tests/lang/interns/concurrent_imports` hires thirty-two interns that all import by
  paths needing normalization.
- **H3 · `run_server` did not fail safely, which turned that race into a hang.** Every intern runs
  until the keeper tells it to stop, and a VM that ends joins every intern it hired. When one worker
  died, `await_fr` on it threw out of `run_server` before the keeper was told to quit, the test
  ended with that error, and its teardown waited forever for a keeper nobody would stop. Now each
  intern's failure is collected rather than raised, the keeper is told to stop as soon as anything
  fails, everything winds down normally, and only then is the first failure raised. Found with a
  temporary file-based trace across every thread and gdb backtraces of a stalled run.
