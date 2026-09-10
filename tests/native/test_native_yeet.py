"""NATIVE_PLAN.md N7's acceptance gate: `funny yeet` produces a standalone
executable.

    funny yeet examples/hello.funny -o hello

must give a binary under 1 MB that prints `yo sup world`, still works after
being moved, and exits 69 on an uncaught `computer.explode()`.

The "still works after being moved" case is the one worth stating plainly:
the stub finds its payload by reading its *own* file, so it has to locate
itself through the OS rather than through `argv[0]`, which is whatever the
caller happened to type.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
ONE_MB = 1024 * 1024


def _yeet(native_binary, stub, source: Path, out: Path, env_extra=None):
    env = dict(os.environ)
    # No FUNNY_TOOLCHAIN: since N10 task 1 the compiler is embedded in the
    # binary. FUNNY_STUB is still needed — `funny yeet` copies `funnyrt` off
    # disk, and a binary cannot contain a copy of a binary containing it.
    env["FUNNY_STUB"] = str(stub)
    if env_extra:
        env.update(env_extra)
    result = subprocess.run(
        [str(native_binary), "yeet", str(source), "-o", str(out)],
        capture_output=True, text=True, timeout=300, env=env,
    )
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"
    return result


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX exec bit; the Windows path is verified by hand")
def test_yeeted_hello_runs_and_fits_the_budget(native_binary, native_stub_binary, tmp_path):
    out = tmp_path / "hello"
    result = _yeet(native_binary, native_stub_binary, ROOT / "examples" / "hello.funny", out)
    assert "no python" in result.stdout

    size = out.stat().st_size
    assert size < ONE_MB, f"yeeted binary is {size} bytes, over the 1 MB budget"

    ran = subprocess.run([str(out)], capture_output=True, text=True, timeout=60)
    assert ran.returncode == 0, ran.stderr
    assert ran.stdout == "yo sup world\n"


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX exec bit; the Windows path is verified by hand")
def test_yeeted_binary_still_works_after_being_moved(native_binary, native_stub_binary, tmp_path):
    """It reads its own file to find its payload, so it must locate itself
    through the OS rather than through argv[0] or the working directory."""
    out = tmp_path / "hello"
    _yeet(native_binary, native_stub_binary, ROOT / "examples" / "hello.funny", out)
    moved_dir = tmp_path / "somewhere" / "else"
    moved_dir.mkdir(parents=True)
    moved = moved_dir / "renamed"
    shutil.move(str(out), str(moved))

    ran = subprocess.run([str(moved)], capture_output=True, text=True, timeout=60, cwd=str(tmp_path))
    assert ran.returncode == 0, ran.stderr
    assert ran.stdout == "yo sup world\n"


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX exec bit; the Windows path is verified by hand")
def test_yeeted_binary_exits_69_on_uncaught_explode(native_binary, native_stub_binary, tmp_path):
    source = tmp_path / "boom.funny"
    source.write_text("gimme computer\ncomputer.explode()\n", encoding="utf-8")
    out = tmp_path / "boom"
    _yeet(native_binary, native_stub_binary, source, out)

    ran = subprocess.run([str(out)], capture_output=True, text=True, timeout=60)
    assert ran.returncode == 69
    assert "KERNEL PANIC" in ran.stdout


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX exec bit; the Windows path is verified by hand")
def test_yeeted_binary_receives_program_args(native_binary, native_stub_binary, tmp_path):
    source = tmp_path / "args.funny"
    source.write_text("yap the_args()\n", encoding="utf-8")
    out = tmp_path / "args"
    _yeet(native_binary, native_stub_binary, source, out)

    ran = subprocess.run([str(out), "one", "two"], capture_output=True, text=True, timeout=60)
    assert ran.returncode == 0, ran.stderr
    assert ran.stdout == '["one", "two"]\n'


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX exec bit; the Windows path is verified by hand")
def test_yeet_creates_a_missing_output_directory(native_binary, native_stub_binary, tmp_path):
    """The M13 packager fix, kept: `-o` into a directory that doesn't exist
    yet creates it rather than failing."""
    out = tmp_path / "brand" / "new" / "dir" / "hello"
    _yeet(native_binary, native_stub_binary, ROOT / "examples" / "hello.funny", out)
    assert out.exists()
    ran = subprocess.run([str(out)], capture_output=True, text=True, timeout=60)
    assert ran.stdout == "yo sup world\n"


def test_naked_stub_says_so(native_stub_binary):
    """Running the stub itself, with nothing appended, is a real thing a
    user can do by accident; it should say what's wrong rather than crash."""
    ran = subprocess.run([str(native_stub_binary)], capture_output=True, text=True, timeout=60)
    assert ran.returncode == 2
    assert ran.stderr.strip() == "this stub is naked. it has no program. sad."
