// grab.js — the page's half. No framework, and no logic the server does not
// already own: this file asks, draws and polls. Which renditions exist, which
// of them plays, what the file is called and how far along it is are all
// decided in FunnyLang and sent here, so the page cannot drift from the
// program.

"use strict";

const $ = (id) => document.getElementById(id);

const els = {
    form: $("find"), url: $("url"), look: $("look"),
    problem: $("problem"), song: $("song"), cover: $("cover"),
    title: $("title"), by: $("by"), style: $("style"),
    via: $("via"), length: $("length"), model: $("model"),
    format: $("format"), grab: $("grab"), note: $("note"),
    from: $("from"), to: $("to"), cuthint: $("cuthint"), disguise: $("disguise"),
    work: $("work"), fill: $("fill"), progress: $("progress"),
    save: $("save"), warning: $("warning"),
};

let current = null;   // the resolved song
let polling = null;   // the interval id, while a download runs

// -- talking to the server --------------------------------------------------

async function ask(path, options) {
    const answer = await fetch(path, options);
    let body = null;
    try {
        body = await answer.json();
    } catch (e) {
        throw new Error(`the server said ${answer.status} and not JSON`);
    }
    if (!answer.ok) {
        throw new Error(body && body.error ? body.error : `the server said ${answer.status}`);
    }
    return body;
}

const post = (path, payload) => ask(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
});

// -- drawing ----------------------------------------------------------------

function complain(text) {
    els.problem.textContent = text;
    els.problem.hidden = false;
}

function clearComplaint() {
    els.problem.hidden = true;
    els.problem.textContent = "";
}

function seconds(n) {
    if (n === null || n === undefined) return "unknown";
    const whole = Math.round(n);
    return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, "0")}`;
}

function bytes(n) {
    if (n === null || n === undefined) return "";
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
    return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

function showSong(song) {
    current = song;
    els.title.textContent = song.title || "untitled";
    els.by.textContent = song.artist ? `by ${song.artist}` : "";
    els.style.textContent = song.tags || "";
    els.via.textContent = song.via + (song.via === "guess" ? " (worth a look)" : "");
    els.length.textContent = seconds(song.duration);
    els.model.textContent = song.model || "—";

    if (song.cover) {
        els.cover.src = song.cover;
        els.cover.alt = `cover art for ${song.title || "this song"}`;
        els.cover.hidden = false;
    } else {
        els.cover.hidden = true;
        els.cover.removeAttribute("src");
    }

    // The server says which renditions exist and which of them actually plays.
    // The page only renders that answer.
    els.format.textContent = "";
    const plays = song.renditions.filter((r) => r.plays);
    const rest = song.renditions.filter((r) => !r.plays);
    for (const r of plays.concat(rest)) {
        const option = document.createElement("option");
        option.value = r.kind;
        option.textContent = r.plays ? r.kind : `${r.kind} — encrypted, will not play`;
        els.format.appendChild(option);
    }
    els.note.textContent = plays.length
        ? "Suno publishes no mp3 for a shared link; this is what it does publish."
        : "nothing this program can fetch is published for this link.";
    els.grab.disabled = plays.length === 0;

    els.song.hidden = false;
    checkCut();
}

// "90", "1:30" or "1:02:03" into seconds; null for empty, NaN for nonsense.
// Kept here as well as on the server because the page can say "that is not a
// time" without a round trip, and the server still checks for itself.
function asSeconds(text) {
    const t = text.trim();
    if (!t) return null;
    let total = 0;
    for (const part of t.split(":")) {
        if (!/^\d+(\.\d+)?$/.test(part)) return NaN;
        total = total * 60 + parseFloat(part);
    }
    return total;
}

function clock(seconds) {
    const whole = Math.round(seconds);
    return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, "0")}`;
}

function checkCut() {
    const a = asSeconds(els.from.value);
    const b = asSeconds(els.to.value);
    if (Number.isNaN(a) || Number.isNaN(b)) {
        els.cuthint.textContent = "that is not a time — try 1:30, or 90";
        return false;
    }
    if (a !== null && b !== null && b <= a) {
        els.cuthint.textContent = "the end has to come after the start";
        return false;
    }
    if (a === null && b === null) {
        els.cuthint.textContent = "optional — m:ss or seconds";
        return true;
    }
    const length = current && current.duration ? current.duration : null;
    const to = b === null ? length : b;
    els.cuthint.textContent = to === null
        ? `keeping from ${clock(a || 0)}`
        : `keeping ${clock(to - (a || 0))} of the song`;
    return true;
}

els.from.addEventListener("input", checkCut);
els.to.addEventListener("input", checkCut);

function showProgress(job) {
    const known = job.total !== null && job.total !== undefined && job.total > 0;
    const pct = known ? Math.min(100, (job.done / job.total) * 100) : 0;
    els.fill.style.width = `${pct}%`;
    if (job.state === "waiting") {
        els.progress.textContent = "waiting for a turn…";
    } else if (known) {
        els.progress.textContent =
            `${bytes(job.done)} of ${bytes(job.total)} — ${pct.toFixed(0)}%`;
    } else {
        els.progress.textContent = `${bytes(job.done)} so far…`;
    }
}

function showDone(job) {
    els.fill.style.width = "100%";
    const kept = job.seconds_kept ? `, ${clock(job.seconds_kept)} kept` : "";
    els.progress.textContent =
        `${job.name} — ${bytes(job.bytes)}${kept}, tagged ${job.tagged}, in ${job.seconds}s`;
    els.save.href = `/api/file?job=${job.job}`;
    els.save.setAttribute("download", job.name);
    els.save.hidden = false;
    if (job.warning) {
        els.warning.textContent = job.warning;
        els.warning.hidden = false;
    }
}

// -- what the buttons do ----------------------------------------------------

els.form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const url = els.url.value.trim();
    if (!url) return;

    clearComplaint();
    els.song.hidden = true;
    els.work.hidden = true;
    els.look.disabled = true;
    els.look.textContent = "looking…";

    try {
        showSong(await post("/api/resolve", { url }));
    } catch (e) {
        complain(e.message);
    } finally {
        els.look.disabled = false;
        els.look.textContent = "Look it up";
    }
});

els.grab.addEventListener("click", async () => {
    if (!current) return;
    clearComplaint();
    els.grab.disabled = true;
    els.save.hidden = true;
    els.warning.hidden = true;
    els.fill.style.width = "0%";
    els.progress.textContent = "starting…";
    els.work.hidden = false;

    if (!checkCut()) {
        complain("the range to keep does not make sense");
        els.grab.disabled = false;
        els.work.hidden = true;
        return;
    }

    try {
        const ask = {
            url: current.source_url || els.url.value.trim(),
            format: els.format.value,
        };
        const from = asSeconds(els.from.value);
        const to = asSeconds(els.to.value);
        if (from !== null) ask.trim_from = from;
        if (to !== null) ask.trim_to = to;
        if (els.disguise.checked) ask.disguise = true;
        const { job } = await post("/api/grab", ask);
        watch(job);
    } catch (e) {
        complain(e.message);
        els.grab.disabled = false;
        els.work.hidden = true;
    }
});

// Four times a second: fast enough that the bar moves, slow enough that the
// server is answering the page rather than serving it.
function watch(job) {
    if (polling) clearInterval(polling);
    polling = setInterval(async () => {
        let state;
        try {
            state = await ask(`/api/status?job=${job}`);
        } catch (e) {
            clearInterval(polling);
            polling = null;
            complain(e.message);
            els.grab.disabled = false;
            return;
        }
        if (state.state === "done") {
            clearInterval(polling);
            polling = null;
            showDone(state);
            els.grab.disabled = false;
        } else if (state.state === "failed") {
            clearInterval(polling);
            polling = null;
            complain(state.error || "it did not say why");
            els.work.hidden = true;
            els.grab.disabled = false;
        } else {
            showProgress(state);
        }
    }, 250);
}
