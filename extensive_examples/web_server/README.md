# A web server, in FunnyLang

```console
$ funny run extensive_examples/web_server/serve.funny -- 8080
serving on http://127.0.0.1:8080 — ctrl-c to stop
21:04:11  127.0.0.1:51150  GET /  -> 200  1297b
21:04:11  127.0.0.1:51154  GET /hello?name=sam  -> 200  135b
```

Three files, and the split is the point:

| | |
|---|---|
| `serve.funny` | the socket loop — the only file that knows what a connection is |
| `http.funny` | HTTP/1.1: request line, headers, bodies, query strings, percent-decoding, responses |
| `routes.funny` | what it says back. Plain functions from a request to a response |
| `test_server.funny` | all of the above, over a real socket, in one program |

`funny test extensive_examples` runs the last one, so this cannot quietly stop
working.

## What it does

```console
$ curl -s localhost:8080/hello?name=you
yo you, sup 👋

$ curl -s localhost:8080/health
{"ok":true,"hits":4,"cores":8}

$ curl -s -X POST -d 'first post' localhost:8080/guestbook
{"ok":true,"entries":1}

$ curl -s 'localhost:8080/primes?upto=120000&crew=1'
{"upto":120000,"interns":1,"primes":11301,"per_intern":[11301],"ms":1041.3}

$ curl -s 'localhost:8080/primes?upto=120000&crew=8'
{"upto":120000,"interns":8,"primes":11301,
 "per_intern":[1590,1522,1489,1503,1308,1301,1299,1289],"ms":279.1}
```

That last pair is the interesting one. `/primes` divides the range across that
many `interns` — **real OS threads**, each running a whole program in its own
VM with its own heap. Nothing is shared and nothing is locked: each worker is
handed a list of ranges, deep-copied in, and hands back a count, deep-copied
out.

Measured on one 8-core machine, warm (the first `/primes` request compiles the
worker and is not representative):

| crew | ms | speedup |
|---|---|---|
| 1 | 1041 | — |
| 2 | 561 | 1.9× |
| 4 | 350 | 3.0× |
| 8 | 279 | 3.7× |

*How* the work is divided turns out to matter more than that it is. One
contiguous slice each gives equal counts of numbers but very unequal work,
because trial division gets dearer as `n` grows; one residue class each is
worse still, since with an even crew size one worker gets every even number.
Dealing small blocks round-robin is what produces the table above. `/primes`
in `routes.funny` says so at more length.

The full route list is on the home page. `/slow?ms=120` uses `clock.chill`,
which yields rather than blocking; `/boom` raises on purpose so the 500 path
is exercised by something other than a bug.

## What it is not

**One request at a time.** The obvious next step is to hand each connection to
an `interns` worker, and it would even work — a connection handle is a
`numba`, and a `numba` is one of the things that can cross to a worker. But a
worker is a whole separate VM with its own heap, so it could not see
`GUESTBOOK` or any other state this program keeps. That is a real consequence
of the isolation the threading model is built on, not an oversight, and an
example that hid it would teach the wrong thing. `/primes` shows where workers
*do* fit: work that takes a value and gives back a value.

**No keep-alive, no chunked request bodies, no TLS, no HTTP/2.** Every
response says `Connection: close` and means it. A server that claimed those
without having them would be worse than one that says it hasn't.

**Not hardened.** It binds `127.0.0.1` by default for a reason. There is a
read timeout and a body-size cap on the guestbook, and that is about the
extent of it: no rate limiting, no request-size ceiling on `/echo`, no slow-
loris defence beyond the timeout. Put it behind something real before pointing
it at the internet.

## The runtime side

Serving needed six new things in `internet`, since everything that was there
before dials *out*:

```funny
yo listener = internet.open_shop(8080, "127.0.0.1")   // bind and listen
yo port     = internet.shop_port(listener)            // which port (after open_shop(0))
yo customer = internet.next_customer(listener, 1000)  // accept, or ghost on timeout
yo text     = internet.hear_them_out(customer["conn"], 65536, 5000)
internet.holler_back(customer["conn"], "HTTP/1.1 200 OK\r\n\r\nhi")
internet.kick_out(customer["conn"])
internet.close_shop(listener)
```

plus `internet.slide_into(host, port)` for the other end, which is what lets
`test_server.funny` be both halves of a conversation. Listeners and
connections are `numba` handles rather than objects — the same reasoning
`sus`'s REPL sessions and `interns`'s workers already use: a plain integer
stays out of the collector, and it can cross to a worker, which an object
could not.

`FUNNY_NO_NET=1` does not block any of this, except `slide_into` to a
non-loopback host. That flag exists so a test runner does not *reach the
network*; a server binding its own loopback port sends no packet anywhere, and
refusing it would make this untestable in exactly the environment that most
needs its tests to run.
