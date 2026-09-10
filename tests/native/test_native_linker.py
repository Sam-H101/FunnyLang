"""NATIVE_PLAN.md N8 task 1: the linker, in FunnyLang.

`selfhost/bundler.funny` + `selfhost/linker.funny` replace
funnylang/modules.py's `build_bundle` and funnylang/serializer.py's
`dump_funnypak` -- between them, the last Python needed to *build* anything,
including the self-hosted compiler's own bootstrap bundle.

The bar is byte-identity, not merely "the bundle runs": a `.funnypak` is a
sequence of `.funnyc` blobs in a particular order, so an equally-correct
walk of the import graph in a different order still produces a different
file. Matching Python's bytes exactly is what proves the two agree about
resolution order, canonical names, and framing all at once.

Python appears here only as the oracle and to link the linker once, to
bootstrap it -- every bundle under test is produced by the C VM.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak, load_funnypak

ROOT = Path(__file__).resolve().parent.parent.parent
LINKER_ENTRY = ROOT / "selfhost" / "linker.funny"
COMPILER_ENTRY = ROOT / "selfhost" / "funnyc.funny"


@pytest.fixture(scope="session")
def linker_pak(tmp_path_factory):
    """The self-hosted linker, linked by the Python one -- the single
    bootstrap step, and the only place Python builds anything here."""
    units, entry_canonical = build_bundle(str(LINKER_ENTRY))
    pak = tmp_path_factory.mktemp("linker") / "linker.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _native_link(binary: Path, linker: Path, entry: Path, out: Path) -> None:
    result = subprocess.run(
        [str(binary), str(linker), str(entry), str(out)],
        capture_output=True, text=True, timeout=300,
    )
    assert result.returncode == 0, f"linker failed:\n{result.stdout}\n{result.stderr}"
    assert out.exists(), f"linker reported success but wrote nothing:\n{result.stdout}"


def _python_link(entry: Path, out: Path) -> None:
    units, entry_canonical = build_bundle(str(entry))
    out.write_bytes(dump_funnypak(units, entry_canonical))


def test_linker_matches_python_bundler_byte_for_byte(native_binary, linker_pak, tmp_path):
    """The compiler's own bundle -- six modules, a diamond in the import
    graph (compiler.funny and parser.funny both reach prelude.funny), and
    the largest thing in the tree."""
    mine = tmp_path / "mine.funnypak"
    theirs = tmp_path / "theirs.funnypak"
    _native_link(native_binary, linker_pak, COMPILER_ENTRY, mine)
    _python_link(COMPILER_ENTRY, theirs)
    assert mine.read_bytes() == theirs.read_bytes()


def test_linker_links_itself_to_the_same_bytes(native_binary, linker_pak, tmp_path):
    """A linker linked by Python and a linker linked by *itself* must be
    interchangeable -- the fixed point that says the FunnyLang implementation
    is not merely close to the Python one."""
    stage2 = tmp_path / "linker_stage2.funnypak"
    _native_link(native_binary, linker_pak, LINKER_ENTRY, stage2)
    assert stage2.read_bytes() == linker_pak.read_bytes()

    from_stage1 = tmp_path / "from_stage1.funnypak"
    from_stage2 = tmp_path / "from_stage2.funnypak"
    _native_link(native_binary, linker_pak, COMPILER_ENTRY, from_stage1)
    _native_link(native_binary, stage2, COMPILER_ENTRY, from_stage2)
    assert from_stage1.read_bytes() == from_stage2.read_bytes()


def test_linked_compiler_actually_compiles(native_binary, linker_pak, tmp_path):
    """End to end with no Python in the loop past the bootstrap: the C VM
    runs the FunnyLang linker to build the compiler, then runs that compiler
    to build a program, then runs the program."""
    compiler_pak = tmp_path / "funnyc.funnypak"
    _native_link(native_binary, linker_pak, COMPILER_ENTRY, compiler_pak)

    src = tmp_path / "answer.funny"
    src.write_text("yap 6 * 7\n", encoding="utf-8")
    out = tmp_path / "answer.funnyc"
    compile_result = subprocess.run(
        [str(native_binary), str(compiler_pak), str(src), str(out)],
        capture_output=True, text=True, timeout=300,
    )
    assert compile_result.returncode == 0, compile_result.stderr

    run_result = subprocess.run(
        [str(native_binary), str(out)], capture_output=True, text=True, timeout=60,
    )
    assert run_result.returncode == 0, run_result.stderr
    assert run_result.stdout.strip() == "42"


def test_module_order_and_entry_match_python(native_binary, linker_pak, tmp_path):
    """Byte-identity already implies this, but when it breaks, *this* is the
    assertion that says why -- a reordered walk is the likeliest cause and
    the least legible one to read out of a byte offset."""
    mine = tmp_path / "mine.funnypak"
    _native_link(native_binary, linker_pak, COMPILER_ENTRY, mine)
    mine_modules, mine_entry = load_funnypak(mine.read_bytes())
    theirs_modules, theirs_entry = build_bundle(str(COMPILER_ENTRY))
    assert list(mine_modules) == list(theirs_modules)
    assert mine_entry == theirs_entry


def test_relative_imports_resolve_from_the_importing_file(native_binary, linker_pak, tmp_path):
    """§3.8 resolves a quoted `gimme` against the *importing* file's own
    directory, not the entry's -- so a module in a subdirectory importing a
    sibling must find it, and its canonical name must stay relative to the
    entry."""
    (tmp_path / "lib").mkdir()
    (tmp_path / "lib" / "helper.funny").write_text("flex bet twice(x) { bounce x * 2 }\n", encoding="utf-8")
    (tmp_path / "lib" / "mid.funny").write_text(
        'gimme { twice } from "helper.funny"\nflex bet quad(x) { bounce twice(twice(x)) }\n', encoding="utf-8"
    )
    entry = tmp_path / "main.funny"
    entry.write_text('gimme { quad } from "lib/mid.funny"\nyap quad(3)\n', encoding="utf-8")

    mine = tmp_path / "mine.funnypak"
    theirs = tmp_path / "theirs.funnypak"
    _native_link(native_binary, linker_pak, entry, mine)
    _python_link(entry, theirs)
    assert mine.read_bytes() == theirs.read_bytes()

    modules, _ = load_funnypak(mine.read_bytes())
    assert sorted(modules) == ["lib/helper.funny", "lib/mid.funny", "main.funny"]

    result = subprocess.run([str(native_binary), str(mine)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "12"


def test_missing_import_is_reported_not_silently_dropped(native_binary, linker_pak, tmp_path):
    """FUNNY_SERIOUS because funny mode prints the flavor's roast ("skill
    issue.") in place of the message, per PLAN.md §4.2 -- the point here is
    that the message names the import that could not be found."""
    entry = tmp_path / "broken.funny"
    entry.write_text('gimme { nope } from "not_here.funny"\nyap 1\n', encoding="utf-8")
    out = tmp_path / "broken.funnypak"
    result = subprocess.run(
        [str(native_binary), str(linker_pak), str(entry), str(out)],
        capture_output=True, text=True, timeout=60,
        env={**os.environ, "FUNNY_SERIOUS": "1"},
    )
    assert result.returncode != 0
    assert "not_here.funny" in (result.stdout + result.stderr)
    assert not out.exists()
