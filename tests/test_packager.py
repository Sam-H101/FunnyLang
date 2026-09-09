"""PLAN.md §M10 acceptance: `funny yeet` produces a standalone binary that
needs no Python on the target machine. Slow (PyInstaller build); skipped if
PyInstaller isn't installed."""
from __future__ import annotations

import shutil
import subprocess
import sys

import pytest

from funnylang.packager import ensure_stub, pyinstaller_available, stub_path, yeet
from funnylang.serializer import dump_funnypak
from funnylang.modules import build_bundle

pytestmark = pytest.mark.slow

if not pyinstaller_available():
    pytest.skip("PyInstaller isn't installed", allow_module_level=True)


@pytest.fixture(scope="session")
def built_stub():
    return ensure_stub()


def _yeet(built_stub, funny_path: str, out_path):
    units, entry = build_bundle(funny_path)
    pak = dump_funnypak(units, entry)
    yeet(pak, str(out_path))
    return out_path


def test_stub_builds_and_is_cached(built_stub):
    assert built_stub.exists()
    assert built_stub == stub_path()


def test_yeet_hello_world(built_stub, tmp_path):
    exe = tmp_path / ("hello.exe" if sys.platform == "win32" else "hello")
    _yeet(built_stub, "examples/hello.funny", exe)
    assert exe.exists()
    result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0
    assert result.stdout == "yo sup world\n"


def test_yeet_exe_works_after_moving(built_stub, tmp_path):
    exe = tmp_path / "orig" / ("hello.exe" if sys.platform == "win32" else "hello")
    exe.parent.mkdir()
    _yeet(built_stub, "examples/hello.funny", exe)
    moved_dir = tmp_path / "moved"
    moved_dir.mkdir()
    moved_exe = moved_dir / exe.name
    shutil.move(str(exe), str(moved_exe))
    result = subprocess.run([str(moved_exe)], capture_output=True, text=True, encoding="utf-8", cwd=str(moved_dir))
    assert result.returncode == 0
    assert result.stdout == "yo sup world\n"


def test_yeet_creates_missing_output_directory(built_stub, tmp_path):
    # PLAN.md §16 (M13): `-o some/nonexistent/dir/name` used to crash with a
    # raw FileNotFoundError (only caught by the top-level "compiler skill
    # issue" handler) instead of just creating the directory, like any
    # ordinary CLI tool that writes a named output file.
    exe = tmp_path / "does" / "not" / "exist" / ("hello.exe" if sys.platform == "win32" else "hello")
    assert not exe.parent.exists()
    _yeet(built_stub, "examples/hello.funny", exe)
    assert exe.exists()
    result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0
    assert result.stdout == "yo sup world\n"


def test_yeet_multi_module_bundle(built_stub, tmp_path):
    exe = tmp_path / ("main.exe" if sys.platform == "win32" else "main")
    _yeet(built_stub, "examples/modules/main.funny", exe)
    result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 0
    assert result.stdout.splitlines() == ["42", "4.0", "20", "6.28318", "QUIET"]


def test_yeet_uncaught_error_exit_code(built_stub, tmp_path):
    exe = tmp_path / ("err.exe" if sys.platform == "win32" else "err")
    _yeet(built_stub, "tests/lang/err_div_zero.funny", exe)
    result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 1
    assert "MathAintMathin" in result.stderr


def test_naked_stub_reports_no_program(built_stub, tmp_path):
    exe = tmp_path / stub_path().name
    shutil.copyfile(built_stub, exe)
    if sys.platform != "win32":
        exe.chmod(exe.stat().st_mode | 0o111)
    result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert result.returncode == 2
    assert "naked" in result.stdout + result.stderr


def test_cli_yeet_command(tmp_path):
    exe = tmp_path / ("hello.exe" if sys.platform == "win32" else "hello")
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "yeet", "examples/hello.funny", "-o", str(exe)],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0
    assert "yeeted" in result.stdout
    run_result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf-8")
    assert run_result.stdout == "yo sup world\n"
