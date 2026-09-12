// checkers.js — the board, the clicking, and the moving.
//
// The page knows no rules. Every legal move arrives with the state, so a click
// is only ever "is this square in the list I was given", and the page and the
// engine can never disagree about whether a capture was compulsory. A page
// that falls out of step asks for the state again rather than guessing.
//
// WHY PIECES ARE THEIR OWN LAYER. The squares are a grid and never move. The
// pieces sit above them, absolutely placed, and are put where they belong with
// a transform. Moving one is then a matter of animating that transform, so a
// piece slides to its square and hops over what it takes -- which is the whole
// difference between seeing a move and being shown a new position.
//
// The server sends `played`: the moves since the page last looked, each with
// the squares it travelled through. The destination alone would not do, because
// a double jump has to be seen to hop twice.

(() => {
  "use strict";

  const SQUARE = 12.5;                 // one square, as a percentage of the board
  const els = {
    squares: document.getElementById("squares"),
    pieces: document.getElementById("pieces"),
    status: document.getElementById("status"),
    you: document.getElementById("you"),
    engine: document.getElementById("engine"),
    redcount: document.getElementById("redcount"),
    blackcount: document.getElementById("blackcount"),
    lastmove: document.getElementById("lastmove"),
    nodes: document.getElementById("nodes"),
    verdict: document.getElementById("verdict"),
    newgame: document.getElementById("newgame"),
    depth: document.getElementById("depth")
  };

  const still = window.matchMedia("(prefers-reduced-motion: reduce)");

  let state = null;
  let picked = null;                   // the square a piece was picked up from
  let busy = false;
  const discs = new Map();             // square number -> the element standing on it
  const cells = new Map();             // square number -> its button
  let trail = [];                      // squares the last move touched

  // Square 1-32 to row and column, the same arithmetic rules.funny does: only
  // dark squares are playable, and on even rows those are the odd columns.
  const rowOf = (n) => Math.floor((n - 1) / 4);
  const colOf = (n) => (rowOf(n) % 2 === 0 ? ((n - 1) % 4) * 2 + 1 : ((n - 1) % 4) * 2);
  const at = (n) => `translate(${colOf(n) * 100}%, ${rowOf(n) * 100}%)`;

  // -- the squares, built once --------------------------------------------

  function buildSquares() {
    const frag = document.createDocumentFragment();
    for (let row = 0; row < 8; row++) {
      for (let col = 0; col < 8; col++) {
        const dark = (row + col) % 2 === 1;
        if (!dark) {
          const light = document.createElement("div");
          light.className = "sq";
          frag.appendChild(light);
          continue;
        }
        const n = row * 4 + Math.floor(col / 2) + 1;
        const cell = document.createElement("button");
        cell.type = "button";
        cell.className = "sq dark playable";
        cell.setAttribute("aria-label", `square ${n}`);
        const num = document.createElement("span");
        num.className = "n";
        num.textContent = String(n);
        cell.appendChild(num);
        cell.addEventListener("click", () => onSquare(n));
        cells.set(n, cell);
        frag.appendChild(cell);
      }
    }
    els.squares.appendChild(frag);
  }

  // -- the pieces ----------------------------------------------------------

  function makeDisc(piece, n) {
    const el = document.createElement("div");
    el.className = "piece " + (piece === "r" || piece === "R" ? "red" : "black");
    el.style.transform = at(n);
    if (piece === "R" || piece === "B") {
      const k = document.createElement("span");
      k.className = "king";
      k.textContent = "K";
      el.appendChild(k);
    }
    els.pieces.appendChild(el);
    return el;
  }

  // Rebuilds the piece layer from a board. Used at the start, after a new
  // game, and to settle any drift once the animations have finished.
  function settle(squares) {
    discs.forEach((el) => el.remove());
    discs.clear();
    for (let n = 1; n <= 32; n++) {
      const piece = squares[n];
      if (piece) discs.set(n, makeDisc(piece, n));
    }
  }

  // One move, shown. The piece travels through every square on its path,
  // lifting between each pair so a jump looks like a jump.
  async function animate(mv) {
    const el = discs.get(mv.from);
    if (!el) return;

    const path = mv.path && mv.path.length > 1 ? mv.path : [mv.from, mv.to];
    const jumping = mv.captures && mv.captures.length > 0;

    if (!still.matches) {
      const frames = [];
      for (let i = 0; i < path.length; i++) {
        if (i > 0) {
          // A lifted midpoint between each pair of squares. Halfway across,
          // and a little above the board.
          const midCol = (colOf(path[i - 1]) + colOf(path[i])) / 2;
          const midRow = (rowOf(path[i - 1]) + rowOf(path[i])) / 2;
          const lift = jumping ? 34 : 14;
          frames.push({
            transform: `translate(${midCol * 100}%, ${midRow * 100}%) translateY(-${lift}%) scale(1.06)`,
            offset: (i - 0.5) / (path.length - 1)
          });
        }
        frames.push({ transform: at(path[i]), offset: i / (path.length - 1) });
      }

      el.style.zIndex = "2";
      const run = el.animate(frames, {
        duration: Math.round(220 * (path.length - 1) + 90),
        easing: "cubic-bezier(.32,.72,.35,1)",
        fill: "none"
      });

      // Each captured piece goes as the jumper passes over it.
      (mv.captures || []).forEach((square, i) => {
        const victim = discs.get(square);
        if (!victim) return;
        setTimeout(() => victim.classList.add("taken"), 140 + i * 220);
      });

      await run.finished.catch(() => {});
      el.style.zIndex = "";
    }

    el.style.transform = at(mv.to);
    discs.delete(mv.from);
    (mv.captures || []).forEach((square) => {
      const victim = discs.get(square);
      if (victim) victim.remove();
      discs.delete(square);
    });
    discs.set(mv.to, el);

    if (mv.promotes && !el.querySelector(".king")) {
      const k = document.createElement("span");
      k.className = "king";
      k.textContent = "K";
      el.appendChild(k);
    }
    trail = path.slice();
  }

  // -- drawing everything that is not a piece --------------------------------

  function movesFrom(square) {
    return state ? state.legal.filter((m) => m.from === square) : [];
  }

  function moveBetween(from, to) {
    return state ? state.legal.find((m) => m.from === from && m.to === to) || null : null;
  }

  function draw() {
    if (!state) return;
    const mine = state.turn === state.human && state.outcome === null && !busy;
    const targets = picked === null ? [] : movesFrom(picked);

    cells.forEach((cell, n) => {
      cell.classList.toggle("picked", picked === n);
      const t = targets.find((m) => m.to === n);
      cell.classList.toggle("target", Boolean(t));
      cell.classList.toggle("capture", Boolean(t && t.captures.length > 0));
      cell.classList.toggle("trail", trail.includes(n));
      cell.disabled = !mine;
    });

    els.you.textContent = state.human;
    els.engine.textContent = state.human === "red" ? "black" : "red";
    els.redcount.textContent = `${state.red.men} + ${state.red.kings} kings`;
    els.blackcount.textContent = `${state.black.men} + ${state.black.kings} kings`;
    els.lastmove.textContent = state.last_engine || "—";
    els.nodes.textContent = state.nodes ? `${state.nodes.toLocaleString()} positions` : "—";
    els.verdict.textContent = state.verdict || "—";

    const s = els.status;
    s.className = "mb-0";
    if (state.outcome === "draw") { s.textContent = "a draw."; s.classList.add("text-warning"); }
    else if (state.outcome) { s.textContent = `${state.outcome} wins.`; s.classList.add("text-warning"); }
    else if (busy) { s.textContent = "thinking…"; s.classList.add("text-body-secondary"); }
    else if (mine) {
      s.textContent = state.forced ? "your move, and you must capture" : "your move";
      s.classList.add("text-success");
    } else { s.textContent = "thinking…"; s.classList.add("text-body-secondary"); }
  }

  // -- talking to the server -------------------------------------------------

  async function show(fresh) {
    if (!fresh || !fresh.ok) return false;
    const played = fresh.played || [];
    state = fresh;
    picked = null;
    for (const mv of played) await animate(mv);
    settle(fresh.squares);      // corrects any drift, and crowns anything crowned
    draw();
    return true;
  }

  async function post(path, body) {
    try {
      const r = await fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      return await r.json();
    } catch { return null; }
  }

  async function refresh() {
    try {
      const r = await fetch("/api/state");
      const fresh = await r.json();
      if (fresh && fresh.ok) { state = fresh; picked = null; trail = []; settle(fresh.squares); draw(); }
    } catch {
      els.status.textContent = "cannot reach the server";
      els.status.className = "mb-0 text-warning";
    }
  }

  function onSquare(n) {
    if (busy || !state || state.outcome !== null || state.turn !== state.human) return;
    if (picked !== null) {
      const move = moveBetween(picked, n);
      if (move) { send(move); return; }
    }
    picked = movesFrom(n).length > 0 && picked !== n ? n : null;
    draw();
  }

  async function send(move) {
    busy = true;
    picked = null;
    trail = [];
    draw();
    const fresh = await post("/api/move", { from: move.from, to: move.to });
    busy = false;
    if (!(await show(fresh))) {
      els.status.textContent = (fresh && fresh.error) || "the server refused that";
      els.status.className = "mb-0 text-warning";
      await refresh();
    }
  }

  els.newgame.addEventListener("submit", async (e) => {
    e.preventDefault();
    busy = true;
    trail = [];
    draw();
    const fresh = await post("/api/new", { depth: Number(els.depth.value) });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; settle(fresh.squares); draw(); }
  });

  buildSquares();
  refresh();
})();
