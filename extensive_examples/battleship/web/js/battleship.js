// battleship.js — the boards, the clicking, and the firing.
//
// THE PAGE KNOWS NO RULES. Whether a ship may go somewhere is the server's
// answer, and the sentence it refuses with is shown to you word for word
// rather than reinvented here -- a second copy of the rules in JavaScript is
// two implementations that will drift. The one piece of geometry the page does
// own is the *preview* under the pointer while you are placing a ship, which
// is a drawing rather than a decision: the server still has the last word, and
// a preview the server disagrees with is drawn in red rather than accepted.
//
// WHAT THE PAGE IS NEVER TOLD. The enemy board is drawn entirely from
// `state.engine`, which is `view_of` the engine's board: your own record of
// where you have fired, the ships you have sunk and where those turned out to
// be, and the names and sizes of the ones still out there. There is no cell of
// a ship still afloat anywhere in it, so there is nothing here to accidentally
// render. When the game ends the server sends `revealed` under its own key,
// and that -- deliberately a different key -- is the only thing that ever
// draws a ship you did not sink.
//
// THREE PHASES, one visible at a time, chosen from `state.phase`: placing,
// playing, over. A reload asks for the state and draws whichever it is in, so
// a game is never lost to a refresh -- it never lived in the browser.

(() => {
  "use strict";

  const SIZE = 10;
  const LETTERS = "ABCDEFGHIJ";
  const notation = (n) => LETTERS[Math.floor(n / SIZE)] + (n % SIZE + 1);

  const els = {
    status: document.getElementById("status"),
    placing: document.getElementById("placing"),
    playing: document.getElementById("playing"),
    over: document.getElementById("over"),
    tray: document.getElementById("tray"),
    rotate: document.getElementById("rotate"),
    auto: document.getElementById("auto"),
    ready: document.getElementById("ready"),
    facing: document.getElementById("facing"),
    heat: document.getElementById("heat"),
    enemyfleet: document.getElementById("enemyfleet"),
    ownfleet: document.getElementById("ownfleet"),
    log: document.getElementById("log"),
    outcome: document.getElementById("outcome"),
    tally: document.getElementById("tally"),
    newgame: document.getElementById("newgame"),
    difficulty: document.getElementById("difficulty")
  };

  const still = window.matchMedia("(prefers-reduced-motion: reduce)");

  let state = null;
  let busy = false;
  let picked = null;       // which ship is in hand while placing
  let across = true;       // which way it is pointing
  let hovering = null;     // the cell the pointer is over

  // -- building a board ------------------------------------------------------

  // Eleven by eleven: a row of column numbers, a column of row letters, and
  // the hundred squares. Built once each and then only ever re-classed, so a
  // redraw never rebuilds the DOM under the pointer.
  function buildBoard(id, onClick, onHover) {
    const host = document.getElementById(id);
    if (!host || host.dataset.built) return host;
    const frag = document.createDocumentFragment();

    const corner = document.createElement("span");
    corner.className = "label";
    frag.appendChild(corner);
    for (let c = 0; c < SIZE; c++) {
      const s = document.createElement("span");
      s.className = "label";
      s.textContent = String(c + 1);
      frag.appendChild(s);
    }

    host.cells = new Map();
    for (let r = 0; r < SIZE; r++) {
      const s = document.createElement("span");
      s.className = "label";
      s.textContent = LETTERS[r];
      frag.appendChild(s);
      for (let c = 0; c < SIZE; c++) {
        const n = r * SIZE + c;
        const cell = document.createElement("button");
        cell.type = "button";
        cell.className = "cell";
        cell.setAttribute("aria-label", notation(n));

        const heat = document.createElement("span");
        heat.className = "heat";
        cell.appendChild(heat);
        const splash = document.createElement("span");
        splash.className = "splash";
        cell.appendChild(splash);

        if (onClick) cell.addEventListener("click", () => onClick(n));
        if (onHover) {
          cell.addEventListener("mouseenter", () => onHover(n));
          cell.addEventListener("focus", () => onHover(n));
        }
        host.cells.set(n, cell);
        frag.appendChild(cell);
      }
    }
    host.appendChild(frag);
    host.dataset.built = "1";
    if (onHover) host.addEventListener("mouseleave", () => onHover(null));
    return host;
  }

  const ownBoards = ["ownboard", "ownboard2", "ownboard3"];

  // -- drawing ---------------------------------------------------------------

  function clear(cell) {
    cell.classList.remove("ship", "sunk", "wreck", "miss", "hit", "ghost", "bad", "playable");
  }

  // Your own waters: your ships, and where the engine has fired.
  function drawOwn(host) {
    if (!host || !host.cells || !state) return;
    const mine = state.you;
    const sunkShips = new Set();
    mine.ships.forEach((s) => { if (s.sunk) sunkShips.add(s.name); });
    const sunkCells = new Set();
    mine.ships.forEach((s) => {
      if (s.sunk && s.cells) s.cells.forEach((c) => sunkCells.add(c));
    });

    host.cells.forEach((cell, n) => {
      clear(cell);
      if (mine.grid[n] !== 0) {
        cell.classList.add("ship");
        if (sunkCells.has(n)) cell.classList.add("sunk");
      }
      if (mine.shots[n] === 1) cell.classList.add("miss");
      if (mine.shots[n] === 2) cell.classList.add("hit");
      cell.disabled = true;
    });
  }

  // Their waters: only what you have found out, plus the ships you sank.
  function drawEnemy(host) {
    if (!host || !host.cells || !state) return;
    const view = state.engine;
    const wrecks = new Set();
    view.sunk.forEach((s) => s.cells.forEach((c) => wrecks.add(c)));
    const yourTurn = state.phase === "playing" && state.turn === "you" && !busy;
    const heat = state.heat || null;
    const most = heat ? Math.max(1, ...heat) : 1;

    host.classList.toggle("showheat", Boolean(heat) && els.heat.checked);
    host.cells.forEach((cell, n) => {
      clear(cell);
      if (wrecks.has(n)) cell.classList.add("wreck");
      if (view.shots[n] === 1) cell.classList.add("miss");
      if (view.shots[n] === 2) cell.classList.add("hit");
      const open = view.shots[n] === 0;
      if (open && yourTurn) cell.classList.add("playable");
      cell.disabled = !(open && yourTurn);
      const weight = heat && open ? heat[n] / most : 0;
      cell.style.setProperty("--weight", String(weight));
    });
  }

  // The engine's fleet, once the game is over and it may be shown.
  function drawRevealed(host) {
    if (!host || !host.cells || !state || !state.revealed) return;
    const view = state.engine;
    host.cells.forEach((cell, n) => {
      clear(cell);
      if (state.revealed[n] !== 0) cell.classList.add("ship");
      if (view.shots[n] === 1) cell.classList.add("miss");
      if (view.shots[n] === 2) cell.classList.add("hit");
      cell.disabled = true;
    });
  }

  // While placing: your ships so far, plus a ghost of the one in hand.
  function drawPlacing() {
    const host = document.getElementById("ownboard");
    if (!host || !host.cells || !state) return;
    const mine = state.you;
    const preview = picked && hovering !== null ? previewCells(hovering, picked.size, across) : null;
    const clash = preview && preview.some((c) => mine.grid[c] !== 0);

    host.cells.forEach((cell, n) => {
      clear(cell);
      if (mine.grid[n] !== 0) cell.classList.add("ship");
      if (preview && preview.includes(n)) {
        cell.classList.add("ghost");
        if (clash) cell.classList.add("bad");
      }
      cell.classList.add("playable");
      cell.disabled = busy;
      cell.style.setProperty("--weight", "0");
    });
  }

  // The cells a ship would cover, or null if it would run off the edge. This
  // is a drawing, not a ruling: `place` on the server decides, and its refusal
  // is what the player is shown.
  function previewCells(at, size, horizontal) {
    const row = Math.floor(at / SIZE);
    const col = at % SIZE;
    if (horizontal && col + size > SIZE) return null;
    if (!horizontal && row + size > SIZE) return null;
    const out = [];
    for (let i = 0; i < size; i++) out.push(horizontal ? at + i : at + i * SIZE);
    return out;
  }

  function drawTray() {
    if (!state) return;
    els.tray.replaceChildren();
    for (const ship of state.you.ships) {
      const li = document.createElement("li");
      li.className = "list-group-item d-flex align-items-center gap-2";
      const down = ship.cells !== null;
      if (down) li.classList.add("down");
      if (picked && picked.name === ship.name) li.classList.add("picked");

      const name = document.createElement("span");
      name.textContent = ship.name;
      const pips = document.createElement("span");
      pips.className = "pips ms-auto";
      for (let i = 0; i < ship.size; i++) {
        const p = document.createElement("span");
        p.className = "pip";
        pips.appendChild(p);
      }
      const act = document.createElement("button");
      act.type = "button";
      act.className = "btn btn-sm " + (down ? "btn-outline-secondary" : "btn-outline-light");
      act.textContent = down ? "lift" : "place";
      act.addEventListener("click", () => (down ? lift(ship.name) : pick(ship)));

      li.append(name, pips, act);
      els.tray.appendChild(li);
    }
    els.ready.disabled = !state.you.ready || busy;
    els.facing.textContent = across ? "across" : "down";
  }

  function drawFleets() {
    if (!state) return;
    els.enemyfleet.replaceChildren();
    const sunkNames = new Set(state.engine.sunk.map((s) => s.name));
    for (const ship of state.fleet) {
      els.enemyfleet.appendChild(fleetLine(ship, sunkNames.has(ship.name)));
    }
    els.ownfleet.replaceChildren();
    for (const ship of state.you.ships) {
      els.ownfleet.appendChild(fleetLine(ship, ship.sunk));
    }
  }

  function fleetLine(ship, sunk) {
    const li = document.createElement("li");
    li.className = "list-group-item fleetline" + (sunk ? " sunk" : "");
    const name = document.createElement("span");
    name.textContent = ship.name;
    const pips = document.createElement("span");
    pips.className = "pips ms-auto";
    for (let i = 0; i < ship.size; i++) {
      const p = document.createElement("span");
      p.className = "pip" + (sunk ? " gone" : "");
      pips.appendChild(p);
    }
    li.append(name, pips);
    return li;
  }

  function drawLog() {
    if (!state) return;
    const history = state.history || [];
    if (history.length === 0) { els.log.textContent = "nothing yet."; return; }
    const recent = history.slice(-14);
    els.log.textContent = recent.map((e) => {
      const who = e.by === "you" ? "you" : "it";
      const what = e.result === "sunk" ? `sank the ${e.ship}` : e.result;
      return `${who} ${e.where} ${what}`;
    }).join("  ·  ");
  }

  function say(text, tone) {
    els.status.className = "mb-0 " + (tone || "text-body-secondary");
    els.status.textContent = text;
  }

  function draw() {
    if (!state) return;
    els.placing.hidden = state.phase !== "placing";
    els.playing.hidden = state.phase !== "playing";
    els.over.hidden = state.phase !== "over";

    if (state.phase === "placing") {
      drawPlacing();
      drawTray();
      say(state.you.ready
        ? "your fleet is out — press ready"
        : "put your five ships out", state.you.ready ? "text-success" : "text-body-secondary");
      return;
    }

    drawOwn(document.getElementById("ownboard2"));
    drawOwn(document.getElementById("ownboard3"));
    drawEnemy(document.getElementById("enemyboard"));
    drawRevealed(document.getElementById("revealedboard"));
    drawFleets();
    drawLog();

    if (state.phase === "over") {
      const won = state.outcome === "you";
      els.outcome.textContent = won ? "You win." : "It wins.";
      els.outcome.className = "h4 mb-1 " + (won ? "text-success" : "text-warning");
      els.tally.textContent =
        `you fired ${state.shots.you}, it fired ${state.shots.engine}. ` +
        `it was playing "${state.difficulty}".`;
      say(won ? "every one of its ships is on the bottom" : "your fleet is gone",
        won ? "text-success" : "text-warning");
      return;
    }

    if (busy) say("it is firing back…");
    else if (state.turn === "you") say("your shot", "text-success");
    else say("it is firing back…");
  }

  // -- talking to the server ---------------------------------------------------

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

  // Applies a server answer, showing each shot in it land before the next.
  async function show(fresh) {
    if (!fresh || !fresh.ok) return false;
    const played = fresh.played || [];
    state = fresh;
    draw();
    for (const shot of played) await land(shot);
    draw();
    return true;
  }

  function land(shot) {
    const host = document.getElementById(shot.by === "you" ? "enemyboard" : "ownboard2");
    const cell = host && host.cells ? host.cells.get(shot.at) : null;
    if (!cell || still.matches) return Promise.resolve();
    const mark = "landed-" + shot.result;
    cell.classList.add(mark);
    return new Promise((done) => setTimeout(() => {
      cell.classList.remove(mark);
      done();
    }, shot.result === "sunk" ? 620 : 420));
  }

  // -- placing ------------------------------------------------------------------

  function pick(ship) {
    picked = picked && picked.name === ship.name ? null : ship;
    drawTray();
    drawPlacing();
  }

  async function lift(name) {
    busy = true;
    const fresh = await post("/api/place", { lift: name });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; draw(); }
    else say((fresh && fresh.error) || "the server refused that", "text-warning");
  }

  async function drop(at) {
    if (!picked || busy) return;
    busy = true;
    const wanted = picked.name;
    const fresh = await post("/api/place", { ship: wanted, at, across });
    busy = false;
    if (fresh && fresh.ok) {
      state = fresh;
      picked = null;
      draw();
    } else {
      // The server's own sentence, verbatim. It knows why.
      say((fresh && fresh.error) || "that will not go there", "text-warning");
      draw();
    }
  }

  function onOwnCell(n) {
    if (!state || state.phase !== "placing") return;
    // Clicking a ship already on the water lifts it off again.
    const here = state.you.grid[n];
    if (here !== 0 && !picked) {
      const ship = state.you.ships.find((s) => s.cells && s.cells.includes(n));
      if (ship) lift(ship.name);
      return;
    }
    drop(n);
  }

  function onOwnHover(n) {
    if (!state || state.phase !== "placing") return;
    hovering = n;
    drawPlacing();
  }

  async function fire(n) {
    if (busy || !state || state.phase !== "playing" || state.turn !== "you") return;
    busy = true;
    draw();
    const fresh = await post("/api/fire", { at: n });
    busy = false;
    if (!(await show(fresh))) {
      say((fresh && fresh.error) || "the server refused that", "text-warning");
      await refresh();
    }
  }

  // -- wiring --------------------------------------------------------------------

  els.rotate.addEventListener("click", () => {
    across = !across;
    drawTray();
    drawPlacing();
  });

  document.addEventListener("keydown", (e) => {
    if (e.key !== "r" && e.key !== "R") return;
    if (!state || state.phase !== "placing") return;
    across = !across;
    drawTray();
    drawPlacing();
  });

  els.auto.addEventListener("click", async () => {
    busy = true;
    const fresh = await post("/api/place", { auto: true });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; draw(); }
  });

  els.ready.addEventListener("click", async () => {
    busy = true;
    const fresh = await post("/api/ready", {});
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; draw(); }
    else say((fresh && fresh.error) || "not yet", "text-warning");
  });

  els.heat.addEventListener("change", () => drawEnemy(document.getElementById("enemyboard")));

  els.newgame.addEventListener("submit", async (e) => {
    e.preventDefault();
    busy = true;
    const fresh = await post("/api/new", { difficulty: els.difficulty.value });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; hovering = null; draw(); }
  });

  buildBoard("ownboard", onOwnCell, onOwnHover);
  ownBoards.slice(1).forEach((id) => buildBoard(id, null, null));
  buildBoard("enemyboard", fire, null);
  buildBoard("revealedboard", null, null);
  refresh();
})();
