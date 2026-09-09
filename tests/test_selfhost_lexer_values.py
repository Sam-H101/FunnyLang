"""PLAN.md §M12 task 2: selfhost/lexer.funny's literal *values* (not just
token kinds/positions, covered separately in test_selfhost_lexer.py),
targeting the trickiest corners the tests/lang/ corpus doesn't happen to
exercise: radix integers, underscores, float exponents, escape sequences
(including \\u{...}), and triple-quoted strings."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from funnylang.lexer import Lexer
from funnylang.source import SourceFile
from funnylang.tokens import TokenKind
from funnylang.values import to_display

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "lex_values.funny"

SNIPPETS = [
    "42",
    "0",
    "1_000_000",
    "0xFF",
    "0xff",
    "0x1_0",
    "0b1010",
    "0o17",
    "3.14",
    "0.5",
    "1e10",
    "1e-10",
    "1E+5",
    "2.5e3",
    '"hello"',
    "'single'",
    '"line1\\nline2\\ttab"',
    '"quote\\"inside"',
    '"unicode \\u{1F480}"',
    '"unicode \\u{4E2D}"',
    '"""triple quoted, no newline"""',
    '"""line one\nline two\nline three"""',
    "hello_world",
    "_leading_underscore",
    "café",
    "中文变量",
    "x1 y2 z3",
]


def _escape(s: str) -> str:
    return s.replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")


def _python_values(src: str):
    tokens = Lexer(SourceFile("<snippet>", src)).tokenize()
    out = []
    for t in tokens:
        if t.kind in (TokenKind.NEWLINE, TokenKind.EOF):
            continue
        value = to_display(t.value) if t.value is not None else "ghost"
        out.append((t.kind.name, _escape(t.text), _escape(value)))
    return out


def _selfhost_values(src: str, tmp_path):
    f = tmp_path / "snippet.funny"
    f.write_text(src, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(f)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, f"driver failed on {src!r}:\n{result.stderr}"
    out = []
    for line in result.stdout.splitlines():
        kind, text, value = line.split("\x1f")
        out.append((kind, text, value))
    return out


def test_literal_values_match_python(tmp_path):
    for src in SNIPPETS:
        expected = _python_values(src)
        actual = _selfhost_values(src, tmp_path)
        assert actual == expected, f"mismatch for {src!r}: {actual} != {expected}"
