// landing.js — the live strip on the home page, and the call to action.
import { api, loadMe } from "/js/api.js";

function duration(seconds) {
    if (seconds < 60) return `${seconds}s`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`;
    return `${Math.floor(seconds / 86400)}d ${Math.floor((seconds % 86400) / 3600)}h`;
}

async function refresh() {
    try {
        const health = await api("/api/health");
        document.getElementById("stat-tls").textContent = health.tls.replace("TLSv", "TLS ");
        document.getElementById("stat-thread").textContent = `#${health.thread}`;
        document.getElementById("stat-threads").textContent = health.threads;
        document.getElementById("stat-uptime").textContent = duration(health.uptime_seconds);
        document.getElementById("live").textContent = `Encrypted with ${health.tls.replace("TLSv", "TLS ")}`;
    } catch {
        document.getElementById("live").textContent = "Server unreachable";
    }
}

const user = await loadMe();
if (user) {
    const cta = document.getElementById("cta");
    cta.textContent = "Open your activity";
    cta.href = "/activity";
}
refresh();
// Each refresh is a new connection, so the thread number moves around: that
// is the kernel handing connections to different acceptors.
setInterval(refresh, 5000);
