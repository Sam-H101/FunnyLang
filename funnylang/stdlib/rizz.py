"""`rizz` — randomness (PLAN.md §M6 task 4)."""
from __future__ import annotations

import random
import uuid as _uuid_mod

from ..errors import TypeVibeMismatch
from ..values import GHOST, Module, NativeFn, Stash, type_name


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _roll(vm, a):
    lo, hi = int(a[0]), int(a[1])
    if lo > hi:
        lo, hi = hi, lo
    return random.randint(lo, hi)


def _float_roll(vm, a):
    return random.random()


def _pick(vm, a):
    s = a[0]
    if not isinstance(s, Stash):
        raise TypeVibeMismatch(f"'pick' needs a stash, not a {type_name(s)}.")
    if not s.items:
        raise TypeVibeMismatch("'pick' on an empty stash.")
    return random.choice(s.items)


def _shuffle(vm, a):
    s = a[0]
    if not isinstance(s, Stash):
        raise TypeVibeMismatch(f"'shuffle' needs a stash, not a {type_name(s)}.")
    random.shuffle(s.items)
    return s


def _coinflip(vm, a):
    return random.random() < 0.5


def _seed(vm, a):
    random.seed(a[0] if a else None)
    return GHOST


def _uuid(vm, a):
    return str(_uuid_mod.uuid4())


def _gamble(vm, a):
    odds = a[0]
    if not isinstance(odds, (int, float)) or isinstance(odds, bool):
        raise TypeVibeMismatch(f"'gamble' needs a numba, not a {type_name(odds)}.")
    return random.random() < odds


def build() -> Module:
    members = {
        "roll": _nf("roll", _roll, 2),
        "float_roll": _nf("float_roll", _float_roll, 0),
        "pick": _nf("pick", _pick, 1),
        "shuffle": _nf("shuffle", _shuffle, 1),
        "coinflip": _nf("coinflip", _coinflip, 0),
        "seed": _nf("seed", _seed, 0, 1),
        "uuid": _nf("uuid", _uuid, 0),
        "gamble": _nf("gamble", _gamble, 1),
    }
    return Module("rizz", members)
