// chat.js — the page, with no framework and no dependencies.
//
// One WebSocket to /ws on the same origin. Everything in both directions is
// one JSON object per text frame:
//
//   out  {op:"join", user, room} {op:"say", text} {op:"typing", on} {op:"who"}
//   in   {ok:true, ...} replies, and {push:"said"|"joined"|"left"|"typing", ...}
//
// Reconnects with backoff, because a laptop lid closing is not an error worth
// making somebody reload for.

(function () {
  "use strict";

  var socket = null;
  var joined = null;          // {user, room} once the server has agreed
  var backoff = 500;          // milliseconds, doubling to a ceiling
  var MAX_BACKOFF = 15000;
  var typingTimer = null;
  var sentTyping = false;
  var typingNow = [];

  var els = {
    status: document.getElementById("status"),
    join: document.getElementById("join"),
    user: document.getElementById("user"),
    room: document.getElementById("room"),
    chat: document.getElementById("chat"),
    say: document.getElementById("say"),
    text: document.getElementById("text"),
    log: document.getElementById("log"),
    who: document.getElementById("who"),
    typing: document.getElementById("typing")
  };

  function status(text, cls) {
    els.status.textContent = text;
    els.status.className = "status" + (cls ? " " + cls : "");
  }

  function clockOf(at) {
    var d = new Date((at || 0) * 1000);
    var h = String(d.getHours()).padStart(2, "0");
    var m = String(d.getMinutes()).padStart(2, "0");
    return h + ":" + m;
  }

  // textContent everywhere, never innerHTML: a message is text somebody else
  // typed, and the one thing it must never be is markup.
  function line(parts, cls) {
    var li = document.createElement("li");
    if (cls) li.className = cls;
    parts.forEach(function (p) {
      var span = document.createElement("span");
      if (p.cls) span.className = p.cls;
      span.textContent = p.text;
      li.appendChild(span);
    });
    els.log.appendChild(li);
    var box = els.log.parentElement;
    box.scrollTop = box.scrollHeight;
  }

  function said(entry) {
    line([
      { text: clockOf(entry.at) + " ", cls: "at" },
      { text: entry.user + ": ", cls: "who-said" },
      { text: entry.text }
    ]);
  }

  function event(text, at) {
    line([{ text: clockOf(at) + " ", cls: "at" }, { text: text }], "event");
  }

  function showWho(names) {
    els.who.textContent = "";
    (names || []).forEach(function (name) {
      var li = document.createElement("li");
      li.textContent = name;
      els.who.appendChild(li);
    });
  }

  function showTyping() {
    var others = typingNow.filter(function (n) { return !joined || n !== joined.user; });
    if (others.length === 0) els.typing.textContent = "";
    else if (others.length === 1) els.typing.textContent = others[0] + " is typing…";
    else els.typing.textContent = others.join(", ") + " are typing…";
  }

  function send(obj) {
    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(obj));
      return true;
    }
    return false;
  }

  function connect() {
    var scheme = location.protocol === "https:" ? "wss:" : "ws:";
    socket = new WebSocket(scheme + "//" + location.host + "/ws");

    socket.onopen = function () {
      backoff = 500;
      status("connected", "live");
      if (joined) send({ op: "join", user: joined.user, room: joined.room });
    };

    socket.onmessage = function (ev) {
      var msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }

      if (msg.push === "said") { said(msg); return; }
      if (msg.push === "joined") {
        event(msg.user + " joined", msg.at);
        showWho(msg.who);
        return;
      }
      if (msg.push === "left") {
        event(msg.user + " left", msg.at);
        typingNow = typingNow.filter(function (n) { return n !== msg.user; });
        showWho(msg.who);
        showTyping();
        return;
      }
      if (msg.push === "typing") {
        typingNow = typingNow.filter(function (n) { return n !== msg.user; });
        if (msg.on) typingNow.push(msg.user);
        showTyping();
        return;
      }

      // A reply to something this page asked.
      if (msg.ok === false) { status(msg.error || "refused", "gone"); return; }
      if (msg.ok === true && msg.history) {
        els.log.textContent = "";
        msg.history.forEach(said);
        showWho(msg.who);
        els.chat.hidden = false;
        els.say.hidden = false;
        els.join.hidden = true;
        els.text.focus();
        return;
      }
      if (msg.ok === true && msg.who) showWho(msg.who);
    };

    socket.onclose = function () {
      status("reconnecting…", "gone");
      setTimeout(connect, backoff);
      backoff = Math.min(backoff * 2, MAX_BACKOFF);
    };

    socket.onerror = function () { /* onclose follows, and handles it */ };
  }

  els.join.addEventListener("submit", function (e) {
    e.preventDefault();
    var user = els.user.value.trim();
    var room = els.room.value.trim() || "lobby";
    if (!user) return;
    joined = { user: user, room: room };
    if (!send({ op: "join", user: user, room: room })) status("not connected yet", "gone");
  });

  els.say.addEventListener("submit", function (e) {
    e.preventDefault();
    var text = els.text.value.trim();
    if (!text) return;
    send({ op: "say", text: text });
    els.text.value = "";
    if (sentTyping) { send({ op: "typing", on: false }); sentTyping = false; }
  });

  // Typing is a flag, not a keystroke stream: it is sent when it changes and
  // cleared after a pause, so a fast typist sends two messages, not fifty.
  els.text.addEventListener("input", function () {
    if (!sentTyping && els.text.value.length > 0) {
      send({ op: "typing", on: true });
      sentTyping = true;
    }
    clearTimeout(typingTimer);
    typingTimer = setTimeout(function () {
      if (sentTyping) { send({ op: "typing", on: false }); sentTyping = false; }
    }, 1500);
  });

  connect();
})();
