"""Always-in-scope globals (PLAN.md §M6 task 3)."""
from __future__ import annotations

from ..errors import SkillIssue, TypeVibeMismatch
from ..values import GHOST, GroupChat, NativeFn, Stash, is_truthy, to_display, to_repr, type_name


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _how_thicc(vm, args):
    x = args[0]
    if isinstance(x, Stash):
        return len(x.items)
    if isinstance(x, GroupChat):
        return len(x.items)
    if isinstance(x, str):
        return len(x)
    raise TypeVibeMismatch(f"a {type_name(x)} doesn't have a length.", roast=f"a {type_name(x)} doesn't have a length. it just is.")


def _what_is_it(vm, args):
    return type_name(args[0])


def _to_yap(vm, args):
    return to_display(args[0], vm)


def _to_numba(vm, args):
    x = args[0]
    if isinstance(x, bool):
        return 1 if x else 0
    if isinstance(x, (int, float)):
        return x
    if isinstance(x, str):
        s = x.strip()
        try:
            return int(s, 0) if s[:2].lower() not in ("0x", "0b", "0o") else int(s, 0)
        except ValueError:
            try:
                return float(s)
            except ValueError:
                raise TypeVibeMismatch(f"'{x}' isn't a numba.", roast=f"'{x}' isn't a numba. nice try though.")
    raise TypeVibeMismatch(f"can't turn a {type_name(x)} into a numba.")


def _to_int(vm, args):
    x = args[0]
    if isinstance(x, bool):
        return 1 if x else 0
    if isinstance(x, int):
        return x
    if isinstance(x, float):
        return int(x)
    if isinstance(x, str):
        try:
            return int(float(x.strip()))
        except ValueError:
            raise TypeVibeMismatch(f"'{x}' isn't a numba.", roast=f"'{x}' isn't a numba. nice try though.")
    raise TypeVibeMismatch(f"can't turn a {type_name(x)} into a numba.")


def _sheesh(vm, args):
    x = args[0]
    vm.stdout.write(to_repr(x, vm) + "\n")
    return x


def _no_cap(vm, args):
    cond = args[0]
    msg = args[1] if len(args) > 1 else "assertion failed. couldn't be you."
    if not is_truthy(cond):
        raise SkillIssue(msg if isinstance(msg, str) else to_display(msg, vm), roast="skill issue.")
    return GHOST


def _ask(vm, args):
    if args and args[0] is not GHOST:
        vm.stdout.write(to_display(args[0], vm))
        flush = getattr(vm.stdout, "flush", None)
        if callable(flush):
            flush()
    try:
        return input()
    except EOFError:
        return ""


def _dip(vm, args):
    code = args[0] if args else 0
    if code is GHOST:
        code = 0
    raise SystemExit(code)


def _the_args(vm, args):
    return Stash(list(getattr(vm, "program_args", [])))


def _combo(vm, args):
    fns = list(args)

    def composed(inner_vm, inner_args):
        value = inner_args[0] if inner_args else GHOST
        for fn in fns:
            value = inner_vm.call_value(fn, [value])
        return value

    return NativeFn("combo", composed, 0, 1)


def _identity(vm, args):
    return args[0]


def _range_stash(vm, args):
    a = args[0]
    b = args[1] if len(args) > 1 else None
    step = args[2] if len(args) > 2 and args[2] is not GHOST else 1
    if b is None or b is GHOST:
        start, stop = 0, a
    else:
        start, stop = a, b
    if step == 0:
        raise TypeVibeMismatch("range_stash step can't be 0.")
    out = []
    n = start
    if step > 0:
        while n < stop:
            out.append(n)
            n += step
    else:
        while n > stop:
            out.append(n)
            n += step
    return Stash(out)


def _zip_em(vm, args):
    a, b = args[0], args[1]
    if not (isinstance(a, Stash) and isinstance(b, Stash)):
        raise TypeVibeMismatch("zip_em needs two stashes.")
    return Stash([Stash([x, y]) for x, y in zip(a.items, b.items)])


def _enumerate_em(vm, args):
    a = args[0]
    if not isinstance(a, Stash):
        raise TypeVibeMismatch("enumerate_em needs a stash.")
    return Stash([Stash([i, v]) for i, v in enumerate(a.items)])


def _deep_clone(vm, args):
    return _clone_value(args[0])


def _clone_value(v):
    if isinstance(v, Stash):
        return Stash([_clone_value(x) for x in v.items])
    if isinstance(v, GroupChat):
        return GroupChat({k: _clone_value(x) for k, x in v.items.items()})
    return v


def build_globals() -> dict:
    return {
        "how_thicc": _nf("how_thicc", _how_thicc, 1),
        "what_is_it": _nf("what_is_it", _what_is_it, 1),
        "to_yap": _nf("to_yap", _to_yap, 1),
        "to_numba": _nf("to_numba", _to_numba, 1),
        "to_int": _nf("to_int", _to_int, 1),
        "sheesh": _nf("sheesh", _sheesh, 1),
        "no_cap": _nf("no_cap", _no_cap, 1, 2),
        "ask": _nf("ask", _ask, 0, 1),
        "dip": _nf("dip", _dip, 0, 1),
        "the_args": _nf("the_args", _the_args, 0),
        "combo": _nf("combo", _combo, 0, 255),
        "identity": _nf("identity", _identity, 1),
        "range_stash": _nf("range_stash", _range_stash, 1, 3),
        "zip_em": _nf("zip_em", _zip_em, 2),
        "enumerate_em": _nf("enumerate_em", _enumerate_em, 1),
        "deep_clone": _nf("deep_clone", _deep_clone, 1),
    }
