"""NATIVE_PLAN.md N2 acceptance: "every file in tests/lang/ that uses only
the [N2 opcode subset] produces byte-identical stdout under both VMs."
None of the existing tests/lang/ golden files actually stay within that
subset (all of them use functions, at minimum, since every compiled
top-level `bet` declaration needs CLOSURE just to bind the name) -- so
this is exactly the "enumerate that subset explicitly" case the plan's own
acceptance text anticipates, done with dedicated programs under
tests/native/n2_programs/ instead. It grows every milestone as more of the
opcode set becomes available.

Builds the native `funny` binary once (session-scoped fixture) and, per
program: compiles it with the Python toolchain, runs the result through
both VMs, and diffs stdout byte for byte. No hand-written .expected files
(unlike tests/lang/) -- the Python VM's own output *is* the oracle here,
by construction (that's what "differential" means for N2 onward, per
NATIVE_PLAN.md §5).
"""
from __future__ import annotations

import io
import os
import subprocess
import sys
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
PROGRAMS_DIR = Path(__file__).resolve().parent / "n2_programs"
PROGRAMS = sorted(PROGRAMS_DIR.glob("*.funny"))

NATIVE_SRCS = [
    "bignum.c", "chunk.c", "gc.c", "main.c", "numfmt.c", "string.c", "value.c", "vm.c",
]


@pytest.fixture(scope="session")
def native_binary(tmp_path_factory):
    out = tmp_path_factory.mktemp("native_build") / "funny_n2_test"
    srcs = [str(ROOT / "native" / f) for f in NATIVE_SRCS]
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
def test_n2_program_matches_python(path, native_binary, tmp_path):
    try:
        expected = _python_run(path)
    except FunnyError as exc:
        pytest.fail(f"Python VM itself failed on {path.name} (fix the test program, not the C VM): {exc}")

    funnyc_bytes = _python_compile_to_funnyc(path)
    funnyc_path = tmp_path / f"{path.stem}.funnyc"
    funnyc_path.write_bytes(funnyc_bytes)

    actual = _native_run(native_binary, funnyc_path)
    assert actual == expected
