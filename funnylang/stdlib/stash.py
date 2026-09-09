"""`stash` — array utilities (PLAN.md §M6 task 4), plus the stash
instance-method table used by vm.py's GET_PROP (PLAN.md §3.9)."""
from __future__ import annotations

import functools
import random

from ..errors import OutOfPocket, TypeVibeMismatch
from ..values import GHOST, GroupChat, Module, NativeFn, Stash, funny_eq, is_truthy, type_name


def _s(v, fn_name):
    if not isinstance(v, Stash):
        raise TypeVibeMismatch(f"'{fn_name}' needs a stash, not a {type_name(v)}.")
    return v


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _norm_index(i, n, fn_name):
    idx = i + n if i < 0 else i
    if idx < 0 or idx >= n:
        raise OutOfPocket(f"index {i} on a stash of {n}.", roast=f"index {i} on a stash of {n}. that's straight up out of pocket.")
    return idx


def _how_thicc(vm, a):
    return len(_s(a[0], "how_thicc").items)


def _yeet_in(vm, a):
    arr = _s(a[0], "yeet_in")
    arr.items.append(a[1])
    return arr


def _yoink(vm, a):
    arr = _s(a[0], "yoink")
    if not arr.items:
        raise OutOfPocket("yoink on an empty stash.", roast="index -1 on a stash of 0. that's straight up out of pocket.")
    return arr.items.pop()


def _yoink_at(vm, a):
    arr = _s(a[0], "yoink_at")
    idx = _norm_index(a[1], len(arr.items), "yoink_at")
    return arr.items.pop(idx)


def _insert(vm, a):
    arr = _s(a[0], "insert")
    i = a[1]
    n = len(arr.items)
    idx = i + n if i < 0 else i
    idx = max(0, min(idx, n))
    arr.items.insert(idx, a[2])
    return arr


def _contains(vm, a):
    arr = _s(a[0], "contains")
    return any(funny_eq(a[1], x, vm) for x in arr.items)


def _index_of(vm, a):
    arr = _s(a[0], "index_of")
    for i, x in enumerate(arr.items):
        if funny_eq(a[1], x, vm):
            return i
    return -1


def _slice(vm, a):
    arr = _s(a[0], "slice")
    start = a[1] if len(a) > 1 and a[1] is not GHOST else None
    stop = a[2] if len(a) > 2 and a[2] is not GHOST else None
    return Stash(arr.items[start:stop])


def _reverse(vm, a):
    arr = _s(a[0], "reverse")
    arr.items.reverse()
    return arr


def _sort(vm, a):
    arr = _s(a[0], "sort")
    cmp_fn = a[1] if len(a) > 1 and a[1] is not GHOST else None
    if cmp_fn is not None:
        key = functools.cmp_to_key(lambda x, y: vm.call_value(cmp_fn, [x, y]))
        arr.items.sort(key=key)
    else:
        arr.items.sort(key=_default_sort_key)
    return arr


def _default_sort_key(v):
    return (0, v) if isinstance(v, (int, float)) and not isinstance(v, bool) else (1, v)


def _join(vm, a):
    arr = _s(a[0], "join")
    sep = a[1] if len(a) > 1 else ""
    from ..values import to_display
    return sep.join(to_display(x, vm) for x in arr.items)


def _glow_up(vm, a):
    arr = _s(a[0], "glow_up")
    fn = a[1]
    return Stash([vm.call_value(fn, [x]) for x in arr.items])


def _vibe_check(vm, a):
    arr = _s(a[0], "vibe_check")
    fn = a[1]
    return Stash([x for x in arr.items if is_truthy(vm.call_value(fn, [x]))])


def _squish(vm, a):
    arr = _s(a[0], "squish")
    fn = a[1]
    acc = a[2] if len(a) > 2 else GHOST
    items = arr.items
    start = 0
    if acc is GHOST:
        if not items:
            raise TypeVibeMismatch("'squish' on an empty stash needs an initial value.")
        acc = items[0]
        start = 1
    for x in items[start:]:
        acc = vm.call_value(fn, [acc, x])
    return acc


def _any(vm, a):
    arr = _s(a[0], "any")
    fn = a[1]
    return any(is_truthy(vm.call_value(fn, [x])) for x in arr.items)


def _all(vm, a):
    arr = _s(a[0], "all")
    fn = a[1]
    return all(is_truthy(vm.call_value(fn, [x])) for x in arr.items)


def _first(vm, a):
    arr = _s(a[0], "first")
    return arr.items[0] if arr.items else GHOST


def _last(vm, a):
    arr = _s(a[0], "last")
    return arr.items[-1] if arr.items else GHOST


def _clone(vm, a):
    return Stash(list(_s(a[0], "clone").items))


def _clear(vm, a):
    arr = _s(a[0], "clear")
    arr.items.clear()
    return arr


def _extend(vm, a):
    arr = _s(a[0], "extend")
    other = _s(a[1], "extend")
    arr.items.extend(other.items)
    return arr


def _sort_by(vm, a):
    arr = _s(a[0], "sort_by")
    keyfn = a[1]
    return Stash(sorted(arr.items, key=lambda x: _SortKeyWrap(vm.call_value(keyfn, [x]))))


class _SortKeyWrap:
    """Wraps a FunnyLang value so Python's sort can compare via funny rules
    without choking on cross-type comparisons it wouldn't otherwise allow."""

    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        a, b = self.value, other.value
        if isinstance(a, bool) or isinstance(b, bool):
            return bool(a) < bool(b)
        return a < b


def _group_by(vm, a):
    arr = _s(a[0], "group_by")
    keyfn = a[1]
    out = {}
    for x in arr.items:
        k = vm.call_value(keyfn, [x])
        out.setdefault(k, []).append(x)
    return GroupChat({k: Stash(v) for k, v in out.items()})


def _unique(vm, a):
    arr = _s(a[0], "unique")
    out = []
    for x in arr.items:
        if not any(funny_eq(x, y, vm) for y in out):
            out.append(x)
    return Stash(out)


def _flatten(vm, a):
    arr = _s(a[0], "flatten")

    def flat(items):
        out = []
        for x in items:
            if isinstance(x, Stash):
                out.extend(flat(x.items))
            else:
                out.append(x)
        return out

    return Stash(flat(arr.items))


def _chunk(vm, a):
    arr = _s(a[0], "chunk")
    n = a[1]
    if n <= 0:
        raise TypeVibeMismatch("'chunk' needs a positive size.")
    items = arr.items
    return Stash([Stash(items[i:i + n]) for i in range(0, len(items), n)])


def _sum_up(vm, a):
    arr = _s(a[0], "sum_up")
    total = 0
    for x in arr.items:
        if isinstance(x, bool) or not isinstance(x, (int, float)):
            raise TypeVibeMismatch(f"'sum_up' needs numbas, found a {type_name(x)}.")
        total += x
    return total


def _shuffle_it(vm, a):
    arr = _s(a[0], "shuffle_it")
    items = list(arr.items)
    random.shuffle(items)
    return Stash(items)


METHODS = {
    "how_thicc": _how_thicc,
    "yeet_in": _yeet_in,
    "yoink": _yoink,
    "yoink_at": _yoink_at,
    "insert": _insert,
    "contains": _contains,
    "index_of": _index_of,
    "slice": _slice,
    "reverse": _reverse,
    "sort": _sort,
    "join": _join,
    "glow_up": _glow_up,
    "vibe_check": _vibe_check,
    "squish": _squish,
    "any": _any,
    "all": _all,
    "first": _first,
    "last": _last,
    "clone": _clone,
    "clear": _clear,
    "extend": _extend,
}


def build() -> Module:
    members = {name: _nf(name, fn, 1, 255) for name, fn in METHODS.items()}
    members.update({
        "sort_by": _nf("sort_by", _sort_by, 2),
        "group_by": _nf("group_by", _group_by, 2),
        "unique": _nf("unique", _unique, 1),
        "flatten": _nf("flatten", _flatten, 1),
        "chunk": _nf("chunk", _chunk, 2),
        "sum_up": _nf("sum_up", _sum_up, 1),
        "shuffle_it": _nf("shuffle_it", _shuffle_it, 1),
    })
    return Module("stash", members)
