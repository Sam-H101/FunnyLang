"""`internet` — networking (PLAN.md §7.10). `urllib` only, no third-party
deps. `FUNNY_NO_NET=1` makes every network call raise a clean SkillIssue
instead of touching the network (CI sets this)."""
from __future__ import annotations

import os
import socket
import time
import urllib.error
import urllib.request

from ..errors import SkillIssue, TypeVibeMismatch
from ..values import GroupChat, Module, NativeFn, Stash, type_name

DEFAULT_TIMEOUT = 10
NET_SAID_NO = "the internet said no. 🚫"


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _net_disabled() -> bool:
    return os.environ.get("FUNNY_NO_NET") == "1"


def _no_net_error() -> SkillIssue:
    return SkillIssue(NET_SAID_NO, roast=NET_SAID_NO)


def _url_str(v, fn_name):
    if not isinstance(v, str):
        raise TypeVibeMismatch(f"'{fn_name}' needs a yapstring URL, not a {type_name(v)}.")
    return v


def _go_brrrr(vm, a):
    if _net_disabled():
        raise _no_net_error()
    url = _url_str(a[0], "go_brrrr")
    opts = a[1] if len(a) > 1 else GroupChat({})
    if not isinstance(opts, GroupChat):
        raise TypeVibeMismatch("'go_brrrr' options need to be a groupchat.")
    method = opts.items.get("method", "GET")
    body = opts.items.get("body")
    headers = opts.items.get("headers", GroupChat({}))
    timeout = opts.items.get("timeout", DEFAULT_TIMEOUT)
    data = body.encode("utf-8") if isinstance(body, str) else None
    header_dict = dict(headers.items) if isinstance(headers, GroupChat) else {}
    req = urllib.request.Request(url, data=data, headers=header_dict, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            resp_body = resp.read().decode("utf-8", errors="replace")
            status = resp.status
            resp_headers = dict(resp.headers.items())
    except (urllib.error.URLError, OSError, ValueError):
        raise _no_net_error()
    return GroupChat({
        "status": status,
        "body": resp_body,
        "headers": GroupChat(resp_headers),
    })


def _is_it_up(vm, a):
    if _net_disabled():
        return False
    url = _url_str(a[0], "is_it_up")
    try:
        with urllib.request.urlopen(url, timeout=DEFAULT_TIMEOUT):
            return True
    except (urllib.error.URLError, OSError, ValueError):
        return False


def _download(vm, a):
    if _net_disabled():
        raise _no_net_error()
    url = _url_str(a[0], "download")
    path = a[1]
    try:
        with urllib.request.urlopen(url, timeout=DEFAULT_TIMEOUT) as resp:
            data = resp.read()
    except (urllib.error.URLError, OSError, ValueError):
        raise _no_net_error()
    with open(path, "wb") as f:
        f.write(data)
    return len(data)


def _speed_test(vm, a):
    if _net_disabled():
        raise _no_net_error()
    url = "https://example.com/"
    try:
        start = time.perf_counter()
        with urllib.request.urlopen(url, timeout=DEFAULT_TIMEOUT) as resp:
            data = resp.read()
        elapsed = max(time.perf_counter() - start, 1e-6)
    except (urllib.error.URLError, OSError, ValueError):
        raise _no_net_error()
    mbps = (len(data) * 8 / 1_000_000) / elapsed
    return f"your internet: {mbps:.2f} mbps. mid."


def _ping(vm, a):
    if _net_disabled():
        raise _no_net_error()
    host = _url_str(a[0], "ping")
    try:
        start = time.perf_counter()
        with socket.create_connection((host, 80), timeout=DEFAULT_TIMEOUT):
            pass
        return (time.perf_counter() - start) * 1000
    except OSError:
        raise _no_net_error()


def build() -> Module:
    members = {
        "go_brrrr": _nf("go_brrrr", _go_brrrr, 1, 2),
        "is_it_up": _nf("is_it_up", _is_it_up, 1),
        "download": _nf("download", _download, 2),
        "speed_test": _nf("speed_test", _speed_test, 0),
        "ping": _nf("ping", _ping, 1),
    }
    return Module("internet", members)
