"""NATIVE_PLAN.md N6's acceptance gate: for every `err_*.funny` golden, the
C VM's *stderr* is byte-identical to the Python VM's, in both funny and
serious modes.

Strictly stronger than the differential suite next door, which only checks
stdout and (for these programs) the error flavour. Here the whole rendered
diagnostic has to match: the source snippet, the caret column, the roast,
the fix hint, and the stack of shame -- PLAN.md §4.2's exact shape.

The expected side is rendered live by `funnylang.errors.render_diagnostic`
rather than read from checked-in files, so the two implementations can
never quietly drift apart behind a stale golden.
"""
from __future__ import annotations

import io
import os
import subprocess
from pathlib import Path

import pytest

from funnylang.compiler import Compiler
from funnylang.errors import FunnyError, render_diagnostic
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.serializer import dump_funnyc
from funnylang.source import SourceFile
from funnylang.stdlib import install_stdlib
from funnylang.vm import VM

LANG_DIR = Path(__file__).resolve().parent.parent / "lang"
EXAMPLES_DIR = Path(__file__).resolve().parent.parent.parent / "examples"

# These three fail during *resolution*, before any bytecode exists, so the C
# VM never sees them -- it runs compiled units and has no compiler of its own
# until N8. tests/test_bootstrap.py and test_selfhost_emitter.py carve out the
# same files for the same reason.
COMPILE_TIME_ONLY = {
    "err_undefined_variable",
    "err_immutable_reassign",
    "err_ptr_address_of_const",
}


def _programs():
    paths = sorted(LANG_DIR.glob("err_*.funny"))
    paths.append(LANG_DIR / "squad_undefined_method_is_whodis.funny")
    paths.append(EXAMPLES_DIR / "errors.funny")
    return [p for p in paths if p.stem not in COMPILE_TIME_ONLY]


def _python_stderr(path: Path, serious: bool) -> str:
    """What `funny run <path>` prints to stderr, straight from the renderer."""
    source = SourceFile(str(path), path.read_text(encoding="utf-8"))
    program = parse_source(source)
    resolved = resolve_program(program, source)
    unit = Compiler(resolved, source).compile_program(program, str(path))
    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    previous = os.environ.get("FUNNY_SERIOUS")
    os.environ["FUNNY_SERIOUS"] = "1" if serious else "0"
    try:
        vm.interpret(unit, source)
    except FunnyError as err:
        return render_diagnostic(err, color=False)
    finally:
        if previous is None:
            os.environ.pop("FUNNY_SERIOUS", None)
        else:
            os.environ["FUNNY_SERIOUS"] = previous
    raise AssertionError(f"{path.name} was expected to raise, but didn't")


def _compile_to(path: Path, out: Path) -> None:
    source = SourceFile(str(path), path.read_text(encoding="utf-8"))
    program = parse_source(source)
    resolved = resolve_program(program, source)
    unit = Compiler(resolved, source).compile_program(program, str(path))
    out.write_bytes(dump_funnyc(unit))


@pytest.mark.parametrize("serious", [False, True], ids=["funny", "serious"])
@pytest.mark.parametrize("path", _programs(), ids=lambda p: p.stem)
def test_native_stderr_matches_python(path, serious, native_binary, tmp_path):
    expected = _python_stderr(path, serious)
    funnyc = tmp_path / f"{path.stem}.funnyc"
    _compile_to(path, funnyc)
    flags = ["--no-color"] + (["--serious"] if serious else [])
    result = subprocess.run(
        [str(native_binary), *flags, str(funnyc)], capture_output=True, text=True, timeout=60
    )
    assert result.stderr == expected


@pytest.mark.skipif(not hasattr(os, "openpty"), reason="needs a pty to make the native VM see a tty")
def test_colour_turns_on_for_a_tty_and_lands_in_the_same_places(native_binary, tmp_path):
    """`--no-color` is the mode the goldens above pin down; this covers the
    other half -- that colour switches itself on for a terminal, and that the
    ANSI codes wrap exactly the spans Python wraps (header, flavour, caret).

    stdout gets the pty (that's what `_use_color` checks, oddly enough, even
    though the diagnostic goes to stderr) while stderr stays an ordinary pipe
    so it can still be captured on its own.
    """
    import pty

    path = LANG_DIR / "err_type_mismatch.funny"
    source = SourceFile(str(path), path.read_text(encoding="utf-8"))
    program = parse_source(source)
    resolved = resolve_program(program, source)
    unit = Compiler(resolved, source).compile_program(program, str(path))
    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    expected = None
    try:
        vm.interpret(unit, source)
    except FunnyError as err:
        expected = render_diagnostic(err, color=True)
    assert expected is not None and "\x1b[" in expected

    funnyc = tmp_path / "colour.funnyc"
    funnyc.write_bytes(dump_funnyc(unit))
    primary, secondary = pty.openpty()
    try:
        result = subprocess.run(
            [str(native_binary), str(funnyc)], stdout=secondary, stderr=subprocess.PIPE, timeout=60
        )
    finally:
        os.close(secondary)
        os.close(primary)
    assert result.stderr.decode("utf-8") == expected


# Stdlib errors have no `err_*.funny` golden of their own, but their roasts
# come from the same §4.1 mechanism and are just as easy to get wrong. These
# cover the three modules whose Python counterpart passes an explicit
# `roast=` and which this runtime has ported so far -- filez, internet and
# computer. The rest of the stdlib still falls back to its flavour's default
# roast where Python would print something more specific; see NATIVE_PLAN.md
# §9 for the list.
STDLIB_ERROR_PROGRAMS = {
    "filez_missing": 'gimme filez\nfilez.slurp("definitely/not/here.txt")\n',
    "internet_no_net": 'gimme internet\ninternet.go_brrrr("http://example.com/")\n',
    "computer_blue_screen": "gimme computer\nyo x = computer.blue_screen()\n",
}


@pytest.mark.parametrize("serious", [False, True], ids=["funny", "serious"])
@pytest.mark.parametrize("name", sorted(STDLIB_ERROR_PROGRAMS), ids=sorted(STDLIB_ERROR_PROGRAMS))
def test_stdlib_error_diagnostics_match_python(name, serious, native_binary, tmp_path, monkeypatch):
    monkeypatch.setenv("FUNNY_NO_NET", "1")  # internet_no_net must not touch the network
    source_path = tmp_path / f"{name}.funny"
    source_path.write_text(STDLIB_ERROR_PROGRAMS[name], encoding="utf-8")
    expected = _python_stderr(source_path, serious)
    funnyc = tmp_path / f"{name}.funnyc"
    _compile_to(source_path, funnyc)
    flags = ["--no-color"] + (["--serious"] if serious else [])
    result = subprocess.run(
        [str(native_binary), *flags, str(funnyc)], capture_output=True, text=True, timeout=60
    )
    assert result.stderr == expected


def test_serious_and_funny_modes_actually_differ():
    """Guards the test above from passing vacuously: if the two modes ever
    rendered the same text, every comparison here would still 'pass'."""
    path = LANG_DIR / "err_div_zero.funny"
    assert _python_stderr(path, serious=False) != _python_stderr(path, serious=True)
