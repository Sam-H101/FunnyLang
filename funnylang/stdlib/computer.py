"""`computer` — the harmless joke module (PLAN.md §7.9). No subprocess, no
filesystem writes, no real system calls beyond printing and reading basic
platform info."""
from __future__ import annotations

import os
import platform
import sys
import time
from pathlib import Path

from ..errors import ComputerExploded, SkillIssue
from ..values import GHOST, Module, NativeFn, to_display

_PROCESS_START = time.time()

MUSHROOM_CLOUD = r"""
                  _.-^^---....,,--
              _--                  --_
             <                        >)
             |                         |
              \._                   _./
                 ```--. . , ; .--'''
                       | |   |
                    .-=||  | |=-.
                    `-=#$%&%$#=-'
                       | ;  :|
              _____.,-#%&$@%#&#~,._____
"""


def _nf(name, fn, lo, hi=None):
    return NativeFn(name, fn, lo, hi)


def _ram_bytes() -> int:
    try:
        if hasattr(os, "sysconf") and "SC_PAGE_SIZE" in os.sysconf_names and "SC_PHYS_PAGES" in os.sysconf_names:
            return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (ValueError, OSError):
        pass
    if sys.platform == "win32":
        try:
            import ctypes

            class MEMORYSTATUSEX(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("sullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            stat = MEMORYSTATUSEX()
            stat.dwLength = ctypes.sizeof(MEMORYSTATUSEX)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(stat))
            return int(stat.ullTotalPhys)
        except Exception:
            pass
    return 0


def _uptime_seconds() -> float:
    try:
        if sys.platform.startswith("linux"):
            with open("/proc/uptime") as f:
                return float(f.readline().split()[0])
        if sys.platform == "win32":
            import ctypes

            return ctypes.windll.kernel32.GetTickCount64() / 1000.0
    except Exception:
        pass
    return time.time() - _PROCESS_START


def _explode(vm, a):
    vm.stdout.write(MUSHROOM_CLOUD)
    vm.stdout.write("\n  🍄 KERNEL PANIC: user was cringe\n")
    vm.stdout.write("  computer.explode() called on purpose. couldn't be you.\n")
    vm.stdout.write("  it's over. exit code 69.\n")
    raise ComputerExploded(
        "computer.explode() was called.",
        roast="it's over. exit code 69.",
    )


def _flex(vm, a):
    vm.stdout.write(f"OS: {platform.system()} {platform.release()}\n")
    vm.stdout.write(f"CPUs: {os.cpu_count()}\n")
    vm.stdout.write(f"RAM: {_ram_bytes()} bytes\n")
    vm.stdout.write(f"python: {platform.python_version()}\n")
    vm.stdout.write("your rig: mid\n")
    return GHOST


def _ram(vm, a):
    return _ram_bytes()


def _readline(vm, a):
    """One line of stdin, or `ghost` at end of input.

    The builtin `ask()` returns "" for both an empty line and end of input,
    which a REPL cannot live with: Ctrl-D has to end the session and a blank
    line has to not. NATIVE_PLAN.md N8 task 5 named this as its prerequisite.
    The trailing newline is stripped, exactly as `ask` strips it.
    """
    import sys

    if a and a[0] is not GHOST:
        vm.stdout.write(to_display(a[0], vm))
        flush = getattr(vm.stdout, "flush", None)
        if callable(flush):
            flush()
    line = sys.stdin.readline()
    if line == "":
        return GHOST
    while line and line[-1] in ("\n", "\r"):
        line = line[:-1]
    return line


def _env(vm, a):
    """An environment variable's value, or the default (`ghost` when none is
    given) if it is unset. Added for NATIVE_PLAN.md N8 task 6: the CLI reads
    FUNNY_TOOLCHAIN and FUNNY_STUB, and a language whose own toolchain is
    written in it has to be able to see its own environment."""
    from ..errors import TypeVibeMismatch
    from ..values import type_name

    if not isinstance(a[0], str):
        raise TypeVibeMismatch(f"'env' needs a yapstring name, not a {type_name(a[0])}.")
    value = os.environ.get(a[0])
    if value is None:
        return a[1] if len(a) > 1 else GHOST
    return value


def _exe_path(vm, a):
    """The path of the running executable, or `ghost` if it cannot be
    determined. The native runtime answers with the `funny` binary itself,
    which is how it finds its own sidecars; under this implementation there
    is no such binary, so the closest true answer is the entry script
    Python was pointed at. Documented as differing, since a program that
    prints it cannot be byte-compared across the two."""
    path = sys.argv[0] if sys.argv and sys.argv[0] else None
    if not path:
        return GHOST
    return str(Path(path).resolve())


def _yeet_to_void(vm, a):
    return GHOST


def _beep(vm, a):
    vm.stdout.write("\a")
    return GHOST


def _clear(vm, a):
    vm.stdout.write("\x1b[2J\x1b[H")
    return GHOST


def _uptime(vm, a):
    return _uptime_seconds()


def _blue_screen(vm, a):
    vm.stdout.write("\x1b[44m\x1b[2J\x1b[H")
    vm.stdout.write("FUNNYLANG_FAULT_NOT_HANDLED\n")
    vm.stdout.write("\x1b[0m")
    raise SkillIssue("computer.blue_screen() was called.", roast="FUNNYLANG_FAULT_NOT_HANDLED")


def build() -> Module:
    members = {
        "explode": _nf("explode", _explode, 0),
        "flex": _nf("flex", _flex, 0),
        "ram": _nf("ram", _ram, 0),
        "yeet_to_void": _nf("yeet_to_void", _yeet_to_void, 0, 1),
        "readline": _nf("readline", _readline, 0, 1),
        "env": _nf("env", _env, 1, 2),
        "exe_path": _nf("exe_path", _exe_path, 0),
        "beep": _nf("beep", _beep, 0),
        "clear": _nf("clear", _clear, 0),
        "uptime": _nf("uptime", _uptime, 0),
        "blue_screen": _nf("blue_screen", _blue_screen, 0),
    }
    return Module("computer", members)
