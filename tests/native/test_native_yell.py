"""`yell(...)`, added in NATIVE_PLAN.md N8 task 3.

`yap`/`mumble` are statements with their own opcodes, so the language had no
way at all to reach stderr -- which the self-hosted CLI needs (`fmt --check`
prints "isn't formatted" there, and exits 1). `yell` is the stderr
counterpart to `yap`, as a plain builtin.

The rest of the differential suite compares stdout only, so this one runs
both VMs as subprocesses and compares *both* streams: the whole point of
`yell` is which stream the bytes land on.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent

PROGRAM = """gimme mafs

yap "on stdout"
yell("on stderr")
yell("several", "values", 1, fax, ghost)
yell()
yell([1, 2], {"k": "v"})
yap "stdout again"
// yell returns ghost, like yap, so it composes into nothing.
yap what_is_it(yell("returns"))
"""


@pytest.fixture(scope="session")
def program(tmp_path_factory):
    p = tmp_path_factory.mktemp("yell") / "yell.funny"
    p.write_text(PROGRAM, encoding="utf-8", newline="")
    return p


def test_yell_writes_to_stderr_on_both_vms(native_binary, program, tmp_path):
    from funnylang.modules import build_bundle
    from funnylang.serializer import dump_funnypak

    pak = tmp_path / "yell.funnypak"
    units, entry = build_bundle(str(program))
    pak.write_bytes(dump_funnypak(units, entry))

    native = subprocess.run(
        [str(native_binary), str(pak)], capture_output=True, text=True, encoding="utf-8", timeout=60
    )
    python = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(program)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=60,
    )

    assert native.returncode == python.returncode == 0
    assert native.stdout == python.stdout
    assert native.stderr == python.stderr

    # And the split is real, not "everything happens to land in one place".
    assert "on stdout" in native.stdout and "on stdout" not in native.stderr
    assert "on stderr" in native.stderr and "on stderr" not in native.stdout


def test_yell_is_a_known_global_to_both_resolvers(native_binary, tmp_path):
    """A builtin the resolver doesn't know about is a compile-time WhoDis,
    so the name has to be added in three places: both stdlibs and both
    resolvers' BUILTIN_GLOBAL_NAMES. This catches three of the four."""
    from funnylang.resolver import BUILTIN_GLOBAL_NAMES

    assert "yell" in BUILTIN_GLOBAL_NAMES
    assert "yell" in (ROOT / "selfhost" / "compiler.funny").read_text(encoding="utf-8")
