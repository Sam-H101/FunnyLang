"""CLI smoke tests. M5 wires up just `run`; M8 builds out the rest
(build/yeet/vibe/xray/fmt/test/bootstrap, global flags) with a full suite."""
from __future__ import annotations

import subprocess
import sys

import pytest


def _run_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-m", "funnylang", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def test_no_args_prints_banner():
    result = _run_cli()
    assert result.returncode == 0
    assert "FunnyLang" in result.stdout


def test_run_hello_world():
    result = _run_cli("run", "examples/hello.funny")
    assert result.returncode == 0
    assert result.stdout == "yo sup world\n"


def test_run_fizzbuzz():
    result = _run_cli("run", "examples/fizzbuzz.funny")
    assert result.returncode == 0
    lines = result.stdout.splitlines()
    assert len(lines) == 100
    assert lines[2] == "Fizz"
    assert lines[4] == "Buzz"
    assert lines[14] == "FizzBuzz"


def test_run_missing_file_reports_error_exit_1():
    result = _run_cli("run", "examples/does_not_exist.funny")
    assert result.returncode == 1


def test_run_uncaught_error_reports_exit_1():
    result = _run_cli("run", "tests/lang/err_div_zero.funny")
    assert result.returncode == 1
    assert "MathAintMathin" in result.stderr


def test_run_needs_a_file_argument():
    result = _run_cli("run")
    assert result.returncode == 1
