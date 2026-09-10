"""NATIVE_PLAN.md N9 task 2: `funny bootstrap --verify` on the C VM.

The self-hosting fixed point, with no Python in the loop: stage 2 is the
toolchain linked from `selfhost/` by this build, stage 3 is what stage 2
produces from the same sources, stage 4 is what stage 3 produces. Byte-equal
stage 3 and stage 4 means the compiler compiles itself into itself.

The native version stages the **linker**, not the compiler, and that is a
deliberate difference from `cli.py`'s. A bare `.funnyc` compiler has no
bundle, so its own `gimme { ... } from "compiler.funny"` has nothing to
resolve against; the Python VM gets away with it by compiling those imports
on the fly, which needs a compiler inside the running VM. The native runtime
has none by design — a file import is resolved at link time. Staging the
linker makes every stage a self-contained bundle, and exercises the bundler
on top of everything compiling one file exercises.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak

ROOT = Path(__file__).resolve().parent.parent.parent


def _native(binary: Path, *args: str, cwd: Path = ROOT) -> subprocess.CompletedProcess:
    return subprocess.run([str(binary), *args], capture_output=True, text=True,
                          encoding="utf-8", cwd=cwd, timeout=900)


def test_bootstrap_verify_reaches_a_fixed_point(native_binary):
    result = _native(native_binary, "bootstrap", "--verify")
    assert result.returncode == 0, result.stderr
    assert "🥁 stage 2... compiled." in result.stdout
    assert "🥁 stage 3... compiled by stage 2." in result.stdout
    assert "🥁 stage 4... compiled by stage 3." in result.stdout
    assert "stage3 and stage4 are byte-identical" in result.stdout
    assert "we are so back" in result.stdout


def test_the_output_shape_matches_the_python_cli(native_binary):
    """Both bootstraps stage different things — the native one the linker,
    the Python one the compiler — so the byte counts differ by design. Every
    other line is the same, including the `:,`-formatted thousands."""
    mine = _native(native_binary, "bootstrap", "--verify")
    theirs = subprocess.run([sys.executable, "-m", "funnylang", "bootstrap", "--verify"],
                            capture_output=True, text=True, encoding="utf-8", cwd=ROOT, timeout=900)
    assert mine.returncode == theirs.returncode == 0
    strip = lambda s: re.sub(r"\([\d,]+ bytes\)", "(N bytes)", s)
    assert strip(mine.stdout) == strip(theirs.stdout)
    assert re.search(r"\(\d{1,3}(,\d{3})+ bytes\)", mine.stdout), "thousands separators"


def test_verify_is_required(native_binary):
    result = _native(native_binary, "bootstrap")
    assert result.returncode == 1
    assert "needs --verify" in result.stderr


def test_keep_leaves_the_stages_behind(native_binary, tmp_path):
    """`--keep` writes into build/bootstrap/ so a failed run can be picked
    over; without it the stages are temporary and cleaned up."""
    import shutil

    work = tmp_path / "tree"
    shutil.copytree(ROOT / "selfhost", work / "selfhost")
    result = _native(native_binary, "bootstrap", "--verify", "--keep", cwd=work)
    assert result.returncode == 0, result.stderr
    for name in ("stage2.funnypak", "stage3.funnypak", "stage4.funnypak"):
        assert (work / "build" / "bootstrap" / name).exists(), name
    assert (work / "build" / "bootstrap" / "stage3.funnypak").read_bytes() == \
           (work / "build" / "bootstrap" / "stage4.funnypak").read_bytes()


def test_without_keep_nothing_is_left_in_the_tree(native_binary, tmp_path):
    import shutil

    work = tmp_path / "tree"
    shutil.copytree(ROOT / "selfhost", work / "selfhost")
    assert _native(native_binary, "bootstrap", "--verify", cwd=work).returncode == 0
    assert not (work / "build" / "bootstrap").exists()


# The driver has to sit *beside* bootstrap.funny, not under _drivers/: a
# bundle keys its modules by their path relative to the entry's directory, so
# an import that escapes that directory (`../bootstrap.funny`) gets an
# absolute key the runtime loader can never resolve back. Both implementations
# share that limitation -- see NATIVE_PLAN.md §9 -- so the fix is to put the
# driver where a normal sibling import works, in a throwaway copy of
# selfhost/ rather than in the real one.
DIFF_DRIVER_SOURCE = """gimme { stage_diff } from "bootstrap.funny"

yo args = the_args()
yap stage_diff(args[0], args[1])
"""


def _diff_driver_pak(tmp_path):
    import shutil

    work = tmp_path / "selfhost_copy"
    shutil.copytree(ROOT / "selfhost", work)
    driver = work / "stage_diff_driver.funny"
    driver.write_text(DIFF_DRIVER_SOURCE, encoding="utf-8", newline="")
    pak = tmp_path / "driver.funnypak"
    units, entry = build_bundle(str(driver))
    pak.write_bytes(dump_funnypak(units, entry))
    return pak


def test_diff_reports_the_first_divergent_proto(native_binary, tmp_path):
    """`--diff`'s renderer is unreachable through the CLI — the bootstrap
    reaches a fixed point, so the stages never differ, so the one code path
    whose whole job is explaining a failure would otherwise ship untested.
    This hands it two bundles that do differ."""
    # The same *file name* in two directories, so the bundles agree about
    # their module names and the comparison gets as far as the protos —
    # which is what a real stage3/stage4 divergence looks like.
    paks = []
    # The bodies differ in f's *code*, not just in a constant: two protos that
    # differ only in which constant they push compare equal (constants live in
    # the unit's pool, not the proto), which the reference reports as "protos
    # are equal but the raw bytes differ" — covered separately below.
    for i, body in enumerate(("flex bet f() { bounce 1 }\nyap f()\n",
                              "flex bet f() { yap 9\n bounce 1 }\nyap f()\n")):
        d = tmp_path / f"v{i}"
        d.mkdir()
        src = d / "prog.funny"
        src.write_text(body, encoding="utf-8", newline="")
        pak = tmp_path / f"v{i}.funnypak"
        units, entry = build_bundle(str(src))
        pak.write_bytes(dump_funnypak(units, entry))
        paks.append(pak)

    driver = _diff_driver_pak(tmp_path)
    result = _native(native_binary, str(driver), str(paks[0]), str(paks[1]))
    assert result.returncode == 0, result.stderr
    assert "first divergent proto:" in result.stdout
    assert "-- stage3 --" in result.stdout and "-- stage4 --" in result.stdout
    assert "CONST" in result.stdout, "the proto is disassembled, not summarised"


def test_diff_reports_a_module_count_mismatch(native_binary, tmp_path):
    """A structural difference is reported as such rather than being chased
    down to a proto that may not exist on both sides."""
    single = tmp_path / "single.funny"
    single.write_text("yap 1\n", encoding="utf-8", newline="")
    (tmp_path / "helper.funny").write_text("flex bet h() { bounce 2 }\n", encoding="utf-8", newline="")
    multi = tmp_path / "multi.funny"
    multi.write_text('gimme { h } from "helper.funny"\nyap h()\n', encoding="utf-8", newline="")

    paks = []
    for src in (single, multi):
        pak = tmp_path / (src.stem + ".funnypak")
        units, entry = build_bundle(str(src))
        pak.write_bytes(dump_funnypak(units, entry))
        paks.append(pak)

    driver = _diff_driver_pak(tmp_path)
    result = _native(native_binary, str(driver), str(paks[0]), str(paks[1]))
    assert result.returncode == 0, result.stderr
    assert "module count differs" in result.stdout


def test_diff_reports_a_const_only_difference_honestly(native_binary, tmp_path):
    """Two protos that differ only in which constant they push compare equal
    — constants live in the unit's pool, not in the proto — so the report
    says exactly that instead of pointing at an innocent proto. cli.py's
    `_bootstrap_diff` says the same thing for the same reason."""
    paks = []
    for i, body in enumerate(("yap 1\n", "yap 2\n")):
        d = tmp_path / f"c{i}"
        d.mkdir()
        src = d / "prog.funny"
        src.write_text(body, encoding="utf-8", newline="")
        pak = tmp_path / f"c{i}.funnypak"
        units, entry = build_bundle(str(src))
        pak.write_bytes(dump_funnypak(units, entry))
        paks.append(pak)

    driver = _diff_driver_pak(tmp_path)
    result = _native(native_binary, str(driver), str(paks[0]), str(paks[1]))
    assert result.returncode == 0, result.stderr
    assert "protos are equal but the raw bytes differ" in result.stdout
