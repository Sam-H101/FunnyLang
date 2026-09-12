# checkers — a game, and an opponent written in FunnyLang

English draughts in a browser, against an engine. The rules, the engine, the server and the page
are all FunnyLang except the page's own JavaScript, which has no framework and no dependencies.

```console
$ funny run extensive_examples/checkers/serve.funny
checkers at https://localhost:8080 — ctrl-c to stop
(a test certificate, so your browser will warn about the issuer -- that is correct)
you are red and move first; the engine is black, looking 3 moves ahead

$ funny run extensive_examples/checkers/serve.funny -- 8443 --host 0.0.0.0
$ funny run extensive_examples/checkers/serve.funny -- 8080 --no-tls

$ funny run extensive_examples/checkers/play.funny -- --depth 5
$ funny run extensive_examples/checkers/play.funny -- --self --moves 40
```

It serves over TLS by default, using `certs/site.p12` — this example's own test identity, signed by a
test CA that nothing on earth trusts. A browser will warn about the issuer, which is exactly what an
unknown CA is for; accept it for localhost, or import `certs/ca.pem`. `certs/make_cert.sh` makes a
fresh pair. `--no-tls` serves plain HTTP instead, and `--pfx FILE` uses a real identity.

| | |
|---|---|
| `rules.funny` | The board, and what may legally be done to it |
| `engine.funny` | The opponent: negamax with alpha-beta pruning |
| `serve.funny` | The socket loop and a three-route JSON API |
| `http.funny`, `static.funny` | Enough HTTP to serve a page and take a move |
| `play.funny` | The same game in a terminal |
| `web/` | The board you click on |
| `web/vendor/` | Bootstrap 5.3.3, vendored rather than fetched from a CDN |
| `certs/` | The test CA and identity — **test material, not secrets** |
| `test_checkers.funny` | The golden: the rules, the engine, and a whole game |

`funny test extensive_examples` runs the golden on every platform CI builds.

## The rules it actually implements

Three rules separate draughts from moving discs around, and all three are here:

- **Captures are compulsory.** If a jump exists, only jumps are legal. `legal_moves` enforces it, so
  no caller can forget.
- **A jump that can carry on, must.** `29x22x13` is one move, not two. The whole chain is found
  before anything is played, and a piece already jumped cannot be jumped twice in the same move.
- **Promotion ends the turn.** A man crowned in the middle of a jump stops there; it does not carry
  on as a king.

Men move and capture forwards only, kings both ways. A side with no legal move has lost, which in
draughts is a loss rather than a stalemate. Forty moves by each side with no capture and no
promotion is a draw.

Squares are numbered 1 to 32, which is how the game has been written down for two centuries, so a
move reads as `11-15` or `22x15x8` and a player can type it.

## How the engine thinks

Negamax with alpha-beta pruning. Negamax is minimax written once rather than twice: every position
is scored from the point of view of whoever is to move, and a position's value is the best reply's
value negated. Alpha-beta is the observation that most of the tree never needs looking at — if a
reply is better for the mover than the opponent would ever have allowed, the rest of that branch
cannot matter.

The score is material first, because checkers is mostly material, then three small positional
terms: a man is worth more the closer it is to being crowned, the middle of the board is worth
holding, and men on their own back rank stop the opponent crowning.

**It is deterministic, deliberately.** Moves are searched in sorted order and a move must be
strictly better to displace the one already found, so the same position always gives the same move.
That is what lets the golden play a whole game and assert it move for move. The cost is that a
player can learn its habits, which for a demonstration is a fair trade for being testable at all.

## What it measured

An 8-core machine under WSL2, using one core. From a position five moves into a game:

| depth | positions looked at | seconds |
|---|---|---|
| 1 | 8 | 0.002 |
| 2 | 43 | 0.010 |
| 3 | 163 | 0.038 |
| 4 | 397 | 0.094 |
| 5 | 1,237 | 0.28 |
| 6 | 4,567 | 1.04 |

There are usually seven or eight legal moves, so without pruning each extra ply would multiply the
work by about that much. The measured growth is nearer three to four times per ply, and the
difference is the whole of what alpha-beta buys.

Depth 3 is the default because it answers instantly and still sees a piece being given away. Depth
5 takes about a third of a second and plays noticeably better. Depth 6 crosses a second, which is
long enough to feel like waiting.

The golden — every rule check, the engine's choices, and a 300-move engine-versus-engine game
played to a draw — runs in **1.7 seconds**.

## What it is not

- **Not a strong engine.** No opening book, no endgame tablebase, no transposition table, no
  iterative deepening, and no quiescence search — so it can misjudge a position where the captures
  have not finished at the depth it stopped at. Its self-play game ends in a draw with both sides
  shuffling kings, which is exactly what a shallow search does in a king ending.
- **No threefold repetition.** The draw rule here is the forty-move one. A repetition draw would
  have ended that self-play game sooner and is the obvious next rule to add.
- **Not international draughts.** This is the 8×8 English game. The 10×10 game has flying kings and
  captures resolved by count, and is a different program.
- **One game per server, not per browser.** There are no sessions and no cookies: this is a program
  you run to play a game. A second tab shares the first one's board.
- **No undo, no save, no clock.** Refreshing keeps the game, because the game lives in the server.
- **Single-threaded, on purpose.** One board, one player, and the only expensive thing is the engine
  thinking about one position. Threads would add a lock around the board and buy nothing. The event
  loop is there so a favicon request cannot queue behind a search.
- **TLS, but with a certificate nothing trusts.** The identity in `certs/` is test material. It
  proves the transport works, not that the server is who it says it is, and a browser is right to
  say so. There is still no authentication: anyone who can reach the port can play.
- **The framework is vendored, not built.** `web/vendor/bootstrap.min.css` is Bootstrap 5.3.3 as
  published, 233 KB, MIT licensed, committed rather than fetched — the page is served under
  `default-src 'self'` and is meant to work with no network. There is no build step, no tree
  shaking, and most of that file is unused.
- **The board itself is not Bootstrap.** No framework draws an eight-by-eight draughts board with
  pieces that hop between squares, so `web/style.css` does that part: a grid for the squares, a
  layer of absolutely-placed discs above it, and the Web Animations API to move them. A piece's
  size comes from its square and never from what is written on it, which is what stops a crowned
  king resizing the grid.
