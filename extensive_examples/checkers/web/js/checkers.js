// checkers.js — the board, and clicking on it. No framework, no dependencies.
//
// The page knows no rules. Every legal move arrives with the state, so a click
// is only ever "is this square in the list I was given" — which means the page
// and the engine can never disagree about whether a capture was compulsory,
// and a page that got out of step redraws instead of guessing.

(function () {
  "use strict";

  var state = null;
  var picked = null;      // the square a piece was picked up from
  var busy = false;

  var els = {
    board: document.getElementById("board"),
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

  // Square n (1-32) to its row and column, the same arithmetic rules.funny
  // does: only dark squares are playable, and on even rows those are the odd
  // columns.
  function rowOf(n) { return Math.floor((n - 1) / 4); }
  function colOf(n) {
    var row = rowOf(n);
    var offset = (n - 1) % 4;
    return row % 2 === 0 ? offset * 2 + 1 : offset * 2;
  }

  function status(text, cls) {
    els.status.textContent = text;
    els.status.className = "status" + (cls ? " " + cls : "");
  }

  function movesFrom(square) {
    if (!state) return [];
    return state.legal.filter(function (m) { return m.from === square; });
  }

  function moveBetween(from, to) {
    return state.legal.filter(function (m) { return m.from === from && m.to === to; })[0] || null;
  }

  function draw() {
    if (!state) return;
    els.board.textContent = "";

    var targets = picked === null ? [] : movesFrom(picked).map(function (m) { return m.to; });
    var captureTargets = picked === null ? [] : movesFrom(picked)
      .filter(function (m) { return m.captures.length > 0; })
      .map(function (m) { return m.to; });

    var lastSquares = [];
    state.legal.forEach(function () { /* no-op: keeps the closure honest */ });

    for (var row = 0; row < 8; row++) {
      for (var col = 0; col < 8; col++) {
        var dark = (row + col) % 2 === 1;
        var n = dark ? row * 4 + Math.floor(col / 2) + 1 : 0;
        var cell;

        if (!dark) {
          cell = document.createElement("div");
          cell.className = "square light";
          els.board.appendChild(cell);
          continue;
        }

        cell = document.createElement("button");
        cell.type = "button";
        cell.className = "square dark playable";
        cell.setAttribute("aria-label", "square " + n);

        var num = document.createElement("span");
        num.className = "num";
        num.textContent = String(n);
        cell.appendChild(num);

        var piece = state.squares[n];
        if (piece) {
          var disc = document.createElement("span");
          var red = piece === "r" || piece === "R";
          disc.className = "piece " + (red ? "red" : "black");
          if (piece === "R" || piece === "B") disc.textContent = "K";
          cell.appendChild(disc);
        }

        if (picked === n) cell.className += " picked";
        if (targets.indexOf(n) >= 0) {
          cell.className += " target";
          if (captureTargets.indexOf(n) >= 0) cell.className += " capture";
        }
        if (lastSquares.indexOf(n) >= 0) cell.className += " wasmoved";

        cell.disabled = busy || state.outcome !== null || state.turn !== state.human;
        cell.addEventListener("click", onSquare.bind(null, n));
        els.board.appendChild(cell);
      }
    }

    els.you.textContent = state.human;
    els.engine.textContent = state.human === "red" ? "black" : "red";
    els.redcount.textContent = state.red.men + " + " + state.red.kings + " kings";
    els.blackcount.textContent = state.black.men + " + " + state.black.kings + " kings";
    els.lastmove.textContent = state.last_engine ? state.last_engine : "—";
    els.nodes.textContent = state.nodes ? state.nodes.toLocaleString() + " positions" : "—";
    els.verdict.textContent = state.verdict || "—";

    if (state.outcome === "draw") status("a draw.", "over");
    else if (state.outcome) status(state.outcome + " wins.", "over");
    else if (busy) status("thinking…", "thinking");
    else if (state.turn === state.human) {
      status(state.forced ? "your move, and you must capture" : "your move", "yours");
    } else status("thinking…", "thinking");
  }

  function onSquare(n) {
    if (busy || !state || state.outcome !== null || state.turn !== state.human) return;

    if (picked !== null) {
      var move = moveBetween(picked, n);
      if (move) { send(move); return; }
    }
    // Picking up: only a square this side can actually move from.
    if (movesFrom(n).length > 0) picked = (picked === n ? null : n);
    else picked = null;
    draw();
  }

  function send(move) {
    busy = true;
    picked = null;
    draw();
    post("/api/move", { from: move.from, to: move.to }).then(function (fresh) {
      busy = false;
      if (fresh && fresh.ok) { state = fresh; draw(); }
      else { status(fresh && fresh.error ? fresh.error : "the server refused that", "over"); refresh(); }
    });
  }

  function post(path, body) {
    return fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    }).then(function (r) { return r.json(); }).catch(function () { return null; });
  }

  function refresh() {
    return fetch("/api/state").then(function (r) { return r.json(); }).then(function (fresh) {
      if (fresh && fresh.ok) { state = fresh; picked = null; draw(); }
    }).catch(function () { status("cannot reach the server", "over"); });
  }

  els.newgame.addEventListener("submit", function (e) {
    e.preventDefault();
    busy = true;
    status("dealing…", "thinking");
    post("/api/new", { depth: Number(els.depth.value) }).then(function (fresh) {
      busy = false;
      if (fresh && fresh.ok) { state = fresh; picked = null; draw(); }
    });
  });

  refresh();
})();
