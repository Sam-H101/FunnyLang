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
import os
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

ROOT = Path(__file__).resolve().parent.parent.parent
PROGRAMS_DIR = Path(__file__).resolve().parent / "programs"
PROGRAMS = sorted(PROGRAMS_DIR.glob("*.funny"))


@pytest.fixture(scope="session")
def native_binary(tmp_path_factory):
    out = tmp_path_factory.mktemp("native_build") / "funny_native_test"
    srcs = sorted(str(p) for p in (ROOT / "native").glob("*.c"))
    cc = os.environ.get("CC", "cc")  # e.g. `CC=clang python3 -m pytest ...`
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(out), *srcs, "-lm"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
    except OSError as exc:
        # `cc` not found at all (e.g. windows-latest CI runners have no C
        # compiler on PATH without an explicit setup step, unlike the
        # `native` ci.yml job) -- FileNotFoundError, not a nonzero exit, so
        # this needs its own catch alongside the compile-failure case below.
        pytest.skip(f"no C compiler on PATH: {exc}")
    if result.returncode != 0:
        pytest.skip(f"no C toolchain available to build the native binary:\n{result.stdout}\n{result.stderr}")
    return out


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
