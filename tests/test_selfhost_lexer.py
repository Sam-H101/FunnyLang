"""PLAN.md §M12 task 2, second file: selfhost/lexer.funny, cross-checked
against funnylang/lexer.py's own tokenization -- the byte-exactness
discipline (§M12 task 3) applies to the lexer just as much as the emitter:
if the self-hosted lexer disagrees with the Python one on a single token's
kind, line, or column anywhere in the corpus, self-hosting can't work."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.lexer import Lexer
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "lex_positions.funny"

CORPUS = sorted((ROOT / "tests" / "lang").glob("*.funny")) + sorted((ROOT / "examples").glob("**/*.funny"))


def _python_positions(path: Path) -> list[tuple[str, int, int]]:
    text = path.read_text(encoding="utf-8")
    tokens = Lexer(SourceFile(str(path), text)).tokenize()
    return [(t.kind.name, t.span.line, t.span.col) for t in tokens]


def _selfhost_positions(path: Path) -> list[tuple[str, int, int]]:
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, f"driver failed on {path}:\n{result.stderr}"
    out = []
    for line in result.stdout.splitlines():
        kind, line_no, col = line.rsplit(" ", 2)
        out.append((kind, int(line_no), int(col)))
    return out


@pytest.mark.parametrize("path", CORPUS, ids=lambda p: p.name)
def test_lexer_positions_match_python(path):
    assert _selfhost_positions(path) == _python_positions(path)
