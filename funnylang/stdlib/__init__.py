"""The stdlib registry (PLAN.md §M6 task 5) — lazily built."""
from __future__ import annotations

from ..errors import ImportSkillIssue

STDLIB_NAMES = (
    "mafs", "yapper", "stash", "groupchat", "rizz", "filez",
    "clock", "sus", "computer", "internet",
)


def _build(name: str):
    if name == "mafs":
        from . import mafs
        return mafs.build()
    if name == "yapper":
        from . import yapper
        return yapper.build()
    if name == "stash":
        from . import stash
        return stash.build()
    if name == "groupchat":
        from . import groupchat
        return groupchat.build()
    if name == "rizz":
        from . import rizz
        return rizz.build()
    if name == "filez":
        from . import filez
        return filez.build()
    if name == "clock":
        from . import clock
        return clock.build()
    if name == "sus":
        from . import sus
        return sus.build()
    if name == "computer":
        from . import computer
        return computer.build()
    if name == "internet":
        from . import internet
        return internet.build()
    return None


STDLIB: dict = {name: (lambda n=name: _build(n)) for name in STDLIB_NAMES}


def get_stdlib_module(name: str):
    builder = STDLIB.get(name)
    return builder() if builder is not None else None


def install_builtins(vm) -> None:
    from . import builtins as builtins_mod

    for name, value in builtins_mod.build_globals().items():
        vm.globals[name] = value


def _stdlib_module_loader(vm, path: str, mode: int):
    if mode == 2:
        module = get_stdlib_module(path)
        if module is None:
            raise ImportSkillIssue(
                f"no stdlib module named '{path}'.",
                roast=f"can't find `{path}`. did you make it up?",
            )
        return module
    # File-based imports (modes 0/1) need the real module resolver — M7.
    raise ImportSkillIssue(
        f"can't find '{path}'. file imports land in M7.",
        roast=f"can't find `{path}`. did you make it up?",
    )


def install_stdlib(vm) -> None:
    """Wires builtins + stdlib `gimme` support into a fresh VM."""
    install_builtins(vm)
    vm.module_loader = _stdlib_module_loader
