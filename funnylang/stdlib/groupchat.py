"""`groupchat` — map utilities (PLAN.md §M6 task 4), plus the groupchat
instance-method table used by vm.py's GET_PROP (PLAN.md §3.9)."""
from __future__ import annotations

from ..errors import KeyGhosted, TypeVibeMismatch
from ..values import GHOST, GroupChat, Module, NativeFn, Stash, type_name


def _g(v, fn_name):
    if not isinstance(v, GroupChat):
        raise TypeVibeMismatch(f"'{fn_name}' needs a groupchat, not a {type_name(v)}.")
    return v


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _how_thicc(vm, a):
    return len(_g(a[0], "how_thicc").items)


def _keys(vm, a):
    return Stash(list(_g(a[0], "keys").items.keys()))


def _values(vm, a):
    return Stash(list(_g(a[0], "values").items.values()))


def _pairs(vm, a):
    return Stash([Stash([k, v]) for k, v in _g(a[0], "pairs").items.items()])


def _has(vm, a):
    return a[1] in _g(a[0], "has").items


def _get(vm, a):
    m = _g(a[0], "get")
    default = a[2] if len(a) > 2 else GHOST
    return m.items.get(a[1], default)


def _set(vm, a):
    m = _g(a[0], "set")
    m.items[a[1]] = a[2]
    return m


def _remove(vm, a):
    m = _g(a[0], "remove")
    if a[1] not in m.items:
        raise KeyGhosted(f"key '{a[1]}' not found.", roast=f"key `{a[1]}` left the group chat.")
    del m.items[a[1]]
    return m


def _merge(vm, a):
    m = _g(a[0], "merge")
    other = _g(a[1], "merge")
    m.items.update(other.items)
    return m


def _clone(vm, a):
    return GroupChat(dict(_g(a[0], "clone").items))


def _clear(vm, a):
    m = _g(a[0], "clear")
    m.items.clear()
    return m


def _invert(vm, a):
    m = _g(a[0], "invert")
    return GroupChat({v: k for k, v in m.items.items()})


def _from_pairs(vm, a):
    pairs = a[0]
    if not isinstance(pairs, Stash):
        raise TypeVibeMismatch("'from_pairs' needs a stash of [key, value] pairs.")
    out = {}
    for p in pairs.items:
        if not isinstance(p, Stash) or len(p.items) != 2:
            raise TypeVibeMismatch("'from_pairs' needs each entry to be a 2-element stash.")
        out[p.items[0]] = p.items[1]
    return GroupChat(out)


METHODS = {
    "how_thicc": _how_thicc,
    "keys": _keys,
    "values": _values,
    "pairs": _pairs,
    "has": _has,
    "get": _get,
    "set": _set,
    "remove": _remove,
    "merge": _merge,
    "clone": _clone,
    "clear": _clear,
}


def build() -> Module:
    members = {name: _nf(name, fn, 1, 255) for name, fn in METHODS.items()}
    members.update({
        "invert": _nf("invert", _invert, 1),
        "from_pairs": _nf("from_pairs", _from_pairs, 1),
    })
    return Module("groupchat", members)
