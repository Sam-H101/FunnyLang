"""NATIVE_PLAN.md N8 task 3: `funny fmt`, in FunnyLang.

`selfhost/fmt.funny` ports funnylang/formatter.py and `selfhost/fmtcli.funny`
ports cli.py's `cmd_fmt`. Comment-dropping is ported, not fixed: it is
inherent to formatting from an AST that never recorded comments, and fixing
it means changing what the *parser* retains -- which would put this formatter
out of step with the one it has to match byte for byte.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.formatter import format_program
from funnylang.modules import build_bundle
from funnylang.parser import parse_source
from funnylang.serializer import dump_funnypak
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent.parent
FMT_ENTRY = ROOT / "selfhost" / "fmtcli.funny"

# The constructs whose formatting has structure worth pinning: chained
# else-branches that append to the previous physical line, squads with a
# spawn and one-line methods, block-bodied lambdas (which format at a
# relative depth and get shifted), every import spelling, templates, and
# pointers.
CORPUS = [
    "examples/hello.funny",
    "examples/fizzbuzz.funny",
    "examples/closures.funny",
    "examples/squads.funny",
    "tests/lang/squad_magic_to_yap.funny",
    "tests/lang/template_strings.funny",
    "tests/lang/defaults.funny",
    "tests/lang/ptr_five_kinds.funny",
    "tests/lang/break_continue_nested.funny",
    "selfhost/prelude.funny",
]


@pytest.fixture(scope="session")
def fmt_pak(tmp_path_factory):
    units, entry_canonical = build_bundle(str(FMT_ENTRY))
    pak = tmp_path_factory.mktemp("fmt") / "fmt.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _python_format(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return format_program(parse_source(SourceFile(str(path), text)))


def _native_format(binary: Path, pak: Path, path: Path, *flags: str):
    return subprocess.run(
        [str(binary), str(pak), *flags, str(path)],
        capture_output=True, text=True, encoding="utf-8", timeout=300,
    )


@pytest.mark.parametrize("target", CORPUS)
def test_formatting_matches_python(native_binary, fmt_pak, tmp_path, target):
    src = ROOT / target
    subject = tmp_path / Path(target).name
    subject.write_text(src.read_text(encoding="utf-8"), encoding="utf-8", newline="")

    result = _native_format(native_binary, fmt_pak, subject)
    assert result.returncode == 0, result.stderr
    assert subject.read_text(encoding="utf-8") == _python_format(src)


@pytest.mark.parametrize("target", CORPUS)
def test_formatting_is_idempotent(native_binary, fmt_pak, tmp_path, target):
    """Formatting formatted output must change nothing -- and `--check` must
    then agree, which is the property the CLI actually promises."""
    subject = tmp_path / Path(target).name
    subject.write_text((ROOT / target).read_text(encoding="utf-8"), encoding="utf-8", newline="")

    _native_format(native_binary, fmt_pak, subject)
    once = subject.read_text(encoding="utf-8")
    _native_format(native_binary, fmt_pak, subject)
    assert subject.read_text(encoding="utf-8") == once

    check = _native_format(native_binary, fmt_pak, subject, "--check")
    assert check.returncode == 0, check.stderr


def test_check_reports_unformatted_on_stderr(native_binary, fmt_pak, tmp_path):
    """cli.py puts this message on stderr and exits 1. `yell`, added this
    milestone, is the only way a FunnyLang program can reach stderr."""
    subject = tmp_path / "messy.funny"
    subject.write_text("yo   x=1\nyap    x\n", encoding="utf-8", newline="")
    before = subject.read_text(encoding="utf-8")

    result = _native_format(native_binary, fmt_pak, subject, "--check")
    assert result.returncode == 1
    assert result.stdout == ""
    assert "isn't formatted" in result.stderr
    assert str(subject) in result.stderr
    assert subject.read_text(encoding="utf-8") == before, "--check must not rewrite the file"


def test_formatting_announces_only_when_it_changed(native_binary, fmt_pak, tmp_path):
    subject = tmp_path / "messy.funny"
    subject.write_text("yo   x=1\nyap    x\n", encoding="utf-8", newline="")

    first = _native_format(native_binary, fmt_pak, subject)
    assert first.returncode == 0
    assert first.stdout.strip() == f"formatted {subject}."

    second = _native_format(native_binary, fmt_pak, subject)
    assert second.returncode == 0
    assert second.stdout == "", "an already-formatted file should print nothing"


def test_comments_are_dropped_like_the_python_formatter(native_binary, fmt_pak, tmp_path):
    """Pinned deliberately: this is the ported limitation, and a future
    change that starts preserving comments must break this test loudly
    rather than silently diverge from funnylang/formatter.py."""
    subject = tmp_path / "commented.funny"
    subject.write_text("// a comment\nyo x = 1\nyap x  // trailing\n", encoding="utf-8", newline="")

    result = _native_format(native_binary, fmt_pak, subject)
    assert result.returncode == 0
    formatted = subject.read_text(encoding="utf-8")
    assert formatted == "yo x = 1\nyap x\n"
    assert formatted == _python_format(subject)


def test_matches_the_python_cli_end_to_end(native_binary, fmt_pak, tmp_path):
    """Both CLIs run over the same messy file: same bytes on disk, same
    stdout, same exit code."""
    messy = "squad P{spawn(a){me.a=a}\nbet go(){bounce me.a*2}}\nyo p=P(3)\nyap p.go()\n"
    mine = tmp_path / "mine.funny"
    theirs = tmp_path / "theirs.funny"
    mine.write_text(messy, encoding="utf-8", newline="")
    theirs.write_text(messy, encoding="utf-8", newline="")

    a = _native_format(native_binary, fmt_pak, mine)
    b = subprocess.run(
        [sys.executable, "-m", "funnylang", "fmt", str(theirs)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=300,
    )
    assert a.returncode == b.returncode == 0
    assert mine.read_text(encoding="utf-8") == theirs.read_text(encoding="utf-8")
    assert a.stdout.replace(str(mine), "F") == b.stdout.replace(str(theirs), "F")
