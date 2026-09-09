"""`clock` — time utilities (PLAN.md §M6 task 4)."""
from __future__ import annotations

import time

from ..values import GHOST, Module, NativeFn


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _now(vm, a):
    return time.time()


def _now_ms(vm, a):
    return int(time.time() * 1000)


def _touch_grass(vm, a):
    time.sleep(a[0] if a else 0)
    return GHOST


def _stopwatch(vm, a):
    start = time.perf_counter()

    def elapsed(inner_vm, inner_args):
        return time.perf_counter() - start

    return NativeFn("elapsed", elapsed, 0)


def _date_yap(vm, a):
    fmt = a[0] if a and a[0] is not GHOST else "%Y-%m-%d %H:%M:%S"
    return time.strftime(fmt)


def build() -> Module:
    members = {
        "now": _nf("now", _now, 0),
        "now_ms": _nf("now_ms", _now_ms, 0),
        "touch_grass": _nf("touch_grass", _touch_grass, 0, 1),
        "stopwatch": _nf("stopwatch", _stopwatch, 0),
        "date_yap": _nf("date_yap", _date_yap, 0, 1),
    }
    return Module("clock", members)
