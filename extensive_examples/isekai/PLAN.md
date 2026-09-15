# FunnyLang — Isekai Plan (`extensive_examples/isekai/`)

> **Status:** built. I1 through I6 are done and `funny test extensive_examples` is 12/12 on this
> machine. Branch `feature/isekai`, cut from `master` at the battleship merge (`27b18bd`). §8 has
> the state of each milestone and §11 every deviation — including, again, that this environment has
> no browser: the API was driven over a real socket and the page run against a stub DOM, which is
> not the same as somebody having played it.
> **Prerequisite:** nothing new in the runtime. `extensive_examples/battleship/` is in the tree and
> this example takes its skeleton — `http.funny`, `static.funny`, `certs/`, the shape of
> `serve.funny` and of the golden — as read.
> **Deliverable:** a **simulation** you play in a browser. You are summoned into another world; you
> choose who you were, what the summoning made of you, and which element answers you; and then you
> descend. Every action resolves through a table of interactions between your element, the ground
> you are standing on and what is standing on it — so the same spell is a rout in one room and a
> catastrophe in the next. Some results do not end your turn: they **open a second choice that only
> exists because of the first**. `funny test extensive_examples` runs a golden that plays a whole
> run and asserts every line of it.

---

## 0. Rules for the executing agent

1. **An example is FunnyLang.** If it needs something the runtime does not have, that is a
   `RUNTIME_PLAN.md` §9 entry and a small, general primitive — never a special case for one
   example. Nothing here is expected to need one.
2. **There are no dice in this simulation, anywhere.** A run is a pure function of the build, the
   choices made, and the fixed descent. `rizz` is not imported by any module in this example. That
   is what lets the golden assert a whole run line by line, and it is also the point of the thing:
   this is a simulation of *consequences*, and a consequence you can blame on a die roll is not
   one. Where a real game would roll, this consults a stat.
3. **The preview and the outcome are the same code.** A hovered action shows what it would do by
   running the resolver with `commit: cap`. There is no second, approximate predictor — that is
   the classic way for a game's tooltip to start lying. §5.4, and the golden asserts it.
4. **Every example has a README** that says what it is, how to run it, what it measured, and — in a
   section titled exactly that — what it is not.
5. **`the_script()` for every path.** An example runs from any working directory.
6. **A worker cannot import through a parent directory** (`RUNTIME_PLAN.md` §9), so `http.funny`
   and `static.funny` are **copies**, as battleship's are of chess's. Say so in the README.
7. **Stage by explicit path; commit per milestone on `feature/isekai`; push; never merge to
   master.** Other sessions work on this repository at the same time.
8. **Every deliberate deviation gets a §11 entry**, including the ones that turn out to be wrong.

---

## 1. What "done" looks like

```console
$ funny run extensive_examples/isekai/serve.funny
the hollow road at https://localhost:8443 — ctrl-c to stop
(a test certificate, so your browser will warn about the issuer -- that is correct)
you were nobody an hour ago. choose what the summoning makes of you.
```

Three screens, in order.

1. **The summoning.** Pick an **origin** (what you were before), a **class** (what the summoning
   made you) and an **affinity** (which element answers). The sheet updates as you choose, and it
   shows what each choice *closes off* as loudly as what it opens — including the vulnerability
   every affinity carries.
2. **The descent.** Five floors. Each shows the room's **ground**, what is standing on it and what
   those things are made of, and your kit. Hovering an action shows exactly what it would do.
   Committing resolves it, and the consequences arrive one at a time.
3. **The reckoning.** The ledger: every choice, what it cost, how far the world came to accepting
   you, and how far you went the other way.

```console
$ funny run extensive_examples/isekai/play.funny -- --origin librarian --class mage --affinity fire
$ funny run extensive_examples/isekai/play.funny -- --script scripts/the-gas-hall.txt
$ funny test extensive_examples/isekai
```

---

## 2. The idea, and why it is a simulation rather than a story

A branching story is a tree somebody wrote down: option A leads to paragraph 7 because an author
typed that. Nothing is being *modelled*, and the second time through, the tree is the whole game.

This is the other thing. There is a small **world model** — your element, the ground you are
standing on, and what the thing in front of you is made of — and a **table of interactions**
between them. Nobody wrote down what happens when you cast Ember in a gas-filled hall against a
frost-blooded troll while standing in water. The rules each fire on their own condition, in order,
and what happens is whatever they add up to.

That is the whole design goal: **the interesting outcomes should be ones nobody enumerated.**

It also gives the mechanic its shape. An action is not "input, then output". It is:

```
   your element  +  the ground  +  what it is made of  +  what state you are in
        |
        +--> a list of effects, each of which may change the ground
        |
        +--> and, sometimes, a SECOND CHOICE that exists only because of the first
```

Setting a gas hall alight is not a damage number. It removes the `gas` tag, sets the ground
`burning`, hurts everything in the room including you, and then asks you — because it is now a
real question — whether you shield the party or press the attack while everything is on fire.

---

## 3. The model

### 3.1 The sheet (`hero.funny`)

Four stats, no derived-stat soup: **MIGHT** (what you swing), **FOCUS** (what you channel),
**GRIT** (what you survive), **WIT** (what you notice). Plus `hp`, `mp`, and the two that make this
an isekai rather than a dungeon crawl:

- **RESONANCE** — how far the world has accepted you. Rises when you act in tune with your
  affinity. At 10 it unlocks your affinity's final art.
- **CORRUPTION** — how far you have pushed against it. Rises when you use `void`, when you burn a
  `sacred` floor, and when you strike something already helpless. At 5 the world starts answering
  differently; at 10 the reckoning says so.

**Origins** — what you were an hour ago. Each gives stats and one trait that is genuinely a rule,
not a number:

| origin | stats | trait |
| --- | --- | --- |
| `librarian` | +2 WIT, +1 FOCUS | **Read the Room** — enemy composition is visible before you act |
| `soldier` | +2 MIGHT, +1 GRIT | **Drilled** — the first action of every floor costs no MP |
| `nurse` | +2 FOCUS, +1 WIT | **Triage** — any healing also clears one condition |
| `chef` | +2 GRIT, +1 MIGHT | **Mise en Place** — consumables do double, and never take a turn |
| `programmer` | +2 WIT, +1 FOCUS | **Rubber Duck** — once per floor, re-read a resolution you did not like |

**Classes** — what the summoning made of you. A class gates the skill pool and nothing else; it is
not a second stat block.

| class | kit |
| --- | --- |
| `knight` | Guard, Shield Bash, Riposte, Hold the Line |
| `mage` | the three tiers of your affinity, plus Dispel and Read Aether |
| `ranger` | Loose, Snare, Scout, Volley |
| `cleric` | Mend, Ward, Smite, Consecrate |

**Affinities** — which element answered. Each boosts its own and carries the matching weakness.
Opposed in pairs: `fire`↔`frost`, `storm`↔`earth`, `light`↔`void`.

### 3.2 The ground and what stands on it (`world.funny`)

**Ground tags**, which the room starts with and actions change: `damp`, `gas`, `narrow`, `sacred`,
`dark`, `metal`, `windy` — plus `burning` and `slick`, which only ever arrive by being caused.

**Material traits**, which is what a thing is made of rather than what it is called:
`frost-blooded`, `ember-blooded`, `armoured`, `undead`, `swarm`, `flying`, `grounded`, `wet`.

A troll is not special-cased. It is `frost-blooded` and `grounded`, and everything that follows,
follows from that.

**The descent** is five fixed floors, chosen so that each one makes a different rule matter:

| floor | the room | what is on it | the question it asks |
| --- | --- | --- | --- |
| 1 | the wet stair — `damp`, `dark` | two `wet`, `grounded` ghouls | does your element like water? |
| 2 | the gas hall — `gas`, `narrow` | a `swarm` | the obvious thing is a catastrophe |
| 3 | the frozen chapel — `sacred`, `damp` | a `frost-blooded` warden | your best spell, in the worst room |
| 4 | the iron gallery — `metal`, `windy` | two `armoured`, one `flying` | armour and lightning disagree |
| 5 | the hollow throne — `dark`, `sacred` | an `undead` king, `armoured` | everything you chose, at once |

### 3.3 The resolver (`resolve.funny`) — the heart

One function, and it is the whole simulation:

```funny
flex bet resolve(state, action, opts)
    // -> {"effects": [...], "state": ..., "asks": ghost or a follow-up choice}
```

It walks an **ordered table of rules**. Each rule has a condition over (element, ground, traits,
sheet) and, when it fires, appends effects and may set `asks`. Rules do not short-circuit: several
fire and the result is their sum. That is where outcomes nobody wrote down come from.

The table, in the order it is consulted:

| # | when | what it does |
| --- | --- | --- |
| 1 | `fire` on `gas` | **ignition** — heavy damage to everything in the room, you included; `gas` removed, `burning` added; **asks** "shield / press" |
| 2 | `fire` vs `frost-blooded` | double damage, and the trait is gone afterwards |
| 3 | `fire` on `damp` | halved, and `damp` becomes steam: the room gains `dark` |
| 4 | `fire` on `sacred` | works, and **+2 corruption** |
| 5 | `frost` on `damp` | the floor freezes: `slick` added, `grounded` things lose their next action |
| 6 | `frost` vs `wet` | double damage and the target is held |
| 7 | `storm` on `damp` or vs `wet` | chains to **every** unit in the room — and on `narrow`, that includes you |
| 8 | `storm` vs `armoured` | double damage, armour ignored |
| 9 | `storm` on `metal` | chains again, and you take a third of it back |
| 10 | `earth` on `narrow` | collapse — blocks the retreat for the rest of the floor; **asks** "bring it down / hold it up" |
| 11 | `earth` vs `flying` | misses entirely |
| 12 | `light` vs `undead` | triple damage |
| 13 | `light` on `dark` | `dark` removed, and whatever was hiding is revealed; **asks** if there was something |
| 14 | `void` anything | drains MP from the target to you; **+1 corruption**, always |
| 15 | any strike on a `held` or `helpless` target | **+1 corruption** |
| 16 | acting in tune with your affinity | **+1 resonance** |

Then a **cascade** pass, which is the second half of the propagation: `burning` ground damages
everything standing on it at the top of the next turn and burns off `gas`; `slick` makes
`grounded` things stumble; a `swarm` reduced below half splits into two; `frost-blooded` things in
a `burning` room lose the trait.

### 3.4 The follow-up — the mechanic this example exists for

When a rule sets `asks`, the turn **does not end**. The server returns a pending question with two
or three options that exist only because of what just happened, and answering it resolves through
the same table. The golden asserts a run that triggers three of them.

That is the literal reading of "based on the selected input, another targeted input happens", and
it is worth building rather than gesturing at: it is the difference between a menu and a
consequence.

---

## 4. Each file

| file | what it is | est. lines |
| --- | --- | --- |
| `serve.funny` | the HTTPS server and the JSON API | 480 |
| `web/index.html`, `web/style.css`, `web/js/isekai.js` | the three screens | 180 + 320 + 560 |
| `hero.funny` | origins, classes, affinities, the sheet, the kit | 360 |
| `world.funny` | ground tags, material traits, the five floors | 300 |
| `resolve.funny` | the rule table, the cascade, the follow-ups | 520 |
| `run.funny` | the run as a state machine: summon, descend, reckon | 280 |
| `play.funny` | the same run in a terminal, and `--script` | 260 |
| `http.funny`, `static.funny` | copies of battleship's, per §0 rule 6 | 340 (copied) |
| `test_isekai.funny`, `.expected` | the golden, §5 | 480 + 250 |
| `web/vendor/bootstrap.min.css`, `certs/` | copied | copied |
| `README.md` | §9 | 420 |

---

## 5. The golden

`test_isekai.funny`, five parts, no sockets.

1. **The sheet.** Every origin, class and affinity builds; stats add up; the kit is what the class
   says and no more; an affinity's weakness is the opposed element; an unknown name is refused.
2. **The rule table, one rule at a time.** For each of the sixteen: a hand-built state where it
   must fire, and one differing in exactly the condition where it must not. This is the part that
   makes the table trustworthy, and it is most of the golden.
3. **Rules compose.** The cases nobody wrote down: `fire` on `gas` *and* `sacred` (ignition and
   corruption together); `storm` on `damp` *and* `narrow` (you are in the chain); `fire` vs a
   `frost-blooded` thing on a `damp` floor (halved, then doubled, and the room goes dark).
4. **The preview is the outcome.** For a spread of states and actions, `resolve(..., commit: cap)`
   and `resolve(..., commit: fax)` produce identical effect lists. Asserted, not promised.
5. **A whole run**, one fixed build and one fixed list of choices, printed turn by turn with every
   effect and every follow-up, then the reckoning. Deterministic because nothing rolls.

---

## 6. What could go wrong, and the answer

- **A rule table becomes a special-case pile.** The guard is §2: a rule may only read the element,
  the ground, the traits and the sheet. No rule may name a floor or a monster.
- **The page reimplements the rules to draw a preview.** It does not; it asks the server, which
  runs the real resolver with `commit: cap`. §0 rule 3.
- **It is a lot of content.** Five floors and sixteen rules is the smallest set that makes every
  affinity meaningfully different, and it is fixed content rather than generated, so it is
  finishable.

---

## 7. Decisions already made

- **No randomness at all.** §0 rule 2.
- **Five floors, fixed.** A generated dungeon would need a seed, and a seed is the one thing §0
  rule 2 is trying to avoid; it would also make the golden assert a dungeon rather than a rule set.
- **The rule table is ordered and additive, not first-match.** First-match is how a simulation
  quietly becomes a lookup table.
- **The class gates the kit and nothing else.** A class that was also a stat block would make the
  origin's stats decorative.
- **One run per server, no sessions.** As with every game example here.
- **The version bump is not part of this plan**; the changelog entry is `[Unreleased]`.

---

## 8. Milestones

Commit per milestone on `feature/isekai`, staged by explicit path. Each leaves
`funny test extensive_examples` green.

### I1 — the sheet and the world
- [x] `hero.funny`, `world.funny`; skeleton, `http.funny`, `static.funny`, `certs/`, vendor copied
- [x] golden part 1 — 120 builds, every one legal

### I2 — the resolver
- [x] `resolve.funny`: **twenty-one** rules (§11), the cascade, `asks`, and purity in place of a
      `commit` flag (§11)
- [x] golden parts 2, 3 and 4 — the table one rule at a time, the compositions, and
      preview-equals-outcome over 252 combinations

### I3 — the run
- [x] `run.funny`: summon, descend, the follow-up state machine, reckon, and the programmer's
      take-back, which purity made nearly free
- [x] golden part 5 — a whole descent, five floors, 24 turns, three follow-ups

### I4 — the server and the three screens
- [x] `serve.funny`; `web/` — summoning, descent with hover forecasts and follow-up prompts,
      reckoning
- [x] the API driven end to end over loopback: 30 checks, including that previewing five actions
      leaves the run byte-identical and that acting then matches the forecast exactly
- [x] the page run against a stub DOM: 31 checks across all three screens

### I5 — the terminal twin
- [x] `play.funny`, `--list`, `--script`, and `scripts/the-gas-hall.txt` — a whole descent
      somebody else can replay and get the same result

### I6 — the write-up
- [x] `README.md` per §9; `CHANGELOG.md`; the top-level `README.md`'s example list

---

## 9. The README, in outline

What it is and how to run it. What is here. **Why this is a simulation and not a branching story**
(§2) — the most important section. The sheet: origins, classes, affinities, and the two isekai
meters. The ground and the materials. **The rule table in full**, because a reader should be able
to predict what an action will do. **The follow-up mechanic** and why it was worth building. **The
preview is the outcome**, as a property and a line in the golden. **No dice anywhere**, and what
that costs. The API. The page. What it is not.

---

## 10. Cost sheet

| piece | lines (est.) |
| --- | --- |
| model (`hero`, `world`, `resolve`, `run`) | 1,460 |
| `serve.funny` | 480 |
| `web/` | 1,060 |
| `play.funny` | 260 |
| golden + expected | 730 |
| `README.md` | 420 |
| copied | — |
| **new, written** | **~4,400** |

---

## 11. Deviations log

**`resolve` is pure instead of taking `commit: cap`.** §3.3 and §0 rule 3 specify a flag that
says whether to keep the result. What is built has no flag: `resolve` deep-clones the state it is
handed, works on the copy and returns it, and the caller either adopts the answer or drops it. That
is a strictly stronger guarantee than the plan asked for — the preview and the act are not two code
paths that agree, they are one call made twice — and it made the programmer's take-back nearly free,
because the previous run is simply the value we were holding before.

**The table is twenty-one rules, not sixteen.** The plan's list covered element against ground and
element against material, and left out the baseline: that armour halves what is not storm or light,
and that a swarm is the opposite shape of problem from a single creature. Those had to be rules
too, per §0's restriction that a rule may only read the four things — otherwise they would have been
special cases hidden inside the damage calculation, which is exactly what §6 warns about. Rule 5
(storm doubles against armour) then cancels rule 1 by being a separate rule rather than a flag,
which reads better than it sounds.

**"Calling" rather than "class".** The plan says class throughout. `class` is not a FunnyLang
keyword — `squad` is — but naming a variable after a word that is a keyword in most languages
invites exactly the kind of parse error this session already hit twice, and "calling" is better
isekai anyway.

**Mana comes back, two a turn.** Not in the plan. Without it a descent did not end in defeat, it
ended *empty*: the hero stood in a room unable to afford anything, which is the least interesting
way for a run to stop. With it, mana is a budget across a floor rather than across the whole
descent, and the choice between a cheap answer and the right one stays live.

**The numbers were tuned by playing, and the plan had none.** Health went from `24 + grit * 2` to
`30 + grit * 3`, Mend from 9 to 11, and the fourth floor from three enemies at 4-5 power to three at
3, because a single-target calling could not survive it otherwise. The README says plainly that
these were moved until it played, not derived.

**The golden's script is kept per floor, not as one flat list.** §5 asks for "one fixed list of
choices". A floor does not take a fixed number of turns — setting the gas hall alight clears it in
two where grinding it takes five — so a flat list runs the third floor's plan on the second the
moment anything changes. Keyed by depth it says what it means, and a small deterministic fallback
(mend below half, otherwise hit it) covers a floor that outlasts its plan.

**The golden triggers three follow-ups but only two of the three kinds.** §3.4 asks for three. The
scripted run is a cleric of fire, which opens `revealed` twice and `ignition` once; `collapse` needs
an earth affinity in a narrow room, and one build has one affinity. `collapse` is covered in part
two of the golden, where every rule is fired on its own, so the rule is tested — it is the *run*
that does not reach it.

**Two language traps worth writing down.** A ternary cannot be broken across lines: a statement ends
at the newline, so `x = a\n ? b : c` is a parse error even though the same thing inside a call's
brackets is fine. And PowerShell's `Set-Content -Encoding utf8` writes a byte order mark, which the
lexer refuses with "i don't know what '﻿' is supposed to mean here" — anything copied through
PowerShell on Windows needs the BOM stripped afterwards.

**Neither test harness is committed, for the same reason as battleship's.** The API driver is
FunnyLang and could have been, but it needs a running server and the golden deliberately has no
sockets in it; the page check needs Node, and a repository whose whole claim is that it needs
nothing installed should not grow a JavaScript test runner. Both were run, both pass, and the README
says so along with what they do not cover.

**And the page has still never been opened in a browser**, for the same reason as last time. 30
checks over a real socket and 31 against a stub DOM is a great deal better than nothing and is not
the same thing, and the README's "what it is not" leads with that.
