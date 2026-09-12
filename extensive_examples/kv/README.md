# kv — a key-value database

A server, a client library and a command line. Keys and byte values, an append-only log with
periodic snapshots, expiry, and fourteen commands.

```console
$ funny run extensive_examples/kv/server.funny -- --data ./kvdata
kv listening on 127.0.0.1:6380 — ctrl-c to stop
holding 0 keys from ./kvdata

$ funny run extensive_examples/kv/cli.funny -- set greeting yo
OK
$ funny run extensive_examples/kv/cli.funny -- get greeting
yo
$ funny run extensive_examples/kv/cli.funny -- incr visits
1
$ funny run extensive_examples/kv/cli.funny -- keys 'g*'
greeting
```

| | |
|---|---|
| `store.funny` | The data, the log, the snapshot, and expiry |
| `protocol.funny` | What goes over the socket, in both directions |
| `commands.funny` | What each command does |
| `server.funny` | The socket loop and the command line |
| `kvclient.funny` | Talking to it, as a library another example can import |
| `cli.funny` | One command from a shell |
| `test_kv.funny` | The golden: the store, the protocol, the commands, and a real server |
| `test_client.funny` | The client half of the golden, which runs in a worker |

`funny test extensive_examples` runs the golden on every platform CI builds.

## The commands

`GET SET DEL EXISTS KEYS EXPIRE TTL INCR MGET MSET FLUSH INFO PING STOP`

`KEYS` takes a glob where `*` matches any run and `?` any one character. `TTL` answers `-2` for a
key that is not there and `-1` for one with no expiry, so "missing" and "immortal" are different
answers. `INCR` on a value that is not a whole number is an error rather than a reset to 1.

## The protocol

One text line per request. When a request carries a value, the line ends with that value's length
in **bytes** and the bytes follow:

```
GET greeting\r\n
SET greeting 2\r\nyo\r\n
MSET 2\r\na 1\r\nx\r\nb 1\r\ny\r\n
```

Replies are tagged by their first character: `+OK`, `$2\r\nyo`, `$-1` for no such key, `:42` for a
number, `*2` for a list, and `-TypeVibeMismatch ...` for an error, which carries the flavor so a
client can tell a wrong type from a missing key without matching on English.

A byte length rather than a delimiter is the whole point. A value is bytes — a PNG, a key, a string
with a newline in it — and any delimiter would need escaping, which is where binary-safe protocols
go wrong. The golden stores a value containing a NUL, a CRLF and a byte no UTF-8 sequence can
start, and reads it back identical.

This is not Redis's protocol, deliberately. Answering RESP would invite `redis-cli`, and this
implements fourteen commands out of hundreds; looking like Redis and not being Redis is a worse
lie than looking like nothing in particular.

## How it survives a restart

Two files in the data directory. `log.jsonl` gets one JSON object appended per change.
`snapshot.json` is the whole store, written with `filez.yeet_out_atomic`, which writes beside the
file and renames over it — so a crash halfway through leaves the previous snapshot whole rather
than half of a new one.

A snapshot alone would lose everything written since the last one. A log alone would be replayed
from the beginning of time and grow without bound. Together the snapshot is a checkpoint and the
log covers only what came after it.

The order matters and is the other way round from the obvious one: the snapshot is written **first**
and the log truncated **after** it lands. A crash between the two replays operations that are
already in the snapshot, and every one of them is idempotent, so the cost is wasted work. The
reverse order loses them.

## One thread, for the opposite reason to the web server

`web_server/` is single-threaded because its handlers share a guestbook and threads would need
locks around it. This is single-threaded because **every command touches the same map**. A store
spread over real threads needs a lock on that map, or a keeper owning it that every thread
messages — and then the throughput is the keeper's, so the threads bought nothing.

A store is the case where one thread is not a compromise but the design. The concurrency that does
matter is between *connections*, and that is what the event loop gives: a task per connection,
every read waiting on the loop rather than on the socket, so a client that connects and says
nothing costs one parked task instead of the whole server.

## What it measured

An 8-core machine under WSL2, over loopback, one client on one connection, values of about fifty
bytes. Only one core is used, by design.

| | |
|---|---|
| `SET` | 9,100 per second |
| `GET` | 10,500 per second |
| `INCR` | 8,600 per second |
| 2,000 keys on disk | 241 KB snapshot |
| the golden, end to end | 2.2 s |

Two thousand of each command, timed separately. The figures are round-trip: each one is a request
written, a reply read, and the client waiting in between, so they measure latency on loopback as
much as the server's own work.

## What it is not

- **Not durable against a power cut.** The log is appended, not flushed to the disk on every write.
  A process that crashes loses nothing, because the operating system still has the bytes; a machine
  that loses power can lose the most recent operations. Making every write survive that means an
  `fsync` per operation, which costs roughly a thousand times what the append does, and the honest
  thing is to say which one this is rather than imply the other.
- **Not Redis.** Fourteen commands, no clustering, no replication, no pub/sub, no transactions or
  `MULTI`, no Lua, no sorted sets, hashes or lists — the value is bytes and nothing else.
- **No authentication and no TLS.** It binds loopback by default for exactly that reason.
  `--host 0.0.0.0` puts an unauthenticated database on the network, and that is the operator's
  decision to make knowingly.
- **The whole store is in memory.** There is no eviction, no `maxmemory`, and no paging to disk:
  the snapshot is how it survives a restart, not how it holds more than fits.
- **Expiry is checked when a key is touched and swept once a second.** A key can sit expired but
  present for up to a second before the sweep removes it, though no command will ever hand back a
  value that has expired.
- **A slow command blocks the others.** One thread means the event loop runs one command at a time.
  Every command here is a map lookup, so none of them is slow, which is what makes the choice safe
  rather than lucky.
