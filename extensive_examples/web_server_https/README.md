# An HTTPS web site, in FunnyLang

```console
$ funny run extensive_examples/web_server_https/serve.funny
serving https://localhost:8443 on 24 threads — ctrl-c to stop
redirecting http://localhost:8080 to https
config: .../web_server_https/data/config.default.json (no customization yet)
sign in as admin or demo, password funnylang -- and change it on the settings page
00:07:12  #02  127.0.0.1:54884  TLSv1.3  GET /  -> 200  4927b
00:07:12  #19  127.0.0.1:54885  TLSv1.3  GET /css/site.css  -> 200  12511b
00:07:30  #07  127.0.0.1:54917  TLSv1.3  POST /api/login  -> 200  812b  admin
```

Open <https://localhost:8443/>. Your browser will not trust the certificate, because it was issued by
this example's own test CA; either accept it for localhost, or import `certs/ca.pem` into a
throwaway browser profile. With curl:

```console
$ curl --cacert extensive_examples/web_server_https/certs/ca.pem https://localhost:8443/api/health
{"ok":true,"tls":"TLSv1.3","thread":7,"threads":24,"uptime_seconds":12}
```

On Windows, Git's curl is built on Schannel, which also checks certificate revocation; the test CA
publishes no revocation list, so add `--ssl-no-revoke` there.

## What it is

A small, modern site — a landing page, a sign-in page, a settings page and an activity page — served
over TLS 1.2 or 1.3 by 24 acceptor threads, with every byte of HTTP above the TLS record layer handled
by FunnyLang. It is `web_server/` grown up, and the growth is mostly in three directions: encryption,
threads, and a site that remembers things.

| File | What it does |
|---|---|
| `serve.funny` | The command line: flags in, banner and log out |
| `server.funny` | Opens the listeners, hires every thread, waits, winds down |
| `worker.funny` | One acceptor thread: accept, handshake, read, route, answer — many connections at once |
| `keeper.funny` | The one thread that owns everything shared: config, sessions, activity, rate limits |
| `keeper_client.funny` | A worker's side of the keeper protocol, with a small connection pool |
| `redirect.funny` | The plain-http listener, which only ever says 301 |
| `http.funny` | HTTP/1.1, strictly: limits on everything, a status for every way to be malformed |
| `routes.funny` | Pages from `web/`, and `/api/*` |
| `static.funny` | The path guard between a URL and a file |
| `store.funny` | The data files, and what "protected at rest" means here |
| `security.funny` | Constant-time compare, tokens, the session cookie, rate windows, validation |
| `crypto.funny` | SHA-256 and base64 |
| `json.funny` | A JSON reader and writer |
| `web/` | The front end: four pages, one stylesheet, a script per page, no framework |
| `data/config.default.json` | The committed defaults |
| `certs/` | `make_cert.sh`, the test CA and the PKCS#12 identity — **test material, not secrets** |
| `test_server.funny` | The golden: the pieces, then the real server over real TLS, then the files it wrote |

`funny test extensive_examples` runs the last one on every platform CI builds.

## The site

| Page | |
|---|---|
| `/` | What this is, and a live strip: TLS version, which thread answered, threads serving, uptime |
| `/login` | Sign in. Five wrong passwords from one address and sign-in pauses for five minutes |
| `/settings` | Profile (display name, email), defaults (theme, activity page size, where to land after signing in), password |
| `/activity` | Every sign-in, page view and change, newest first. Filter by user and action; paged by your own page size. Admins see everybody, everyone else sees themselves |

The pages are static HTML in `web/`. Everything that changes goes through `/api/*` as JSON:
`GET /api/health`, `GET /api/me`, `GET /api/activity`, and `POST` to `/api/login`, `/api/logout`,
`/api/profile`, `/api/defaults` and `/api/password`.

Two accounts come in the default config, `admin` (sees all activity) and `demo`, both with the
password `funnylang`. The settings page nags until it is changed.

### The data

| File | Written | When |
|---|---|---|
| `data/config.default.json` | never — it is committed | |
| `data/config.custom.json` | by the keeper | the first time anybody changes a password, profile or default, and on every change after |
| `data/activity.json` | by the keeper | at most every quarter second while there is something new, and at shutdown; newest 5,000 kept |

On start the customization file is read if it exists, the default otherwise. A customization file
that exists and will not parse **stops the server** instead of falling back — falling back would
quietly undo a password change. `--data DIR` puts the two written files somewhere else.

**Protected at rest, honestly.** Passwords are stored as `sha256:<hex>`: one-way, but unsalted, which
was a deliberate choice for a demo and is not what a real deployment should do (it wants scrypt or
Argon2, with a salt). Email addresses, display names, client addresses and user agents are stored as
`b64:<base64>`. **Base64 is an encoding, not encryption**: it keeps a value from being read over a
shoulder or matched by a grep, and anyone with the file can reverse it. That too was a deliberate
choice for a proof of concept. Both live behind `seal`/`unseal` and `hash_password` in `store.funny`,
and every value carries its prefix, so replacing either with the real thing is a change in one place.

## Secure by default

None of these has a flag to turn it off.

- **TLS 1.2 minimum**, 1.3 where the platform has it. Forward-secret AEAD suites only: no CBC, no
  plain-RSA key exchange, no renegotiation, no compression.
- **Plain HTTP only redirects.** The http listener answers every request with a 301 to the same path
  on HTTPS and never serves content; `Strict-Transport-Security` then keeps the browser off it.
- **Loopback by default.** `--host 0.0.0.0` is how you mean it.
- **Headers** on every HTTPS response: HSTS, a `default-src 'self'` Content-Security-Policy (the pages
  contain no inline script or style, so it can be that strict), `nosniff`, `X-Frame-Options: DENY`,
  a strict referrer policy, a permissions policy, same-origin opener and resource policies. API
  responses are `Cache-Control: no-store`.
- **Sessions**: `__Host-sid`, `Secure; HttpOnly; SameSite=Strict`, 32 bytes from the OS CSPRNG.
- **Three locks on every change**: it must be JSON, it must come from this origin if it says where it
  comes from, and it must carry the session's CSRF token in a header, compared in constant time.
- **Sign-in** does the same work for "no such user" as for "wrong password", and records failures
  without the name somebody typed unless it is a real account (a mistyped username is sometimes a
  password).
- **Limits** on the request line (8 KiB), headers (16 KiB, 64 of them), body (64 KiB), the handshake
  (5 s), the head (5 s) and the body (10 s); 300 requests per address per 10 s; 256 connections in
  flight per thread.
- **Strict HTTP parsing**: exactly three parts on the request line, no folded headers, no chunked
  bodies, no duplicate `Content-Length` or `Host`, `Host` required, percent-escapes that do not decode
  are refused rather than guessed at.
- **Files only from `web/`**, rebuilt from URL segments that pass a safe-alphabet check: no `..`, no
  dot-files, no directory listings, allow-listed extensions only.
- **Errors** say what went wrong with the request, never a stack trace. **Logs** never contain a body,
  a cookie or a password.

## Twenty-four threads, and one more

`interns` are real OS threads, each with its own VM and heap. A worker is hired once with one
argument and there is no channel to hand it connections later, so the shape is a **pre-threaded
server**: the main thread opens the secure listener, and every worker is handed the same one and
accepts from it. The kernel gives each connection to one of them. Each worker is also an event loop,
so it serves many connections at once — 24 threads each doing what `web_server/` does on one.

Twenty-four heaps cannot share a session table. The **keeper** is one more intern that owns
everything shared, and the only thread that ever writes to `data/`. Workers ask it over loopback, one
JSON object per line, after a random secret as the first line of every connection. Every request it
handles runs start to finish without awaiting anything, so its state needs no lock either. The
request log travels the same way: a worker's own output is held until it finishes, so each worker
tells the keeper, and the keeper streams the lines to the main thread, which prints them.

Measured on an 8-core machine under WSL2, `--threads 1` against the default 24, TLS 1.3 throughout,
with the per-address rate limit raised so it did not interfere:

| | 1 thread | 24 threads |
|---|---|---|
| one request at a time | 11 ms | 12 ms |
| 200 × `/api/health`, 50 at a time | 635 ms | 471 ms |
| 1,000 × `/api/health`, 100 at a time | 7,600 ms | 2,425 ms |
| 400 × `/` (a 4 KB page), 50 at a time | 6,173 ms | 1,043 ms |
| one request while 40 connections sit silent | 38 ms | 13 ms |

Every one of those requests was answered with a 200. The single-request cost is dominated by the
TLS handshake and the round trips to the keeper, not by thread count; the bursts are where two dozen
threads earn their keep. The silent connections are a slow-loris in miniature: each one parks a task,
not a thread, and is dropped at the five-second handshake deadline.

## TLS, per platform

TLS is the one thing here that is not FunnyLang, on purpose. Cryptography in an interpreter cannot
be constant-time and would take seconds per handshake; "secure by default" and "hand-rolled AES-GCM"
cannot both be true. So the runtime uses the TLS each operating system already ships, the same
backends `internet.go_brrrr` uses for `https://`, and shows it to FunnyLang as the ordinary socket
handles doing the ordinary calls.

| Platform | Backend | Identity | Status |
|---|---|---|---|
| Linux / BSD | OpenSSL, `dlopen`'d at run time | `PKCS12_parse` | built and run: the golden, curl, `openssl s_client` |
| Windows | Schannel (SSPI) | `PFXImportCertStore` | built with llvm-mingw and run natively on Windows 11: the golden, curl, `openssl s_client` |
| macOS | Secure Transport | `SecPKCS12Import` into a temporary keychain | written; **not yet compiled or run** — there is no Mac here, and CI runs when the branch is pushed |

PKCS#12 is the one identity format all three import natively. The committed `certs/site.p12` is
encrypted with PBE-SHA1-3DES and a SHA-1 MAC, the one PKCS#12 encoding all three read out of the box.
The leaf is valid for 825 days because Apple rejects longer ones even from a private CA; it expires in
December 2028, and `sh certs/make_cert.sh` makes a new pair.

## The runtime side

```funny
yo shop = internet.open_secure_shop(8443, "127.0.0.1", {"pfx": "certs/site.p12", "password": "funnylang"})
yo customer = internet.next_customer(shop, 0)          // {conn, peer, secure}
bruh (!internet.handshake(customer["conn"])) {         // one step at a time, never blocking
    await_fr internet.hold_up(customer["conn"], 5000)
}
yap internet.tls_info(customer["conn"])                 // {"version": "TLSv1.3", "cipher": ...}

yo c = internet.slide_into("127.0.0.1", 8443, {"tls": fax, "server_name": "localhost", "ca": "certs/ca.pem"})
yo token = rizz.entropy(32)                             // the OS CSPRNG, as hex
yo here = filez.dir_of(the_script())                    // where this program lives
```

Underneath: a listener that several threads can accept from (it is non-blocking now, so a thread that
loses the race gets `ghost` instead of freezing in `accept()`), SIGPIPE ignored so one impatient client
cannot kill the process, and a side table in `platform.c` mapping socket handles to TLS sessions so
`recv`/`send`/`close`/`poll` do the right thing without `internet.c` knowing. `the_script()` needed
the command line to pass the program's path down, which touched `selfhost/` and regenerated
`native/toolchain_blob.c`. `PLAN.md` in this directory has the design and every deviation from it.

## What it is not

- **Not a product.** It is an example of what the language can carry. Put it behind something real
  before you point it at the internet.
- **Not encrypted at rest.** See above: base64 and unsalted SHA-256, by choice, for a demo.
- **Not persistent across restarts for sessions.** Sessions live in the keeper's memory; a restart
  signs everybody out. The config and the activity log survive.
- **Not crash-safe to the last quarter second.** Ctrl-C kills the process; activity recorded in the
  last flush interval is lost. Stopping with `--seconds` or `--requests` flushes everything.
- **No keep-alive, no HTTP/2, no chunked request bodies.** Every response is `Connection: close`.
- **No binary files.** `web/` is served as text, and everything in it is text; the favicon is SVG.
- **Symlinks inside `web/` are followed.** The path guard stops a request from leaving `web/`; it does
  not stop whoever controls `web/` from pointing somewhere else.
- **Rate limits are per process** and by client address, so everything behind one NAT shares one.
