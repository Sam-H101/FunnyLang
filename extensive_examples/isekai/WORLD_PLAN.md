# FunnyLang — World Plan (`extensive_examples/isekai/`)

> **Status:** planned, nothing built. Branch `feature/isekai-world`, to be cut from `master` once
> `feature/isekai` has landed. §12 has the state of each milestone, §15 every deviation.
> **Prerequisite:** `extensive_examples/isekai/PLAN.md`, complete — the four model files, the
> twenty-one rules, the server, the page, the terminal twin and the golden.
> **Deliverable:** the four things the example's own README admits it is not. Something to carry,
> weather, somebody who remembers you, and a corruption meter that changes what the world *does*
> rather than only what the reckoning *says*. Plus one repair: one of the five origins has shipped
> with a trait that refers to a thing that does not exist (§4.1).

---

## 0. Rules for the executing agent

1. **NO NEW INPUTS.** This is the whole plan, and everything else in it is a consequence. A rule may
   read the element in play, the ground tags, the material traits, and the sheet — and after this
   work it may still read exactly those four things. Weather arrives as **ground tags**. What you
   carry lives on **the sheet** and resolves as **an art**. Somebody who remembers you is a
   **creature with a trait**. A corruption threshold is a **fact about the sheet**. If an addition
   needs a fifth input, it is the wrong addition and goes back in the drawer.
2. **No rule may name a floor, a creature, or an item.** Unchanged from `PLAN.md` §0. A survivor is
   not special-cased; it is `bystander`, and rules that already exist have opinions about it.
3. **Still no dice.** `rizz` is imported by no file here, and this work must not change that. The
   weather is deterministic because it is *caused* (§4.2), which is also what makes it worth having.
4. **`resolve` stays pure, and the preview stays the outcome.** Every addition is reachable through
   the same `resolve(state, choice)` call, so the page's forecast keeps working with no new code. A
   flask of oil must be previewable, and will be, because it is an art.
5. **Everything that happens on arrival happens in one place, in one order** (§3.2). Three of the
   four additions change what a floor looks like when you get there; if they each did it their own
   way, the order would be an accident and the golden could not pin it down.
6. **The rule table stays ordered and additive**: six new rules, not six new branches inside old
   ones. Twenty-one becomes twenty-seven, and the README keeps printing all of them.
7. **Each milestone leaves `funny test extensive_examples` green** and is worth shipping without the
   ones after it. W1 is a bug fix and could ship alone.
8. **Stage by explicit path; commit per milestone; push; never merge to master.**
9. **Every deviation gets a §15 entry**, including the ones that turn out to be wrong.

---

## 1. What "done" looks like

A chef, who arrives carrying things, in the gas hall, where there is somebody standing:

```console
$ funny run extensive_examples/isekai/play.funny -- \
      --origin chef --calling mage --affinity fire --script scripts/the-oil-and-the-spark.txt
```

```
  floor 2 of 5, turn 6
  THE GAS HALL   (gas, narrow, dry)
  the mountain has been running hot since you set the stair alight.
   - a boil of rats       20/20 hp   swarm, grounded
   - a man with a lamp     2/6 hp    bystander, grounded
       "don't. whatever it is you're about to do, don't."

  chef         -> mage     of fire   (soft to frost)
  hp 41/45   mp 14/16   resonance 3   corruption 0
  carrying: a flask of oil, a waterskin, a stone that hums     sky: heat 4  wet 1  foul 1

> 7 1
      you   ground    the flask goes over everything: the room is gas, and more of it
      you   note      mise en place: that cost you the flask and nothing else
> 3 1
      you   note      the gas goes up all at once, and the whole hall with it
      you   damage    a boil of rats  (20)
      you   kill      a boil of rats: down
      you   damage    a man with a lamp  (20)
      you   kill      a man with a lamp: down
      you   meter     and it minds you in the other way too  (4)
```

Nothing in the rule table mentions the man with the lamp. Rule 9 takes the whole room and he was in
it. **That is the entire argument for this plan**: the model already had an opinion about him, and
all that was missing was somebody to have it about.

Two floors later:

```
   - a woman on the stair   2/8 hp   bystander, grounded
       she will not look at you. word gets down here faster than you do.
```

And the reckoning:

```
  you came out the other side of it.
  floors: 5 of 5   turns: 29
  the world learned your name: resonance 11.
  and it will be a long time putting right what you did: corruption 14.
  three people saw you coming. one of them is still alive.
```

---

## 2. Why this shape

The example's README ends with a list of what it is not, and this plan is that list. The risk is
worth naming first: **four new subsystems is how a clean model turns into a pile.** Twenty-one rules
over four inputs is legible. Twenty-one rules plus a weather engine plus an inventory system plus an
NPC system plus a reputation system is a codebase.

So none of these is a subsystem. Each is an **existing input, extended**:

| what the README admits | where it actually goes | what the table has to learn |
| --- | --- | --- |
| no inventory | **the sheet**, and an item is an art | nothing — items resolve as arts do |
| no weather | **ground tags**, applied on arrival | nothing — the tags already exist |
| nobody who remembers | **a creature with a trait** | one rule, for killing one |
| corruption does nothing | **a fact about the sheet** | five rules |

Six new rules, and three of the four additions need none. That is the test of whether an addition
belongs: **if it makes the table bigger than it makes the game, it is the wrong addition.**

The better argument is that each makes the *existing* rules do more work rather than sitting beside
them:

- A flask of oil lays down `gas`. Rule 9 stops being something that happens to you and becomes
  something you **set up** — the first player-authored combination in the example.
- Weather means the fifth floor's ground is a consequence of the first four. Rule 9 becomes a
  decision about the rest of the descent, not just this room.
- A bystander makes every area effect a moral question, using rules already written.
- Corruption thresholds give rules 11, 19 and 20 somewhere to lead. Today they debit a number that
  only `reckoning()` ever reads.

---

## 3. The model afterwards

### 3.1 The four inputs, unchanged

```
   element  +  ground tags  +  material traits  +  the sheet
      |            |                |                 |
      |    now includes:      now includes:     now includes:
      |    dry, choking,      bystander         carrying[],
      |    hollow                               (and the two meters,
      |                                          which rules may now read)
      v
   the table, 27 rules, ordered, additive
      v
   effects, ground changes, and sometimes a second choice
```

### 3.2 The arrival pipeline — the one new seam

Today `arrive(hero, n)` in `run.funny` is five lines: fetch the floor, set `seen` for a librarian.
After this work it is the single place three additions plug in, **in this order and no other**:

```
   1. fresh_floor(n)                 the fixed room, as written
   2. sky.apply(floor, sky)          dry / damp / gas / choking; sacred stripped at foul >= 8
   3. corruption: hollow             if the sheet says >= 10, the room arrives hollow
   4. folk.place(floor, ledger)      the bystander, and whether they will speak
   5. read_the_room                  the librarian sees what things are made of
   6. one log line per change        "the mountain has been running hot"
```

Order is load-bearing. Weather goes before corruption because a fouled, hollow room should be
fouled *and* hollow, not one or the other. Corruption goes before folk because `hollow` is one of
the reasons somebody will not speak to you. And every one of these is a function of the run's
state, so an arrival is as deterministic and as previewable as a turn.

### 3.3 A turn, afterwards

```
   you act          resolve.funny, as now -- and an item is just an art
     |
     +-- a chef using a consumable: the turn DOES NOT END here; act again
     +-- a second choice opened: THE TURN STOPS HERE and waits
     |
   they act         foes_act -- skips anything with `bystander`
   the room acts    cascade  -- burning, slick, choking, swarms dividing; hurts bystanders too
   the sky moves    sky.push -- what this turn did to the mountain
```

---

## 4. The four

### 4.1 Something to carry — and the chef's trait, which is currently a lie

**This is a bug fix before it is a feature.** `hero.funny` ships five origins and four of their traits
are wired up. The chef's reads:

> **Mise en Place** — what you carry works twice as well and never costs a turn

There is nothing in the game to carry. `mise_en_place` appears in `hero.funny` and in no other file.
One origin has been a decorative sentence since the day it shipped, and anybody who picked it got
worse stats than a soldier and nothing in exchange. That alone justifies W1; the rest is what makes
it worth doing properly.

**An item is an art.** Same record — `id`, `name`, `cost`, `power`, `element`, `tags`, `says` — with
one more field, `"consumable": fax`. `kit(hero)` appends what is carried, `resolve` needs no new
path, `wants_a_target` already knows what a `ground_add` art wants, and the page's forecast previews
a flask of oil for free because it previews arts. That is §0 rule 1 paying for itself on the first
day.

The seven, and the four you can hold at once:

| item | tags | what it does | why it is here |
| --- | --- | --- | --- |
| a flask of oil | `ground_add` | the floor gains `gas` | **rule 9 on demand** |
| a waterskin | `ground_add` | the floor gains `damp` | rules 4, 6, 12, 13 on demand |
| tinder | `ground_add` | the floor gains `burning` | the cascade, without spending fire |
| a censer | `ground_add` | the floor gains `sacred` | rule 11 as a trap, or rule 22 as a refuge |
| grit-salt | `ground_clear` | `slick` and `choking` lift | the only answer to a fouled mountain |
| smelling salts | `heal`, power 0 | every condition on you, cleared | what a nurse gets for free |
| a stone that hums | — | **+2 resonance** | the third tier, sooner |

**The flask is the point.** Pour, then light: two existing rules combined by a player into a thing
neither describes. Up to now the ground has been something the floor decided. Now it is a move.

**Where items come from**, fixed and deterministic, one per source:

| source | item |
| --- | --- |
| the `revealed` follow-up on floor 1, *take it* | a stone that hums |
| the man with the lamp, alive when floor 2 clears | a flask of oil |
| the girl behind the altar, alive when floor 3 clears | a censer |
| the woman on the stair, alive when floor 4 clears | smelling salts |
| the `revealed` follow-up on floor 5, *take it* | a waterskin |
| **a chef, from the start** | a flask of oil **and** a waterskin |

The `revealed` follow-up's *take it* currently fills your will. It gives an item instead — a better
prize, and it keeps the corruption cost of taking on a sacred floor meaningful. Seven possible items
and four slots means a chef, at least, will have to choose what to put down.

**The chef's trait, at last.** *Mise en place* means everything prepared beforehand, so the chef
**starts the descent carrying** the two most useful things, and a consumable **does not end the
chef's turn** — `finish_turn` is not called, and you act again. "Pour, then light" is one turn for a
chef and two for everyone else, which is a real reason to have been one. The old text's "works twice
as well" is dropped: the only item with a number is the stone, and doubling one item's one number is
not a trait. The trait's `says` line changes to match (§15).

**What changes to make this work**, precisely:

- `hero.funny`: `"carrying": []` on the sheet; `kit()` appends carried items; `mise_en_place` text.
- `carried.funny` (new): the seven, `give(hero, id)`, `drop(hero, id)`, the four-slot check, and
  `is_consumable(art)`.
- `resolve.funny`: `smelling salts` needs a `heal` art with power 0 to clear conditions without
  healing — the existing branch handles power 0 already. A `ground_add` art currently always adds
  `sacred` (Consecrate is the only one); it gains a per-art `adds` field. Using a consumable removes
  it from `carrying`.
- `run.funny`: after `resolve`, if the art was consumable and the hero is a chef, do not call
  `finish_turn`. `begin()` gives a chef the two starting items.

### 4.2 The mountain has weather, because you gave it some

You are five floors underground. Literal weather would be a lie, and a random one would break rule 3.
So the weather is **what you have done to the mountain**: heat you put in, water you boiled or froze,
and air you spoiled. It is the same idea as the corruption meter, one level down — the world keeping
score of your element.

Three axes on the run, each from 0 to 10 and nowhere outside that, pushed by fixed amounts and
nothing else — `sky.funny`:

| axis | pushed up by | pushed down by |
| --- | --- | --- |
| `heat` | a fire art **+2**; a turn spent on `burning` ground **+1** | a frost art **−1** |
| `wet` | frost on `damp` **+1**; fire on `damp` **+1** (steam has to go somewhere) | an earth art **−1** |
| `foul` | a turn on `burning` ground **+1**; a void art **+2**; a storm art **+1** | grit-salt, used **−2** |

The push happens **once per turn, after the cascade**, from what that turn did. Nothing pushes an
axis mid-resolution, so a preview never has to predict the sky.

What the axes do, **on arrival at a floor and at no other time** — so the weather is something you
walk into, never something that changes under you mid-fight:

| when | the next floor arrives |
| --- | --- |
| `heat ≥ 4` | **`dry`**: any `damp` it would have had is gone |
| `heat ≥ 7` | `dry`, and **`gas`** — the mountain is venting |
| `wet ≥ 4` | **`damp`**, whether it would have been or not |
| `foul ≥ 4` | **`choking`**: everything standing on it, you included, loses 2 at the top of each turn |
| `foul ≥ 8` | `choking`, and **no `sacred`** — you have fouled it past meaning anything |

**When heat and wet disagree** — fire on a damp floor raises both — the larger axis wins, and on a
tie the room is `damp`: steam condenses. That is the one conflict, and it is resolved in one line
that the golden pins down.

`dry` and `choking` are new ground tags, both `CAUSED_TAGS` so the page draws them as things done
rather than things found. Two existing rules already key on the *absence* of `damp`, so `dry`
needs only one thing of its own, in the cascade: **on `dry` ground, `burning` burns for double and
does not go out.**

**Why this is the good version.** A fire mage arrives at the hollow throne in a dried-out, choking
room of their own making, and the last floor is harder because of how the first four were played.
That is not written down anywhere. It is the rule-table trick, one level up.

**What changes**: `sky.funny` (new); `run["sky"]`; `arrive()` gains step 2; `world.funny` gains the
two tags and their `GROUND_SAYS`; the cascade gains `dry` and `choking`; the sheet and the terminal's
status line show the three numbers; the log says when a floor arrives changed.

### 4.3 Corruption that does something

Today corruption is debited by rules 11, 19 and 20 and read by `reckoning()` alone. It is a score,
and a score is not a consequence. Five rules give it teeth — and give resonance some too, since the
two meters should be symmetrical in kind if not in effect. All five read the sheet, which the table
may already read.

| # | when | what |
| --- | --- | --- |
| 22 | `corruption ≥ 5`, healing on `sacred` ground | **halved** — the floor will not help you |
| 23 | `corruption ≥ 5`, a void art | costs **one less** — it likes you now, which is the problem |
| 24 | on **`hollow`** ground | light **halved**, void **+2 power**, `Consecrate` refused; the tag is put there on arrival at `corruption ≥ 10` |
| 25 | `resonance ≥ 5` | **what your own element costs you, it stops costing you** (below) |
| 26 | `resonance ≥ 8` | arts of your own element cost **one less** |

**Rule 23 is the one to get right.** Corruption must not be a straight penalty, or it is a losing
condition with extra steps. It has to be a road that is genuinely easier to walk: `Whisper` costs 1
today, so at corruption 5 it is free, and that is how the meter becomes a decision rather than a
warning light.

**Rule 25 replaces something, and the replacement is harsher.** Today a fire mage is simply immune to
`burning` — *"it is yours; it does not touch you"* — from the first turn. Under rule 25 that line
moves behind resonance 5: until the world has accepted you, your own fire burns you like anyone
else. It is a real change to existing behaviour and it is the right one, because it makes resonance
matter in the first two floors instead of only at the tenth point. The same rule covers every element
that has a cost to its own user:

| element | what it costs you today | and after resonance 5 |
| --- | --- | --- |
| fire | `burning` at the cascade | nothing |
| frost | `slick` at the cascade (**new**: 2 damage, a fall — today it trips only creatures) | nothing |
| storm | a third back on `metal` (rule 14); 4 in a `narrow` room (rule 15) | nothing |
| earth | holding the ceiling up costs 3 | nothing |
| void | `choking` at the cascade | nothing |
| light | **nothing — light has no cost to its user, and so gains nothing here** | — |

That last row is deliberate and the README should say it: light is the element with no downside,
and it is therefore the one that mastery cannot improve.

`hollow` is the third new ground tag, and the only one a player cannot lay down with an item — it is
caused by what they have become. Its effect is a rule that reads the tag, not a flag checked
elsewhere, so it composes: a hollow room that is also `dark` is where a corrupted light-caller finds
out that Glimmer no longer lifts the dark.

**What changes**: rules 22–26 in `resolve.funny`; the fire immunity line becomes rule 25; the
cascade's `slick` branch hurts the hero; `hollow` in `world.funny`; `arrive()` gains step 3;
`Consecrate` refused on `hollow`.

### 4.4 Somebody who remembers

Three people, on floors 2, 3 and 4. Each is an ordinary creature record with **`bystander`** in its
traits, `power` 0, and — this is the design — **two hit points out of six or eight**, so that
anything meant for the room will do for them.

**No rule is added to make an area effect kill a bystander.** Rule 9 takes the whole room. Rule 13
chains through everything standing in water. The cascade burns everything on `burning` ground. The
tragedy is already in the table; it has never had anybody to happen to.

| floor | who | hp | says, the first time | and if you have killed somebody |
| --- | --- | --- | --- | --- |
| 2, the gas hall | a man with a lamp | 2 / 6 | *"don't. whatever it is you're about to do, don't."* | — (he is the first) |
| 3, the frozen chapel | a girl behind the altar | 2 / 8 | *"it kneels all day. it doesn't see me. you might not either."* | *"she has heard. she has gone very still."* |
| 4, the iron gallery | a woman on the stair | 2 / 8 | *"the ones below told me you were coming. they said you were all right."* | *"she will not look at you. word gets down here faster than you do."* |

A lamp, in a hall full of gas. A child, in the room where fire is the right answer. Nobody on the
first floor, which should teach the rules, and nobody on the last, which should be about what you
have become.

**What you can do about them**, all through actions that already exist:

- **Mend one.** `Mend` gains an optional target — untargeted, it is still you. A nurse's *triage*
  applies to them too, because it is the same rule. They live through what follows, and when you
  clear the floor they give you something (§4.1).
- **Leave them.** At two hit points they live only if nothing touches the room. They remember that
  you did not stop.
- **Kill one.** On purpose, or because you set the room alight and they were in it. The model does not
  distinguish and neither does the ledger.

The run gains a **`ledger`**: one entry per person — `helped`, `left`, `killed` — read in exactly
three places, only one of which is a rule:

- **Rule 27** (new): striking anything with `bystander` is **+3 corruption**, on top of rule 20's +1
  for a thing that could not answer. Four in total for killing one, which is most of the way from
  clean to noticed in one turn.
- **On arrival** (pipeline step 4, not a rule): if the ledger holds a `killed`, or the room is
  `hollow`, the person is there but will not speak, and gives nothing.
- **In the reckoning**: how many saw you, and how many are still alive.

**What changes**, and every one of these is small: `folk.funny` (new) holds the three and the
placement rule; `world.funny`'s `standing()` still includes bystanders (the cascade and area rules
need that) but `cleared()` ignores them — a floor is cleared when nothing *hostile* stands; the
default-target fallback in `resolve` skips anything with `bystander`, so an untargeted Smite can
never hit one by accident; `foes_act` skips them; `Mend` and `wants_a_target` learn an optional
target for `heal`; the ledger on the run; rule 27.

---

## 5. The table afterwards

Twenty-seven rules in five groups. Seventeen are untouched. The README prints all of them, because
a simulation whose model is a secret is a slot machine with better prose.

- **what a thing is made of** — 1–8, unchanged. `bystander` is a trait these already apply to.
- **where you are standing** — 9–18, unchanged. `dry`, `choking` and `hollow` are grounds the
  existing rules already key on by the absence of `damp` or the presence of nothing.
- **the sheet** — 19–21, plus **22–26** (§4.3)
- **and one for people** — **27** (§4.4)

And the cascade, which is not numbered because it is what the room does rather than what a rule
says: `burning` hurts everything standing and burns off `gas`; on `dry` ground it burns for double
and does not go out; `frost-blooded` things in a burning room lose the trait; `slick` trips
creatures **and now the hero**; `choking` costs everything 2; a `swarm` below half divides.

---

## 6. The state, the API and the two clients

The state gains four things and the API gains one route:

```
   run.sky        {"heat", "wet", "foul"}
   hero.carrying  [art, art, ...]          at most four
   run.ledger     [{"who", "did"}, ...]    "helped" | "left" | "killed"
   floor.ground   may now hold dry, choking, hollow
```

| route | what it does |
| --- | --- |
| `POST /api/drop` | `{"item": id}` — put something down to make room |

Nothing else is new. An item is used through `/api/act` because it is an art; a bystander is mended
through `/api/act` because Mend now takes a target; the sky and the ledger arrive with
`/api/state`. `/api/preview` previews a flask exactly as it previews Ember.

**The page.** No carry strip: items already appear in the kit because they are arts, marked
*carried* and with a small *put down* control. The sky is three short bars under the two meters, and
a floor that arrived changed says so in the log. A bystander is drawn in the room with the others,
with what they said under their name and no attack line. The reckoning gains the people line.

**The terminal.** `carry` and `drop N`; the sky on the status line; what a person said, indented
under them, the first time you see the room.

---

## 7. Two things the plan has to get exactly right

**The default target.** Today, an art that wants a target and is not given one hits *the first thing
standing*. With bystanders in `standing()`, that would put an untargeted Smite into the man with the
lamp. The fallback skips `bystander`. The golden asserts it directly: an untargeted Smite in a room
holding one rat and one bystander hits the rat.

**Cleared.** Today a floor is cleared when `standing()` is empty. With a bystander alive, it never
would be. `cleared()` ignores `bystander`. The golden asserts that too.

Both are one-line changes, and both are the kind of thing that ships wrong quietly.

---

## 8. What the existing golden loses

The existing `test_isekai.expected` will change in two places, and both are regenerations rather
than rewrites:

- **Part 5, the careful descent.** The sky now touches the floors it walks through. The cleric of
  fire casts Ember once and stands in a burning room for one turn, so it arrives at floor 3 with
  `heat` 3 — under every threshold. Nothing visible should change until floor 5, and there `heat` is
  still below 4. The expected output for the whole run may come out byte-identical. If it does not,
  the diff is the sky doing something the table above does not predict, and that is a bug in the
  sky, not in the test.
- **The fire immunity line.** *"it is yours; it does not touch you"* now needs resonance 5. The
  cleric has 2 when it presses the attack in the gas hall, so **it will now take 3 from its own
  fire**, and the run will be one Mend longer. That is the intended consequence of rule 25, and the
  regenerated file records it.

---

## 9. The golden

Four new parts and one changed one. Each part is in the same shape as the existing table tests — a
room where the thing must happen, and one differing only in its condition where it must not.

1. **Items are arts.** A flask previews identically to how it resolves, over every item, which is
   the existing preview-is-the-outcome loop extended to `carrying`. Pour then light produces exactly
   rule 9's effects, and pouring on a floor that already has `gas` produces nothing. The chef's turn
   does not end and the nurse's does. The fourth slot refuses a fifth. A chef begins with two things
   and a soldier with none.
2. **The sky, on its own.** Each axis pushed by what pushes it and by nothing else; each held at 0
   and 10; each threshold applied on arrival and **not** mid-floor; `heat ≥ 4` removing a `damp` the
   floor would otherwise have had; `wet` beating `heat` on a tie; `foul ≥ 8` taking `sacred` off the
   fifth floor; `burning` on `dry` ground burning for double and not going out.
3. **Corruption with teeth.** Rules 22–26 each fired and not fired. Rule 25 for every element that
   has a cost, and the assertion that light gains nothing. Rule 23 making Whisper free. `hollow`
   halving Glimmer and refusing Consecrate.
4. **The person, and the assertion this plan exists for.** A fire mage ignites the gas hall with the
   man with the lamp in it, and the golden asserts two things. The obvious one: he dies. The one
   that matters: **the effect list is identical to igniting the same room with a second boil of rats
   of the same hit points standing where he stood** — line for line, the same rules, the same
   numbers, the same order. The bystander was treated exactly as any other thing in the room,
   because the table does not know he is different. Then: the default target skips him; `cleared()`
   ignores him; mending him at two hit points and clearing the floor yields the flask; killing him is
   four corruption; the girl on the next floor will not speak.
5. **A second whole descent — the cruel one.** The careful run stays as it is. The second takes
   every bad road: void everywhere, the chapel set alight, all three people dead. It should finish
   with high resonance *and* high corruption, which is the run the two meters exist to make
   possible, and it asserts the ledger, the sky's thresholds arriving on later floors, `hollow` on
   the throne, and the reckoning's new line.

---

## 10. What could go wrong

- **The model becomes a pile.** The guard is §0 rule 1, and it is checkable: after this work,
  `resolve.funny` still takes `(element, ground, traits, sheet)` and nothing else. A milestone that
  cannot be built inside that is the wrong milestone.
- **Items trivialise the table.** A flask makes rule 9 available on demand. That is intended, but
  four slots, no shop, seven fixed finds and one flask per run keep it a resource. If it still
  dominates, the flask gains a corruption cost — not a nerf, a price.
- **Bystanders become a guilt tax.** If every area effect kills somebody, area effects stop being
  used and half the table goes quiet. Three people, none on the first or last floor, each at two hit
  points so that mending them is a real choice and each avoidable by a player who reads the room.
- **The weather is invisible.** Three numbers nobody reads are worse than none. The sky gets a line
  in both clients and a floor that arrives changed says so in the log, in words.
- **Rule 25 makes early fire mages miserable.** It is meant to. If playtesting says it is *too*
  miserable, the threshold moves to 3, not to 0.
- **The golden doubles in size.** It is already the biggest file here. Part 5's second descent is
  the only part that grows it much, and it is the part that earns it.

---

## 11. Decisions already made

- **No new inputs.** Everything else follows from this.
- **One arrival pipeline, one order.** Sky, then corruption, then folk.
- **Weather is caused, never rolled**, and never changes mid-floor.
- **Items are arts, not a second kind of thing.** One record, one resolver, one preview path.
- **A bystander is a creature with a trait**, at two hit points, so the existing rules kill them
  without being told to.
- **Corruption must open a road, not only close them.** Rule 23 is not optional.
- **Rule 25 replaces the free fire immunity**, and early fire mages burn. Recorded in §8.
- **The chef's trait is "starts prepared, and a consumable does not end the turn."** The old
  "twice as well" is dropped because it would have applied to one number on one item.
- **Nobody on the first or last floor.**
- **The version bump is not part of this plan.** Changelog under `[Unreleased]`, as before.

---

## 12. Milestones

Each is worth shipping on its own and each leaves the suite green. W1 first because it is a bug fix;
W2 before W3 and W4 because it builds the arrival pipeline they both plug into.

### W1 — the chef's trait stops being a lie
- [ ] `carried.funny`; `carrying` on the sheet; items resolve as arts through the existing path
- [ ] `ground_add` arts gain a per-art `adds`; a used consumable leaves `carrying`
- [ ] the `revealed` follow-up gives an item; a chef begins with two; four slots; `drop`
- [ ] `mise_en_place`: a consumable does not end the chef's turn; the trait's text corrected
- [ ] golden part 1; `play.funny` gains `carry` and `drop`; `/api/drop`; the kit marks carried items

### W2 — the arrival pipeline, and the mountain's weather
- [ ] `arrive()` becomes the six-step pipeline of §3.2, with the log line
- [ ] `sky.funny`; `run["sky"]`; the push after the cascade; `dry` and `choking`
- [ ] the cascade's `dry` and `choking` behaviour; the sheet and status line show the axes
- [ ] golden part 2; the careful descent's `.expected` regenerated and the diff explained (§8)

### W3 — corruption that does something
- [ ] rules 22–26; the fire immunity moved behind rule 25; `slick` trips the hero
- [ ] `hollow` in `world.funny`; pipeline step 3; `Consecrate` refused on `hollow`
- [ ] golden part 3

### W4 — somebody who remembers
- [ ] `folk.funny`; the three people; pipeline step 4; the ledger; rule 27
- [ ] the default-target fallback skips `bystander`; `cleared()` ignores it; `foes_act` skips it
- [ ] `Mend` takes an optional target; *triage* applies to them
- [ ] the item for a person still alive when the floor clears
- [ ] golden part 4, **including the identical-to-a-rat assertion**; the reckoning's people line
- [ ] the page draws them with what they said; the terminal prints it

### W5 — the second descent
- [ ] `scripts/the-oil-and-the-spark.txt` — a chef, pour then light, the man with the lamp
- [ ] `scripts/the-long-way-down.txt` — the cruel one
- [ ] golden part 5; `.expected` regenerated

### W6 — the write-up
- [ ] `README.md`: the table at twenty-seven; the sky; the carry with pour-then-light as the worked
      example; the three people and the identical-to-a-rat assertion; rule 25's harshness; light's
      asymmetry; a **shorter** "what it is not"
- [ ] `CHANGELOG.md`; the API driven end to end again; the page against a stub DOM again

---

## 13. What the README gains, and loses

Gains: the arrival pipeline and why it has one order; the sky and why it is caused rather than
rolled; the carry, with pour-then-light as the worked example; the three people and the assertion
that the table treats them as anything else; rule 25 and the admission that it makes early fire mages
burn; that light gains nothing from mastery, and why.

Loses, from "what it is not": *no weather*, *no inventory*, *nobody who remembers you*, and
*corruption changes what the reckoning says and nothing else*. Those four lines come out. Two go in:
that the people are three fixed encounters rather than a populated world, and that the weather is
three numbers rather than a climate. Both are the next honest sentence after this work rather than a
reason not to do it.

---

## 14. Cost sheet

| piece | lines (est.) |
| --- | --- |
| `carried.funny`, `sky.funny`, `folk.funny` | 620 |
| changes to `hero`, `world`, `resolve`, `run` | 520 |
| `serve.funny`, `web/`, `play.funny` | 400 |
| golden, two new scripts | 420 |
| `README.md`, `CHANGELOG.md` | 220 |
| **new, written** | **~2,200** |

---

## 15. Deviations log

*(empty — nothing built yet)*
