"""NATIVE_PLAN.md §5's differential golden testing, the acceptance gate for
N2 through N6: run a program through both the Python VM and the native one
and diff stdout byte for byte. No hand-written .expected files (unlike
tests/lang/) -- the Python VM's own output *is* the oracle here, by
construction.

Started life as N2-only (`test_n2_differential.py`, `n2_programs/`), since
none of the existing tests/lang/ golden files stay inside N2's narrow
opcode subset (every one declares a function, which needs CLOSURE just to
bind the name). Renamed once N3 outgrew that framing -- this is now the
one differential suite, and tests/native/programs/ grows with whatever
each new milestone unlocks. It's expected to keep absorbing real
tests/lang/ files directly once enough of the language exists natively
(NATIVE_PLAN.md §5 guesses around N4).

Builds the native `funny` binary once per session (globs native/*.c, the
same way build.sh does, so this file never needs editing when a new
native/ source is added) and, per program: compiles it with the Python
toolchain, runs the result through both VMs, and diffs stdout.
"""
from __future__ import annotations

import io
import subprocess
from pathlib import Path

import pytest

from funnylang.compiler import Compiler
from funnylang.errors import FunnyError
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.serializer import dump_funnyc
from funnylang.source import SourceFile
from funnylang.stdlib import install_stdlib
from funnylang.vm import VM

PROGRAMS_DIR = Path(__file__).resolve().parent / "programs"
PROGRAMS = sorted(PROGRAMS_DIR.glob("*.funny"))


# `internet` (N5 task 5) is the one module that can touch the real network.
# No program under programs/ exercises live network I/O -- same policy
# tests/test_stdlib.py's own internet tests already follow, always under
# FUNNY_NO_NET=1 -- so every program here runs with it set. Done per test
# through monkeypatch rather than by assigning os.environ at import time,
# so it's restored afterwards and can't reach into the *other* suite in
# this directory, N5b's live-HTTPS gate, which needs the real value.
# `_native_run`'s subprocess inherits it either way (no `env=` override).
@pytest.fixture(autouse=True)
def _network_disabled(monkeypatch):
    monkeypatch.setenv("FUNNY_NO_NET", "1")


def _python_run(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    source = SourceFile(str(path), text)
    program = parse_source(source)
    result = resolve_program(program, source)
    unit = Compiler(result, source).compile_program(program, str(path))
    out = io.StringIO()
    vm = VM(stdout=out)
    install_stdlib(vm)
    vm.interpret(unit, source)
    return out.getvalue()


def _python_compile_to_funnyc(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    source = SourceFile(str(path), text)
    program = parse_source(source)
    result = resolve_program(program, source)
    unit = Compiler(result, source).compile_program(program, str(path))
    return dump_funnyc(unit)


def _native_run(binary: Path, funnyc_path: Path) -> str:
    result = subprocess.run([str(binary), str(funnyc_path)], capture_output=True, text=True)
    assert result.returncode == 0, f"native VM exited {result.returncode}:\n{result.stderr}"
    return result.stdout


@pytest.mark.parametrize("path", PROGRAMS, ids=lambda p: p.name)
def test_native_program_matches_python(path, native_binary, tmp_path):
    try:
        expected = _python_run(path)
    except FunnyError as exc:
        pytest.fail(f"Python VM itself failed on {path.name} (fix the test program, not the C VM): {exc}")

    funnyc_bytes = _python_compile_to_funnyc(path)
    funnyc_path = tmp_path / f"{path.stem}.funnyc"
    funnyc_path.write_bytes(funnyc_bytes)

    actual = _native_run(native_binary, funnyc_path)
    assert actual == expected
