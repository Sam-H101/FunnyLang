"""NATIVE_PLAN.md N8 task 2: `funny xray`, in FunnyLang.

`selfhost/xray.funny` + `selfhost/disasm.funny` + `selfhost/loader.funny` +
`selfhost/astdump.funny` replace funnylang/disasm.py and cli.py's `cmd_xray`.
The bar, as everywhere in `selfhost/`, is byte-identical output rather than
"looks about right" -- so every assertion here is a diff against the Python
CLI running on the same file.

`loader.funny` is the piece with no Python counterpart in `selfhost/` before
now: the exact inverse of `emitter.funny`, so a `.funnyc` or `.funnypak` on
disk disassembles through the same code path as a program just compiled.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak

ROOT = Path(__file__).resolve().parent.parent.parent
XRAY_ENTRY = ROOT / "selfhost" / "xray.funny"

# Chosen for coverage of what the disassembler has to render, not breadth:
# closures with upvalues (the one variable-width opcode), pointers (the two
# opcodes whose *second* operand is the name const), squads, templates,
# every literal kind, and the self-hosted lexer itself -- whose constant
# pool holds the NUL-bearing string that N8 task 1 turned up.
CORPUS = [
    "examples/hello.funny",
    "examples/fizzbuzz.funny",
    "examples/closures.funny",
    "examples/squads.funny",
    "tests/lang/ptr_five_kinds.funny",
    "tests/lang/ptr_aliasing.funny",
    "tests/lang/template_strings.funny",
    "tests/lang/defaults.funny",
    "tests/lang/arith_float_promotion.funny",
    "selfhost/lexer.funny",
]


@pytest.fixture(scope="session")
def xray_pak(tmp_path_factory):
    units, entry_canonical = build_bundle(str(XRAY_ENTRY))
    pak = tmp_path_factory.mktemp("xray") / "xray.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _python_xray(target: str, *flags: str) -> str:
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "xray", *flags, target],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=300,
    )
    assert result.returncode == 0, result.stderr
    return result.stdout


def _native_xray(binary: Path, pak: Path, target: str, *flags: str) -> str:
    result = subprocess.run(
        [str(binary), str(pak), *flags, target],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=300,
    )
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"
    return result.stdout


@pytest.mark.parametrize("target", CORPUS)
def test_disassembly_matches_python(native_binary, xray_pak, target):
    assert _native_xray(native_binary, xray_pak, target) == _python_xray(target)


@pytest.mark.parametrize("target", CORPUS)
def test_ast_dump_matches_python(native_binary, xray_pak, target):
    assert _native_xray(native_binary, xray_pak, target, "--ast") == _python_xray(target, "--ast")


@pytest.mark.parametrize("target", CORPUS)
def test_token_dump_matches_python(native_binary, xray_pak, target):
    """Token reprs go through a hand-rolled port of CPython's own `repr` for
    strings, and a TEMPLATE token's value is a nested list of tokens -- the
    two places this output is easiest to get subtly wrong."""
    assert _native_xray(native_binary, xray_pak, target, "--tokens") == _python_xray(target, "--tokens")


def test_disassembles_a_compiled_funnyc(native_binary, xray_pak, tmp_path):
    """The loader path: `emitter.funny` wrote these bytes, `loader.funny`
    reads them back, and the disassembly must be the same either way."""
    from funnylang.compiler import Compiler
    from funnylang.parser import parse_source
    from funnylang.resolver import resolve_program
    from funnylang.serializer import dump_funnyc
    from funnylang.source import SourceFile

    src = ROOT / "tests" / "lang" / "arith_float_promotion.funny"
    source = SourceFile(str(src), src.read_text(encoding="utf-8"))
    program = parse_source(source)
    unit = Compiler(resolve_program(program, source), source, fold_constants=True).compile_program(program, str(src))
    out = tmp_path / "arith_float_promotion.funnyc"
    out.write_bytes(dump_funnyc(unit))

    assert _native_xray(native_binary, xray_pak, str(out)) == _python_xray(str(out))


def test_disassembles_a_bundle(native_binary, xray_pak, tmp_path):
    """`--pak` output: the entry line, one header per module, and every
    module's disassembly, in bundle order."""
    pak = tmp_path / "main.funnypak"
    units, entry = build_bundle(str(ROOT / "examples" / "modules" / "main.funny"))
    pak.write_bytes(dump_funnypak(units, entry))

    mine = _native_xray(native_binary, xray_pak, str(pak))
    assert mine == _python_xray(str(pak))
    assert mine.startswith("entry: ")
    assert "=== module: " in mine


def test_float_constants_survive_the_round_trip(native_binary, xray_pak, tmp_path):
    """`mafs.bits_to_float`, added for this milestone, is the only way a
    FunnyLang program can read a float back out of `.funnyc` bytes -- so a
    file whose constants are all floats is the test that it works."""
    from funnylang.compiler import Compiler
    from funnylang.parser import parse_source
    from funnylang.resolver import resolve_program
    from funnylang.serializer import dump_funnyc
    from funnylang.source import SourceFile

    src = tmp_path / "floats.funny"
    src.write_text(
        "yo a = 3.14159\nyo b = -0.5\nyo c = 1e300\nyo d = 2.5e-8\nyo e = 0.1\nyap a, b, c, d, e\n",
        encoding="utf-8",
    )
    source = SourceFile(str(src), src.read_text(encoding="utf-8"))
    program = parse_source(source)
    unit = Compiler(resolve_program(program, source), source, fold_constants=True).compile_program(program, str(src))
    out = tmp_path / "floats.funnyc"
    out.write_bytes(dump_funnyc(unit))

    assert _native_xray(native_binary, xray_pak, str(out)) == _python_xray(str(out))
