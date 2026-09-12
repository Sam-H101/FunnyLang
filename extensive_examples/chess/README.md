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
| `search_worker.funny` | one slice of a root search, run on its own thread |
| `serve.funny` | the HTTPS server and the three-route JSON API |
| `play.funny` | the same game in a terminal |
| `http.funny` | request parsing and response building |
| `static.funny` | serving files from `web/` without serving anything else |
| `test_chess.funny` | the golden: rules, perft, engine, threads, and a whole game |
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
that the opening would take thirty moves to reach — and how a position is
handed to a worker on another thread, since nothing else can cross.

## Is it right?

Move generation is checked by **perft**: count the leaf nodes reachable in
exactly N moves and compare against numbers that every correct chess program
agrees on. It is the test that matters, because it is sensitive to exactly the
rules that are easy to get wrong — a castling case, an en passant, a promotion
counted once instead of four times, a move that leaves your own king in check.

From the opening position:

| depth | nodes | time |
| --- | --- | --- |
| 1 | 20 | 0 ms |
| 2 | 400 | 18 ms |
| 3 | 8,902 | 0.32 s |
| 4 | 197,281 | 9.0 s |

All four agree. The golden asserts depths 1 to 3, because depth 4 is nine
seconds to confirm something the position below establishes far more sharply.

The times are as the code stands, after the optimisation work described below;
the counts are facts about chess and never move, which is exactly why they make
a good test.

That position is the standard one for this — castling available both ways for
both sides, a pawn that can take en passant, and pawns a square from promoting,
none of which the opening exercises:

```
r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1
```

It gives 48 and 2,039 at depths 1 and 2, and both agree.

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

## Making it faster

The first version took about a second to reply at depth 3 and ten at depth 4,
which is long enough to be unpleasant in a browser. Three things were wrong,
and none of them was the search algorithm.

**Leaves were generating moves they never used.** The search asked "is this
checkmate?" before it asked "am I at the bottom?", so every leaf — the
overwhelming majority of the tree — built a full legal move list, costing a
board copy and a check test per pseudo-move, and then threw it away. Asking in
the cheap order instead (one check test, and the move list only if the side to
move is actually in check) leaves checkmate correctly detected and skips the
work everywhere else.

**Sorting cost more than generating.** Both the rules and the engine sorted
moves with a hand-rolled selection sort that tracked which indices it had used
in a dictionary keyed by `to_yap(i)` — so an O(n²) inner loop paid for a
number-to-string conversion and a hash lookup every step. At a 49-move
middlegame position that was 43% of the entire cost of producing legal moves.
They now use the runtime's own `sort` and `stash.sort_by`.

**The search was sorting twice.** `legal_moves` returned a sorted list and the
engine immediately re-ordered it by its own criteria. The search now uses
`legal_moves_unsorted` and sorts once; the sorted form is still what the page
and the tests are handed.

| | before | after |
| --- | --- | --- |
| 200 × `sorted_moves` | 284 ms | **1 ms** |
| 200 × `legal_moves` | 660 ms | 403 ms |
| best move, depth 3 | 1,023 ms | **690 ms** |
| best move, depth 4 | 10,066 ms | **5,601 ms** |

Node counts and chosen moves are identical throughout — this removed waste, it
did not change how the engine plays.

## The transposition table

Chess transposes constantly: 1.Nf3 d5 2.d4 and 1.d4 d5 2.Nf3 reach the same
board by different roads. Without a table the search works both out from
scratch, and it does that over and over. With one, the second arrival is a
lookup.

The subtlety is that alpha-beta does not always learn a position's *value*. It
cuts off the moment it knows a move is too good or not good enough, and what it
has established then is only a bound. So every entry records which of the three
it holds — an exact score, a lower bound, or an upper bound — and a bound is
only allowed to narrow the window rather than answer outright. Getting that
wrong produces an engine that is fast and occasionally believes nonsense.

Two things are deliberately kept out of it. Mate scores, because a mate score
says how far away the mate is *from here*, so filing one away and reading it
back elsewhere in the tree reads it at the wrong distance. And anything below
depth 2, because the key is the sixty-four squares joined into a string, which
is only worth building when the subtree it might save is larger than the key.

| | before | after |
| --- | --- | --- |
| depth 3 | 2,500 nodes, 686 ms | **1,530 nodes, 430 ms** |
| depth 4 | 5,940 nodes, 5,577 ms | **3,384 nodes, 2,927 ms** |
| depth 5 | 128,630 nodes, 39.8 s | **30,654 nodes, 9.5 s** |
| depth 4, from the opening | 9,774 nodes, 3,128 ms | **2,741 nodes, 838 ms** |

One thing did change about how it plays. From the opening at depth 3 it now
answers g1f3 where it used to answer b1c3 — **at the same score of 50**. The two
are a genuine tie, and because a table hands back bounds as well as exact
scores, a bound arriving at the root can flip which of two equal moves is met
first. It is still perfectly deterministic, and the golden still asserts that
asking twice gives the same answer. Every other assertion in the golden — the
whole self-play game, every perft count, every rule — is unchanged.

## Precomputed attack tables

Where a knight on e4 can go does not depend on the position. Neither does which
squares lie outward from e4 along a diagonal, nor which two squares a black
pawn would have to stand on to attack it. All of that depends only on the
square — and all of it was being worked out again at every node.

Every generator funnelled through `square_at(rank, file)`, which compares four
ways and multiplies before it answers, and it was called on every step of every
ray of every piece, plus eight more times for a knight and eight for a king
inside `attacked_by`. So it is all worked out once, at load, into six tables:

| table | what it holds |
| --- | --- |
| `KNIGHT_TO[n]`, `KING_TO[n]` | the squares reachable in one hop, edge already accounted for |
| `DIAGONAL_RAYS[n]`, `STRAIGHT_RAYS[n]` | the squares outward in each direction, in order |
| `WHITE_PAWN_FROM[n]`, `BLACK_PAWN_FROM[n]` | where a pawn would stand to attack this square |

A slider now walks a ready-made list until something blocks it rather than
computing where the next square would be and whether it is still on the board.
`attacked_by` gains twice over, because `legal_moves_unsorted` calls it once per
pseudo-move to check that a move does not leave its own king in check — it is
the hottest thing in the profile.

| | before | after |
| --- | --- | --- |
| 200 × `legal_moves` | 392 ms | **303 ms** |
| depth 3 | 432 ms | **368 ms** |
| depth 4 | 2,927 ms | **2,356 ms** |
| depth 5 | 9,513 ms | **8,007 ms** |

Node counts are identical to the digit, and the golden came out byte-identical.
That is the point: this changes how the same work is done, not what work is
done. The tables are built by walking the direction lists in the order they are
written, so moves are still generated in exactly the sequence they always were
— which matters, because the golden asserts whole move lists and the engine
breaks its ties on the order it is handed.

## Threads

`engine.funny` can split the root across real OS threads. Each worker is a
whole VM with its own heap, so nothing is shared and nothing needs a lock: a
position crosses as FEN, a list of moves crosses as notations, and a score per
move comes back.

**The obvious way to do this is much slower than not doing it.** Handing every
root move straight out to a worker was measured at **a quarter the speed of one
thread**. Alpha-beta at the root gets its power from having already found
something good: once one move is known to be worth 220, every later move can be
refuted cheaply. Workers searching with the window wide open re-derive all of
that. At depth 4 the split looked at **76,742 positions where one thread needed
5,940**.

So the best-ordered move is searched by the parent *before anybody is hired*,
and the alpha it establishes is handed to every worker. They begin knowing what
they have to beat. With that, the node count comes back to exactly the
sequential figure, and the split is finally worth something:

| depth | one thread | 8 threads | |
| --- | --- | --- | --- |
| 3 | 697 ms | 1,999 ms | 0.34× |
| 4 | 5,655 ms | **1,750 ms** | **3.23×** |
| 5 | 39.8 s | **16.3 s** | **2.44×** |

**And then the transposition table took most of that back.** The two
optimisations pull against each other: a table pays off by sharing what it has
learned across the whole tree, and the root split cuts the tree into eight VMs
that share nothing, so each worker builds its own table from nothing. The same
measurement, on the same machine, from the same position, after the table:

| depth | one thread | 8 threads | |
| --- | --- | --- | --- |
| 4 | **2,927 ms** | 3,612 ms | 0.81× |
| 5 | 9,513 ms | **8,862 ms** | 1.07× |

The precomputed attack tables then widened the gap again. They make one thread
and eight faster by the same proportion, while the split goes on paying its own
overheads regardless:

| depth | one thread | 8 threads | |
| --- | --- | --- | --- |
| 4 | **2,356 ms** | 3,700 ms | 0.64× |
| 5 | 8,007 ms | **7,286 ms** | 1.10× |

The threaded runs look at 2.4× the positions one thread does. Eight processors
buy ten percent at depth 5, and at depth 4 they cost a third of the speed. So
`PARALLEL_FROM` is 5 rather than 4 — the point where it stops being a loss —
and in a served game, which caps at depth 5, the threads now almost never run.

That is an honest result rather than a tidy one. Making them worth having again
would mean handing each worker the parent's table to start from. It is plain
data, so it can cross; it is also a copy per worker, and it has not been tried.

The move and the score are the same at every depth, always — that is the
guarantee, and the golden asserts it.

The node count is a different matter. In the position above it comes out
exactly equal to the sequential figure, but only because the best move there is
a capture, so the ordering searched it first and the alpha the parent handed
over was already the final one. Where the best move is quiet — a back-rank mate,
say — it sorts after the captures, the workers start from a weaker alpha, and
they cannot narrow one another's the way a sequential search narrows its own.
They then look at somewhat more than one thread would. The golden contains one
position of each kind so that both behaviours are on the record.

Below depth 4 the search finishes sooner than eight workers can be started, so
`hands_for` asks for one worker and the game runs in sequence. That threshold
is `PARALLEL_FROM`, and on a slower machine or one with fewer processors it
would be higher. The "hard" difficulty is depth 4, so it is the setting that
gets the 3.2×.

One wrinkle worth knowing if you write something similar: `interns.hire` with a
bare relative path resolves it against the **process's working directory**, not
against the file asking for it. The worker is therefore named with an absolute
path built from `the_script()`.

## What it is not

It plays legal chess. It does not play good chess, and the gap is mostly these:

- **No quiescence search.** The search stops at a fixed depth even in the
  middle of a sequence of captures, so it can misjudge a position where pieces
  are still hanging at the last ply. This is the single largest weakness and
  the usual first thing a real engine adds.
- **A stalemate exactly on the horizon is scored as a position**, not as a
  draw. That is the price of not generating moves at leaves; checkmate is still
  detected, and one ply further up both are.
- **No opening book**, so the first few moves are whatever the tables like.
- **No endgame knowledge.** The king's table wants a corner, which is right
  while there are pieces about and wrong once there are not. It will not drive
  a lone king to the edge to mate it, and there are no tablebases.
- **The transposition table does not outlive a single search.** A fresh one is
  built for every move, so what one move works out is thrown away before the
  next begins. That is deliberate — it keeps `best_move` a function of the
  position in front of it rather than of the game so far — but a real engine
  would keep it. Workers do not share one either, which is most of why the
  threads stopped paying.
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

White uses the outline figures (♔♕♖♗♘♙) and black the solid ones (♚♛♜♝♞♟),
rather than one set coloured two ways. That is not a style choice: **U+265F
BLACK CHESS PAWN is a standard emoji**, so a browser draws it from the emoji
font, where CSS `color` means nothing — which made every pawn on the board come
out black whichever side it belonged to, while the five figures that are not
emoji coloured correctly. Each glyph is followed by U+FE0E to ask for the text
form, and the sides now differ in shape as well as in colour, so they stay
distinct even if a font substitutes.

Bootstrap is vendored into `web/vendor/` rather than fetched: the page is served
under `default-src 'self'` and is meant to work on a machine with no network.

## The certificate

`certs/site.p12` is a test certificate signed by a test CA that nothing else
trusts, so a browser will warn about the issuer — which is exactly what an
unknown CA is for. It is test material and not a secret. `certs/make_cert.sh`
makes a fresh pair.

Run with `--no-tls` for plain HTTP, which is fine on loopback and not on a
network.

## One thread for the serving, several for the thinking

The server itself is single-threaded and unapologetically so. There is one
board and one player, and connections are handled by tasks on an event loop, so
a second tab, a favicon request or a health check cannot sit in the queue
behind a search. Threads would add a lock around the board and buy nothing.

The threads are inside the search instead, where the work actually is, and they
share nothing at all — which is why they need no lock either.

## The tests

```
funny test extensive_examples/chess
```

Four parts, each able to fail on its own: the rules from positions set up by
hand, perft, the engine choosing from fixed positions at fixed depths — which
includes the threaded split picking the same move and score as one thread — and
a complete game of engine against itself asserted move for move. No sockets
anywhere — the server
is a thin wrapper around these modules, and what a wrapper is worth testing for
is that it wraps, not the rules of chess.
