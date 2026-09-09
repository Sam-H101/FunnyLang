"""PLAN.md §M12 tasks 4/5/7: `funny bootstrap --verify` reaches the
stage3==stage4 self-hosting fixed point, and stage3 (the self-hosted
compiler, itself compiled by stage2) compiles the entire tests/lang/ corpus
with output identical to the Python (stage1) compiler -- the real proof
self-hosting works, not just that funnyc.funny reproduces its own bytes."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.cli import _bootstrap_run_stage
from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak

pytestmark = pytest.mark.slow

ROOT = Path(__file__).resolve().parent.parent
SELFHOST_DIR = ROOT / "selfhost"
FUNNYC_PATH = SELFHOST_DIR / "funnyc.funny"
CORPUS = sorted((ROOT / "tests" / "lang").glob("*.funny"))

# These two goldens deliberately fail *resolution* (undefined variable,
# const reassignment) -- both stage1 and the self-hosted resolver reject
# them, which is the correct, matching behavior (see test_selfhost_compiler.py).
RESOLVE_TIME_FAILURES = {
    "err_immutable_reassign.funny", "err_undefined_variable.funny",
    "err_ptr_address_of_const.funny",
}


def test_bootstrap_verify_reaches_fixed_point():
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "bootstrap", "--verify"],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert result.returncode == 0, result.stderr
    assert "stage3 and stage4 are byte-identical" in result.stdout
    assert "FunnyLang now compiles FunnyLang" in result.stdout


def test_bootstrap_verify_keep_writes_all_three_stages(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "bootstrap", "--verify", "--keep"],
        capture_output=True, text=True, encoding="utf-8", cwd=tmp_path,
    )
    assert result.returncode == 0, result.stderr
    build_dir = tmp_path / "build" / "bootstrap"
    stage2 = build_dir / "stage2.funnypak"
    stage3 = build_dir / "stage3.funnyc"
    stage4 = build_dir / "stage4.funnyc"
    assert stage2.exists() and stage3.exists() and stage4.exists()
    assert stage3.read_bytes() == stage4.read_bytes()


@pytest.fixture(scope="session")
def stage3_path(tmp_path_factory):
    """Builds stage2 (Python-compiled bundle) then runs it once to produce
    stage3 (funnyc.funny compiled by the self-hosted compiler's own logic,
    exercised for real for the first time) -- shared across every corpus
    file in the cross-validation test below instead of rebuilding it once
    per file."""
    build_dir = tmp_path_factory.mktemp("bootstrap")
    units, entry_canonical = build_bundle(str(FUNNYC_PATH))
    stage2 = build_dir / "stage2.funnypak"
    stage2.write_bytes(dump_funnypak(units, entry_canonical))
    stage3 = build_dir / "stage3.funnyc"
    ok, err = _bootstrap_run_stage(stage2, FUNNYC_PATH, stage3, SELFHOST_DIR)
    assert ok, err
    return stage3


@pytest.mark.parametrize("path", CORPUS, ids=lambda p: p.name)
def test_stage3_compiles_corpus_identically_to_stage1(path, stage3_path, tmp_path):
    out_path = tmp_path / f"{path.stem}.funnyc"
    ok, err = _bootstrap_run_stage(stage3_path, path, out_path, SELFHOST_DIR)
    if path.name in RESOLVE_TIME_FAILURES:
        assert not ok, f"stage3 should have rejected {path} but compiled it"
        return
    assert ok, f"stage3 failed to compile {path}:\n{err}"

    via_stage3 = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(out_path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    via_stage1 = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(path)],
        capture_output=True, text=True, encoding="utf-8", cwd=ROOT,
    )
    assert via_stage3.returncode == via_stage1.returncode, path
    assert via_stage3.stdout == via_stage1.stdout, path
