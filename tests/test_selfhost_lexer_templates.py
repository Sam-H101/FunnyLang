"""PLAN.md §M12 task 2: selfhost/lexer.funny's TEMPLATE token structure
(nested str/tokens parts, each `tokens` sub-list ending in an EOF sentinel)
cross-checked against funnylang/lexer.py's own `_scan_template`."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.lexer import Lexer
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "lex_template.funny"

SNIPPETS = [
    "yap `hi {name}, next {age + 1}!`\n",
    "yap `no substitutions here`\n",
    "yap `{just_one_expr}`\n",
    "yap `nested {f(1, 2)} call`\n",
    "yap `back \\` tick and \\n newline`\n",
    "yap `{a}{b}{c}`\n",
]


def _escape(s: str) -> str:
    return s.replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")


def _python_template_lines(src: str) -> list[str]:
    tokens = Lexer(SourceFile("<snippet>", src)).tokenize()
    lines = []
    for t in tokens:
        if t.kind.name != "TEMPLATE":
            continue
        for kind, payload in t.value:
            if kind == "str":
                lines.append("STR:" + _escape(payload))
            else:
                lines.append("TOKENS:" + ",".join(tok.kind.name for tok in payload))
    return lines


def _selfhost_template_lines(src: str, tmp_path) -> list[str]:
    f = tmp_path / "snippet.funny"
    f.write_text(src, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(f)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, f"driver failed on {src!r}:\n{result.stderr}"
    return result.stdout.splitlines()


@pytest.mark.parametrize("src", SNIPPETS)
def test_template_parts_match_python(src, tmp_path):
    assert _selfhost_template_lines(src, tmp_path) == _python_template_lines(src)
