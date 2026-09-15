# battleship — a game in a browser, and an opponent that has to work it out

Battleship, in FunnyLang, played in a browser. You put your fleet out, you fire,
and the language fires back — but unlike the chess and draughts examples next
door, it cannot see what it is shooting at, and that changes what an engine
even is.

```console
$ funny run extensive_examples/battleship/serve.funny
battleship at https://localhost:8443 — ctrl-c to stop
(a test certificate, so your browser will warn about the issuer -- that is correct)
place your five ships, then fire. the engine is playing "hard".
```

Open <https://localhost:8443>. Your browser will warn about the certificate, and
it is right to — see [the certificate](#the-certificate).

```console
$ funny run extensive_examples/battleship/serve.funny -- 8080 --no-tls
$ funny run extensive_examples/battleship/serve.funny -- --difficulty normal --seed 7

$ funny run extensive_examples/battleship/play.funny              # the same game, in text
$ funny run extensive_examples/battleship/play.funny -- --self    # the engine against itself

$ funny run extensive_examples/battleship/measure.funny -- --games 1000 --compare
$ funny test extensive_examples/battleship
```

## What is here

| file | what it is |
| --- | --- |
| `serve.funny` | the HTTPS server and the five-route JSON API the page plays through |
| `web/` | the page: two boards, a fleet to lay out, and no framework in the logic |
| `rules.funny` | the board, the fleet, firing — and the redaction that hides a fleet |
| `engine.funny` | three strategies that choose a shot from what can be seen |
| `measure.funny`, `measure_worker.funny` | thousands of games across every core |
| `play.funny` | the same modules against a text board |
| `http.funny`, `static.funny` | request parsing, and serving `web/` and nothing else |
| `test_battleship.funny` | the golden: the rules, the engine, and a whole game |
| `Dockerfile`, `compose.yaml` | a C compiler in, a 1.2 MB game out |
| `healthcheck.funny` | what the container asks itself, in FunnyLang |
| `certs/` | a test certificate, and the script that made it |

`http.funny` and `static.funny` are copies of chess's, which are copies of
chat's, rather than imports. That is deliberate and it is a workaround: a module
reached through a parent directory cannot be loaded by a worker, because bundle
keys are relative to the entry file's directory. It is written up in
`RUNTIME_PLAN.md` §9.

## The rules it implements

Hasbro's, as printed since 2002. A ten-by-ten grid each, rows lettered **A–J**
and columns numbered **1–10**, so a square is written `B7` — the notation the
physical pegboard carries, and one a person can type. Five ships, seventeen
cells: **carrier 5, battleship 4, cruiser 3, submarine 3, destroyer 2**. You
alternate single shots, a square may be fired at once, and the first to sink all
five of the other's ships wins. There is no draw.

Two things about this implementation are decisions rather than transcription,
and both are here because of what they do to the engine.

**Ships may touch.** The no-touching rule is a house rule and a different game.
It is also the rule that would have made the engine's job easy, and touching
ships are exactly the case that makes inference hard — which is the part worth
having.

**Sinking a ship reveals its cells, not only its name.** This one *is* a
departure from the printed game, it applies to both sides equally, and the
reason is in the next section: it turns "which of my hits are still
unaccounted for" from a guess into a set difference. The physical game announces
the name because a person can usually work the cells out from it; where two
ships touch, that inference is genuinely ambiguous, and an engine that gets it
wrong either chases a hit that has already been explained or writes off one that
has not.

Not implemented, and not pretended: the *salvo* variant, the 1967 board, and any
fleet but this one.

## Why there is no search

Chess and draughts are games of **perfect information**, and both engines in
this repository are the same algorithm: build the tree, score the leaves, prune
what cannot matter. Battleship hands minimax nothing to work with. The fleet is
hidden, your shot does not change what your opponent is able to do, and there is
no position to evaluate — only a record of what you have fired and what it hit.

What replaces the search is **inference**, and the question it answers is: given
the misses, the hits nobody has accounted for, and which ships are still afloat,
which untried square is most likely to be holding one?

That is a different kind of program from the other two games, which is the
reason to write a third. It also changes what a golden can hold still. A search
engine is reproducible as soon as its tie-breaks are; a probabilistic one is
reproducible only if it draws no random numbers at all — so the strongest
strategy here is built to draw none.

## The three strategies

Difficulty is an **algorithm, not a depth**. In chess and draughts the settings
run the same search further; here they are three different ideas, and the
weakest is not a handicapped version of the strongest.

**`random` (easy)** — a uniformly random untried square. The baseline, and the
only honest way to say what the other two are worth.

**`hunt` (normal)** — how somebody plays after a minute's thought. With nothing
hit, fire at a random untried square of **one parity**: the smallest ship is two
cells long, so every ship covers at least one square of each colour of the
chequerboard and half the board can be skipped with nothing at risk. Once
something is hit, stop sweeping and finish it — fire beside the hits that are
unaccounted for, and if two of them are in a line, at the **ends** of that line
rather than beside it, because a ship is straight and beside it is water.

**`density` (hard)** — for every ship still afloat and every way it could be
lying, the placement is *possible* if it crosses no miss and no cell of a ship
already sunk. Every possible placement adds one to each untried square it
covers, and the square under the most of them is where it fires. When there are
unaccounted-for hits, only placements covering at least one of them count, and
each is weighted by how many it would explain — a line of three unexplained hits
is far better explained by a cruiser lying along it than by one clipping its
end.

Parity falls out of that rather than being bolted on: a square more placements
pass through is one the map already prefers. So does the opening move. On an
empty board the map's maximum is dead centre, and **it opens on E5 every single
game** — for the same reason a person taps the middle first.

One shot costs at most five ships × two orientations × a hundred starts × five
cells, so about five thousand cheap checks, in a game of at most a hundred
shots. That is why the server answers instantly and the measurement below can
play thousands of games.

### What the model does not do

`density` counts placements of each ship **independently**. It never checks that
the five could all fit on the board at once, which is the standard
approximation and is where a stronger engine would start. It also does not model
the person on the other side at all — people do not place ships uniformly at
random, and an engine that knew that would open differently.

## What it measured

`measure.funny` plays whole games against randomly placed fleets and reports the
distribution of how many shots each strategy needed. One game says nothing: a
lucky random sweep beats a careful one often enough to be worthless as evidence.

Two thousand games per strategy, on an Intel i7-7700K (8 logical cores) under
Windows 11:

| strategy | games | mean | median | best | worst |
| --- | --- | --- | --- | --- | --- |
| `random` | 2,000 | 95.3 | 97.0 | 59 | 100 |
| `hunt` | 2,000 | 51.7 | 53.0 | 22 | 69 |
| `density` | 2,000 | **44.5** | 44.0 | 21 | 70 |

Firing at random needs about 95 shots to clear a hundred-square board of
seventeen cells of ship, which is what the arithmetic says it should. Thinking
about parity and finishing what you start halves it. Working out where the ships
*must* be takes another seven shots off, and the gap between the second and
third rows is smaller than people expect — most of the value in playing well is
in the first idea, not the third.

Every figure in that table is **exactly reproducible**: five separate runs of
this measurement, at two different game counts and on one worker and on eight,
produced identical means, medians, bests and worsts. That is the point of
seeding each game from its own game number, and it is what makes the timings
below a measurement of the split rather than of two different samples.

`hunt` comes out better than the figure usually quoted for hunt-and-target
(around 62), because this one also goes for the ends of a line rather than
merely the neighbours of a hit. That is a real difference in the algorithm, not
a difference in luck.

### The threads are in the measurement, not the game

Playing one game is work for one core: the engine's most expensive act is
counting a few thousand placements, once per shot. Playing a thousand games is
embarrassingly parallel, and that is where `interns` earns its place here.

**This example does not quote a speed-up multiplier, and the reason is the
honest part of this section.** Here are three repeats of the identical command,
2,000 games each, seconds:

| strategy | 8 workers | 1 worker |
| --- | --- | --- |
| `random` | 3.3, 2.8, 2.9 | 8.5, 8.0, 8.0 |
| `hunt` | 9.1, 8.8, 12.2 | 54.6, 54.3, **15.2** |
| `density` | 40.2, 64.0, 56.5 | 290.1, **142.2**, 284.3 |

Eight workers beat one in all nine pairings, so the split is worth having and
that much is safe to say. But the single-worker figures for `hunt` and
`density` are **bimodal across identical repeats** — the same 2,000 `hunt` games
took 54 seconds twice and 15 seconds once — and a ratio computed from those
would be anywhere between 1.2× and 6.2× depending on which pair you picked.
Something on this machine, or in how a single long-lived worker's heap behaves
over two thousand games, is not being controlled for here. Publishing a precise
multiplier off numbers that move by 3.6× would be inventing a result, so this
says what was seen instead. The first of the three runs was taken while the
machine was doing other work and the other two on an idle one, and that is
plainly not the explanation, because the odd one out is an idle run.

What survives is the shape of it: `random` is stable and gains least, because
2,000 cheap games leave eight VM startups a large share of the budget, and
`density` gains most, because its games are expensive enough to swamp them.

**The split is static**, one contiguous range of game numbers per worker, which
is deliberately the opposite of `word_count/`'s pool. That example deals work
out a job at a time because its files are wildly different sizes; every game
here costs about the same, so a static split balances itself and a mailbox would
be pure overhead.

**Both columns are the same two thousand games.** A worker seeds its generator
from the **game number** before each game, so game 41 is the same fleet whoever
plays it and however many workers there are. That is why the distribution table
above is identical on one worker and on eight, and it is the only reason a
timing comparison here means anything at all.

## The engine cannot cheat, and that is structural

A hidden-information opponent is only worth anything if it really is blind, and
"we were careful" is not a guarantee. So the redaction is a function in
`rules.funny`:

```funny
flex bet view_of(board)   // -> {"shots", "sunk", "afloat"}
```

That is your own record of where you have fired, the ships you have sunk and
where those turned out to be, and the **names and sizes** of the ones still out
there. No grid, no cell of any ship still in the game, no hit counts. Sizes are
not a leak — which five ships are in a fleet is printed on the box.

**Every function in `engine.funny` takes a view.** Not a board. There is no
fleet in scope for it to consult, so it cannot consult one by accident, and the
difficulty settings cannot differ in what they are allowed to see. The golden
asserts it rather than trusting this paragraph: it fires at a fleet, takes the
view, and checks that every cell the view names belongs to a ship that has
already been sunk.

The same property is checked from the far side of a socket, which is a stronger
statement, because it is the one a player could verify themselves: drive the
API, read the JSON, and confirm that `state.engine` has exactly the three keys
above at every phase of the game and that `revealed` is absent until somebody
has won.

## The API

Five routes, holding one game:

| route | what it does |
| --- | --- |
| `GET /api/state` | everything the page may know |
| `POST /api/new` | start again; `{"difficulty": "hard", "seed": 7}` both optional |
| `POST /api/place` | `{"ship","at","across"}`, or `{"auto":true}`, or `{"lift":name}` |
| `POST /api/ready` | the fleet is locked; the shooting starts |
| `POST /api/fire` | `{"at": 44}` — your shot, then the engine's reply |

`fire` answers with **both halves of the exchange** in one response. The engine
replies in microseconds, so a second round trip would exist only to be waited
for; the page animates the two in sequence regardless.

An illegal placement comes back as a `400` carrying **the rules' own sentence**,
which the page shows word for word. A second copy of those reasons in JavaScript
would be a second implementation of the rules, and the two would drift. Firing
out of turn, before the fleets are out, or after the game is over is a `409`;
firing twice at one square is a `400` naming the square.

## The page

Three phases, one visible at a time, chosen from `phase` in the state.

**Placing.** A tray of five ships beside your empty waters. Pick one and a ghost
of it follows the pointer; `R` rotates it; click to drop. An illegal drop is
drawn in red and says why. *Place for me* does the lot, clicking a ship already
on the water lifts it off again, and *Ready* puts the fleet to sea.

**Firing.** Two boards: yours with the damage on it, theirs with only what you
have found out. Untried squares are clickable and tried ones are not, so the
repeated-shot error is a thing the page cannot cause. A ring for a miss, a
burst for a hit; then, a beat later, the engine's shot lands on your board the
same way. A ship you sink is drawn where the server said it was.

**A ship is one object, not five squares.** Each vessel is drawn as a single
hull spanning the squares it occupies, with a rounded bow so it has a
direction, and the square underneath darkened so the hull reads as floating in
it rather than pasted on. The hull is placed **in the same CSS grid as the
squares** — `grid-column: 4 / span 5` — rather than positioned absolutely over
them. That is the whole trick: the board has a gap between squares, so
percentage arithmetic would drift a little further out of true with every
square it crossed, and across a five-square carrier the drift is visible.
Sharing the grid makes misalignment impossible at any board size. The cost is
that every square must carry an explicit grid position too, because CSS places
explicit items before it auto-flows the rest and a single auto-placed square
would be shoved past the hull sitting on it.

**Over**, and the engine's fleet is revealed — the one moment it may be, and it
arrives under a separate `revealed` key so the page cannot mistake it for the
view.

There is also a **heat map** toggle: every untried enemy square shaded by how
much the hard engine wants to fire at it. It is the clearest picture of how the
engine thinks, it is computed from the view the page already holds so it tells
you nothing you were not told, and it is off by default because it is a hint.

Reloading recovers the game, because the game never lived in the browser.
Bootstrap is vendored into `web/vendor/` rather than fetched: the page is served
under `default-src 'self'` and is meant to work on a machine with no network.
The boards themselves are `style.css` — no framework draws a ten-by-ten sea.

## One thread for the game, eight for the measurement

The server is single-threaded and unapologetically so. There is one game and one
player, and the most expensive thing that happens is the engine counting
placements for one shot. Threads would add a lock around the game and buy
nothing. The event loop is there so a second tab or a favicon request cannot
queue behind a turn.

The threads are in `measure.funny`, where the work actually is, and they share
nothing at all — so they need no lock either.

## In a container

```console
$ docker build -f extensive_examples/battleship/Dockerfile -t funnylang-battleship .
$ docker run --rm -p 8443:8443 funnylang-battleship
```

Then <https://localhost:8443>, and accept the certificate warning for the same
reason as ever. Or `docker compose -f extensive_examples/battleship/compose.yaml
up --build`, which adds a read-only root filesystem and drops every capability.

**The build stage installs a C compiler and nothing else.** `gcc` and
`libc6-dev` go in; about fifteen seconds later a working toolchain comes out,
the example's golden runs, and the game is frozen into a standalone executable.
No Python, no package manager for the language, no build system, no
third-party library. This repository already has a CI job that proves that in a
bare Debian container; a Dockerfile is the same proof in a form you can run.

**The runtime stage has no toolchain in it at all.** `funny yeet` appends
compiled bytecode to the runtime stub, so what ships is two binaries of about
450 KB, the page, and a test certificate — 1.2 MB of `/app` in total. There is
no interpreter in the image and nothing that could compile anything, which the
CI job checks by looking for one.

**The healthcheck is FunnyLang too.** The usual move is to `apt-get` curl into
the runtime image so Docker has something to ask the server with, which would be
a strange thing to do to an image whose whole point is that it needs nothing
installed. `healthcheck.funny` is yeeted beside the server: it completes the TLS
handshake **against the certificate the server is actually presenting**, sends a
real request, and insists on a real game in the reply. A process that is
listening but has lost its board is not healthy.

Two things worth knowing before you change the command:

- **`--host 0.0.0.0` is not optional.** The server binds loopback by default,
  and a container's loopback is not yours. It is in the image's `CMD`; if you
  replace that, keep it.
- **If you move the port, move `BATTLESHIP_PORT` with it**, or the healthcheck
  will be asking the wrong door whether anybody is home.

Run it with `--no-tls` behind a reverse proxy and the image does not need
OpenSSL at all — `ldd` on the yeeted binary lists libm, libc and the loader and
nothing else, because OpenSSL is `dlopen`'d the first time a TLS socket opens.

### What was actually checked

There is no Docker on the machine this was written on, so **the image has never
been built**. What was done instead was to run every command in the Dockerfile
on Linux, outside Docker: the toolchain compiles from C source alone in 15
seconds, the golden passes, both programs yeet, and a yeeted server started from
an unrelated directory finds `web/` and `certs/` beside its own executable and
serves the game over TLS verified against its own CA. The healthcheck was
exercised in all five states a container can be in — nothing listening, a real
TLS server, the wrong CA, plain HTTP checked plainly, and plain HTTP checked as
TLS — and returns the right answer each time. The `.dockerignore` was checked by
building from a pruned copy of the tree with everything it excludes deleted.

That is a good deal better than nothing and it is **not** the same as a built
image. `.github/workflows/docker.yml` is what closes the gap: it builds the
image, proves there is no compiler in it, waits for the container's own
healthcheck, and then places a fleet and fires a shot from outside the
container. Until that workflow has run green, treat the image as unproven.

## The certificate

`certs/site.p12` is a test certificate signed by a test CA that nothing else
trusts, so a browser will warn about the issuer — which is exactly what an
unknown CA is for. It is test material and not a secret. `certs/make_cert.sh`
makes a fresh pair. Run with `--no-tls` for plain HTTP, which is fine on
loopback and not on a network.

## The tests

```console
$ funny test extensive_examples/battleship
```

Five parts, each able to fail on its own: the board and its notation, placing a
fleet, firing and the view, the engine choosing from hand-made positions, and a
complete game of the hard engine against itself asserted shot for shot. It runs
in about a quarter of a second.

**The golden never depends on a seeded random stream.** `native/rizz.h` says in
as many words that no golden relies on the generator's exact output, and this
one keeps that true: `density` draws no random number so its every shot is
asserted exactly, both fleets in the whole-game part are placed by hand, and
`random` and `hunt` are asserted as *properties* — all one colour, nothing fired
at twice, the fleet sunk inside a hundred shots — which hold whatever the stream
produces.

There are no sockets in it. `serve.funny` is a thin wrapper around these
modules, and what a wrapper is worth testing for is that it wraps. That part was
driven by hand instead: every route, every refusal, a whole game to its end, the
no-leak property at each phase, and a `/../rules.funny` that comes back 404.

## What it is not

- **The page has not been opened in a browser here.** The environment this was
  built in has no browser in it. Two things were done instead. The server was
  driven end to end over a real loopback socket — every route, a game played to
  a finish, the JSON read and checked at each phase — and `web/` was written
  against exactly the state that produced. And `battleship.js` was then run
  against a stub DOM to check the part most likely to be quietly wrong, which
  is the hull geometry: that every square is placed explicitly, that a carrier
  at A1 spans five columns on row 2, that a cruiser at E6 spans three rows from
  column 7, that the enemy board draws a hull only for a ship actually sunk,
  and that redrawing does not pile hulls up. That check is **not in this
  repository**, deliberately: it needs Node, and a repository whose whole claim
  is that it needs nothing installed should not grow a JavaScript test runner
  to check a hundred lines of page code. So it stays outside, and what is
  written down here is that it was done. None of this is the same as somebody
  having played a game in a browser, and that remains untested.
- **One game per server, not per browser.** No sessions and no cookies: this is
  a program you run to play a game. A second tab shares the first one's board.
- **One human against the engine.** Battleship is a two-player game, and
  `chat/`'s `ws.funny` could carry two browsers against each other — but that
  needs sessions and a lobby, which every game example here has declined, and
  the engine is what this one is for. It is the obvious extension.
- **A per-ship probability model.** `density` never checks that all five
  remaining ships could be placed at once, only that each could be placed where
  it is counting. Where the board is nearly full that is measurably wrong.
- **No model of the opponent.** People place ships in habits — edges, corners,
  never touching — and the engine assumes uniform placement. It is therefore
  measured against uniformly random fleets, which is not the same as being
  measured against a person.
- **The hard engine has a fixed opening**, and no memory between games. It plays
  E5 first every time, and a player who learns its habits keeps the advantage
  for ever. That is the price of being reproducible enough to assert a whole
  game in a golden, and for a demonstration it is a fair trade.
- **No salvo, no ship-adjacency variant, no 10×10 international rules**, and no
  undo, save or clock.
