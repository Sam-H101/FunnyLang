"""NATIVE_PLAN.md N8 task 4: `funny test`, in FunnyLang.

`selfhost/test.funny` ports cli.py's `cmd_test`/`_run_one_test`. It matters
beyond N8: N11 deletes the Python implementation and with it the pytest suite
that uses it as an oracle, and what survives is the `.funny`/`.expected`
golden corpus — which this runs. So the bar is byte-identical output against
the Python CLI over the real corpus, not a smoke test.

Two pieces of new language surface make it possible at all:
`sus.run_bytecode` (run compiled FunnyLang from FunnyLang, in an isolated VM
with stdout captured) and `oops` (build an error with a chosen flavor, so the
self-hosted resolver can raise the `WhoDis` an `!ERROR WhoDis` golden asks
for instead of burying the name in a `SkillIssue`).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from funnylang.modules import build_bundle
from funnylang.serializer import dump_funnypak

ROOT = Path(__file__).resolve().parent.parent.parent
TEST_ENTRY = ROOT / "selfhost" / "testcli.funny"  # the CLI wrapper; test.funny is a library


@pytest.fixture(scope="session")
def test_pak(tmp_path_factory):
    units, entry_canonical = build_bundle(str(TEST_ENTRY))
    pak = tmp_path_factory.mktemp("testrunner") / "test.funnypak"
    pak.write_bytes(dump_funnypak(units, entry_canonical))
    return pak


def _native(binary: Path, pak: Path, target: str, cwd: Path = ROOT):
    return subprocess.run(
        [str(binary), str(pak), target],
        capture_output=True, text=True, encoding="utf-8", cwd=cwd, timeout=900,
    )


def _python(target: str, cwd: Path = ROOT):
    """PYTHONPATH is set explicitly because several of these run with `cwd`
    somewhere else entirely, and `python -m funnylang` otherwise only resolves
    when the repo root *is* the working directory or the package happens to be
    pip-installed. CI's `native` job installs only pytest, so without this the
    oracle silently produces nothing and every comparison below is against an
    empty string — which is exactly how this was found."""
    env = {**os.environ, "PYTHONPATH": os.pathsep.join(filter(None, [str(ROOT), os.environ.get("PYTHONPATH")]))}
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "test", target],
        capture_output=True, text=True, encoding="utf-8", cwd=cwd, timeout=900, env=env,
    )
    assert "No module named" not in result.stderr, (
        f"the Python oracle could not be imported, so there is nothing to compare against:\n{result.stderr}"
    )
    return result


# Directives the native runner understands and cli.py never will. N11 grows
# the golden corpus to replace the pytest suite, and some of what it has to
# replace -- argv, exit codes, the *rendering* of a diagnostic, token and AST
# dumps -- cannot be written as bare stdout. `selfhost/test.funny` gained a
# `.expected` directive block for it; the Python runner is being deleted, so
# it did not.
#
# That makes the two runners genuinely different programs on those files, and
# comparing them there would be comparing a feature against its absence. The
# differential still has to mean something, so it runs over the subset both
# can express, staged into a tmp tree that preserves the layout -- which keeps
# the walk-order property this test is also checking.
NATIVE_ONLY_DIRECTIVES = ("!ARGS", "!EXIT", "!DIAG", "!XRAY", "!STDOUT")


def _is_shared_golden(expected: Path) -> bool:
    head = expected.read_text(encoding="utf-8").split("\n", 1)[0]
    return not head.startswith(NATIVE_ONLY_DIRECTIVES)


@pytest.fixture(scope="session")
def shared_corpus(tmp_path_factory):
    """The corpus with the native-only *goldens* removed, structure intact.

    Copies both trees whole and then deletes only the offending `.expected`
    files, rather than copying the pairs it wants. A `.funny` left without a
    golden is ignored by both runners (see
    test_a_funny_file_with_no_golden_is_ignored) but is still there to be
    imported -- `examples/modules/main.funny` needs `mathstuff.funny`, which
    has no golden of its own and vanished when this picked files pair by pair.
    """
    root = tmp_path_factory.mktemp("shared_corpus")
    for source in ("tests/lang", "examples"):
        shutil.copytree(ROOT / source, root / source)
    for expected in sorted(root.rglob("*.expected")):
        if not _is_shared_golden(expected):
            expected.unlink()
    return root


@pytest.mark.parametrize("target", ["tests/lang", "examples", "."])
def test_matches_the_python_cli_over_the_real_corpus(native_binary, test_pak, shared_corpus, target):
    """Every PASS/FAIL line, the blank line, the count, and the exit code."""
    mine = _native(native_binary, test_pak, target, cwd=shared_corpus)
    theirs = _python(target, cwd=shared_corpus)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0


def test_error_goldens_check_the_flavor_not_stdout(native_binary, test_pak):
    """An `!ERROR <Flavor>` golden passes only if that exact flavor escapes.
    Three of these come from the *resolver* (undefined variable, const
    reassignment) and could not pass at all before `oops`, because the
    self-hosted resolver could only ever raise a SkillIssue."""
    result = _native(native_binary, test_pak, "tests/lang")
    assert result.returncode == 0
    for name in ("err_undefined_variable", "err_immutable_reassign", "err_ptr_address_of_const"):
        assert f"PASS tests/lang/{name}.funny" in result.stdout


def test_a_wrong_golden_fails_with_the_python_detail(native_binary, test_pak, tmp_path):
    """The FAIL line's detail is built with CPython's own `repr` for strings,
    so it has to match character for character."""
    (tmp_path / "wrong.funny").write_text("yap 1\n", encoding="utf-8", newline="")
    (tmp_path / "wrong.expected").write_text("2\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 1
    assert "expected '2\\n', got '1\\n'" in mine.stdout


def test_a_wrong_error_flavor_fails(native_binary, test_pak, tmp_path):
    (tmp_path / "flavor.funny").write_text("yap [][0]\n", encoding="utf-8", newline="")
    (tmp_path / "flavor.expected").write_text("!ERROR KeyGhosted\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 1
    assert "expected !ERROR KeyGhosted, got OutOfPocket" in mine.stdout


def test_an_error_golden_on_a_program_that_succeeds_fails(native_binary, test_pak, tmp_path):
    (tmp_path / "fine.funny").write_text("yap 1\n", encoding="utf-8", newline="")
    (tmp_path / "fine.expected").write_text("!ERROR SkillIssue\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 1
    assert "but nothing was raised" in mine.stdout


def test_empty_directory_says_so_and_succeeds(native_binary, test_pak, tmp_path):
    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0
    assert "no *.funny/*.expected pairs found" in mine.stdout


def test_a_funny_file_with_no_golden_is_ignored(native_binary, test_pak, tmp_path):
    (tmp_path / "paired.funny").write_text("yap 1\n", encoding="utf-8", newline="")
    (tmp_path / "paired.expected").write_text("1\n", encoding="utf-8", newline="")
    (tmp_path / "orphan.funny").write_text("chuck \"never run\"\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    assert mine.returncode == 0
    assert "paired.funny" in mine.stdout
    assert "orphan.funny" not in mine.stdout
    assert "1/1 passed." in mine.stdout


def test_nested_directories_are_walked_in_the_same_order(native_binary, test_pak, tmp_path):
    """cli.py sorts `Path` objects, which compares path *components* — so a
    subdirectory's contents sort right after its own name and interleave with
    sibling files by name, rather than files being grouped first."""
    (tmp_path / "b_dir").mkdir()
    for name, body in [
        ("a_top.funny", "yap 1\n"),
        ("b_dir/inner.funny", "yap 2\n"),
        ("c_top.funny", "yap 3\n"),
    ]:
        (tmp_path / name).write_text(body, encoding="utf-8", newline="")
        (tmp_path / name.replace(".funny", ".expected")).write_text(body.split()[-1] + "\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0


def test_a_multi_module_program_is_linked_not_just_compiled(native_binary, test_pak, tmp_path):
    """The native runtime resolves a file import at *link* time — there is no
    compiler inside a running VM to load one on demand — so the runner
    bundles each test rather than compiling a single unit. cli.py gets the
    same result via the Python VM's module resolver."""
    (tmp_path / "helper.funny").write_text("flex bet twice(x) { bounce x * 2 }\n", encoding="utf-8", newline="")
    (tmp_path / "uses.funny").write_text(
        'gimme { twice } from "helper.funny"\nyap twice(21)\n', encoding="utf-8", newline="")
    (tmp_path / "uses.expected").write_text("42\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0
    assert "1/1 passed." in mine.stdout


def test_one_test_cannot_disturb_another(native_binary, test_pak, tmp_path):
    """Each test runs in its own VM, so a global one defines is invisible to
    the next — and a `dip()` in one does not end the run."""
    (tmp_path / "a_defines.funny").write_text("yo leaked = 1\nyap leaked\n", encoding="utf-8", newline="")
    (tmp_path / "a_defines.expected").write_text("1\n", encoding="utf-8", newline="")
    (tmp_path / "b_dips.funny").write_text("yap \"before\"\ndip(0)\nyap \"after\"\n", encoding="utf-8", newline="")
    (tmp_path / "b_dips.expected").write_text("before\n", encoding="utf-8", newline="")
    (tmp_path / "c_undefined.funny").write_text("yap leaked\n", encoding="utf-8", newline="")
    (tmp_path / "c_undefined.expected").write_text("!ERROR WhoDis\n", encoding="utf-8", newline="")

    mine = _native(native_binary, test_pak, str(tmp_path), cwd=tmp_path)
    theirs = _python(str(tmp_path), cwd=tmp_path)
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0
    assert "3/3 passed." in mine.stdout
