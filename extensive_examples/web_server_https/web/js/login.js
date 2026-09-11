// login.js — the sign-in form.
import { api, loadMe, remember, say, safeNext } from "/js/api.js";

const form = document.getElementById("login");
const error = document.getElementById("error");
const submit = document.getElementById("submit");
const next = new URLSearchParams(location.search).get("next");

const already = await loadMe();
if (already) location.replace(safeNext(next, already.defaults.landing || "/"));

document.getElementById("username").focus();

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const username = form.username.value.trim().toLowerCase();
    const password = form.password.value;
    if (!username || !password) {
        say(error, "Both fields, please.");
        return;
    }
    submit.disabled = true;
    say(error, "");
    try {
        const data = await api("/api/login", { method: "POST", body: { username, password } });
        remember(data);
        location.replace(safeNext(next, data.user.defaults.landing || "/"));
    } catch (e) {
        say(error, e.message);
        form.password.value = "";
        form.password.focus();
    } finally {
        submit.disabled = false;
    }
});
