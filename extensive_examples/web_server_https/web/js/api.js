// api.js — what every page shares: talking to /api/*, who is signed in, the
// theme, and the bar across the top.
//
// Every value that came from the server goes into the page through
// textContent, never innerHTML, so nothing a user typed can become markup.

let me = null;
let csrf = null;

export class ApiError extends Error {
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}

// JSON to and from the server. State-changing requests carry the session's
// CSRF token in a header, which a cross-site page cannot read and so cannot
// forge.
export async function api(path, { method = "GET", body } = {}) {
    const headers = { Accept: "application/json" };
    if (body !== undefined) headers["Content-Type"] = "application/json";
    if (method !== "GET" && csrf) headers["X-CSRF-Token"] = csrf;
    const response = await fetch(path, {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        credentials: "same-origin",
    });
    let data = null;
    try {
        data = await response.json();
    } catch {
        data = null;
    }
    if (!response.ok || (data && data.ok === false)) {
        throw new ApiError(response.status, (data && data.error) || `the server said ${response.status}.`);
    }
    return data;
}

// The signed-in user, or null. Asked once per page.
export async function loadMe() {
    try {
        const data = await api("/api/me");
        remember(data);
    } catch {
        me = null;
        csrf = null;
    }
    applyTheme();
    renderNav();
    return me;
}

// After a sign-in, or anything that changed the user.
export function remember(data) {
    if (data.user) me = data.user;
    if (data.csrf) csrf = data.csrf;
    if ("default_password" in data && me) me.default_password = data.default_password;
    applyTheme();
    renderNav();
}

export function currentUser() {
    return me;
}

export function applyTheme() {
    const theme = me && me.defaults ? me.defaults.theme : "system";
    if (theme === "light" || theme === "dark") document.documentElement.dataset.theme = theme;
    else delete document.documentElement.dataset.theme;
}

export async function signOut() {
    try {
        await api("/api/logout", { method: "POST", body: {} });
    } finally {
        me = null;
        csrf = null;
        location.href = "/";
    }
}

// The right-hand end of the nav: a user chip and a sign-out button, or a
// sign-in link.
function renderNav() {
    const slot = document.querySelector("[data-nav-user]");
    if (!slot) return;
    slot.replaceChildren();
    for (const link of document.querySelectorAll("[data-needs-user]")) link.hidden = !me;
    if (!me) {
        if (location.pathname !== "/login") slot.append(el("a", { class: "btn small", href: "/login" }, "Sign in"));
        return;
    }
    const name = me.display_name || me.username;
    const chip = el("a", { class: "chip", href: "/settings", title: "Settings" },
        el("span", { class: "avatar" }, initials(name)), el("span", {}, name));
    const out = el("button", { class: "btn ghost small", type: "button" }, "Sign out");
    out.addEventListener("click", signOut);
    slot.append(chip, out);
}

function initials(name) {
    const parts = name.trim().split(/\s+/).filter(Boolean);
    const letters = parts.length > 1 ? parts[0][0] + parts[parts.length - 1][0] : name.slice(0, 2);
    return letters.toUpperCase();
}

// A tiny element builder: el("a", {href: "/"}, "text", childElement, ...).
// Strings become text nodes -- never HTML.
export function el(tag, attrs = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs)) {
        if (value === false || value === null || value === undefined) continue;
        node.setAttribute(key, value === true ? "" : String(value));
    }
    for (const child of children) {
        if (child === null || child === undefined) continue;
        node.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return node;
}

// A status line under a form: `kind` is "good", "bad" or "".
export function say(node, text, kind = "") {
    if (!node) return;
    node.textContent = text;
    node.className = node.className.replace(/\b(good|bad|warn)\b/g, "").trim();
    if (kind) node.classList.add(kind);
}

// Only ever redirect to a path on this site: `//evil.example` is a URL on
// someone else's.
export function safeNext(value, fallback) {
    if (typeof value === "string" && value.startsWith("/") && !value.startsWith("//") && !value.includes("\\")) {
        return value;
    }
    return fallback;
}
