"""The point of the whole exercise: FunnyLang compiling FunnyLang, with no
Python in the loop.

The C VM loads the self-hosted compiler (`selfhost/`, itself FunnyLang,
linked into a PLAN.md §5.3 bundle), runs it to compile a `.funny` file to
bytecode, and then runs that bytecode. Python appears here only to *link*
the bundle -- a dev-time step, the same way tools/gen_unicode.py regenerates
a checked-in table -- and to be the oracle the result is compared against.

What this actually exercises, beyond "it works": bundled-module imports on
the C VM (`gimme { compile_source } from "compiler.funny"`), which need
per-closure constant pools, since a call from one bundled module into
another crosses compiled units mid-execution.
"""
from __future__ import annotations

import io
import subprocess
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
