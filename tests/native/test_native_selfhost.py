"""The point of the whole exercise: FunnyLang compiling FunnyLang, with no
Python in the loop.

The C VM loads the self-hosted compiler (`selfhost/`, itself FunnyLang,
linked into a PLAN.md §5.3 bundle), runs it to compile a `.funny` file to
bytecode, and then runs that bytecode. Python appears here only to *link*
the bundle -- a dev-time step -- and to be the oracle the result is compared
against. (The other dev-time generators, tools/gen_unicode.funny and
tools/bin2c.funny, are FunnyLang: N11 leaves no .py anywhere.)

What this actually exercises, beyond "it works": bundled-module imports on
the C VM (`gimme { compile_source } from "compiler.funny"`), which need
per-closure constant pools, since a call from one bundled module into
another crosses compiled units mid-execution.
"""
from __future__ import annotations

import io
import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak
from funnylang.stdlib import install_stdlib
from funnylang.vm import VM

ROOT = Path(__file__).resolve().parent.parent.parent
SELFHOST_ENTRY = ROOT / "selfhost" / "funnyc.funny"

# A representative slice rather than the whole corpus: enough to cover
# functions, closures, squads, collections and control flow without making
# the suite pay for 60+ compile-and-run round trips.
CORPUS = ["hello", "fizzbuzz", "fib", "closures", "squads", "arrays"]


@pytest.fixture(scope="session")
def selfhost_pak(tmp_path_factory):
    """The self-hosted compiler, linked the way `funny build` links it."""
    units, entry_canonical = build_bundle(str(SELFHOST_ENTRY))
    pak = tmp_path_factory.mktemp("selfhost") / "funnyc.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _native_compile(binary: Path, pak: Path, source: Path, out: Path) -> None:
    """Compile `source` using the C VM running the self-hosted compiler."""
    result = subprocess.run(
        [str(binary), str(pak), str(source), str(out)], capture_output=True, text=True, timeout=300
    )
    assert result.returncode == 0, f"native compile of {source.name} failed:\n{result.stdout}\n{result.stderr}"
    assert out.exists(), f"native compile of {source.name} produced no output"


def _native_run(binary: Path, funnyc: Path) -> str:
    result = subprocess.run([str(binary), str(funnyc)], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, f"native run failed:\n{result.stdout}\n{result.stderr}"
    return result.stdout


def test_c_vm_runs_the_selfhosted_compiler(native_binary, selfhost_pak, tmp_path):
    """The headline: compile and run a program without Python touching it."""
    source = tmp_path / "tiny.funny"
    source.write_text("yap 1 + 1\n", encoding="utf-8")
    out = tmp_path / "tiny.funnyc"
    _native_compile(native_binary, selfhost_pak, source, out)
    assert _native_run(native_binary, out) == "2\n"


@pytest.mark.parametrize("name", CORPUS)
def test_natively_compiled_corpus_matches_its_golden(name, native_binary, selfhost_pak, tmp_path):
    source = ROOT / "examples" / f"{name}.funny"
    if not source.exists():
        source = ROOT / "tests" / "lang" / f"{name}.funny"
    expected = source.with_suffix(".expected").read_text(encoding="utf-8")
    out = tmp_path / f"{name}.funnyc"
    _native_compile(native_binary, selfhost_pak, source, out)
    assert _native_run(native_binary, out) == expected


def test_checked_in_toolchain_blob_is_not_stale():
    """`native/toolchain_blob.c` is the one generated artifact in the tree,
    and since N10 task 1 it is what makes the binary work at all: the whole
    toolchain, compiled in as a byte array. A checked-in artifact is only
    safe while something notices when it stops matching its sources --
    linking is deterministic, so this is a byte comparison against a fresh
    link of `selfhost/`. See bootstrap/STAGE0.md to regenerate it."""
    from funnylang.modules import build_bundle
    from funnylang.serializer import dump_funnypak

    blob_c = ROOT / "native" / "toolchain_blob.c"
    assert blob_c.exists(), "native/toolchain_blob.c is missing; see bootstrap/STAGE0.md"
    text = blob_c.read_text(encoding="utf-8")

    declared = int(re.search(r"toolchain_blob_len = (\d+)u;", text).group(1))
    body = text[text.index("toolchain_blob[] = {") + len("toolchain_blob[] = {"):text.rindex("};")]
    embedded = bytes(int(n) for n in body.split(",") if n.strip())
    assert len(embedded) == declared, "the array and its declared length disagree"

    units, entry_canonical = build_bundle(str(ROOT / "selfhost" / "cli.funny"))
    assert embedded == dump_funnypak(units, entry_canonical), (
        "native/toolchain_blob.c is stale -- selfhost/ changed without it being regenerated. "
        "Run: funny build selfhost/cli.funny -o bootstrap/cli.funnypak && "
        "funny tools/bin2c.funny -- bootstrap/cli.funnypak native/toolchain_blob.c toolchain_blob"
    )


def test_cli_runs_a_source_file_in_one_command(native_binary, tmp_path):
    """`funny foo.funny` -- compile and run, no Python, one command. Since
    N10 task 1 the toolchain is embedded in the binary, so this needs nothing
    set up at all: the binary on its own is the whole install."""
    source = tmp_path / "hello.funny"
    source.write_text('yap "hi from source"\n', encoding="utf-8")
    result = subprocess.run(
        [str(native_binary), str(source)], capture_output=True, text=True, timeout=300, cwd=tmp_path
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "hi from source\n"


def _python(*args: str, cwd: Path = ROOT) -> subprocess.CompletedProcess:
    """The reference CLI, with PYTHONPATH set explicitly -- several callers run
    with `cwd` somewhere else, and `python -m funnylang` otherwise resolves
    only from the repo root or a pip install."""
    env = {**os.environ, "PYTHONPATH": os.pathsep.join(filter(None, [str(ROOT), os.environ.get("PYTHONPATH")]))}
    return subprocess.run(
        [sys.executable, "-m", "funnylang", *args],
        capture_output=True, text=True, encoding="utf-8", cwd=cwd, timeout=300, env=env,
    )


def test_cli_reports_a_source_error_without_compiler_internals(native_binary, tmp_path):
    """A syntax error is the user's problem, so it must not be buried under a
    stack trace through the compiler's own source.

    It used to be reported as a two-line `couldn't compile X:` summary, which
    was the honest thing to print while the self-hosted front end carried no
    source path and the renderer therefore had no file to quote. N11 threaded
    the path through, so this is now the full §4.2 diagnostic — checked
    against `python -m funnylang run` on the same input and byte-identical to
    it, roast included."""
    source = tmp_path / "bad.funny"
    source.write_text('yap "unclosed\n', encoding="utf-8")
    result = subprocess.run(
        [str(native_binary), str(source)], capture_output=True, text=True, timeout=300, cwd=tmp_path
    )
    theirs = _python("run", str(source), cwd=tmp_path)
    assert result.returncode == 1
    assert result.stderr == theirs.stderr
    # The caret line, and the roast rather than the flavor's generic default.
    assert 'yap "unclosed' in result.stderr
    assert "strings don't just end whenever they feel like it" in result.stderr
    assert "funnyc.funny" not in result.stderr, "compiler internals leaked into a user-facing error"


def test_cli_version_flag(native_binary):
    """The version itself is read from funnylang/__init__.py rather than
    hard-coded here: four places carry it, and a test that pins a literal is
    the fifth thing to update on every bump. That the *four* implementations
    agree is what `test_native_cli.py` checks."""
    from funnylang import __version__

    result = subprocess.run([str(native_binary), "--version"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0
    assert result.stdout == f"funny {__version__} (bytecode v2)\n"


def test_both_vms_run_the_compiler_to_the_same_bytecode(native_binary, selfhost_pak, tmp_path):
    """Stronger than "the output looks right": the C VM and the Python VM,
    running the *same* FunnyLang compiler over the same input, must produce
    byte-identical bytecode. Any divergence in how the two VMs execute --
    an operator, a closure capture, a dict ordering -- would show up here as
    different bytes, whatever the compiled program then happened to print."""
    source = ROOT / "examples" / "fizzbuzz.funny"

    native_out = tmp_path / "native.funnyc"
    _native_compile(native_binary, selfhost_pak, source, native_out)

    python_out = tmp_path / "python.funnyc"
    units, entry_canonical = build_bundle(str(SELFHOST_ENTRY))
    from funnylang.modules import CanonicalSource, make_pak_module_loader

    vm = VM(stdout=io.StringIO())
    install_stdlib(vm)
    vm.module_loader = make_pak_module_loader(units, entry_canonical)
    vm.program_args = [str(source), str(python_out)]
    vm.interpret(units[entry_canonical], CanonicalSource(entry_canonical))

    assert native_out.read_bytes() == python_out.read_bytes()
