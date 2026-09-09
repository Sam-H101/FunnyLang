"""PLAN.md §M12 task 2, third file: selfhost/parser.funny, cross-checked
against funnylang/parser.py's own dump_ast() S-expression output across the
entire tests/lang/ + examples/ corpus -- the byte-exactness discipline
(§M12 task 3) applies to the AST just as much as tokens: any structural
disagreement here means the self-hosted compiler would generate different
bytecode than the Python one for the same source."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.ast_nodes import dump_ast
from funnylang.parser import parse_source
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "dump_ast.funny"

# Every `err_*.funny` golden in the corpus tests a *runtime* error flavor
# (MathAintMathin, TypeVibeMismatch, ...), not a parse error -- they all
# parse cleanly, so they're included like any other file.
CORPUS = sorted((ROOT / "tests" / "lang").glob("*.funny")) + sorted((ROOT / "examples").glob("**/*.funny"))


def _python_dump(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    program = parse_source(SourceFile(str(path), text))
    return dump_ast(program)


def _selfhost_dump(path: Path) -> str:
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, f"driver failed on {path}:\n{result.stderr}"
    return result.stdout.rstrip("\n")


@pytest.mark.parametrize("path", CORPUS, ids=lambda p: p.name)
def test_ast_matches_python(path):
    assert _selfhost_dump(path) == _python_dump(path)
