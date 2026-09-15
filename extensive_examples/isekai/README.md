# the hollow road — a simulation with a rule table instead of a script

You are summoned into another world. You choose what you were an hour ago,
what the summoning makes of you, and which element answers — and then you go
down five floors of it.

What makes this different from the other games in this repository is that
**nobody wrote down what happens.** There is a small model — your element, the
ground you are standing on, what the thing in front of you is made of, and the
state you are in — and a table of rules over it. Throw fire into a gas-filled
hall at a frost-blooded warden while standing in water and several rules fire
at once, in order, and the answer is whatever they add up to. No author typed
that outcome.

```console
$ funny run extensive_examples/isekai/serve.funny
the hollow road at https://localhost:8443 — ctrl-c to stop
(a test certificate, so your browser will warn about the issuer -- that is correct)
you were nobody an hour ago. choose what the summoning makes of you.
```

Open <https://localhost:8443>. The browser will warn about the certificate and
it is right to — see [the certificate](#the-certificate).

```console
$ funny run extensive_examples/isekai/serve.funny -- 8080 --no-tls
$ funny run extensive_examples/isekai/play.funny -- --list
$ funny run extensive_examples/isekai/play.funny -- --origin soldier --calling knight --affinity storm
$ funny run extensive_examples/isekai/play.funny -- \
      --origin nurse --calling cleric --affinity fire --script scripts/the-gas-hall.txt
$ funny test extensive_examples/isekai
```

## What is here

| file | what it is |
| --- | --- |
| `resolve.funny` | **the simulation**: the rule table, the cascade, the follow-ups |
| `hero.funny` | origins, callings, affinities, the sheet, the kit |
| `world.funny` | ground tags, material traits, the five floors |
| `run.funny` | a descent as a state machine: summon, turn, floor, reckoning |
| `serve.funny` | the HTTPS server and the six-route API |
| `web/` | three screens, no framework in the logic |
| `play.funny` | the same modules against a text screen, and `--script` |
| `scripts/` | a whole descent somebody else can replay exactly |
| `test_isekai.funny` | the golden: the table one rule at a time, and a whole run |
| `http.funny`, `static.funny` | copies of battleship's, which are copies of chess's |

`http.funny` and `static.funny` are copies rather than imports because a module
reached through a parent directory cannot be loaded by a worker —
`RUNTIME_PLAN.md` §9.

## Why this is a simulation and not a branching story

A branching story is a tree somebody wrote down. Option A leads to paragraph
seven because an author typed that it does. Nothing is being modelled, and the
second time through, the tree is the whole game.

This is the other thing. Twenty-one rules, each with a condition over four
things and nothing else:

- **the element in play** — fire, frost, storm, earth, light, void, or nothing
- **the ground tags** — what the room is made of
- **the material traits** — what the thing you are hitting is made of
- **the sheet** — what state you are in

**No rule may name a floor or a creature.** A troll is not a troll to the
resolver; it is `frost-blooded` and `grounded`, and everything that happens to
it follows from those two words. That restriction is the whole reason this is a
model rather than a pile of special cases, and it is what makes the interesting
outcomes ones nobody enumerated.

**The table is ordered and additive, not first-match.** First-match is how a
simulation quietly turns back into a lookup table: one condition wins, the rest
are dead, and the compositions never happen. Here every rule whose condition
holds fires. Fire on a floor that is both `gas` and `sacred` both ignites the
room *and* stains you for it.

## The rule table, in full

You should be able to predict what an action will do, so here is everything.

**What a thing is made of** — per target, because armour is a fact about one
creature and not about the room:

| | when | what |
| --- | --- | --- |
| 1 | `armoured`, and not storm or light | halved — plate is good against a sword and no use against a potential difference |
| 2 | `swarm` | halved from a single strike, **doubled** from anything covering the room |
| 3 | fire vs `frost-blooded` | doubled, and the cold does not come back |
| 4 | frost vs `wet` | doubled, and it is held where it stands |
| 5 | storm vs `armoured` | doubled — the rule that cancels rule 1 rather than arguing with it |
| 6 | earth vs `flying` | nothing at all |
| 7 | light vs `undead` | tripled |
| 8 | fire vs `ember-blooded` | nothing — it is fed by that |

**Where you are standing** — global, and applied before any damage is worked
out:

| | when | what |
| --- | --- | --- |
| 9 | fire on `gas` | **ignition**: the whole room, doubled, you included; `gas` gone, `burning` added; **and it asks you something** |
| 10 | fire on `damp` | halved, and the steam leaves the room `dark` |
| 11 | fire on `sacred` | works perfectly well, and **+2 corruption** |
| 12 | frost on `damp` | the floor freezes: `slick`, and `grounded` things stop getting a footing |
| 13 | storm on `damp` | chains to everything standing in it |
| 14 | storm on `metal` | chains, and a third of it comes back up your arm |
| 15 | storm in `narrow` | there is nowhere to be that is not next to it |
| 16 | earth in `narrow` | the ceiling comes down — **and it asks you something** |
| 17 | light on `dark` | the dark lifts, permanently — **and it asks you something** |
| 18 | `windy`, for fire or anything thrown | thinned by a quarter |

**And the sheet:**

| | when | what |
| --- | --- | --- |
| 19 | void, always | drains the thing into you, **+1 corruption**, no exceptions |
| 20 | striking something `held` or `stumbling` | **+1 corruption** |
| 21 | acting in tune with your affinity | **+1 resonance** |

Then a **cascade** runs at the top of every turn: `burning` ground damages
everything standing on it and burns off any remaining `gas`; `frost-blooded`
things in a burning room stop being frost-blooded; `slick` ground trips what
walks on it; and a `swarm` cut below half **comes apart into two of itself**.

### What that adds up to

Some worked examples straight out of the golden:

| you do | and it comes to |
| --- | --- |
| fire at an armoured frost-thing, in water | half for the water, half for the plate, double for the cold: **2 damage**, and the cold is gone |
| fire into a gas-filled chapel | **10 done, 6 taken, +2 corruption**, and it still asks you something |
| storm into a narrow flooded room | 5 to everything, **and 4 of it to you** |
| storm down the iron gallery | 25 across three of them, 1 back up your arm |

The first of those is the one worth staring at. Fire is exactly the right
answer to a frost-blooded warden, and it does almost nothing, because the
chapel is wet and the warden is plated. Nobody wrote that down. It is four
rules meeting.

## The second choice

Some results do not end your turn. They open a question that exists **only
because of what you just did**, and nothing else moves until you answer it:

- **Ignition.** The hall is alight and so are you. Cover the room, or press the
  attack while everything burns?
- **Collapse.** The ceiling is coming down. Bring it down on all of it and lose
  the way back, or hold it up and pay for that yourself?
- **Revealed.** The dark lifts and there is something behind the stonework that
  was not meant to be found. Take it, or leave it where it is?

This is the mechanic the example exists for, and it was worth building rather
than gesturing at: it is the difference between a menu and a consequence. A
follow-up is part of the turn that caused it, so the enemies do not act and the
room does not cascade until it is settled.

Taking the thing behind the stonework fills you back up. Taking it **on a
sacred floor** also costs you a corruption — which is rule 11 and rule 20's
logic applied one more time, not a special case for treasure.

## The sheet

Three choices, doing three different jobs.

**Origin** — what you were an hour ago. Gives stats, and one trait that is a
*rule* rather than a number: the librarian can see what things are made of, the
soldier's first action on each floor is free, the nurse's healing also clears a
condition, and the programmer can take back one resolution a floor.

**Calling** — what the summoning made of you. Gates the kit and **nothing
else**. A calling that was also a stat block would make the origin's stats
decorative. A mage gets all three tiers of their element; everybody else gets
the first, so an affinity is never wasted.

**Affinity** — which element answered. Every one is soft to its opposite:
`fire`↔`frost`, `storm`↔`earth`, `light`↔`void`. There is no affinity without a
cost, and none of them is correct — the five floors are built so that each one
makes a different element look brilliant or useless.

And two meters that make this an isekai rather than a dungeon crawl.
**Resonance** rises only when you act in tune with your affinity, and at 10 the
third tier of your element finally answers. **Corruption** rises from void,
from burning sacred ground, and from hitting things that could not hit back.

**They are not two ends of one bar**, and the page draws them apart so nobody
reads them as a slider. A run can finish carrying a great deal of both, which
is the most interesting way for one to end.

## The preview is the outcome

Hovering an action shows what it would do. That forecast is not an estimate:
the server runs **the real resolver** and throws the answer away.

```funny
flex bet resolve(state, choice)   // deep-clones, works on the copy, returns it
```

`resolve` is pure. It never touches the state it is handed, so the caller
either keeps the answer or does not — and the preview and the act are the same
call made twice, not two code paths that agree. There is no second, cheaper
predictor in this example to drift out of step with the first, which is the
usual way a game's tooltip starts quietly lying.

That is asserted rather than promised, in two places. The golden resolves 252
combinations of element, ground and material twice each and compares them byte
for byte. And the same property is checked from the far side of a real socket:
preview five different actions, then confirm the run is byte-identical
afterwards, then act and confirm the effects match the forecast exactly.

## No dice, anywhere

There is no `gimme rizz` in any file in this example. Where a game would roll,
this consults a stat. A run is a pure function of the build and the choices.

That buys three things. The golden can assert a whole descent line for line.
`scripts/the-gas-hall.txt` is a complete run somebody else can replay and get
the same result. And — the real reason — a consequence you can blame on a die
roll is not a consequence. If the gas hall goes up in your face, it is because
you threw fire into a room full of gas.

What it costs is that the enemies are predictable. They hit for what they have,
in order, every turn. A real game would want some variance there; this one
wants to be able to prove what it claims.

## The API

Six routes, holding one run:

| route | what it does |
| --- | --- |
| `GET /api/state` | everything the page needs, whichever screen it is on |
| `POST /api/new` | `{"origin","calling","affinity"}` — the summoning |
| `POST /api/act` | `{"art","target"}` — your turn |
| `POST /api/answer` | `{"option"}` — the second choice, when one is open |
| `POST /api/preview` | the same as `act`, run and discarded |
| `POST /api/undo` | the programmer's trait, once a floor |

Acting while a question is open is a `409` — you owe the world an answer first.
So is answering one nobody asked.

## The page

Three screens, one visible at a time, chosen from `phase`.

**The summoning** shows all three lists at once with the sheet redrawn as you
choose, and every option says what it **closes** as loudly as what it opens —
which is the half these screens usually leave out. An element's card leads with
what it is soft to.

**The descent** is the room on the left with its ground tags and whatever is
standing on it, you on the right, and the kit along the bottom. Hovering
anything fills the forecast. When something is asked, the kit locks out and the
question takes the floor.

**The reckoning** is what the run added up to, and the whole log.

Reloading recovers everything, because the run lives in the server and never
in the browser. Bootstrap is vendored; the page is served under
`default-src 'self'` and works with no network.

## One thread

One run, one player, and the most expensive thing that happens is walking a
table of twenty-one rules. Threads would add a lock around the run and buy
nothing. The event loop is there so a second tab cannot queue behind a turn.

## The certificate

`certs/site.p12` is a test certificate signed by a test CA that nothing else
trusts, so a browser will warn about the issuer — which is what an unknown CA
is for. It is test material, not a secret. `certs/make_cert.sh` makes a fresh
pair, and `--no-tls` serves plain HTTP, which is fine on loopback and not on a
network.

## The tests

```console
$ funny test extensive_examples/isekai
```

Five parts, about a quarter of a second, no sockets. The sheet; **the rule
table one rule at a time**, each in a room where it must fire and again in one
differing only in its condition; the compositions; preview-equals-outcome; and
a whole descent asserted turn by turn.

The second of those is most of the golden and the part that matters. A rule
table is only trustworthy if each row has been shown to fire *and* shown not to.

## What it is not

- **The page has not been opened in a browser here.** This environment has no
  browser. Two things were done instead: the API was driven end to end over a
  real loopback socket — every route, both refusal codes, a whole descent, the
  preview-purity property, a path traversal refused — and the client was run
  against a stub DOM to check all three screens render, the unaffordable art is
  disabled, the kit locks out while a question is open, and the log draws. That
  is 31 checks on the page and 30 on the server, and it is **not** the same as
  somebody having played it. Neither harness is committed: both need Node, and
  a repository whose whole claim is that it needs nothing installed should not
  grow a JavaScript test runner.
- **The numbers were tuned by playing, not derived.** Health, damage and costs
  were moved until a competent run of each calling could get through five
  floors and a careless one could not. There is no balance theory here.
- **The enemies do not think.** They hit for what they have, in order. All the
  decisions in this simulation are yours, which is the point, but it does mean
  the fifth floor is a damage race rather than a duel.
- **Five fixed floors, and no generation.** A generated dungeon needs a seed,
  and a seed is the one thing this example is built to do without.
- **Twenty-one rules is not a world.** There is no weather, no time, no
  inventory, no NPC who remembers you. Corruption changes what the reckoning
  says and nothing else — a longer version would have it change what the world
  does.
- **One run per server, not per browser.** No sessions and no cookies.
- **No save, no undo beyond the programmer's one-a-floor**, and no way back up.
