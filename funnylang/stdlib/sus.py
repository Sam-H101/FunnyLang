"""`sus` — reflection / debug helpers (PLAN.md §M6 task 4)."""
from __future__ import annotations

from ..values import GroupChat, Instance, Module, NativeFn, Stash, to_repr, type_name


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _type_of(vm, a):
    return type_name(a[0])


def _fields_of(vm, a):
    x = a[0]
    if not isinstance(x, Instance):
        return GroupChat({})
    return GroupChat(dict(x.fields))


def _is_a(vm, a):
    return type_name(a[0]) == a[1]


def _stack_trace(vm, a):
    return Stash(list(vm._build_trace(vm.frames[-1], vm.frames[-1].ip)) if vm.frames else [])


def _dump(vm, a):
    vm.stdout.write(to_repr(a[0], vm) + "\n")
    return a[0]


def build() -> Module:
    members = {
        "type_of": _nf("type_of", _type_of, 1),
        "fields_of": _nf("fields_of", _fields_of, 1),
        "is_a": _nf("is_a", _is_a, 2),
        "stack_trace": _nf("stack_trace", _stack_trace, 0),
        "dump": _nf("dump", _dump, 1),
    }
    return Module("sus", members)
