from __future__ import annotations

import os

import pytest

from conftest import expect_error
from funnylang.errors import render_diagnostic

# 12 error kinds, each with a program that reliably triggers it.
CASES = {
    "LexerSaidNah": "yo x = @\n",
    "ParserHadAStroke": "yo 1 = 2\n",
    "WhoDis": "yap totally_undefined_name\n",
    "TypeVibeMismatch": 'yap "x" + 1\n',
    "MathAintMathin": "yap 1 / 0\n",
    "OutOfPocket": "yap [1,2,3][10]\n",
    "KeyGhosted": 'yap {"a": 1}["b"]\n',
    "GhostError": "yo x = ghost\nyap x.name\n",
    "NotACallableRizz": "yo x = 5\nyap x()\n",
    "WrongNumberOfHomies": "bet f(a, b) { bounce a }\nyap f(1)\n",
    "TooDeepBro": "bet f() { bounce f() }\nf()\n",
    "ImmutableVibes": "deadass PI = 3\nPI = 4\n",
}


@pytest.fixture(autouse=True)
def _clean_serious_env():
    had = os.environ.get("FUNNY_SERIOUS")
    yield
    if had is None:
        os.environ.pop("FUNNY_SERIOUS", None)
    else:
        os.environ["FUNNY_SERIOUS"] = had


@pytest.mark.parametrize("flavor,src", CASES.items())
def test_diagnostic_funny_mode_has_emoji_and_roast(flavor, src, monkeypatch):
    monkeypatch.delenv("FUNNY_SERIOUS", raising=False)
    err = expect_error(src)
    assert err.flavor == flavor
    rendered = render_diagnostic(err, color=False)
    assert "💀💀💀 FUNNYLANG MOMENT 💀💀💀" in rendered
    assert flavor in rendered
    assert err.roast in rendered
    assert "^ this right here" in rendered


@pytest.mark.parametrize("flavor,src", CASES.items())
def test_diagnostic_serious_mode_has_no_emoji(flavor, src, monkeypatch):
    monkeypatch.setenv("FUNNY_SERIOUS", "1")
    err = expect_error(src)
    assert err.flavor == flavor
    rendered = render_diagnostic(err, color=False)
    assert "FUNNYLANG ERROR" in rendered
    assert "💀" not in rendered
    assert "🥞" not in rendered
    assert "💡" not in rendered
    assert flavor in rendered
    assert err.message in rendered


def test_compile_time_errors_have_no_stack_of_shame(monkeypatch):
    monkeypatch.delenv("FUNNY_SERIOUS", raising=False)
    for flavor in ("LexerSaidNah", "ParserHadAStroke", "WhoDis", "ImmutableVibes"):
        err = expect_error(CASES[flavor])
        rendered = render_diagnostic(err, color=False)
        assert "stack of shame" not in rendered


def test_runtime_errors_have_stack_of_shame(monkeypatch):
    monkeypatch.delenv("FUNNY_SERIOUS", raising=False)
    for flavor in ("TypeVibeMismatch", "MathAintMathin", "OutOfPocket", "KeyGhosted", "GhostError", "NotACallableRizz", "WrongNumberOfHomies"):
        err = expect_error(CASES[flavor])
        rendered = render_diagnostic(err, color=False)
        assert "🥞 stack of shame:" in rendered


def test_caret_points_at_correct_column():
    err = expect_error('yap "x" + 1\n')
    assert err.span.col == 5  # 1-indexed: "yap " is 4 chars, "x" starts at col 5
    rendered = render_diagnostic(err, color=False)
    lines = rendered.splitlines()
    src_line = next(l for l in lines if l.split("│", 1)[-1].strip().startswith("yap"))
    caret_line = lines[lines.index(src_line) + 1]
    code_part = src_line.split("│", 1)[1]
    caret_part = caret_line.split("│", 1)[1]
    caret_index = caret_part.index("^")
    assert code_part[caret_index] == '"'  # the opening quote of "x", col 5


def test_hint_present_for_numba_yapstring_add():
    err = expect_error('yap "x" + 1\n')
    rendered = render_diagnostic(err, color=False)
    assert "💡 skill issue fix:" in rendered
    assert err.hint in rendered


def test_whodis_suggests_close_match():
    err = expect_error("yo greeting = 1\nyap greetng\n")
    assert "greeting" in (err.hint or "")


def test_parse_error_bundle_caps_display_and_summarizes():
    from funnylang.errors import render_parse_error_bundle
    from funnylang.parser import Parser
    from funnylang.lexer import Lexer
    from funnylang.source import SourceFile

    src = "\n".join("1 + 2 = 3" for _ in range(7))
    source = SourceFile("<test>", src)
    tokens = Lexer(source).tokenize()
    parser = Parser(tokens, source)
    from funnylang.errors import ParseErrorBundle

    with pytest.raises(ParseErrorBundle) as excinfo:
        parser.parse_program()
    rendered = render_parse_error_bundle(excinfo.value)
    assert rendered.count("ParserHadAStroke") == 5
    assert "... and 2 more. i'll stop." in rendered


def test_exit_code_1_for_uncaught_runtime_error():
    import subprocess
    import sys

    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", "tests/lang/err_div_zero.funny"],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 1


def test_computer_explode_exits_69(tmp_path):
    import subprocess
    import sys

    funny_file = tmp_path / "explode.funny"
    funny_file.write_text("gimme computer\ncomputer.explode()\n", encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(funny_file)],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 69
    assert "KERNEL PANIC" in result.stdout
