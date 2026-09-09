"""PLAN.md §M12 task 2, fifth file: selfhost/emitter.funny, cross-checked
byte-for-byte against funnylang.serializer.dump_funnyc() -- the real point
of self-hosting is that the emitted .funnyc file is bit-identical to what
the Python toolchain would have produced, and is itself loadable and
runnable by the real VM (proving the format is genuinely §5.2-conformant,
not just "close enough")."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.compiler import Compiler
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.serializer import dump_funnyc
from funnylang.source import SourceFile

ROOT = Path(__file__).resolve().parent.parent
DRIVER = ROOT / "selfhost" / "_drivers" / "emit_funnyc.funny"

CORPUS = sorted((ROOT / "tests" / "lang").glob("*.funny")) + sorted((ROOT / "examples").glob("**/*.funny"))

# The two golden files that are supposed to fail resolution (see
# test_selfhost_compiler.py) have nothing to emit.
RESOLVE_TIME_FAILURES = {"err_immutable_reassign.funny", "err_undefined_variable.funny"}

# Files that `gimme "relative/path.funny"` other local files: a standalone
# .funnyc can't resolve those on its own once moved to a different
# directory (as this test does) -- that needs the bundler (.funnypak,
# §5.3), a separate, Python-only mechanism outside M12's plain-emitter
# scope. Not a self-hosting bug -- the *Python* compiler's own single-file
# `.funnyc` output for these has exactly the same limitation.
REQUIRES_BUNDLING = {"main.funny"}

# chaos.funny deliberately uses rizz.gamble() (genuine randomness). Compiling
# it is fully deterministic (covered by test_funnyc_bytes_match_python
# above), but *running* it twice as two independent subprocesses -- once via
# the emitted .funnyc, once via source -- will occasionally disagree on that
# one random line for reasons that have nothing to do with self-hosting
# correctness. Excluded only from the stdout-diffing execution check below.
NON_DETERMINISTIC = {"chaos.funny"}


def _python_funnyc_bytes(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    source = SourceFile(str(path), text)
    program = parse_source(source)
    result = resolve_program(program, source)
    unit = Compiler(result, source, fold_constants=True).compile_program(program, str(path))
    return dump_funnyc(unit)


def _selfhost_funnyc_bytes(path: Path, out_path: Path) -> bytes:
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(path), str(out_path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, f"driver failed on {path}:\n{result.stderr}"
    return out_path.read_bytes()


@pytest.mark.parametrize(
    "path", [p for p in CORPUS if p.name not in RESOLVE_TIME_FAILURES], ids=lambda p: p.name,
)
def test_funnyc_bytes_match_python(path, tmp_path):
    out_path = tmp_path / "out.funnyc"
    assert _selfhost_funnyc_bytes(path, out_path) == _python_funnyc_bytes(path)


def test_selfhost_emitted_funnyc_actually_runs(tmp_path):
    """Not just byte-format-correct -- the real Python VM loads and runs it."""
    out_path = tmp_path / "hello.funnyc"
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--",
         str(ROOT / "examples" / "hello.funny"), str(out_path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, result.stderr
    run_result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(out_path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert run_result.returncode == 0, run_result.stderr
    assert run_result.stdout == "yo sup world\n"


@pytest.mark.slow
def test_selfhost_emitted_funnyc_runs_for_whole_corpus(tmp_path):
    """Every emitted .funnyc, not just hello.funny, both loads and executes
    (not merely byte-matches) -- run through the real VM and diffed against
    running the source directly."""
    for path in CORPUS:
        if path.name in RESOLVE_TIME_FAILURES or path.name.startswith("err_"):
            continue  # runtime-error goldens exit non-zero by design
        if path.name in REQUIRES_BUNDLING or path.name in NON_DETERMINISTIC:
            continue
        out_path = tmp_path / f"{path.stem}.funnyc"
        emit_result = subprocess.run(
            [sys.executable, "-m", "funnylang", "run", str(DRIVER), "--", str(path), str(out_path)],
            capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
        )
        assert emit_result.returncode == 0, f"{path}:\n{emit_result.stderr}"
        via_funnyc = subprocess.run(
            [sys.executable, "-m", "funnylang", "run", str(out_path)],
            capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
        )
        via_source = subprocess.run(
            [sys.executable, "-m", "funnylang", "run", str(path)],
            capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
        )
        assert via_funnyc.returncode == via_source.returncode, path
        assert via_funnyc.stdout == via_source.stdout, path
