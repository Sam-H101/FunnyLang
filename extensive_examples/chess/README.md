# chess

Chess, in FunnyLang, with the language playing one side. Rules, an engine, a
terminal game, and a web server that puts the board in a browser over HTTPS.

```
funny run extensive_examples/chess/serve.funny
```

and open <https://localhost:8443>. Your browser will warn about the
certificate, and it is right to — see [the certificate](#the-certificate) below.

To play in a terminal instead:

```
funny run extensive_examples/chess/play.funny
funny run extensive_examples/chess/play.funny -- --self --depth 3
```

## What is here

| file | what it is |
| --- | --- |
| `rules.funny` | the rules of chess, and nothing else |
| `engine.funny` | negamax with alpha-beta, piece-square tables, move ordering |
| `serve.funny` | the HTTPS server and the three-route JSON API |
| `play.funny` | the same game in a terminal |
| `http.funny` | request parsing and response building |
| `static.funny` | serving files from `web/` without serving anything else |
| `test_chess.funny` | the golden: rules, perft, engine, and a whole game |
| `web/` | the page, the stylesheet, the browser client, vendored Bootstrap |
| `certs/` | a test certificate, and the script that made it |

`http.funny` and `static.funny` are copies of the ones in `chat/`, not imports.
That is deliberate and it is a workaround: a module reached through a parent
directory cannot be loaded by a worker, because bundle keys are relative to the
entry file's directory. It is written up in `RUNTIME_PLAN.md` §9.

## The board

Squares are 0 to 63, `rank * 8 + file`, so a1 is 0 and h8 is 63. Upper case is
white. Moves are written in coordinates — `e2e4`, and `e7e8q` to promote — not
in the algebraic a player would write. `Nbd2` requires knowing which *other*
knight could have gone there, and that disambiguation is a feature this does
not have.

`rules.funny` reads and writes FEN, which is how the tests set up a position
that the opening would take thirty moves to reach.

## Is it right?

Move generation is checked by **perft**: count the leaf nodes reachable in
exactly N moves and compare against numbers that every correct chess program
agrees on. It is the test that matters, because it is sensitive to exactly the
rules that are easy to get wrong — a castling case, an en passant, a promotion
counted once instead of four times, a move that leaves your own king in check.

From the opening position:

| depth | nodes | time |
| --- | --- | --- |
| 1 | 20 | 1 ms |
| 2 | 400 | 28 ms |
| 3 | 8,902 | 0.6 s |
| 4 | 197,281 | 14 s |

All four agree. The golden asserts depths 1 to 3, because depth 4 is a quarter
of a minute to confirm something the position below establishes far more
sharply.

That position is the standard one for this — castling available both ways for
both sides, a pawn that can take en passant, and pawns a square from promoting,
none of which the opening exercises:

```
r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
```

It gives 48 and 2,039 at depths 1 and 2, and both agree.

That works out at roughly 15,000 positions a second, which is what a
tree-walking interpreter costs and is the reason the engine looks three or four
moves ahead rather than eight.

## The engine

Negamax with alpha-beta, and two things chess needs that draughts did not:

- **Piece-square tables.** Material alone plays dreadful chess — it has no
  reason to develop a knight, castle, or push a pawn. Each piece gets a small
  bonus per square, written from white's point of view and mirrored for black.
  They are crude on purpose: the smallest thing that turns "legal moves" into
  "recognisable chess".
- **Move ordering.** Alpha-beta prunes well only when good moves come first,
  and in chess the good move is usually a big capture by a small piece. Sorting
  captures first cuts the tree enormously.

The ordering stays **deterministic** — ties break on the move's coordinates —
so the same position always produces the same move. That is what lets the
golden assert a whole game move for move rather than merely that one was
played.

## What it is not

It plays legal chess. It does not play good chess, and the gap is mostly these:

- **No quiescence search.** The search stops at a fixed depth even in the
  middle of a sequence of captures, so it can misjudge a position where pieces
  are still hanging at the last ply. This is the single largest weakness and
  the usual first thing a real engine adds.
- **No opening book**, so the first few moves are whatever the tables like.
- **No endgame knowledge.** The king's table wants a corner, which is right
  while there are pieces about and wrong once there are not. It will not drive
  a lone king to the edge to mate it, and there are no tablebases.
- **No transposition table**, so the same position reached two ways is searched
  twice.
- **No iterative deepening and no clock.** The depth is fixed by the difficulty
  you pick; the engine takes as long as it takes.
- **Threefold repetition is not seen inside the search** — only at the top
  level, by the server and the terminal game, which keep the positions. The
  engine will happily walk into a repetition it could have avoided.
- **The engine always promotes to a queen** in practice. It generates all four
  and the ordering puts the queen first; a position where a knight is better is
  one it will get wrong. A human player is asked which piece.
- **No SAN, no PGN, no resignation, no draw offers**, and **one game per
  server** — not one per browser. This is a program you run to play a game, and
  pretending to have sessions it does not have would be worse than saying so.

## The API

Three routes, holding one game:

| route | what it does |
| --- | --- |
| `GET /api/state` | the board as it stands |
| `POST /api/new` | start again; `{"depth": n, "side": "white"}` optional |
| `POST /api/move` | `{"from": n, "to": n, "promotion": "q"}`, then the engine replies |

Every legal move travels with the state, so the page never has to know a rule:
it highlights what it was given, and a click that is not in the list cannot be
made. The page and the engine can therefore never disagree about whether
castling was still allowed.

The three moves that are not simply "from here to there" are resolved by the
server, not the browser. Castling sends the rook's squares as well, so the rook
slides with the king. En passant names the square to clear, because the pawn
being taken is not on the square the capturer lands on. Promotion names the new
piece, so the glyph is swapped when it arrives. A page that worked any of those
out for itself would be a second implementation of the rules, and the two would
drift.

## The page

Two layers. Sixty-four squares in a grid, which take the clicks, and above them
one absolutely-placed piece per man, positioned with a transform. Moving a
piece is then a matter of animating that transform, so it slides to its square
— which is the difference between seeing a move and being shown a new position.

Every piece occupies exactly one eighth of the board whatever is drawn on it,
and the glyph is sized from the board rather than from its box. A queen and a
pawn therefore take identical space, and promoting cannot resize a square or
shift the grid.

Bootstrap is vendored into `web/vendor/` rather than fetched: the page is served
under `default-src 'self'` and is meant to work on a machine with no network.

## The certificate

`certs/site.p12` is a test certificate signed by a test CA that nothing else
trusts, so a browser will warn about the issuer — which is exactly what an
unknown CA is for. It is test material and not a secret. `certs/make_cert.sh`
makes a fresh pair.

Run with `--no-tls` for plain HTTP, which is fine on loopback and not on a
network.

## One thread, and not even apologetically

There is one board, one player, and the only expensive thing that happens is
the engine thinking, which is work for one core on one position. Threads would
add a lock around the board and buy nothing. The event loop is there so that a
second tab, a favicon request or a health check cannot sit in the queue behind
a search.

## The tests

```
funny test extensive_examples/chess
```

Four parts, each able to fail on its own: the rules from positions set up by
hand, perft, the engine choosing from fixed positions at fixed depths, and a
complete game of engine against itself asserted move for move. No sockets
anywhere — the server is a thin wrapper around these modules, and what a
wrapper is worth testing for is that it wraps, not the rules of chess.
