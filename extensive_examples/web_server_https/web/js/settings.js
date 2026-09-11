// settings.js — profile, defaults and password.
import { api, loadMe, remember, say } from "/js/api.js";

const user = await loadMe();
if (!user) location.replace("/login?next=/settings");

const profile = document.getElementById("profile");
const defaults = document.getElementById("defaults");
const password = document.getElementById("password");
const banner = document.getElementById("default-password");

function fill(u) {
    document.getElementById("whoami").textContent = `Signed in as ${u.username} (${u.role}).`;
    profile.display_name.value = u.display_name;
    profile.email.value = u.email;
    defaults.theme.value = u.defaults.theme;
    defaults.page_size.value = String(u.defaults.page_size);
    defaults.landing.value = u.defaults.landing;
    banner.hidden = !u.default_password;
}

// Shared by the three forms: disable while saving, then say how it went.
function wire(form, send) {
    const status = form.querySelector(".status");
    const button = form.querySelector("button[type=submit]");
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        button.disabled = true;
        say(status, "Saving…");
        try {
            const message = await send();
            say(status, message, "good");
        } catch (e) {
            say(status, e.message, "bad");
        } finally {
            button.disabled = false;
        }
    });
}

function changedText(changed) {
    return changed.length ? `Saved: ${changed.join(", ")}.` : "Nothing had changed.";
}

wire(profile, async () => {
    const data = await api("/api/profile", {
        method: "POST",
        body: { display_name: profile.display_name.value, email: profile.email.value },
    });
    remember(data);
    return changedText(data.changed);
});

wire(defaults, async () => {
    const data = await api("/api/defaults", {
        method: "POST",
        body: {
            theme: defaults.theme.value,
            page_size: Number(defaults.page_size.value),
            landing: defaults.landing.value,
        },
    });
    remember(data);
    return changedText(data.changed);
});

wire(password, async () => {
    if (password.new.value !== password.confirm.value) throw new Error("The new passwords don't match.");
    if (password.new.value.length < 10) throw new Error("At least 10 characters, please.");
    await api("/api/password", {
        method: "POST",
        body: { current: password.current.value, new: password.new.value },
    });
    password.reset();
    banner.hidden = true;
    return "Password changed. Other sessions have been signed out.";
});

if (user) fill(user);
