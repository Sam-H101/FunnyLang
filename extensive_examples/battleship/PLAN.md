# FunnyLang — Battleship Plan (`extensive_examples/battleship/`)

> **Status:** built. B1 through B6 are done, and `funny test extensive_examples` is 11/11 on this
> machine with the new golden among them. Branch `feature/battleship`, cut from `master` at the
> chess merge (`6ce71e8`). §7 has the state of each milestone and §10 every deviation — including
> the one that matters most, which is that this environment has no browser in it: the page was
> written against an API driven end to end over a real loopback socket, not against a game
> somebody played.
> **Prerequisite:** nothing new. `extensive_examples/chess/` and `checkers/` are in the tree and
> this example has the same skeleton — rules, an engine, a terminal game, an HTTPS server, a page,
> a golden — so their `http.funny`, `static.funny`, `certs/` and the shape of `serve.funny` are
> taken as read.
> **Deliverable:** a **web-based** game of Battleship. `funny run
> extensive_examples/battleship/serve.funny` serves a page over HTTPS where you place your fleet,
> fire at the engine's, and watch it fire back — FunnyLang is the server, the rules, and the
> opponent; the page is plain HTML, CSS and JavaScript with no framework. Around it: `measure.funny`
> plays thousands of games on every core to say how good each of the engine's three strategies
> actually is; `play.funny` is the same modules in a terminal, kept small, because a game that can
> only be played through a socket can only be tested through a socket; and
> `funny test extensive_examples` runs a golden that asserts the rules, the engine's choices, and a
> whole game shot for shot.

---

## 0. Rules for the executing agent

1. **An example is FunnyLang.** If it needs something the runtime does not have, that is a
   `RUNTIME_PLAN.md` §9 entry and a small, general primitive — never a special case for one
   example. Nothing here is expected to need one (§6).
2. **The golden is deterministic, and it does not lean on the random number generator.** §5 says
   how: the strongest strategy draws no random numbers at all, both fleets in the whole-game part
   are placed by hand, and anything that does roll dice is asserted as a property rather than a
   value. `rizz.seed(n)` is per-VM xoshiro256** and gives the same stream on every platform, but
   `native/rizz.h` says in so many words that no golden depends on that, and this one keeps it true.
3. **The engine cannot see the fleet it is shooting at, structurally.** `engine.funny` takes a
   *view* — the shots fired and what they hit — produced by one redaction function in
   `rules.funny`, and the golden asserts that the view carries no ship cell. That is the property
   that makes a hidden-information game honest, and it is a test rather than a promise.
4. **Every example has a README** that says what it is, how to run it, what it measured, and — in a
   section titled exactly that — what it is not. An example that claims more than it does is worse
   than one that does less.
5. **`the_script()` for every path.** An example runs from any working directory, and an `interns`
   worker is hired by an absolute path built from it (the chess README's wrinkle).
6. **A worker cannot import through a parent directory** (`RUNTIME_PLAN.md` §9, the E5 entry), so
   `http.funny` and `static.funny` are **copies** of chess's, as chess's are of chat's. Say so in
   the README, as chess does.
7. **Stage by explicit path; commit per milestone on `feature/battleship`; push; never merge to
   master.** Other sessions work on this repository at the same time.
8. **Every deliberate deviation gets a §10 entry**, including the ones that turn out to be wrong.

---

## 1. What "done" looks like

```console
$ funny run extensive_examples/battleship/serve.funny
battleship at https://localhost:8443 — ctrl-c to stop
(a test certificate, so your browser will warn about the issuer -- that is correct)
place your five ships, then fire. the engine is playing "hard".
```

Open it and the page is the game, start to finish:

1. **Place.** A tray of five ships beside an empty grid. Pick one, a ghost of it follows the
   pointer, `R` rotates, click to drop; an illegal drop shakes and says why. Or *Place for me*.
   Then *Ready*.
2. **Fire.** Two grids now: yours, with your ships on it, and the enemy's, blank. Click a cell on
   the enemy grid. A ring for a miss, a burst for a hit; then, a beat later, the engine's shot
   lands on your grid the same way. A sunk ship is drawn onto the enemy grid where it was.
3. **Win or lose**, and the engine's fleet is revealed — the one moment it may be. *New game*
   picks a difficulty and starts again. Reloading the page mid-game recovers it; the game lives in
   the server.

And beside it:

```console
$ funny run extensive_examples/battleship/play.funny -- --difficulty normal   # the same game, in text
$ funny run extensive_examples/battleship/play.funny -- --self --seed 7       # the engine against itself
$ funny run extensive_examples/battleship/measure.funny -- --games 2000 --workers 8
strategy   games   mean    median   best   worst   time
random      2000   ...     ...      ...    ...     ...
hunt        2000   ...     ...      ...    ...     ...
density     2000   ...     ...      ...    ...     ...
(8 workers; 1 worker: ... s)

$ funny test extensive_examples/battleship
```

The measurement numbers are the point of B5 and are not guessed here. What the literature says to
expect, so that a wildly different result reads as a bug rather than a discovery: uniformly random
firing needs about **96** shots on average to sink the fleet, hunt-and-target with parity about
**62–65**, and a probability-density strategy about **42–45**. A 100-cell board and a 17-cell
fleet bound every strategy at 100.

---

## 2. The game

Hasbro's rules as printed since 2002, which is the game most people mean:

- A **10 × 10** grid per player. Rows are lettered **A–J**, columns numbered **1–10**, so a cell
  is written `B7` — the notation the physical game prints on the pegboard, and one a player can
  type. Internally a cell is `row * 10 + col`, 0 to 99, and the conversion lives in exactly one
  place.
- Five ships: **carrier 5, battleship 4, cruiser 3, submarine 3, destroyer 2** — seventeen cells.
  Placed horizontally or vertically, whole on the board, never overlapping. **Ships may touch.**
  The no-touching rule is a house rule and a different game; §6 says why this one is chosen.
- Players alternate single shots. A shot is a **miss**, a **hit**, or a **hit that sinks** a ship,
  in which case the ship is named. A cell may be fired at once.
- The first player to sink all five of the other's ships wins. There is no draw.

One deliberate departure, applied to both sides equally: **when a ship is sunk, its cells are
revealed**, not only its name. The physical game announces the name because a person can usually
work out the cells from it; where two ships touch, that inference is ambiguous, and the ambiguity
is exactly the case that makes a probability-based engine subtly wrong (§4.2). Revealing the cells
removes it for both players at once. The README says this is a departure and why.

Not in scope: the *salvo* variant (several shots a turn), the 1967 version's board, and any variant
with different ships.

---

## 3. Why this shape

Chess and checkers are games of **perfect information**, and their engines are the same
algorithm: search the tree, score the leaves. Battleship has **no tree to search**. The opponent's
fleet is hidden, a turn does not change the position in any way the other player can see, and
nothing you do influences what your opponent does. Minimax has nothing to bite on.

What replaces it is inference. Everything the engine knows is the record of its own shots, and the
question it answers is "given what has been hit and missed, which untried cell is most likely to
hold a ship?" That is a different kind of program from the two game examples already in the tree,
which is the reason to write it: the third game shows that "FunnyLang plays the other side" does
not have to mean negamax.

It also changes what the golden can hold still. A search engine is deterministic by construction
if its tie-breaks are; a probabilistic one is deterministic only if it draws no random numbers.
The strongest strategy here is built to draw none, so that a whole game can be asserted shot for
shot — §5.

Three things follow for the shape of the code:

- **The page is the deliverable, and it is a client of the API and nothing more.** Every rule
  the page needs is in the state it is sent — which cells are untried, which ships are sunk and
  where, whose turn it is — so it never decides a hit for itself and cannot drift from the rules.
  The terminal game is the same modules against text, kept to what the other two games keep it
  to: enough to play, and enough to see the engine play itself.
- **One thread for the game.** The engine's most expensive act is counting placements: five ships,
  two orientations, at most a hundred starts, a handful of cells each — a few thousand cheap
  checks per shot, and there are at most a hundred shots. There is nothing worth a second thread.
  The event loop stays, as in checkers, so a favicon cannot queue behind a turn.
- **Many threads for the measurement.** Whether a strategy is any good is a statistical question,
  and answering it means playing thousands of games. That is embarrassingly parallel and is where
  `interns` earns a place in this example: one worker per core, each playing its share, no shared
  state, a static split because every game costs about the same (the opposite of `word_count/`,
  where a pool was needed because files were not).

---

## 4. Each file

| file | what it is | est. lines |
| --- | --- | --- |
| `serve.funny` | the HTTPS server and the JSON API the page plays through | 450 |
| `web/index.html`, `web/style.css`, `web/js/battleship.js` | the page — the game as a person sees it | 120 + 220 + 400 |
| `rules.funny` | the board, the fleet, placement, firing, sinking, and the redacted view | 300 |
| `engine.funny` | three strategies that choose a shot from a view | 350 |
| `measure.funny`, `measure_worker.funny` | thousands of games on every core | 180 |
| `play.funny` | the same game in a terminal, and engine against engine | 200 |
| `http.funny`, `static.funny` | copies of chess's, per §0 rule 6 | 340 (copied) |
| `test_battleship.funny`, `.expected` | the golden, §5 | 300 + 150 |
| `web/vendor/bootstrap.min.css` | Bootstrap 5.3.3, vendored | copied |
| `certs/` | the test CA and identity, copied from chess | copied |
| `README.md` | §8 | 350 |

### 4.1 `rules.funny` — the board and what may be done to it

Answers four questions and knows nothing else: may this ship go here; what does this shot hit;
is this fleet sunk; and what may the other player see. It **never mutates a board it is handed**,
matching the other two games — a shot returns a new board, and the old one is still the old one.

```funny
flex deadass SIZE = 10
flex deadass FLEET = [["carrier", 5], ["battleship", 4], ["cruiser", 3],
                      ["submarine", 3], ["destroyer", 2]]

flex bet new_board()                      // no ships, no shots
flex bet cell_of(notation)                // "B7" -> 16, ghost if malformed or off the board
flex bet notation_of(cell)                // 16 -> "B7"
flex bet place(board, name, at, across)   // a new board, or {"error": "..."} -- overlap, edge, name unknown, already placed
flex bet unplace(board, name)             // the ship lifted off again, for the placement page
flex bet random_fleet(board)              // all five placed at random -- the ONLY function here that rolls dice
flex bet fleet_complete(board)            // all five down
flex bet fire(board, cell)                // {"board", "result": "miss"|"hit"|"sunk", "ship", "cells"} or {"error"} on a repeat
flex bet all_sunk(board)
flex bet view_of(board)                   // what the OTHER player may know -- §4.1.1
flex bet render(board, hide_ships)        // the text grid
```

A board is a `groupchat`:

```funny
{
    "grid":  [0] * 100,          // 0 empty, else 1 + index into "ships"
    "shots": [0] * 100,          // 0 untried, 1 miss, 2 hit
    "ships": [                   // in FLEET order, ghost until placed
        {"name": "carrier", "size": 5, "cells": [0, 1, 2, 3, 4], "hits": 0},
        ...
    ],
}
```

`place` checks the name, that the ship is not already down, that every cell is on the board (a
horizontal ship may not wrap a row — `cell_of` arithmetic makes that mistake easy, so it is a
test), and that every cell is empty. It does **not** check adjacency: ships may touch.

`fire` refuses a repeated shot with an error rather than counting it as a miss, because a page or
a player that fires twice at one cell has a bug that should be seen. On a hit it increments that
ship's `hits`; when `hits == size` the result is `"sunk"` with the name **and the cells** (§2).

`random_fleet` places the five in `FLEET` order, largest first, each at a `rizz.roll`ed start and
orientation, retrying until `place` accepts. Largest first means the retry loop is short; a
destroyer always fits somewhere. It is the only function in the rules that touches `rizz`, so a
caller that wants reproducibility seeds once and knows where the draws go.

#### 4.1.1 The view, and why it is in the rules

```funny
{
    "shots": [...],                                        // the same 0/1/2 grid, the shooter's own record
    "sunk":  [{"name": "destroyer", "size": 2, "cells": [57, 67]}],
    "afloat": ["carrier", "battleship", "cruiser", "submarine"],   // names and, through FLEET, sizes
}
```

That is everything the other player is entitled to know, and nothing else: no `grid`, no unsunk
ship's cells, no hit counts. It is in `rules.funny` rather than `serve.funny` so that the golden
can test it without a socket, and so that the engine's signature is `choose_shot(view, ...)` — a
function that is *handed* a fleet cannot cheat by accident, and a function that is not cannot
cheat at all.

### 4.2 `engine.funny` — three ways to choose a shot

```funny
flex deadass STRATEGIES = ["random", "hunt", "density"]
flex bet choose_shot(view, strategy)      // -> a cell, 0..99, untried in view["shots"]
flex bet unresolved_hits(view)            // hit cells not belonging to any sunk ship
flex bet density_map(view)                // the 100 counts, for the page's heat map and the golden
```

**`random`** — a uniformly random untried cell. The baseline; a strategy is only as good as its
distance from this one.

**`hunt`** — the way people play once they have thought about it. While there are no unresolved
hits, fire at a random untried cell of **one parity** (`(row + col) % 2 == 0`): the smallest ship
is two long, so every ship covers at least one cell of each parity and half the board can be
skipped. Once there is a hit, *target*: fire at the untried orthogonal neighbours of unresolved
hits; if two unresolved hits are in a line, at the two ends of that line first. A sunk
announcement resolves its cells and, if nothing is left unresolved, hunting resumes.

**`density`** — for every ship still afloat and every placement of it (two orientations, every
start), the placement is *possible* if it crosses no miss and no sunk ship's cell. Every possible
placement adds one to each cell it covers. If there are unresolved hits, only placements covering
at least one of them count, and each such cell is weighted by how many hits it would explain. Fire
at the untried cell with the highest count. **Ties break on the lowest cell index**, and no
random number is ever drawn — that is what makes it a function of the view and lets the golden
assert it exactly. Parity falls out of this for free: a cell with more placements through it is
one the map already prefers.

Cost of one `density` shot, worst case: 5 ships × 2 orientations × 100 starts × 5 cells = 5,000
cell checks. A game is at most 100 shots. Fast enough that the server answers at once and the
measurement can play thousands.

**What "sunk reveals cells" buys.** Without it, `unresolved_hits` would have to guess which hit
cells belonged to the sunk ship, and where two ships touch the guess can be wrong in a way that
either leaves a phantom hit for the map to chase or wrongly resolves a hit that belonged to a
ship still afloat. With cells revealed, "unresolved" is a set difference and the map is exact.
The README's "what it is not" still says the model is per-ship — it counts placements of each
ship independently and does not check that the five can coexist — which is the standard
approximation and where a stronger engine would start.

**Difficulty is an algorithm, not a depth.** The page calls them *easy*, *normal* and *hard*, and
the README says what each actually does. This is the opposite of chess and checkers, where the
difficulty is how far the same algorithm looks; worth a sentence there.

### 4.3 `serve.funny` — one game, five routes

The shape of chess's server: single-threaded, an event loop, TLS from `certs/site.p12` by
default, `--no-tls`, `--host`, `--pfx`, `--seconds`, and `--difficulty`. One game per server, not
per browser, for the reason the other two give.

| route | body | what it does |
| --- | --- | --- |
| `GET /api/state` | | everything the page may know — §4.3.1 |
| `POST /api/new` | `{"difficulty": "hard", "seed": 7}` both optional | a fresh game; the engine places its fleet; phase is `placing` |
| `POST /api/place` | `{"ship": "carrier", "at": 23, "across": true}` or `{"auto": true}` or `{"lift": "carrier"}` | one ship down, all of them, or one up again; an illegal placement is `400` with the reason |
| `POST /api/ready` | | the fleet is locked; phase is `playing`, human to fire |
| `POST /api/fire` | `{"at": 44}` | resolve the shot, then the engine's reply; both results come back |

`fire` returns both halves of the exchange in one response — `{"you": {"at", "result", "ship",
"cells"}, "engine": {...}}` plus the state — because the engine answers in microseconds and a
second round trip would exist only to be waited for. The page animates the two in sequence anyway.

A shot when it is not the human's turn, before the fleet is ready, or after the game is over is a
`409`. A repeat shot is a `400` with the cell named.

The human fires first. That is a rule of this example rather than of the game (the physical game
says the younger player starts), and it keeps a game with a fixed seed fixed.

#### 4.3.1 What the state carries, and what it never does

```json
{
  "phase": "playing", "turn": "you", "difficulty": "hard", "outcome": null,
  "you":    { "board": {full board, ships and all} },
  "engine": { "view": {shots, sunk, afloat} },
  "last": { "you": {...}, "engine": {...} },
  "shots": { "you": 12, "engine": 11 },
  "played": [ ... every exchange so far, for a page that is reloaded ... ]
}
```

`engine` is **`view_of(engine_board)` and nothing else**. The server has no route, flag, or debug
mode that sends the engine's fleet; the golden asserts `view_of`, and `serve.funny` is a wrapper
around it. When the game is over the engine's board is revealed in full, which is the one moment
that is allowed, and the state says so with a separate `"revealed"` key so the page cannot mistake
it for the view.

### 4.4 `web/` — the page

This is the thing being built; everything else exists so that it can be. Two 10 × 10 grids under
Bootstrap's layout and nothing else of its, as the other two games do it: the board is
`style.css`, the framework is the page around it. Three phases — placing, firing, over — and the
page shows exactly one of them at a time, chosen from `phase` in the state.

**Placing.** A tray of five ships. Click one, hover the grid and a ghost of the ship follows the
pointer; `R` or a button rotates; click to drop. An illegal drop shakes and says why (the server's
reason, verbatim). "Place for me" calls `{"auto": true}`; a placed ship can be clicked to lift it.
"Ready" locks the fleet. This is the page's first still frame, and it works with no shots fired:
the tray, the empty grid, and the two buttons.

**Firing.** Click a cell on the enemy grid. Untried cells are clickable; tried ones are not, so
the repeat-shot error is a thing the page cannot cause. The result is animated — a ring for a
miss, a burst for a hit — then, after a beat, the engine's shot lands on your grid the same way.
A sunk ship is drawn onto the enemy grid from the cells the server revealed.

**A heat map, optional.** A toggle that shades every untried enemy cell by `density_map` — what
the hard engine would fire at next. It is the single most explanatory picture of how the engine
thinks, it costs one extra key in the state, and it is off by default because it is also a hint.

**Over.** The outcome, the shot counts for both sides, the engine's fleet revealed on the enemy
grid (from the state's `revealed` key, which exists only now), and *New game* with a difficulty
picker. That is also where the page starts when the server has no game yet.

**Reload.** The page asks for `/api/state` on load and draws whichever phase it is in, replaying
`played` without animation. A game is never lost to a refresh, because it never lived in the
browser.

No inline script, no framework for the logic, `default-src 'self'`, Bootstrap vendored — as the
other pages. Every rule the page needs is in the state (what is untried, what is sunk); it never
decides a hit for itself.

### 4.5 `play.funny` — the terminal twin

```
play.funny [--difficulty random|hunt|density] [--seed N] [--self] [--auto]
```

The same rules and engine against a text board, and no more of it than chess and checkers have:
two grids side by side, *your fleet* with hits marked on it, and *your shots* with `.` untried,
`o` miss, `x` hit, and `#` for the cells of a ship you have sunk. Placement is typed —
`place carrier B3 across`, `auto` for the rest, `lift cruiser` to pick one up — then `B7` to
fire. `--self` has the engine place and fire for both sides with the chosen strategy and prints
the game, which is the quickest way to see whether a strategy is any good and is the reason this
file exists. `--seed` seeds `rizz` so a `--self` game can be shown to somebody else.

### 4.6 `measure.funny` — how good is each strategy, really

```
measure.funny [--games N] [--workers N] [--strategy random|hunt|density|all]
```

Hires `--workers` interns from `measure_worker.funny`, each assigned `{"strategy", "from", "to"}`
— a contiguous range of game numbers. A worker seeds its own generator from the game number
before each game (per-VM `rizz`, so the workers cannot interfere), places a random fleet, plays
the strategy against it to the end, and delivers the list of shot counts. The boss concatenates
and prints mean, median, best, worst, and the wall time — and then plays the same games on one
worker so the README's speed-up is a measurement and not a claim.

The split is static, which the file's comment says is the deliberate contrast with
`word_count/`'s pool: every game costs about the same, so dealing them out one at a time would
buy nothing but a mailbox.

Game *n* is the same game whichever worker plays it and however many there are, because the seed
is the game number. The golden does not assert that (rule 2), but `measure.funny --workers 1` and
`--workers 8` printing the same table is checked by hand and recorded in the README.

---

## 5. The golden

`test_battleship.funny`, five parts, each able to fail on its own, no sockets anywhere. The
server is a wrapper around these modules and what a wrapper is worth testing for is that it wraps.

1. **The board.** `cell_of`/`notation_of` round trips for the corners and the middle; `A0`, `K1`,
   `A11`, `b7` and `""` are `ghost`; a horizontal ship at `A9` does not wrap onto row B.
2. **Placing.** A legal placement; an overlap refused with a reason; off the edge refused; two
   ships **touching** accepted; a name that does not exist refused; the same ship twice refused;
   `unplace` and place again. Then `rizz.seed(7)` and `random_fleet` — asserted as **five ships,
   seventeen distinct cells, all on the board**, and not as positions (rule 2).
3. **Firing.** On a hand-placed fleet: a miss, a hit, the shot that sinks the destroyer and names
   it *with its cells*, a repeat refused, and `all_sunk` going true on the seventeenth hit. Then
   `view_of` of that board: it has exactly the keys `shots`, `sunk`, `afloat`; `json.spill` of it
   contains no cell of any ship still afloat; and the sunk ship's cells are there.
4. **The engine.** From hand-built views:
   - the **empty** view: `density` fires at one fixed cell (the map's maximum, lowest index on the
     tie), and the same view twice gives the same cell;
   - one hit and nothing else: it fires at an orthogonal neighbour;
   - two hits in a line: it fires at an end of the line, not beside it;
   - a sunk destroyer with no other hits: it goes back to hunting, and never at a sunk cell;
   - a corner cell with misses around it that no afloat ship could reach: its count is zero and
     it is never chosen;
   - `hunt` with `rizz.seed(1)`: its first ten shots all share one parity, it never repeats a cell,
     and it sinks a hand-placed fleet within 100 shots (properties, not values);
   - `random` with a seed: never repeats, finishes within 100.
5. **A whole game.** Two hand-placed fleets — one spread out, one with three ships touching, so
   the resolution logic is exercised — and `density` against `density`, the human's side first.
   Asserted shot for shot in notation, ten to a line, then the winner and the shot count. Runs in
   well under a second.

Under `FUNNY_GC_STRESS` and ThreadSanitizer this golden has nothing special to say: no sockets,
no workers. `measure.funny` is the threaded piece and it is not in the golden, because its output
is a table of timings; it is checked by hand and its sanity (one worker and eight agree) is in
the README.

---

## 6. Decisions already made

- **Ships may touch.** It is the printed rule, the no-touching variant is a house rule, and —
  the real reason — touching ships are the case that makes the engine's inference hard, which is
  the interesting part. A rule chosen to make the engine's life easy would be the wrong lesson.
- **Sinking reveals the ship's cells, to both sides.** §2 and §4.2. The departure from the
  physical game is small, symmetric, and turns an approximation into an exact set difference.
  Recorded in the README, not hidden.
- **The strongest strategy is RNG-free and the golden asserts it exactly; the others are asserted
  as properties.** §5. No golden depends on the cross-platform stability of a seeded stream, so
  `native/rizz.h`'s statement stays true.
- **`view_of` lives in the rules, not the server.** So it can be tested without a socket, and so
  the engine's argument type is the redaction.
- **The game is single-threaded; the measurement is not.** §3.
- **The browser is the deliverable; the terminal is the twin.** The page is where the game is
  played and where the work goes. `play.funny` is kept to what it is in the other two games, and
  it is built *after* the server and page (B4, not B3) so that nothing about the terminal shapes
  the web version.
- **One human against the engine, in one browser.** Battleship is a two-player game and `chat/`'s
  `ws.funny` could carry two browsers against each other, but that needs sessions and a lobby,
  which every game example here has declined, and the engine is what this example is for. It is
  the obvious extension and the README names it as one. If the owner wants two people playing
  each other over the web instead, that is a different plan (a §10 entry to start with), not a
  milestone of this one.
- **No new runtime primitives.** `rizz.seed`, `interns`, `json`, `internet` cover everything.
  If that turns out wrong it is a `RUNTIME_PLAN.md` §9 entry first.
- **Human fires first.** §4.3.
- **The version bump is not part of this plan.** Chess landed as `2.1.1` in its own commit after
  the example was merged, touching the three version files and regenerating the toolchain blob.
  This example gets a `CHANGELOG.md` entry under B6; whether and when it becomes `2.1.2` is the
  owner's call.

---

## 7. Milestones

Commit per milestone on `feature/battleship`, staged by explicit path. Each milestone leaves
`funny test extensive_examples` green.

### B1 — the rules
- [x] `rules.funny`: board, notation, `place`/`unplace`, `random_fleet`, `fire`, `all_sunk`,
      `view_of`, `render`
- [x] `test_battleship.funny` parts 1–3 and its `.expected`
- [x] directory skeleton; `http.funny`, `static.funny`, `certs/`, `web/vendor/` copied from chess

### B2 — the engine
- [x] `engine.funny`: `random`, `hunt`, `density`; `unresolved_hits`; `density_map`
- [x] golden parts 4 and 5; `density` confirmed repeatable — it is asked for the same shot twice
      from the same view and a whole game is asserted shot for shot, which no stream-dependent
      engine could pass. `rizz` appears in `engine.funny` only inside `hunt_shot` and the `random`
      branch of `choose_shot`.

### B3 — the server and the page
- [x] `serve.funny`: the five routes, TLS by default, the same flags as chess
- [x] `web/`: the three phases; placing with a ghost and rotation, *Place for me*, lifting a ship
      back up; firing with animation and the engine's reply a beat later; sunk ships drawn; the
      fleet revealed at the end; the optional heat map; a reload that recovers the game
- [x] **Partly.** There is no browser in this environment, so the API was driven end to end over a
      real loopback socket instead: every route, both refusal codes, a whole game to its end, and
      the no-leak property re-checked at each phase from outside the server. 41 checks, all
      passing. What that does *not* cover is the page itself — see §10, and the README's own
      "what it is not", which says so in the first bullet.

### B4 — the terminal twin
- [x] `play.funny`: placement commands, firing, `--self`, `--seed`, `--auto`, `--difficulty`
- [x] a `--self` game at each difficulty read by eye: 88 shots at easy, 45 at normal, 37 at hard,
      which is the right ordering and the right rough size

### B5 — the measurement
- [x] `measure.funny`, `measure_worker.funny`
- [x] all three strategies over 2,000 games; 1 worker against all eight; the machine named in the
      README; `--workers 1` and `--workers 8` print identical means, medians, bests and worsts

### B6 — the README and the changelog
- [x] `README.md` per §8, with B5's numbers
- [x] `CHANGELOG.md` entry, as `[Unreleased]` per §6; the top-level `README.md`'s example list
- [x] `EXAMPLES_PLAN.md` left alone — it is a closed plan; this file is the record

---

## 8. The README, in outline

What it is and how to run it (serve, play, measure, test). What is here (the file table). The
rules it implements, including the two things this example decides — touching, and sinking
revealing cells — and why. The notation. **Why there is no search**, and what inference replaces
it with; the three strategies, plainly; why the hard one is deterministic and what that costs
(a player can learn it: it always opens on the same cell). **What it measured**: the strategy
table, the parallel measurement with its speed-up and the machine it ran on. **The engine cannot
cheat**, as a property of the code and a line in the golden rather than a promise. The API and
the no-leak rule. The page, the certificate, one thread for the game and eight for the
measurement. **What it is not**: not the salvo variant; not two humans over a network; one game
per server; a per-ship model that does not check the fleet can coexist; no modelling of the
opponent's habits; the hard engine's fixed opening; and that `measure.funny` measures the engine
against random fleets, which is not the same as against a person who places deliberately.

---

## 9. Cost sheet

| piece | lines (est.) |
| --- | --- |
| `rules.funny` | 300 |
| `engine.funny` | 350 |
| `play.funny` | 220 |
| `serve.funny` | 450 |
| `measure.funny` + worker | 180 |
| the golden + expected | 450 |
| `web/` (html, css, js) | 740 |
| `README.md` | 350 |
| copied (`http`, `static`, certs, Bootstrap) | — |
| **new, written** | **~3,000** |

---

## 10. Deviations log

**The page was never opened in a browser, and this is the one that matters.** §7's B3 asks for a
full game played in a browser on TLS and on plain HTTP, a second tab, and a reload mid-game. The
environment this was built in has no browser in it, so none of that happened. What happened
instead: `serve.funny` was started on loopback and its whole API driven from a FunnyLang client —
every route, a fleet placed by hand and then by `auto`, a ship lifted off again, `ready` refused
while ships were ashore, firing refused before the fleets were out and again after the game was
over, a repeated shot refused by square name, a complete game played to a finish, the static files
served, `/../rules.funny` refused with a 404, and the no-leak property re-checked at six points
through the game. Forty-one checks, all passing.

That is a real test of the server and **no test at all of the page**. `web/js/battleship.js` was
written against exactly the JSON that driver read back, which is the best that could be done here,
and it is a weaker claim than the plan asked for. It is recorded in the README's own "what it is
not" as its first bullet rather than left for somebody to discover.

**`step` is a keyword, so the engine's direction parameter is `stride`.** It belongs to
`grind i from 0 to 10 step 2` and cannot be the name of anything. Worth a line because the error
was excellent — *"'step' is a keyword ... pick another name"* — and because the same trap caught
`from` in the chess example, which is noted in its `serve.funny`.

**A ship still afloat is reported with its size as well as its name.** §4.1.1 sketches `afloat` as
a list of names, with sizes to be looked up through `FLEET`. It carries `{"name", "size"}` instead,
because the engine needs the size on every single placement check and a lookup through a constant
table would be the same information one indirection further away. It leaks nothing: which five
ships are in a fleet is printed on the box.

**The state carries `history` as well as `played`.** §4.3.1 lists `played` and describes it as
both the animation queue and what a reloaded page recovers from. Those turn out to be different
things — `played` is cleared at the start of every exchange so the page animates only what just
happened — so the whole game is kept separately under `history`, which is what the log renders and
what a reload reads.

**`cell_of` is strict about case, and the interfaces are forgiving for it.** §5 asks the golden to
show that `b7` does not parse, which makes the rules' parser exact enough that
`notation_of(cell_of(s))` is `s` whenever it is anything. A person typing at `play.funny` would
find that maddening, so `play.funny` upper-cases before it calls in. The strictness lives in the
rules and the forgiveness lives in the interface, rather than every caller inheriting a loose
parser.

**`measure.funny` gained a `--compare` flag** that was not in §4.6's sketch. Without it the
one-worker baseline would have to be produced by a second invocation with different arguments, and
the README's speed-up would be two measurements a reader has to trust were taken under the same
conditions. With it they are one run.

**`hunt` measured far better than §1 said to expect, and it is the algorithm, not a bug.** §1 gives
hunt-and-target as about 62–65 shots and says a wildly different result should read as a defect.
It came out at **51.7**. The cause is in §4.2's own description: this `hunt` does not merely fire at
the neighbours of a hit, it goes for the **ends of a line** of hits once two are adjacent, which
skips every square beside a ship that is already known to be lying straight. That is a genuinely
stronger algorithm than the one the 62–65 figure describes. The other two predictions landed where
§1 said they would — `random` at 95.1 against "about 96", `density` at 44.3 against "42–45" — which
is what makes the third one readable as a difference in the algorithm rather than in the
arithmetic.

**The static split is a loss on cheap work, and the README says so.** At 2,000 games per strategy
it is worth 5.0× on `density` and 1.4× on `random`; at 120 games `random` was *slower* on eight
workers than on one, 0.8 s against 0.5 s, because eight VM startups are most of the budget for work
that small. §4.6 did not predict this. It is in the README with its numbers rather than measured
only at the size that flatters the threads.

**The top-level `README.md`'s example bullet was reworded rather than recounted.** §7's B6 asks for
"the example count and list" to be updated. That bullet sits inside the *"New in v2.1.0"* block and
said "Eight worked examples", which was accurate for 2.1.0 and had already gone stale when chess
landed in 2.1.1 without touching it. Bumping it to ten would have made the v2.1.0 block claim two
examples that came after it, so the version claim was dropped instead — "Worked examples ... ten of
them now" — and chess, which had been missing, is named alongside battleship.

**The `CHANGELOG.md` entry is `[Unreleased]`,** per §6's decision that the version bump is the
owner's call. No version constant was touched and `native/toolchain_blob.c` was therefore not
regenerated, which is what chess did in a separate commit after its example was merged.
