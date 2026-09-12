# chat — WebSockets, and a room that pushes

A chat server: RFC 6455 from the handshake up, rooms held on the main thread, and every message
fanned out to every worker over direct messages. The page is plain HTML, CSS and JavaScript with no
framework.

```console
$ funny run extensive_examples/chat/serve.funny -- 8443
chat at http://localhost:8443 on 8 threads — ctrl-c to stop
(no --pfx, so this is plain http: fine on loopback, not on a network)

$ funny run extensive_examples/chat/serve.funny -- 8443 --pfx ../web_server_https/certs/site.p12
chat at https://localhost:8443 on 8 threads — ctrl-c to stop
```

| | |
|---|---|
| `ws.funny` | RFC 6455: the handshake, and framing |
| `ws_client.funny` | The other half — a client, for the golden and for anything else |
| `rooms.funny` | Who is in which room, and what was said |
| `room_client.funny` | A worker's side of the room: asking, and being pushed to |
| `server.funny` | The boss: the listener, the workers, and the fan-out |
| `worker.funny` | One acceptor thread and the connections it holds |
| `http.funny`, `static.funny` | Enough HTTP to hand over a page and an upgrade |
| `serve.funny` | The command line |
| `web/` | The page |
| `test_chat.funny` | The golden: rooms, framing, and three clients in one room |

`funny test extensive_examples` runs the golden on every platform CI builds.

## The fan-out is the point

A worker holds some connections and can see only its own heap. A message typed into a connection on
one worker has to reach connections held by all of them, and no worker can reach another. The boss
can reach every worker, so the boss is the room: it takes `say` from one worker and pushes it to all
of them, and each writes it to the connections that are in that room.

That is a shape the earlier examples' keeper could not express. A keeper *answers*: a worker asks, the
keeper replies, one for one, and `keeper_client.funny` matches each reply to its question by number.
This has to **push** — to workers that asked nothing, with nothing waiting. So a worker's inbox now
carries two kinds of message, told apart by shape:

```
{"rid": 7, "reply": ...}     an answer to question 7
{"push": "said", ...}        something that happened, addressed to nobody
```

One task owns the inbox and sorts them. That is the whole of `room_client.funny`, and it is why it
exists alongside `keeper_client.funny` rather than replacing it.

## The handshake, and why SHA-1 is in the runtime

A client sends `Sec-WebSocket-Key`, sixteen random bytes in base64, and the server must answer with
`base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`, a constant written into the RFC. It
proves nothing about anybody — everyone can compute it — and exists so that a cache or proxy which
does not understand the upgrade cannot produce a response that looks like a successful one.

That is the entire reason `vault.sha1` exists, and the standard library says so where it is
documented. SHA-1 is broken against collisions; anything choosing its own hash should choose
`sha256`. A protocol that names SHA-1 leaves no choice.

## Masking, and why `blob.xor` exists

A client masks every frame: each byte XOR'd with a four-byte key, cycling. A server masks none. The
asymmetry is not secrecy — the key travels in the frame — it stops a malicious page making a browser
emit bytes it chooses verbatim, which an intermediary might read as a request of its own.

Written as an ordinary loop it cost **360 ms per megabyte**, which the golden's two-megabyte message
pays twice. `blob.xor` does the same work in **2 ms**. `RUNTIME_PLAN.md` §9 records both primitives.

## What it measured

An 8-core machine under WSL2, over loopback. Four clients in one room, each sending 250 messages, so
1,000 messages and 4,000 deliveries — every one of which arrived.

| workers | messages per second | deliveries per second | slowest client |
|---|---|---|---|
| 1 | 5,789 | 23,158 | 172 ms |
| 8 | 8,353 | 33,412 | 119 ms |

**Eight workers is about 1.4 times one worker, not eight times, and that is the honest lesson of this
example.** Every message goes through the boss, which is one thread: it reads the message, updates the
room, and pushes it to each worker in turn. Workers parallelise reading frames off sockets and writing
them back, which is real work and really is spread — but the room in the middle is serial by
construction. Sharding rooms across several boss threads is the answer, and it is not done here,
because the point of this example is the shape rather than the record.

## What it is not

- **No sessions and no sign-in.** The plan had this example importing them from
  `web_server_https/`, and that is not possible: a worker cannot use a module reached through a
  parent directory, because a bundle keys every module relative to its entry file's directory.
  `RUNTIME_PLAN.md` §9 has the two-file reproduction. So this carries its own small `http.funny` and
  `static.funny`, and people are whoever they say they are — which is fine for a demonstration on
  loopback and not fine anywhere else.
- **No presence in `kv`.** The plan marked that optional and it is not here; who-is-online lives in
  the boss's memory and goes when it stops.
- **Plain HTTP unless you pass `--pfx`.** With a PKCS#12 identity it speaks TLS and the page uses
  `wss://`; without one it is `ws://`, which is honest on loopback and wrong on a network.
- **History is a hundred messages per room, in memory.** It is not a log, it is not written down,
  and it goes when the server stops.
- **The two-megabyte fragmented message is exercised at the frame layer**, where fragmentation and
  masking live, and a 256 KB one goes over the socket. Pushing two megabytes of JSON to three
  clients would have measured the string escaper, not the protocol.
- **No compression, no extensions, no subprotocols.** `permessage-deflate` is not negotiated, and a
  frame with a reserved bit set is refused rather than guessed at.
- **One room per connection.** Joining a second room moves you; there are no tabs and no multiplexing.
