"""NATIVE_PLAN.md N8 task 5: `funny vibe`, the REPL, in FunnyLang.

What separates a REPL from every other subcommand is persistence — `yo x = 1`
on one line has to still be there on the next — and that needed three things
the language did not have: a VM that stays alive between inputs
(`sus.new_session`/`sus.run_in`), a resolver that can be handed the names the
previous input defined, and a compiler that leaves a bare trailing
expression's value for RETURN instead of popping it. Plus
`computer.readline`, since `ask()` returns `""` for both an empty line and
end of input and a REPL has to tell Ctrl-D from a blank line.

Each case runs the same scripted session through both REPLs and compares
what comes out after the banner.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak

ROOT = Path(__file__).resolve().parent.parent.parent
VIBE_ENTRY = ROOT / "selfhost" / "vibecli.funny"  # the CLI wrapper; vibe.funny is a library


@pytest.fixture(scope="session")
def vibe_pak(tmp_path_factory):
    units, entry_canonical = build_bundle(str(VIBE_ENTRY))
    pak = tmp_path_factory.mktemp("vibe") / "vibe.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _after_banner(text: str) -> str:
    """The banner is ASCII art plus a version line; everything from the
    "type .help" line on is the session itself."""
    marker = "type .help for help, .exit to leave.\n"
    _, _, rest = text.partition(marker)
    return rest


def _native(binary: Path, pak: Path, script: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(binary), str(pak)], input=script,
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=300,
    )


def _python(script: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-m", "funnylang", "vibe"], input=script,
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=300,
    )


SESSIONS = {
    "expressions and statements": 'yo x = 5\nx + 3\nyap "hi"\n[1, 2, 3]\n"a string"\nghost\n.exit\n',
    "globals persist": "yo x = 10\nbet f() { bounce x * 2 }\nf()\n.exit\n",
    "multi-line block": "bet g(n) {\n    bounce n * n\n}\ng(7)\n.exit\n",
    "squad across lines": "squad P {\n    spawn(a) { me.a = a }\n    bet go() { bounce me.a * 2 }\n}\nP(21).go()\n.exit\n",
    "help": ".help\n.exit\n",
    "clear resets globals": "yo a = 1\na\n.clear\n.exit\n",
    "blank lines": "\n\nyo q = 1\nq\n.exit\n",
    "quit is exit": "yo v = 2\nv\n.quit\n",
    "ctrl-d ends the session": "yo z = 3\nz\n",
    "stdlib import": "gimme mafs\nmafs.sqrt(16)\n.exit\n",
    "closures across inputs": "bet counter() {\n    yo n = 0\n    bounce lowkey () => { n += 1\n bounce n }\n}\nyo c = counter()\nc()\nc()\n.exit\n",
}


@pytest.mark.parametrize("name", sorted(SESSIONS))
def test_session_matches_the_python_repl(native_binary, vibe_pak, name):
    script = SESSIONS[name]
    mine = _native(native_binary, vibe_pak, script)
    theirs = _python(script)
    assert mine.returncode == theirs.returncode == 0
    assert _after_banner(mine.stdout) == _after_banner(theirs.stdout)


def test_a_syntax_error_does_not_end_the_session(native_binary, vibe_pak):
    """The error text differs — the Python REPL renders the full §4.2
    diagnostic because it still has the input text next to the error, and a
    FunnyLang port of that renderer is a separate job — so this asserts the
    behaviour instead: the flavor is right, and the session keeps going."""
    result = _native(native_binary, vibe_pak, 'yo x = 1 +\nyo y = 9\ny\n.exit\n')
    assert result.returncode == 0
    body = _after_banner(result.stdout)
    assert "ParserHadAStroke" in body
    # "funny> 9\n" -- the prompt and the value it produced share a line,
    # since the prompt is written before the input is read back.
    assert "> 9\n" in body, "the session survived and evaluated y"


def test_a_runtime_error_does_not_end_the_session(native_binary, vibe_pak):
    result = _native(native_binary, vibe_pak, '[1][9]\n"still here"\n.exit\n')
    assert result.returncode == 0
    body = _after_banner(result.stdout)
    assert "OutOfPocket" in body
    assert '"still here"' in body


def test_an_undefined_name_is_a_whodis_at_compile_time(native_binary, vibe_pak):
    result = _native(native_binary, vibe_pak, "nope_not_defined\n.exit\n")
    assert result.returncode == 0
    assert "WhoDis" in _after_banner(result.stdout)


def test_clear_really_forgets(native_binary, vibe_pak):
    result = _native(native_binary, vibe_pak, "yo gone = 1\n.clear\ngone\n.exit\n")
    assert result.returncode == 0
    body = _after_banner(result.stdout)
    assert "session cleared." in body
    assert "WhoDis" in body


def test_xray_disassembles_without_touching_the_session(native_binary, vibe_pak):
    result = _native(native_binary, vibe_pak, "yo keep = 7\n.xray 1 + 2\nkeep\n.exit\n")
    assert result.returncode == 0
    body = _after_banner(result.stdout)
    assert "== <script> (entry) (arity 0) ==" in body
    assert "CONST" in body
    assert "> 7\n" in body, "the session survived the .xray"


def test_time_reports_two_decimals_and_the_value(native_binary, vibe_pak):
    """cli.py formats this with an f-string `:.2f`, so the shape has to
    match even though the number itself never can."""
    import re

    result = _native(native_binary, vibe_pak, ".time 2 + 2\n.exit\n")
    assert result.returncode == 0
    body = _after_banner(result.stdout)
    assert re.search(r"ran in \d+\.\d\dms", body), body
    assert "\n4\n" in body


def test_dip_inside_the_repl_does_not_kill_the_repl(native_binary, vibe_pak):
    """`dip()` is the session exiting, not the host — the same isolation
    `funny test` relies on."""
    result = _native(native_binary, vibe_pak, 'dip(0)\n"still here"\n.exit\n')
    assert result.returncode == 0
    assert '"still here"' in _after_banner(result.stdout)


READLINE_PROBE = """gimme computer

yo n = 0
bruh (fax) {
    yo line = computer.readline("> ")
    sus (line == ghost) {
        yap "EOF after " + to_yap(n)
        bail
    }
    n += 1
    yap "got: " + sheesh(line)
}
yap what_is_it(computer.readline())
"""


def test_readline_distinguishes_a_blank_line_from_end_of_input(native_binary, tmp_path):
    """`ask()` returns "" for both, which is why the REPL could not use it.
    The blank line in the middle of this input has to come back as "" and the
    end of input as `ghost`, on both VMs."""
    from funnylang.modules import build_bundle
    from funnylang.serializer import dump_funnypak

    src = tmp_path / "readline.funny"
    src.write_text(READLINE_PROBE, encoding="utf-8", newline="")
    pak = tmp_path / "readline.funnypak"
    units, entry = build_bundle(str(src))
    pak.write_bytes(dump_funnypak(units, entry))

    script = "a\nb\n\nc\n"
    mine = subprocess.run([str(native_binary), str(pak)], input=script,
                          capture_output=True, text=True, encoding="utf-8", timeout=60)
    theirs = subprocess.run([sys.executable, "-m", "funnylang", "run", str(src)], input=script,
                            capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=60)
    assert mine.returncode == theirs.returncode == 0
    assert mine.stdout == theirs.stdout
    # sheesh() prints the repr and returns the value, so a blank line shows
    # up as a bare `""` on the prompt's line and an empty `got: ` after it.
    assert '> ""\n' in mine.stdout, "a blank line is an empty string, not end of input"
    assert "\ngot: \n" in mine.stdout
    assert "EOF after 4" in mine.stdout
    assert mine.stdout.rstrip().endswith("ghost"), "readline past the end keeps returning ghost"
