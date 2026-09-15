// isekai.js — the summoning, the descent, and the reckoning.
//
// THE PAGE KNOWS NO RULES. It does not work out what an action would do, what
// an element beats, or what a room is about to become. Everything on screen
// arrived from the server, including the forecast under the kit: hovering an
// art posts to /api/preview, which runs the REAL resolver and throws the
// answer away. There is no second, cheaper predictor in this example, which is
// the usual way a game's tooltip starts quietly disagreeing with the game.
//
// THREE SCREENS, one visible at a time, chosen from `phase`. A reload asks for
// the state and draws whichever it is on: the run lives in the server, so
// refreshing costs nothing.

(() => {
  "use strict";

  const els = (ids) => Object.fromEntries(ids.map((k) => [k, document.getElementById(k)]));
  const el = els([
    "status", "summoning", "descent", "reckoning", "origins", "callings",
    "affinities", "preview-sheet", "descend", "floorname", "floorcount",
    "floorsays", "ground", "foes", "pending", "pendingsays", "pendingoptions",
    "sheet", "log", "kit", "forecast", "undo", "reckoninglines", "fulllog", "again"
  ]);

  let state = null;
  let picked = { origin: null, calling: null, affinity: null };
  let target = null;
  let busy = false;
  let hoverToken = 0;

  const text = (tag, cls, content) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (content !== undefined) n.textContent = content;
    return n;
  };

  const say = (words, tone) => {
    el.status.className = "mb-0 " + (tone || "text-body-secondary");
    el.status.textContent = words;
  };

  // -- talking to the server -------------------------------------------------

  async function post(path, body) {
    try {
      const r = await fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {})
      });
      return await r.json();
    } catch { return null; }
  }

  async function refresh() {
    try {
      const r = await fetch("/api/state");
      const fresh = await r.json();
      if (fresh && fresh.ok) { state = fresh; draw(); }
    } catch {
      say("cannot reach the server", "text-warning");
    }
  }

  // -- the summoning ---------------------------------------------------------

  function drawChoices() {
    const cat = state.catalogue;

    el.origins.replaceChildren(...cat.origins.map((o) => {
      const b = text("button", "choice" + (picked.origin === o.id ? " on" : ""));
      b.type = "button";
      b.append(text("div", "title", o.id));
      b.append(text("div", "says", o.was));
      b.append(text("div", "cost", o.trait + " — " + o.trait_says));
      b.addEventListener("click", () => { picked.origin = o.id; drawChoices(); drawSheetPreview(); });
      return b;
    }));

    el.callings.replaceChildren(...cat.callings.map((c) => {
      const b = text("button", "choice" + (picked.calling === c.id ? " on" : ""));
      b.type = "button";
      b.append(text("div", "title", c.id));
      b.append(text("div", "says", c.says));
      b.append(text("div", "cost", c.kit.map((k) => k.name).join(", ")));
      b.addEventListener("click", () => { picked.calling = c.id; drawChoices(); drawSheetPreview(); });
      return b;
    }));

    el.affinities.replaceChildren(...cat.affinities.map((a) => {
      const b = text("button", "choice" + (picked.affinity === a.id ? " on" : ""));
      b.type = "button";
      b.dataset.element = a.id;
      b.append(text("div", "title", a.id));
      b.append(text("div", "says", a.says));
      // What it closes, said as loudly as what it opens.
      b.append(text("div", "cost", "soft to " + a.weak_to + " — " +
        a.tiers.map((t) => t.name).join(" / ")));
      b.addEventListener("click", () => { picked.affinity = a.id; drawChoices(); drawSheetPreview(); });
      return b;
    }));
  }

  // Worked out from the catalogue, which is the one place the page does
  // arithmetic — and only to show a choice before it is made. Once the run
  // begins, every number comes from the server.
  function drawSheetPreview() {
    const cat = state.catalogue;
    const box = el["preview-sheet"];
    box.replaceChildren();

    const origin = cat.origins.find((o) => o.id === picked.origin);
    const calling = cat.callings.find((c) => c.id === picked.calling);
    const affinity = cat.affinities.find((a) => a.id === picked.affinity);

    if (!origin && !calling && !affinity) {
      box.append(text("p", "text-body-tertiary mb-0", "nothing chosen yet."));
      el.descend.disabled = true;
      return;
    }

    if (origin) {
      const grid = text("div", "stats");
      for (const s of cat.stats) {
        const cell = text("div", "stat");
        cell.append(text("div", "n", String(3 + (origin.stats[s] || 0))));
        cell.append(text("div", "k", s.slice(0, 3)));
        grid.append(cell);
      }
      box.append(grid);
      box.append(text("p", "mb-2", origin.trait + ": " + origin.trait_says));
    }
    if (calling) {
      box.append(text("p", "mb-1 text-body-secondary",
        "kit: " + calling.kit.map((k) => k.name).join(", ")));
    }
    if (affinity) {
      box.append(text("p", "mb-1 text-body-secondary",
        "and " + (picked.calling === "mage"
          ? affinity.tiers.map((t) => t.name).join(", ")
          : affinity.tiers[0].name + " (a mage would get all three)")));
      box.append(text("p", "mb-0 text-warning",
        "soft to " + affinity.weak_to + ". the third tier waits until the world minds you " +
        cat.third_tier_at + " times."));
    }

    el.descend.disabled = !(picked.origin && picked.calling && picked.affinity);
  }

  // -- the descent -----------------------------------------------------------

  function bar(kind, now, most) {
    const b = text("div", "bar " + kind);
    const fill = text("div", "fill");
    fill.style.width = Math.max(0, Math.min(100, (now / most) * 100)) + "%";
    const label = text("div", "label");
    label.append(text("span", null, kind === "hp" ? "body" : "will"));
    label.append(text("span", null, now + " / " + most));
    b.append(fill, label);
    return b;
  }

  function drawSheet() {
    const h = state.hero;
    const box = el.sheet;
    box.replaceChildren();

    box.append(text("p", "mb-2 text-body-secondary",
      h.origin + ", " + h.calling + " of " + h.affinity + " — soft to " + h.weak_to));

    const grid = text("div", "stats");
    for (const s of state.catalogue.stats) {
      const cell = text("div", "stat");
      cell.append(text("div", "n", String(h.stats[s])));
      cell.append(text("div", "k", s.slice(0, 3)));
      grid.append(cell);
    }
    box.append(grid);
    box.append(bar("hp", h.hp, h.max_hp));
    box.append(bar("mp", h.mp, h.max_mp));

    const meters = text("div", "meters");
    const res = text("div", "meter res");
    res.append(text("div", "k", "resonance"));
    res.append(text("div", "n", h.resonance + " / " + state.catalogue.third_tier_at));
    const cor = text("div", "meter cor");
    cor.append(text("div", "k", "corruption"));
    cor.append(text("div", "n", String(h.corruption)));
    meters.append(res, cor);
    box.append(meters);

    if (h.conditions.length) {
      box.append(text("p", "mb-0 mt-2 text-warning small", h.conditions.join(", ")));
    }
    if (h.resonance < state.catalogue.third_tier_at) {
      box.append(text("p", "mb-0 mt-2 text-body-tertiary small",
        state.locked.name + " is still locked."));
    }
  }

  function drawRoom() {
    const f = state.floor;
    el.floorname.textContent = f.name;
    el.floorcount.textContent = "floor " + state.depth + " of " + state.floors;
    el.floorsays.textContent = f.says;

    const caused = ["burning", "slick", "dark", "sacred"];
    el.ground.replaceChildren(...f.ground.map((t) => {
      const tag = text("span", "tag" + (caused.includes(t) ? " caused" : ""), t);
      tag.title = state.catalogue.ground_says[t] || "";
      return tag;
    }));

    const alive = f.foes.filter((x) => x.hp > 0);
    el.foes.replaceChildren(...alive.map((x) => {
      const box = text("div", "foe" + (target === x.id ? " on" : ""));
      const head = text("div", "d-flex justify-content-between");
      head.append(text("span", "name", x.name));
      head.append(text("span", "text-body-secondary small", x.hp + " / " + x.max_hp));
      box.append(head);
      box.append(bar("hp", x.hp, x.max_hp));
      // What a thing is made of is hidden until you have a reason to know it.
      box.append(text("div", "traits", f.seen ? x.traits.join(", ") : "you cannot tell what it is"));
      if (x.conditions.length) box.append(text("div", "conds", x.conditions.join(", ")));
      box.addEventListener("click", () => { target = x.id; drawRoom(); });
      return box;
    }));
    if (!alive.length) el.foes.append(text("p", "text-body-tertiary mb-0", "nothing left standing."));
  }

  function drawKit() {
    el.kit.replaceChildren(...state.kit.map((a) => {
      const b = text("button", "art");
      b.type = "button";
      if (a.element) b.dataset.element = a.element;
      b.disabled = !a.affordable || busy || Boolean(state.pending);
      b.append(text("div", "name", a.name));
      b.append(text("div", "meta",
        (a.cost ? a.cost + " will" : "free") + (a.element ? " · " + a.element : "") +
        (a.power ? " · " + a.power : "")));
      b.append(text("div", "meta", a.says));
      b.addEventListener("mouseenter", () => forecast(a));
      b.addEventListener("focus", () => forecast(a));
      b.addEventListener("click", () => doAct(a));
      return b;
    }));
    el.undo.hidden = !state.can_undo;
  }

  // The forecast is the outcome, worked out by the server and discarded.
  async function forecast(a) {
    const mine = ++hoverToken;
    const out = await post("/api/preview", { art: a.id, target: target });
    if (mine !== hoverToken || !out) return;
    el.forecast.replaceChildren();
    if (!out.ok) {
      el.forecast.append(text("div", "line warn", out.error));
      return;
    }
    el.forecast.append(text("div", "line text-body-tertiary", "if you do this:"));
    for (const e of out.effects) {
      el.forecast.append(text("div", "line", effectWords(e)));
    }
    if (out.asks) {
      el.forecast.append(text("div", "line warn", "…and then it will ask you something."));
    }
    if (!out.effects.length) {
      el.forecast.append(text("div", "line text-body-tertiary", "nothing much."));
    }
  }

  const effectWords = (e) =>
    (e.who ? e.who + ": " : "") + e.says + (e.amount ? " (" + e.amount + ")" : "");

  function drawPending() {
    el.pending.hidden = !state.pending;
    if (!state.pending) return;
    el.pendingsays.textContent = state.pending.says;
    el.pendingoptions.replaceChildren(...state.pending.options.map((o) => {
      const b = text("button", "btn btn-outline-warning");
      b.type = "button";
      b.disabled = busy;
      b.append(text("div", null, o.name));
      b.append(text("div", "small text-body-secondary", o.hint));
      b.addEventListener("click", () => doAnswer(o.id));
      return b;
    }));
  }

  function drawLog(into, all) {
    into.replaceChildren();
    let lastTurn = null;
    const entries = all ? state.log : state.log.slice(-12);
    for (const entry of entries) {
      if (entry.turn !== lastTurn) {
        into.append(text("div", "turn", "turn " + entry.turn));
        lastTurn = entry.turn;
      }
      for (const e of entry.effects) {
        const line = text("div", "line " + e.kind);
        line.append(text("span", "who", entry.who));
        line.append(text("span", "what", effectWords(e)));
        into.append(line);
      }
    }
    into.scrollTop = into.scrollHeight;
  }

  // -- acting ----------------------------------------------------------------

  async function doAct(a) {
    if (busy) return;
    if (a.needs_target && !target) {
      say("point it at something first", "text-warning");
      return;
    }
    busy = true; drawKit();
    const fresh = await post("/api/act", { art: a.id, target: target });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; target = null; draw(); }
    else { say((fresh && fresh.error) || "it refused that", "text-warning"); drawKit(); }
  }

  async function doAnswer(option) {
    if (busy) return;
    busy = true; drawPending();
    const fresh = await post("/api/answer", { option });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; draw(); }
    else { say((fresh && fresh.error) || "it refused that", "text-warning"); drawPending(); }
  }

  // -- drawing everything ----------------------------------------------------

  function draw() {
    if (!state) return;
    el.summoning.hidden = state.phase !== "summoning";
    el.descent.hidden = state.phase !== "descending";
    el.reckoning.hidden = state.phase !== "over";

    if (state.phase === "summoning") {
      drawChoices();
      drawSheetPreview();
      say("choose three things.");
      return;
    }

    if (state.phase === "over") {
      el.reckoninglines.replaceChildren(...state.reckoning.map(
        (line, i) => text("p", i === 0 ? "h5 mb-2" : "mb-1 text-body-secondary", line)));
      drawLog(el.fulllog, true);
      say(state.outcome === "through" ? "you are through." : "that is as far as you got.",
        state.outcome === "through" ? "text-success" : "text-warning");
      return;
    }

    drawRoom();
    drawSheet();
    drawKit();
    drawPending();
    drawLog(el.log, false);
    if (state.pending) say("it is asking you something.", "text-warning");
    else say("floor " + state.depth + ", turn " + state.turn + ".");
  }

  // -- wiring ----------------------------------------------------------------

  el.descend.addEventListener("click", async () => {
    busy = true;
    const fresh = await post("/api/new", picked);
    busy = false;
    if (fresh && fresh.ok) { state = fresh; target = null; draw(); }
    else say((fresh && fresh.error) || "it refused that", "text-warning");
  });

  el.undo.addEventListener("click", async () => {
    const fresh = await post("/api/undo", {});
    if (fresh && fresh.ok) { state = fresh; target = null; draw(); }
  });

  el.again.addEventListener("click", () => {
    picked = { origin: null, calling: null, affinity: null };
    state = { ok: true, phase: "summoning", catalogue: state.catalogue };
    draw();
  });

  refresh();
})();
