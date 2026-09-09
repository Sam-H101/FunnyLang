"""`mafs` — the math module (PLAN.md §M6 task 4)."""
from __future__ import annotations

import math

from ..errors import MathAintMathin, TypeVibeMismatch
from ..values import Module, NativeFn, type_name


def _num(v, fn_name):
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise TypeVibeMismatch(f"'{fn_name}' needs a numba, not a {type_name(v)}.")
    return v


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _sqrt(vm, a):
    x = _num(a[0], "sqrt")
    if x < 0:
        raise MathAintMathin("sqrt of a negative numba.", roast="you divided by zero. the universe said no.")
    return math.sqrt(x)


def _abs(vm, a):
    return abs(_num(a[0], "abs"))


def _floor(vm, a):
    return math.floor(_num(a[0], "floor"))


def _ceil(vm, a):
    return math.ceil(_num(a[0], "ceil"))


def _round(vm, a):
    x = _num(a[0], "round")
    digits = a[1] if len(a) > 1 else 0
    result = round(x, digits if digits else None)
    return result if digits else int(result)


def _min(vm, a):
    return min(_num(v, "min") for v in a)


def _max(vm, a):
    return max(_num(v, "max") for v in a)


def _pow(vm, a):
    return _num(a[0], "pow") ** _num(a[1], "pow")


def _log(vm, a):
    x = _num(a[0], "log")
    base = a[1] if len(a) > 1 else math.e
    if x <= 0:
        raise MathAintMathin("log of a non-positive numba.", roast="you divided by zero. the universe said no.")
    return math.log(x, base)


def _log2(vm, a):
    x = _num(a[0], "log2")
    if x <= 0:
        raise MathAintMathin("log2 of a non-positive numba.")
    return math.log2(x)


def _log10(vm, a):
    x = _num(a[0], "log10")
    if x <= 0:
        raise MathAintMathin("log10 of a non-positive numba.")
    return math.log10(x)


def _exp(vm, a):
    return math.exp(_num(a[0], "exp"))


def _sin(vm, a):
    return math.sin(_num(a[0], "sin"))


def _cos(vm, a):
    return math.cos(_num(a[0], "cos"))


def _tan(vm, a):
    return math.tan(_num(a[0], "tan"))


def _atan2(vm, a):
    return math.atan2(_num(a[0], "atan2"), _num(a[1], "atan2"))


def _hypot(vm, a):
    return math.hypot(*[_num(v, "hypot") for v in a])


def _clamp(vm, a):
    x, lo, hi = (_num(v, "clamp") for v in a)
    return max(lo, min(x, hi))


def _sign(vm, a):
    x = _num(a[0], "sign")
    return (x > 0) - (x < 0)


def _gcd(vm, a):
    return math.gcd(int(_num(a[0], "gcd")), int(_num(a[1], "gcd")))


def _lcm(vm, a):
    x, y = int(_num(a[0], "lcm")), int(_num(a[1], "lcm"))
    if x == 0 or y == 0:
        return 0
    return abs(x * y) // math.gcd(x, y)


def _is_prime(vm, a):
    n = int(_num(a[0], "is_prime"))
    if n < 2:
        return False
    if n in (2, 3):
        return True
    if n % 2 == 0:
        return False
    i = 3
    while i * i <= n:
        if n % i == 0:
            return False
        i += 2
    return True


def _factorial(vm, a):
    n = int(_num(a[0], "factorial"))
    if n < 0:
        raise MathAintMathin("factorial of a negative numba.", roast="you divided by zero. the universe said no.")
    return math.factorial(n)


def build() -> Module:
    members = {
        "sqrt": _nf("sqrt", lambda vm, a: _sqrt(vm, a), 1),
        "abs": _nf("abs", lambda vm, a: _abs(vm, a), 1),
        "floor": _nf("floor", lambda vm, a: _floor(vm, a), 1),
        "ceil": _nf("ceil", lambda vm, a: _ceil(vm, a), 1),
        "round": _nf("round", lambda vm, a: _round(vm, a), 1, 2),
        "min": _nf("min", lambda vm, a: _min(vm, a), 1, 255),
        "max": _nf("max", lambda vm, a: _max(vm, a), 1, 255),
        "pow": _nf("pow", lambda vm, a: _pow(vm, a), 2),
        "log": _nf("log", lambda vm, a: _log(vm, a), 1, 2),
        "log2": _nf("log2", lambda vm, a: _log2(vm, a), 1),
        "log10": _nf("log10", lambda vm, a: _log10(vm, a), 1),
        "exp": _nf("exp", lambda vm, a: _exp(vm, a), 1),
        "sin": _nf("sin", lambda vm, a: _sin(vm, a), 1),
        "cos": _nf("cos", lambda vm, a: _cos(vm, a), 1),
        "tan": _nf("tan", lambda vm, a: _tan(vm, a), 1),
        "atan2": _nf("atan2", lambda vm, a: _atan2(vm, a), 2),
        "hypot": _nf("hypot", lambda vm, a: _hypot(vm, a), 1, 255),
        "clamp": _nf("clamp", lambda vm, a: _clamp(vm, a), 3),
        "sign": _nf("sign", lambda vm, a: _sign(vm, a), 1),
        "gcd": _nf("gcd", lambda vm, a: _gcd(vm, a), 2),
        "lcm": _nf("lcm", lambda vm, a: _lcm(vm, a), 2),
        "is_prime": _nf("is_prime", lambda vm, a: _is_prime(vm, a), 1),
        "factorial": _nf("factorial", lambda vm, a: _factorial(vm, a), 1),
        "skibidi_pi": math.pi,
        "e": math.e,
        "phi": (1 + math.sqrt(5)) / 2,
        "infinity": math.inf,
        "nan": math.nan,
    }
    return Module("mafs", members)


# -- numba instance methods (PLAN.md §3.9), also used by vm.py's GET_PROP ---


def _m_to_yap(vm, args):
    from ..values import to_display
    return to_display(args[0], vm)


def _m_abs(vm, args):
    return abs(args[0])


def _m_floor(vm, args):
    return math.floor(args[0])


def _m_ceil(vm, args):
    return math.ceil(args[0])


def _m_round(vm, args):
    x = args[0]
    digits = args[1] if len(args) > 1 else 0
    result = round(x, digits if digits else None)
    return result if digits else int(result)


def _m_is_whole(vm, args):
    x = args[0]
    return float(x).is_integer() if isinstance(x, float) else True


NUMBA_METHODS = {
    "to_yap": _m_to_yap,
    "abs": _m_abs,
    "floor": _m_floor,
    "ceil": _m_ceil,
    "round": _m_round,
    "is_whole": _m_is_whole,
}
