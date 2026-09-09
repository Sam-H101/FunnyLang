"""Runtime value types (PLAN.md §M5 task 1).

`numba` is Python int/float, `yapstring` is str, `boolski` is bool, `ghost`
is the GHOST singleton — everything else gets a small wrapper class so the
VM can dispatch on type cleanly.
"""
from __future__ import annotations

import json
import math


class Ghost:
    _instance: "Ghost | None" = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    def __repr__(self) -> str:
        return "ghost"

    def __bool__(self) -> bool:
        return False


GHOST = Ghost()


class Stash:
    __slots__ = ("items",)

    def __init__(self, items=None):
        self.items: list = items if items is not None else []

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"Stash({self.items!r})"


class GroupChat:
    __slots__ = ("items",)

    def __init__(self, items=None):
        self.items: dict = items if items is not None else {}

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"GroupChat({self.items!r})"


class Upvalue:
    """An open upvalue reads/writes live into the VM's shared value stack at
    `slot`; once closed (the owning local goes out of scope) it holds its own
    copy so the closure keeps working after the frame is gone."""

    __slots__ = ("stack", "slot", "closed", "value")

    def __init__(self, stack: list, slot: int):
        self.stack = stack
        self.slot = slot
        self.closed = False
        self.value = None

    def get(self):
        return self.value if self.closed else self.stack[self.slot]

    def set(self, value) -> None:
        if self.closed:
            self.value = value
        else:
            self.stack[self.slot] = value

    def close(self) -> None:
        self.value = self.stack[self.slot]
        self.closed = True
        self.stack = None


class Closure:
    """A callable function value. `const_pool`/`protos` are the owning
    CompiledUnit's — every closure needs its own, since once modules (M7)
    can load independently-compiled units mid-execution, a shared
    VM-level const_pool/protos would get clobbered by whichever module ran
    most recently. `module_globals`/`module_exports` are shared by every
    closure compiled from the same module (nested closures inherit them from
    their enclosing one at CLOSURE-creation time), giving each module its
    own isolated global namespace per §3.8 ("non-flexed names are private")."""

    __slots__ = ("proto", "upvalues", "const_pool", "protos", "module_globals", "module_exports")

    def __init__(self, proto, upvalues: list, const_pool=None, protos=None, module_globals=None, module_exports=None):
        self.proto = proto
        self.upvalues = upvalues
        self.const_pool = const_pool
        self.protos = protos
        self.module_globals = module_globals if module_globals is not None else {}
        self.module_exports = module_exports if module_exports is not None else {}

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<bet {self.proto.name}/{self.proto.arity}>"


class NativeFn:
    """A stdlib/builtin function. `fn(vm, args) -> value`."""

    __slots__ = ("name", "fn", "arity_min", "arity_max")

    def __init__(self, name: str, fn, arity_min: int = 0, arity_max: int | None = None):
        self.name = name
        self.fn = fn
        self.arity_min = arity_min
        self.arity_max = arity_min if arity_max is None else arity_max

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<bet {self.name}/native>"


class Squad:
    __slots__ = ("name", "superclass", "methods", "spawn")

    def __init__(self, name: str, superclass: "Squad | None" = None):
        self.name = name
        self.superclass = superclass
        self.methods: dict[str, Closure] = {}
        self.spawn: Closure | None = None

    def find_method(self, name: str):
        squad = self
        while squad is not None:
            m = squad.methods.get(name)
            if m is not None:
                return m
            squad = squad.superclass
        return None

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<squad {self.name}>"


class Instance:
    __slots__ = ("squad", "fields")

    def __init__(self, squad: Squad):
        self.squad = squad
        self.fields: dict = {}

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<{self.squad.name} instance>"


class BoundMethod:
    __slots__ = ("receiver", "method")

    def __init__(self, receiver, method: Closure):
        self.receiver = receiver
        self.method = method

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<bet {self.method.proto.name}/bound>"


class Module:
    __slots__ = ("name", "members")

    def __init__(self, name: str, members: dict | None = None):
        self.name = name
        self.members: dict = members if members is not None else {}

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return f"<module {self.name}>"


class Iterator:
    __slots__ = ("gen", "exhausted")

    def __init__(self, gen):
        self.gen = gen
        self.exhausted = False

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        return "<iterator>"


def type_name(v) -> str:
    if v is GHOST:
        return "ghost"
    if isinstance(v, bool):
        return "boolski"
    if isinstance(v, (int, float)):
        return "numba"
    if isinstance(v, str):
        return "yapstring"
    if isinstance(v, Stash):
        return "stash"
    if isinstance(v, GroupChat):
        return "groupchat"
    if isinstance(v, (Closure, NativeFn, BoundMethod)):
        return "bet"
    if isinstance(v, Squad):
        return "squad"
    if isinstance(v, Instance):
        return v.squad.name
    if isinstance(v, Module):
        return "module"
    if isinstance(v, Iterator):
        return "iterator"
    raise TypeError(f"funnylang: no type name for {v!r}")  # pragma: no cover


def is_truthy(v) -> bool:
    if v is GHOST:
        return False
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return v != 0
    if isinstance(v, str):
        return len(v) > 0
    if isinstance(v, Stash):
        return len(v.items) > 0
    if isinstance(v, GroupChat):
        return len(v.items) > 0
    return True


def funny_eq(a, b, vm=None) -> bool:
    if a is GHOST or b is GHOST:
        return a is GHOST and b is GHOST
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b
    if isinstance(a, str) and isinstance(b, str):
        return a == b
    if isinstance(a, Stash) and isinstance(b, Stash):
        return len(a.items) == len(b.items) and all(
            funny_eq(x, y, vm) for x, y in zip(a.items, b.items)
        )
    if isinstance(a, GroupChat) and isinstance(b, GroupChat):
        if a.items.keys() != b.items.keys():
            return False
        return all(funny_eq(a.items[k], b.items[k], vm) for k in a.items)
    if isinstance(a, Instance) and isinstance(b, Instance):
        method = a.squad.find_method("same_energy")
        if method is not None and vm is not None:
            return is_truthy(vm.call_value(method, [a, b]))
        return a is b
    return a is b


def _format_float(v: float) -> str:
    if math.isnan(v):
        return "nan"
    if math.isinf(v):
        return "infinity" if v > 0 else "-infinity"
    if v == int(v) and abs(v) < 1e16:
        return f"{v:.1f}"
    return repr(v)


def to_display(v, vm=None, _seen: frozenset = frozenset()) -> str:
    if v is GHOST:
        return "ghost"
    if v is True:
        return "fax"
    if v is False:
        return "cap"
    if isinstance(v, float):
        return _format_float(v)
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return v
    if isinstance(v, Stash):
        if id(v) in _seen:
            return "[...]"
        inner = _seen | {id(v)}
        return "[" + ", ".join(to_repr(x, vm, inner) for x in v.items) + "]"
    if isinstance(v, GroupChat):
        if id(v) in _seen:
            return "{...}"
        inner = _seen | {id(v)}
        return "{" + ", ".join(f"{to_repr(k, vm, inner)}: {to_repr(val, vm, inner)}" for k, val in v.items.items()) + "}"
    if isinstance(v, (Closure, NativeFn)):
        name = v.proto.name if isinstance(v, Closure) else v.name
        arity = v.proto.arity if isinstance(v, Closure) else "native"
        return f"<bet {name}/{arity}>"
    if isinstance(v, BoundMethod):
        return f"<bet {v.method.proto.name}/bound>"
    if isinstance(v, Squad):
        return f"<squad {v.name}>"
    if isinstance(v, Instance):
        method = v.squad.find_method("to_yap")
        if method is not None and vm is not None:
            return to_display(vm.call_value(method, []), vm, _seen)
        return f"<{v.squad.name} instance>"
    if isinstance(v, Module):
        return f"<module {v.name}>"
    if isinstance(v, Iterator):
        return "<iterator>"
    raise TypeError(f"funnylang: no display for {v!r}")  # pragma: no cover


def to_repr(v, vm=None, _seen: frozenset = frozenset()) -> str:
    if isinstance(v, str):
        return json.dumps(v)
    return to_display(v, vm, _seen)
