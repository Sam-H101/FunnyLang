"""PLAN.md §M12 task 2, fourth file: selfhost/compiler.funny (resolver +
codegen), cross-checked against funnylang.compiler's CompiledUnit directly
(not via the disassembler or serializer) across the entire tests/lang/ +
examples/ corpus -- byte-exact bytecode, identical proto metadata, and an
identical constant pool are the actual point of self-hosting."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.chunk import TAG_BOOL, TAG_GHOST, TAG_STRING
from funnylang.compiler import Compiler
from funnylang.errors import FunnyError
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "dump_bytecode.funny"

CORPUS = sorted((ROOT / "tests" / "lang").glob("*.funny")) + sorted((ROOT / "examples").glob("**/*.funny"))


def _python_dump(path: Path) -> list[str] | None:
    """None means "this file is expected to fail resolution" (e.g. an
    undefined-variable or const-reassignment golden) -- both compilers
    refusing to compile it is the correct, matching behavior."""
    text = path.read_text(encoding="utf-8")
    source = SourceFile(str(path), text)
    program = parse_source(source)
    try:
        result = resolve_program(program, source)
    except FunnyError:
        return None
    unit = Compiler(result, source, fold_constants=True).compile_program(program, str(path))
    lines = [f"ENTRY {unit.entry_proto}"]
    for i, p in enumerate(unit.protos):
        lines.append(
            f"PROTO {i} name={json.dumps(p.name)} arity={p.arity} default_count={p.default_count} "
            f"is_variadic={1 if p.is_variadic else 0} upvalue_count={p.upvalue_count} "
            f"max_stack={p.max_stack} local_count={p.local_count}"
        )
        lines.append("CODE " + ",".join(str(b) for b in p.code))
        lines.append("LINES " + ",".join(f"{le.code_offset}:{le.line}:{le.col}" for le in p.lines))
    for i, (tag, value) in enumerate(unit.const_pool.entries):
        if tag == TAG_GHOST:
            rep = "ghost"
        elif tag == TAG_BOOL:
            rep = "fax" if value else "cap"
        elif tag == TAG_STRING:
            rep = json.dumps(value)
        else:
            rep = repr(value) if isinstance(value, float) else str(value)
        lines.append(f"CONST {i} tag={tag} value={rep}")
    return lines


def _selfhost_dump(path: Path) -> tuple[list[str] | None, str]:
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    if result.returncode != 0:
        return None, result.stderr
    return result.stdout.splitlines(), ""


@pytest.mark.parametrize("path", CORPUS, ids=lambda p: p.name)
def test_bytecode_matches_python(path):
    expected = _python_dump(path)
    actual, stderr = _selfhost_dump(path)
    if expected is None:
        assert actual is None, f"Python resolver rejected {path} but the self-hosted one accepted it"
        return
    assert actual is not None, f"self-hosted driver failed on {path} but the Python resolver accepted it:\n{stderr}"
    assert actual == expected
