// chess.js — the board, the clicking, and the moving.
//
// The page knows no rules. Every legal move arrives with the state, so a click
// is only ever "is this square in the list I was given", and the page and the
// engine can never disagree about whether castling was still allowed. A page
// that falls out of step asks for the state again rather than guessing.
//
// WHY PIECES ARE THEIR OWN LAYER. The squares are a grid and never move. The
// pieces sit above them, absolutely placed, and are put where they belong with
// a transform. Moving one is then a matter of animating that transform, so a
// piece slides to its square -- which is the whole difference between seeing a
// move and being shown a new position.
//
// THE THREE MOVES THAT ARE NOT "FROM HERE TO THERE". Castling moves two pieces,
// so the server sends the rook's squares as well and the rook slides with the
// king. En passant takes a pawn that is not on the square the capturer lands
// on, so the server sends the square to clear rather than letting the page
// assume it is the destination. Promotion changes what the piece is, so the
// glyph is swapped when it arrives. Each of those is a field in `played`,
// because a page that inferred them would be a second implementation of the
// rules, and the two would drift.

(() => {
  "use strict";

  // TWO GLYPH SETS, NOT ONE COLOURED TWO WAYS. The obvious approach -- solid
  // figures throughout, told apart by CSS `color` -- does not survive contact
  // with U+265F BLACK CHESS PAWN, which is a standard emoji. A browser renders
  // it from the emoji font, where `color` means nothing, so both sides' pawns
  // came out the same black picture while every other piece coloured properly.
  //
  // So: the outline figures for white and the solid ones for black, which is
  // how a printed diagram does it, and U+FE0E after each to ask for the text
  // form rather than the emoji one. The two sides now differ in shape as well
  // as in colour, which also means they stay distinct if a font substitutes.
  const TEXT = "︎";
  const WHITE_GLYPH = { k: "♔", q: "♕", r: "♖", b: "♗", n: "♘", p: "♙" };
  const BLACK_GLYPH = { k: "♚", q: "♛", r: "♜", b: "♝", n: "♞", p: "♟" };

  // Upper case is white, the way the board itself is written.
  const isWhite = (piece) => piece === piece.toUpperCase();
  const glyphFor = (piece) =>
    ((isWhite(piece) ? WHITE_GLYPH : BLACK_GLYPH)[piece.toLowerCase()] || "") + TEXT;

  const els = {
    squares: document.getElementById("squares"),
    pieces: document.getElementById("pieces"),
    status: document.getElementById("status"),
    you: document.getElementById("you"),
    engine: document.getElementById("engine"),
    whitecount: document.getElementById("whitecount"),
    blackcount: document.getElementById("blackcount"),
    lastmove: document.getElementById("lastmove"),
    nodes: document.getElementById("nodes"),
    verdict: document.getElementById("verdict"),
    movelist: document.getElementById("movelist"),
    nomoves: document.getElementById("nomoves"),
    newgame: document.getElementById("newgame"),
    depth: document.getElementById("depth"),
    side: document.getElementById("side"),
    promotion: document.getElementById("promotion"),
    cancelpromo: document.getElementById("cancelpromo")
  };

  const still = window.matchMedia("(prefers-reduced-motion: reduce)");

  let state = null;
  let picked = null;              // the square a piece was picked up from
  let pendingPromotion = null;    // {from, to} waiting on which piece to become
  let busy = false;
  const pieces = new Map();       // square number -> the element standing on it
  const cells = new Map();        // square number -> its button
  let trail = [];                 // the squares the last move touched

  // The same arithmetic rules.funny does: a1 is 0, h8 is 63, index is
  // rank * 8 + file. Rank 8 is drawn at the top, so the screen row is flipped.
  const rankOf = (n) => Math.floor(n / 8);
  const fileOf = (n) => n % 8;
  const at = (n) => `translate(${fileOf(n) * 100}%, ${(7 - rankOf(n)) * 100}%)`;
  const nameOf = (n) => "abcdefgh"[fileOf(n)] + (rankOf(n) + 1);

  // -- the squares, built once --------------------------------------------

  function buildSquares() {
    const frag = document.createDocumentFragment();
    for (let row = 0; row < 8; row++) {
      const rank = 7 - row;
      for (let file = 0; file < 8; file++) {
        const n = rank * 8 + file;
        const cell = document.createElement("button");
        cell.type = "button";
        // a1 is dark, and the colours alternate from there.
        cell.className = "sq playable" + ((rank + file) % 2 === 0 ? " dark" : "");
        cell.setAttribute("aria-label", nameOf(n));
        if (row === 7) {
          const f = document.createElement("span");
          f.className = "file";
          f.textContent = "abcdefgh"[file];
          cell.appendChild(f);
        }
        if (file === 0) {
          const r = document.createElement("span");
          r.className = "rank";
          r.textContent = String(rank + 1);
          cell.appendChild(r);
        }
        cell.addEventListener("click", () => onSquare(n));
        cells.set(n, cell);
        frag.appendChild(cell);
      }
    }
    els.squares.appendChild(frag);
  }

  // -- the pieces ----------------------------------------------------------

  function makePiece(piece, n) {
    const el = document.createElement("div");
    el.className = "piece " + (isWhite(piece) ? "white" : "black");
    el.style.transform = at(n);
    const g = document.createElement("span");
    g.className = "glyph";
    g.textContent = glyphFor(piece);
    el.appendChild(g);
    els.pieces.appendChild(el);
    return el;
  }

  // Rebuilds the piece layer from a board. Used at the start, after a new
  // game, and to settle any drift once the animations have finished.
  function settle(squares) {
    pieces.forEach((el) => el.remove());
    pieces.clear();
    for (let n = 0; n < 64; n++) {
      const piece = squares[n];
      if (piece) pieces.set(n, makePiece(piece, n));
    }
  }

  // Slides one element from square to square. Resolves when it has arrived.
  function glide(el, from, to, lift) {
    if (still.matches) return Promise.resolve();
    const midFile = (fileOf(from) + fileOf(to)) / 2;
    const midRow = ((7 - rankOf(from)) + (7 - rankOf(to))) / 2;
    const run = el.animate([
      { transform: at(from), offset: 0 },
      {
        transform: `translate(${midFile * 100}%, ${midRow * 100}%) translateY(-${lift}%) scale(1.05)`,
        offset: 0.5
      },
      { transform: at(to), offset: 1 }
    ], { duration: 230, easing: "cubic-bezier(.32,.72,.35,1)", fill: "none" });
    return run.finished.catch(() => {});
  }

  // One move, shown.
  async function animate(mv) {
    const el = pieces.get(mv.from);
    if (!el) return;

    // A knight is the one piece that goes over things, so it is the one that
    // gets a real arc.
    const knight = mv.piece === "n" || mv.piece === "N";
    el.style.zIndex = "2";
    const travelling = [glide(el, mv.from, mv.to, knight ? 26 : 12)];

    // The rook comes along when the king castles.
    const rook = mv.rook_from >= 0 ? pieces.get(mv.rook_from) : null;
    if (rook) travelling.push(glide(rook, mv.rook_from, mv.rook_to, 10));

    // Whatever was taken goes as the piece arrives on it. For en passant that
    // is not the square being landed on, which is why the server names it.
    if (mv.captured_at >= 0) {
      const victim = pieces.get(mv.captured_at);
      if (victim && !still.matches) setTimeout(() => victim.classList.add("taken"), 120);
    }

    await Promise.all(travelling);
    el.style.zIndex = "";

    el.style.transform = at(mv.to);
    pieces.delete(mv.from);
    if (mv.captured_at >= 0) {
      const victim = pieces.get(mv.captured_at);
      if (victim) victim.remove();
      pieces.delete(mv.captured_at);
    }
    pieces.set(mv.to, el);

    if (rook) {
      rook.style.transform = at(mv.rook_to);
      pieces.delete(mv.rook_from);
      pieces.set(mv.rook_to, rook);
    }

    // The promotion carries its own case, so the new piece keeps its side.
    if (mv.promotion) {
      const g = el.querySelector(".glyph");
      if (g) g.textContent = glyphFor(mv.promotion);
    }
    trail = [mv.from, mv.to];
  }

  // -- drawing everything that is not a piece --------------------------------

  const movesFrom = (square) => (state ? state.legal.filter((m) => m.from === square) : []);
  const movesBetween = (from, to) =>
    (state ? state.legal.filter((m) => m.from === from && m.to === to) : []);

  function drawMoveList() {
    const history = (state && state.history) || [];
    els.movelist.replaceChildren();
    els.nomoves.hidden = history.length > 0;
    for (const notation of history) {
      const li = document.createElement("li");
      li.className = "list-group-item py-1 font-monospace";
      li.textContent = notation;
      els.movelist.appendChild(li);
    }
    els.movelist.scrollTop = els.movelist.scrollHeight;
  }

  function draw() {
    if (!state) return;
    const mine = state.turn === state.human && state.outcome === null && !busy;
    const targets = picked === null ? [] : movesFrom(picked);

    cells.forEach((cell, n) => {
      cell.classList.toggle("picked", picked === n);
      const t = targets.find((m) => m.to === n);
      cell.classList.toggle("target", Boolean(t));
      cell.classList.toggle("capture", Boolean(t && t.capture));
      cell.classList.toggle("trail", trail.includes(n));
      cell.classList.toggle("check", state.check_square === n);
      cell.disabled = !mine;
    });

    els.you.textContent = state.human;
    els.engine.textContent = state.human === "white" ? "black" : "white";
    els.whitecount.textContent = `${state.white} pieces`;
    els.blackcount.textContent = `${state.black} pieces`;
    els.lastmove.textContent = state.last_engine || "—";
    els.nodes.textContent = state.nodes ? `${state.nodes.toLocaleString()} positions` : "—";
    els.verdict.textContent = state.verdict || "—";
    drawMoveList();

    const s = els.status;
    s.className = "mb-0";
    if (state.outcome) {
      s.textContent = state.outcome + ".";
      s.classList.add("text-warning");
    } else if (busy) {
      s.textContent = "thinking…";
      s.classList.add("text-body-secondary");
    } else if (mine) {
      s.textContent = state.check ? "your move — you are in check" : "your move";
      s.classList.add(state.check ? "text-warning" : "text-success");
    } else {
      s.textContent = "thinking…";
      s.classList.add("text-body-secondary");
    }
  }

  // -- talking to the server -------------------------------------------------

  async function show(fresh) {
    if (!fresh || !fresh.ok) return false;
    const played = fresh.played || [];
    state = fresh;
    picked = null;
    for (const mv of played) await animate(mv);
    settle(fresh.squares);      // corrects any drift, and swaps a promoted glyph
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

  function askPromotion(from, to) {
    pendingPromotion = { from, to };
    els.promotion.hidden = false;
    els.status.textContent = "what should the pawn become?";
    els.status.className = "mb-0 text-info";
  }

  function closePromotion() {
    pendingPromotion = null;
    els.promotion.hidden = true;
  }

  function onSquare(n) {
    if (busy || pendingPromotion || !state || state.outcome !== null || state.turn !== state.human) return;
    if (picked !== null) {
      const options = movesBetween(picked, n);
      // Four moves that differ only in what the pawn becomes: that is a
      // question for the player, not something to decide for them.
      if (options.length > 1 && options.every((m) => m.promotion)) { askPromotion(picked, n); return; }
      if (options.length > 0) { send(options[0]); return; }
    }
    picked = movesFrom(n).length > 0 && picked !== n ? n : null;
    draw();
  }

  async function send(move) {
    busy = true;
    picked = null;
    trail = [];
    closePromotion();
    draw();
    const fresh = await post("/api/move", {
      from: move.from, to: move.to, promotion: move.promotion || ""
    });
    busy = false;
    if (!(await show(fresh))) {
      els.status.textContent = (fresh && fresh.error) || "the server refused that";
      els.status.className = "mb-0 text-warning";
      await refresh();
    }
  }

  els.promotion.addEventListener("click", (e) => {
    const btn = e.target.closest("button[data-piece]");
    if (!btn || !pendingPromotion) return;
    const from = pendingPromotion.from;
    const to = pendingPromotion.to;
    const wanted = movesBetween(from, to).find(
      (m) => (m.promotion || "").toLowerCase() === btn.dataset.piece
    );
    if (wanted) send(wanted);
  });

  els.cancelpromo.addEventListener("click", () => { closePromotion(); draw(); });

  els.newgame.addEventListener("submit", async (e) => {
    e.preventDefault();
    busy = true;
    trail = [];
    closePromotion();
    draw();
    const fresh = await post("/api/new", {
      depth: Number(els.depth.value), side: els.side.value
    });
    busy = false;
    if (fresh && fresh.ok) { state = fresh; picked = null; settle(fresh.squares); draw(); }
  });

  buildSquares();
  refresh();
})();
