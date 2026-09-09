"""`yapper` — string utilities (PLAN.md §M6 task 4), plus the yapstring
instance-method table used by vm.py's GET_PROP (PLAN.md §3.9)."""
from __future__ import annotations

from ..errors import OutOfPocket, TypeVibeMismatch
from ..values import GHOST, Module, NativeFn, Stash, type_name


def _str(v, fn_name):
    if not isinstance(v, str):
        raise TypeVibeMismatch(f"'{fn_name}' needs a yapstring, not a {type_name(v)}.")
    return v


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _split(vm, a):
    s = _str(a[0], "split")
    sep = a[1] if len(a) > 1 and a[1] is not GHOST else None
    return Stash(s.split(sep) if sep is not None else s.split())


def _join(vm, a):
    sep, items = _str(a[0], "join"), a[1]
    if not isinstance(items, Stash):
        raise TypeVibeMismatch("'join' needs a stash of yapstrings.")
    return sep.join(str(x) for x in items.items)


def _scream(vm, a):
    return _str(a[0], "SCREAM").upper()


def _whisper(vm, a):
    return _str(a[0], "whisper").lower()


def _trim(vm, a):
    return _str(a[0], "trim").strip()


def _ltrim(vm, a):
    return _str(a[0], "ltrim").lstrip()


def _rtrim(vm, a):
    return _str(a[0], "rtrim").rstrip()


def _replace(vm, a):
    return _str(a[0], "replace").replace(_str(a[1], "replace"), _str(a[2], "replace"))


def _contains(vm, a):
    return _str(a[1], "contains") in _str(a[0], "contains")


def _starts_with(vm, a):
    return _str(a[0], "starts_with").startswith(_str(a[1], "starts_with"))


def _ends_with(vm, a):
    return _str(a[0], "ends_with").endswith(_str(a[1], "ends_with"))


def _index_of(vm, a):
    return _str(a[0], "index_of").find(_str(a[1], "index_of"))


def _slice(vm, a):
    s = _str(a[0], "slice")
    start = a[1] if len(a) > 1 and a[1] is not GHOST else None
    stop = a[2] if len(a) > 2 and a[2] is not GHOST else None
    return s[start:stop]


def _reverse(vm, a):
    return _str(a[0], "reverse")[::-1]


def _repeat(vm, a):
    return _str(a[0], "repeat") * int(a[1])


def _pad_left(vm, a):
    s = _str(a[0], "pad_left")
    n = int(a[1])
    c = a[2] if len(a) > 2 else " "
    return s.rjust(n, c)


def _pad_right(vm, a):
    s = _str(a[0], "pad_right")
    n = int(a[1])
    c = a[2] if len(a) > 2 else " "
    return s.ljust(n, c)


def _chars(vm, a):
    return Stash(list(_str(a[0], "chars")))


def _ord_of(vm, a):
    s = _str(a[0], "ord_of")
    if len(s) != 1:
        raise TypeVibeMismatch("'ord_of' needs a single-character yapstring.")
    return ord(s)


def _chr_of(vm, a):
    return chr(int(a[0]))


def _format(vm, a):
    s = _str(a[0], "format")
    try:
        return s.format(*a[1:])
    except (IndexError, KeyError) as exc:
        raise TypeVibeMismatch(f"'format' couldn't fill in every placeholder: {exc}")


def _is_numba(vm, a):
    s = _str(a[0], "is_numba").strip()
    try:
        float(s)
        return True
    except ValueError:
        return False


def _lines(vm, a):
    return Stash(_str(a[0], "lines").splitlines())


def _words(vm, a):
    return Stash(_str(a[0], "words").split())


def _title_case(vm, a):
    return _str(a[0], "title_case").title()


def _sarcasm_case(vm, a):
    s = _str(a[0], "sarcasm_case")
    return "".join(c.upper() if i % 2 else c.lower() for i, c in enumerate(s))


def _at(vm, a):
    s = _str(a[0], "at")
    i = a[1]
    n = len(s)
    idx = i + n if i < 0 else i
    if idx < 0 or idx >= n:
        raise OutOfPocket(f"index {i} on a yapstring of length {n}.", roast=f"index {i} on a stash of {n}. that's straight up out of pocket.")
    return s[idx]


def _code_at(vm, a):
    return ord(_at(vm, a))


def _to_numba(vm, a):
    from .builtins import _to_numba as builtin_to_numba
    return builtin_to_numba(vm, a)


def _how_thicc(vm, a):
    return len(_str(a[0], "how_thicc"))


def build() -> Module:
    members = {
        "split": _nf("split", _split, 1, 2),
        "join": _nf("join", _join, 2),
        "SCREAM": _nf("SCREAM", _scream, 1),
        "whisper": _nf("whisper", _whisper, 1),
        "trim": _nf("trim", _trim, 1),
        "ltrim": _nf("ltrim", _ltrim, 1),
        "rtrim": _nf("rtrim", _rtrim, 1),
        "replace": _nf("replace", _replace, 3),
        "contains": _nf("contains", _contains, 2),
        "starts_with": _nf("starts_with", _starts_with, 2),
        "ends_with": _nf("ends_with", _ends_with, 2),
        "index_of": _nf("index_of", _index_of, 2),
        "slice": _nf("slice", _slice, 1, 3),
        "reverse": _nf("reverse", _reverse, 1),
        "repeat": _nf("repeat", _repeat, 2),
        "pad_left": _nf("pad_left", _pad_left, 2, 3),
        "pad_right": _nf("pad_right", _pad_right, 2, 3),
        "chars": _nf("chars", _chars, 1),
        "ord_of": _nf("ord_of", _ord_of, 1),
        "chr_of": _nf("chr_of", _chr_of, 1),
        "format": _nf("format", _format, 1, 255),
        "is_numba": _nf("is_numba", _is_numba, 1),
        "lines": _nf("lines", _lines, 1),
        "words": _nf("words", _words, 1),
        "title_case": _nf("title_case", _title_case, 1),
        "sarcasm_case": _nf("sarcasm_case", _sarcasm_case, 1),
    }
    return Module("yapper", members)


YAPSTRING_METHODS = {
    "how_thicc": _how_thicc,
    "SCREAM": _scream,
    "whisper": _whisper,
    "trim": _trim,
    "split": _split,
    "contains": _contains,
    "starts_with": _starts_with,
    "ends_with": _ends_with,
    "replace": _replace,
    "index_of": _index_of,
    "slice": _slice,
    "reverse": _reverse,
    "to_numba": _to_numba,
    "chars": _chars,
    "at": _at,
    "code_at": _code_at,
    "repeat": _repeat,
    "pad_left": _pad_left,
    "pad_right": _pad_right,
}
