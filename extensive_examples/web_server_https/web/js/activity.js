// activity.js — the activity table: filters, paging, and a gentle refresh.
import { api, loadMe, el, say } from "/js/api.js";

const user = await loadMe();
if (!user) location.replace("/login?next=/activity");

const rows = document.getElementById("rows");
const empty = document.getElementById("empty");
const summary = document.getElementById("summary");
const error = document.getElementById("error");
const userSelect = document.getElementById("user");
const actionSelect = document.getElementById("action");
const prev = document.getElementById("prev");
const next = document.getElementById("next");
const live = document.getElementById("live");

let page = 1;
let pages = 1;

// Keeps a <select>'s options in step with what the log contains, without
// losing what is selected.
function syncOptions(select, values, allLabel) {
    const chosen = select.value;
    select.replaceChildren(el("option", { value: "" }, allLabel));
    for (const value of values) select.append(el("option", { value }, value.replaceAll("_", " ")));
    select.value = values.includes(chosen) ? chosen : "";
}

function when(iso) {
    const date = new Date(iso);
    if (Number.isNaN(date.getTime())) return iso;
    return date.toLocaleString(undefined, { dateStyle: "medium", timeStyle: "medium" });
}

function row(entry) {
    return el("tr", {},
        el("td", { class: "num" }, when(entry.at)),
        el("td", {}, entry.user),
        el("td", {}, el("span", { class: `tag ${entry.action}` }, entry.action.replaceAll("_", " "))),
        el("td", { class: "detail" }, entry.detail),
        el("td", { class: "num" }, entry.status || "–"),
        el("td", { class: "num" }, entry.thread ? `#${entry.thread}` : "–"),
        el("td", { class: "mono", title: entry.agent || "" }, entry.ip || "–"));
}

async function load() {
    const query = new URLSearchParams({ page: String(page), user: userSelect.value, action: actionSelect.value });
    try {
        const data = await api(`/api/activity?${query}`);
        say(error, "");
        pages = data.pages;
        page = data.page;
        rows.replaceChildren(...data.entries.map(row));
        empty.hidden = data.entries.length > 0;
        document.getElementById("user-field").hidden = !data.all_users;
        syncOptions(userSelect, data.users, "Everybody");
        syncOptions(actionSelect, data.actions, "Everything");
        const first = data.total === 0 ? 0 : (page - 1) * data.page_size + 1;
        const last = Math.min(page * data.page_size, data.total);
        summary.textContent = `${first}–${last} of ${data.total} · page ${page} of ${pages}`;
        prev.disabled = page <= 1;
        next.disabled = page >= pages;
    } catch (e) {
        if (e.status === 401) location.replace("/login?next=/activity");
        say(error, e.message);
    }
}

document.getElementById("refresh").addEventListener("click", load);
prev.addEventListener("click", () => { page = Math.max(1, page - 1); load(); });
next.addEventListener("click", () => { page = Math.min(pages, page + 1); load(); });
userSelect.addEventListener("change", () => { page = 1; load(); });
actionSelect.addEventListener("change", () => { page = 1; load(); });

// Only the first page refreshes itself: somebody reading page four does not
// want it to shift under them.
setInterval(() => {
    if (live.checked && page === 1 && document.visibilityState === "visible") load();
}, 5000);

if (user) load();
