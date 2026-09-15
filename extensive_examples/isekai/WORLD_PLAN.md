# FunnyLang — World Plan (`extensive_examples/isekai/`)

> **Status:** planned, nothing built. Branch `feature/isekai-world`, to be cut from `master` once
> `feature/isekai` has landed. §9 has the state of each milestone, §12 every deviation.
> **Prerequisite:** `extensive_examples/isekai/PLAN.md`, complete — the four model files, the
> twenty-one rules, the server, the page, the terminal twin and the golden.
> **Deliverable:** the four things the example's own README admits it is not. Weather, something to
> carry, somebody who remembers you, and a corruption meter that changes what the world *does*
> rather than only what the reckoning *says*.

---

## 0. Rules for the executing agent

1. **NO NEW INPUTS.** This is the whole plan, and everything else in it is a consequence. A rule may
   read the element in play, the ground tags, the material traits, and the sheet — and after this
   work it may still read exactly those four things. Weather arrives as **ground tags**. What you
   carry lives on **the sheet**. Somebody who remembers you is a **creature with a trait**. A
   corruption threshold is a **fact about the sheet**. If an addition needs a fifth input, it is the
   wrong addition and it goes back in the drawer.
2. **No rule may name a floor, a creature, or an item.** Unchanged from `PLAN.md` §0. A survivor is
   not special-cased; it is `bystander`, and the rules that already exist have opinions about it.
3. **Still no dice.** `rizz` is not imported by any file in this example, and this work must not be
   the thing that changes that. Weather is deterministic because it is caused (§3.1), which is also
   what makes it worth having.
4. **`resolve` stays pure**, and the preview stays the outcome. Every addition here is reachable
   through the same `resolve(state, choice)` call, so the page's forecast keeps working without a
   line of new code — a consumable that lays down `gas` must be previewable, and will be, for free.
5. **The rule table stays ordered and additive.** Six new rules, not six new branches inside old
   ones.
6. **Each milestone leaves `funny test extensive_examples` green**, and each is worth shipping
   without the ones after it.
7. **Stage by explicit path; commit per milestone; push; never merge to master.**
8. **Every deviation gets a §12 entry.**

---

## 1. What "done" looks like

```console
$ funny run extensive_examples/isekai/play.funny -- \
      --origin chef --calling mage --affinity fire --script scripts/the-oil-and-the-spark.txt
```

```
  floor 2 of 5, turn 6
  THE GAS HALL   (gas, narrow, dry)
  the mountain has been running hot since you set the stair alight.
   - a boil of rats    20/20 hp   swarm, grounded
   - a man with a lamp  6/6 hp    bystander          <- he is in the room

  you carry: a flask of oil, tinder, a stone that hums
  heat 5   wet 0   foul 2

> 6 1
      you   ground    the flask goes over the floor: the room is gas, and more of it
      you   note      mise en place: that cost you nothing but the flask
> 3 1
      you   note      the gas goes up all at once, and the whole hall with it
      you   damage    a boil of rats  (20)
      you   kill      a boil of rats: down
      you   damage    a man with a lamp  (20)
      you   kill      a man with a lamp: down
      you   meter     and it minds you in the other way too  (4)
```

Nothing in the rule table mentions the man with the lamp. Rule 9 takes the whole room, and he was
in the room. **That is the entire argument for this plan**: the model already had an opinion about
him, and all that was missing was somebody to have it about.

Later, on the fourth floor:

```
   - a woman on the stair  8/8 hp   bystander
> 1 1
      you   note      she will not look at you.
      you   note      word gets down here faster than you do.
```

And at the end:

```
  you came out the other side of it.
  floors: 5 of 5   turns: 31
  the world learned your name: resonance 11.
  and it will be a long time putting right what you did: corruption 14.
  three people saw you coming. one of them is still alive.
```

---

## 2. Why this shape

The example's README ends with a list of what it is not, and this plan is that list. The risk is
obvious and worth naming up front: **four new subsystems is how a clean model turns into a pile.**
Twenty-one rules over four inputs is legible. Twenty-one rules over four inputs plus a weather
engine plus an inventory system plus an NPC system plus a reputation system is a codebase.

So none of these is a subsystem. Each one is an **existing input, extended**:

| what the README admits | where it actually goes | what the table has to learn |
| --- | --- | --- |
| no weather | **ground tags**, applied on arrival | nothing — the tags already exist |
| no inventory | **the sheet**, and items are arts | nothing — items resolve as arts do |
| nobody who remembers | **a creature with a trait** | one rule, for killing one |
| corruption does nothing | **a fact about the sheet** | five rules |

Six new rules, and three of the four additions need no rule at all. That is the test of whether an
addition belongs: if it makes the table bigger than it makes the game, it is the wrong addition.

There is a second argument, and it is the better one. Each of these makes the **existing** rules do
more work rather than sitting beside them:

- Weather means floor five's ground is a consequence of how you played floors one to four. Rule 9
  (`fire` on `gas`) becomes a decision about the rest of the descent, not just this room.
- A flask of oil lays down `gas`. That turns rule 9 from something that happens to you into
  something you can **set up** — the first player-authored combination in the example.
- A bystander makes every area effect a moral question, using rules that are already written.
- Corruption thresholds give rules 11, 19 and 20 somewhere to lead. At the moment they debit a
  number that only the reckoning ever reads.

---

## 3. The four

### 3.1 The mountain has weather, because you gave it some

You are five floors underground. Literal weather would be a lie, and a random one would break rule
3. So the weather is **what you have done to the mountain**: heat you put in, water you boiled or
froze, and air you spoiled.

Three axes on the run, all starting at zero, all pushed by fixed amounts — `sky.funny`:

| axis | what pushes it |
| --- | --- |
| `heat` | a fire art **+2**; a frost art **−1**; a turn spent on `burning` ground **+1** |
| `wet` | frost on `damp` **+1**; fire on `damp` **+1** (steam has to go somewhere); an earth art **−1** |
| `foul` | a turn on `burning` ground **+1**; a void art **+2**; a storm art **+1** |

And what they do, **on arrival at a floor and nowhere else** — so the weather is something you walk
into, not something that happens mid-fight:

| when | the next floor arrives |
| --- | --- |
| `heat ≥ 4` | `dry`: any `damp` it would have had is gone |
| `heat ≥ 7` | `dry`, and `gas` — the mountain is venting |
| `wet ≥ 4` | `damp`, whether it would have been or not |
| `foul ≥ 4` | `choking`: everything standing on it, you included, loses 2 at the top of each turn |
| `foul ≥ 8` | `choking`, and no `sacred` — you have fouled it past meaning anything |

`dry` and `choking` are two new ground tags, and they are `CAUSED_TAGS`, which the page already
draws differently.

**Why this is the good version.** A fire mage arrives at the hollow throne in a dried-out, choking
room of their own making, and the last floor is harder because of how the first four were played.
Nobody wrote that down either. It is the same trick as the rule table, one level up.

The sheet gains a small readout, and `--script` runs stay reproducible because every push is fixed.

### 3.2 Something to carry — and the chef's trait, which is currently a lie

**This part is a bug fix before it is a feature.** `hero.funny` ships five origins, and four of
their traits are wired up. The chef's says:

> **Mise en Place** — what you carry works twice as well and never costs a turn

There is nothing in the game to carry. `mise_en_place` appears in `hero.funny` and in no other file.
One of the five origins has been a decorative sentence since the day it shipped, and anybody who
picked the chef got worse stats than a soldier and nothing in exchange. That alone justifies this
milestone; the rest is what makes it worth doing properly.

**An item is an art.** Same record — `id`, `name`, `cost`, `power`, `element`, `tags`, `says` —
plus `"consumable": fax`. `resolve` needs no new path, `kit()` appends what you carry, and the
page's forecast previews a flask of oil for free because it previews arts. That is the discipline
from §0 rule 1 paying for itself.

Four slots, and what is in them:

| item | tags | what it does |
| --- | --- | --- |
| a flask of oil | `ground_add` | the floor gains `gas` |
| a waterskin | `ground_add` | the floor gains `damp` |
| tinder | `ground_add` | the floor gains `burning` |
| a censer | `ground_add` | the floor gains `sacred` |
| grit-salt | `ground_clear` | `slick` and `choking` lift |
| smelling salts | `heal` | clears every condition on you |
| a stone that hums | — | **+2 resonance** |

**The flask is the point.** Pour oil, then ignite: two existing rules, combined by a player, to make
a thing neither rule describes. Up to now the ground has been something the floor decided. Now it is
a move.

Where items come from, deterministically: the `revealed` follow-up's *take it* gives an item instead
of filling your will (a better prize, and it keeps the corruption cost meaningful), and clearing a
floor where a bystander is still alive gives one (§3.3). Full slots mean you choose what to drop.

The chef's trait, at last: a consumable **does not end the turn** (`finish_turn` is not called) and
any number in its effect doubles. "Pour and light" becomes one turn for a chef and two for everyone
else, which is a real reason to have been a chef.

### 3.3 Somebody who remembers

Three people, on floors 2, 3 and 4. Each is an ordinary creature record with **`bystander`** in
its traits, `power` 0, and few enough hit points that anything meant for the room will do for them
too.

That last clause is the whole design. **No rule is added to make an area effect kill a bystander.**
Rule 9 takes the whole room. Rule 13 chains through everything standing in water. Rule 2 says a
swarm takes double from anything covering the room, and a bystander is standing in that room as
well. The tragedy is already in the table; it has just never had anybody to happen to.

What you can do about them, all through actions that already exist:

- **Mend one** — they live, and when you clear that floor they give you something (§3.2).
- **Leave them** — they live, and they remember that you did not stop.
- **Kill one** — on purpose, or because you set the room on fire and they were in it. The model does
  not distinguish, and neither does the ledger.

The run gains a `ledger`: one entry per person, `helped`, `left` or `killed`. It is read in three
places, and only one of them is a new rule:

- **Rule 27** (new): killing a `bystander` is **+3 corruption**, on top of rule 20's +1 for striking
  something that could not answer.
- **On arrival** (not a rule — it is what `folk.funny` does when it places somebody): if anyone in
  the ledger is `killed`, the next person will not speak. No item, and a line saying why.
- **In the reckoning**: how many saw you, and how many are still alive.

The fifth floor has nobody on it, and that is deliberate: the last floor should be about what you
have become, not about one more decision.

### 3.4 Corruption that does something

At present corruption is debited by rules 11, 19, 20 and read only by `reckoning()`. It is a score,
and a score is not a consequence. Five rules give it teeth, and all five read the sheet, which the
table may already read.

| # | when | what |
| --- | --- | --- |
| 22 | `corruption ≥ 5`, healing on `sacred` ground | halved — the floor will not help you |
| 23 | `corruption ≥ 5`, a void art | costs **one less** — it likes you now, which is the problem |
| 24 | `corruption ≥ 10`, on arrival | every floor arrives `hollow`: `Consecrate` is refused and nobody will speak to you |
| 25 | `resonance ≥ 5` | the ground tag your own element causes stops hurting you — generalising the fire mage's existing immunity to `burning` |
| 26 | `resonance ≥ 8` | arts of your own element cost **one less** |

Rule 23 is the one to get right. Corruption must not be a straight penalty, or it is just a losing
condition with extra steps; it has to be a road that is genuinely easier to walk. Void gets cheaper
as you fall, and that is how the meter becomes a decision rather than a warning light.

`hollow` is the third new ground tag, and the only one the player cannot cause directly — it is
caused by what they have become, which is the point.

---

## 4. The table afterwards

Twenty-one rules become twenty-seven, and the README keeps printing all of them, because a
simulation whose model is a secret is a slot machine with better prose. The four groups become five:

- **what a thing is made of** — 1–8, unchanged
- **where you are standing** — 9–18, unchanged, plus `dry`, `choking` and `hollow` as grounds that
  the existing rules already key on (`fire` on `dry` is not halved; `frost` on `dry` does not freeze)
- **the sheet** — 19–21, plus **22–26**
- **and one for people** — **27**

Seventeen of the twenty-seven are untouched by this work.

---

## 5. Files

| file | what happens to it | est. lines |
| --- | --- | --- |
| `sky.funny` | **new** — the three axes, what pushes them, what they do on arrival | 180 |
| `carried.funny` | **new** — the items, the four slots, finding and dropping | 220 |
| `folk.funny` | **new** — the three people, where they stand, the ledger | 200 |
| `resolve.funny` | six new rules; `bystander` in the aftermath; consumables resolve as arts | +180 |
| `world.funny` | `dry`, `choking`, `hollow`; bystanders placed on arrival | +90 |
| `hero.funny` | `carrying` on the sheet; `mise_en_place` finally means something | +70 |
| `run.funny` | the sky advances; the ledger; a consumable does not end a chef's turn | +140 |
| `serve.funny` | the sky, what you carry, and the ledger in the state; a drop route | +90 |
| `web/` | a carry strip, a sky readout, a ledger line in the reckoning | +260 |
| `play.funny` | `carry` and `drop`; the sky on the status line | +80 |
| `test_isekai.funny` | §6 | +320 |
| `README.md` | §10 | +180 |
| **new, written** | | **~2,000** |

---

## 6. The golden

Four new parts, and one changed one.

1. **The sky, on its own.** Each axis pushed by the actions that push it and by nothing else; each
   threshold applied on arrival and **not** mid-floor; `heat ≥ 4` removing a `damp` the floor would
   otherwise have had; `foul ≥ 8` taking `sacred` off the fifth floor.
2. **Items are arts.** A flask previews identically to how it resolves (the same
   preview-is-the-outcome check, now over items); pouring oil then igniting produces exactly rule
   9's effects; the chef's turn does not end and everybody else's does; the fourth slot refuses a
   fifth item.
3. **The bystander, and the one assertion this whole plan is for.** A fire mage ignites the gas hall
   with somebody standing in it, and the golden asserts that the bystander dies **and that no rule
   in the table mentions bystanders** — grep-style, by checking the effect list carries no rule
   naming one. Then: mending one and clearing the floor yields an item; killing one costs 4
   corruption in total (rule 20 plus rule 27); and the next person will not speak.
4. **Corruption with teeth.** Each of rules 22–26 in a state where it fires and one differing only
   in the meter, exactly as the existing table is tested.
5. **A second whole descent — the cruel one.** The existing scripted run is careful, and it stays as
   it is. A second one takes every bad road: void everywhere, burn the chapel, kill all three. It
   should finish with high resonance *and* high corruption, which is the run the two meters exist to
   make possible, and it asserts the ledger, `hollow`, and the reckoning's new line.

The existing five parts are unchanged in intent. Part five's numbers will move, because the sky now
touches the floors it walks through; that is a regenerated `.expected`, not a rewritten test.

---

## 7. What could go wrong

- **The model becomes a pile.** The guard is §0 rule 1, and it is checkable: after this work,
  `resolve.funny` must still take `(element, ground, traits, sheet)` and nothing else. If a
  milestone cannot be built inside that, it is the wrong milestone.
- **Items trivialise the rule table.** A flask that lays down `gas` makes rule 9 available on demand.
  That is intended — it is the first combination a player authors — but four slots, no shop and a
  fixed number of finds keep it a resource rather than a strategy. If playtesting says otherwise,
  the flask gets a corruption cost, not a nerf.
- **Bystanders become a guilt tax.** If every area effect kills somebody, area effects stop being
  used and half the table goes quiet. Three people across five floors, none on the first or last,
  and each one avoidable by a player who is paying attention.
- **The weather is invisible.** Three numbers nobody reads are worse than no numbers. The sky gets a
  line on the page and in the terminal, and a floor that arrives changed **says so** in the log.
- **The golden doubles in size.** It is already the biggest file here. Part 5's second descent is
  the only part that grows it much, and it is the part that earns it.

---

## 8. Decisions already made

- **No new inputs to the rule table.** Everything else follows from this.
- **Weather is caused, never rolled.** It is the mountain's memory of your element, which keeps rule
  3 and makes the weather thematically the same idea as the corruption meter.
- **Items are arts, not a second kind of thing.** One record, one resolver, one preview path.
- **A bystander is a creature with a trait**, so the existing rules kill them without being told to.
- **Corruption must open a road, not only close them.** Rule 23 is not optional.
- **Nobody on the first or last floor.** The first should teach the rules and the last should be
  about what you have become.
- **The version bump is not part of this plan.** Changelog under `[Unreleased]`, as before.

---

## 9. Milestones

Each is worth shipping on its own, and each leaves the suite green.

### W1 — the chef's trait stops being a lie
- [ ] `carried.funny`; `carrying` on the sheet; items resolve as arts through the existing path
- [ ] `mise_en_place`: a consumable does not end the turn, and its numbers double
- [ ] items from the `revealed` follow-up; four slots; dropping
- [ ] golden part 2, including pour-then-ignite and preview-is-the-outcome over items
- [ ] `play.funny` gains `carry` and `drop`; the page gains a carry strip

### W2 — corruption that does something
- [ ] rules 22–26 in `resolve.funny`; `hollow` in `world.funny`
- [ ] golden part 4, each rule fired and not fired

### W3 — the mountain's weather
- [ ] `sky.funny`; the three axes; `dry` and `choking`; applied on arrival only
- [ ] the log says when a floor arrives changed; the sheet and the terminal show the axes
- [ ] golden part 1

### W4 — somebody who remembers
- [ ] `folk.funny`; three bystanders; the ledger; rule 27
- [ ] the arrival rule for somebody who has heard about you; the item for helping
- [ ] golden part 3, **including the assertion that no rule names a bystander**
- [ ] the reckoning's new line

### W5 — the second descent
- [ ] the cruel script; golden part 5b; regenerate `.expected`
- [ ] `scripts/the-oil-and-the-spark.txt` and `scripts/the-long-way-down.txt`

### W6 — the write-up
- [ ] `README.md`: the table at twenty-seven, the sky, the carry, the people, and a **shorter**
      "what it is not"
- [ ] `CHANGELOG.md`; the API driven end to end again; the page checked against a stub DOM again

---

## 10. What the README gains, and loses

Gains: the sky and why it is caused rather than rolled; the carry, with pour-then-ignite as the
worked example; the three people and the fact that no rule mentions them; the six new rules in the
table that is already printed in full.

Loses, from "what it is not": *no weather*, *no inventory*, *nobody who remembers you*, and
*corruption changes what the reckoning says and nothing else*. Those four lines come out, and one
goes in — that the people are three fixed encounters rather than a populated world, which will be
the next honest sentence after this work rather than a reason not to do it.

---

## 11. Cost sheet

| piece | lines (est.) |
| --- | --- |
| `sky.funny`, `carried.funny`, `folk.funny` | 600 |
| changes to the four model files | 480 |
| `serve.funny`, `web/`, `play.funny` | 430 |
| golden | 320 |
| `README.md` | 180 |
| **new, written** | **~2,000** |

---

## 12. Deviations log

*(empty — nothing built yet)*
