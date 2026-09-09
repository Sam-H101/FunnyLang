"""CLI tests: `funny run/build/yeet/vibe/xray/fmt/test/bootstrap` + global
flags (PLAN.md §M8), invoked via subprocess end to end."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest


def _run_cli(*args: str, input: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-m", "funnylang", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        input=input,
    )


def test_no_args_prints_banner():
    result = _run_cli()
    assert result.returncode == 0
    assert "FunnyLang" in result.stdout


def test_version_flag():
    result = _run_cli("--version")
    assert result.returncode == 0
    assert "funny" in result.stdout
    assert "bytecode" in result.stdout


# -- run --------------------------------------------------------------


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
    assert result.returncode == 2  # argparse's standard usage-error exit code


def test_run_extra_args_after_dashdash(tmp_path):
    f = tmp_path / "args.funny"
    f.write_text("yap the_args()\n", encoding="utf-8")
    result = _run_cli("run", str(f), "--", "a", "b", "3")
    assert result.returncode == 0
    assert result.stdout == '["a", "b", "3"]\n'


def test_run_time_flag_prints_timing():
    result = _run_cli("--time", "run", "examples/hello.funny")
    assert result.returncode == 0
    assert "blazingly fast" in result.stdout


def test_run_vibes_flag_prints_quips():
    result = _run_cli("--vibes", "run", "examples/hello.funny")
    assert result.returncode == 0
    assert "[lexing]" in result.stdout or "[lexing]" in result.stderr


def test_run_serious_flag_removes_emoji():
    result = _run_cli("--serious", "run", "tests/lang/err_div_zero.funny")
    assert result.returncode == 1
    assert "💀" not in result.stderr
    assert "FUNNYLANG ERROR" in result.stderr


def test_run_no_color_flag_has_no_ansi():
    result = _run_cli("--no-color", "run", "tests/lang/err_div_zero.funny")
    assert "\x1b[" not in result.stderr


def test_run_redirected_output_has_no_ansi_by_default():
    # not a TTY when captured by subprocess -- should auto-detect no color
    result = _run_cli("run", "tests/lang/err_div_zero.funny")
    assert "\x1b[" not in result.stderr


# -- build / run on compiled artifacts ---------------------------------


def test_build_and_run_funnyc(tmp_path):
    out = tmp_path / "hello.funnyc"
    result = _run_cli("build", "examples/hello.funny", "-o", str(out))
    assert result.returncode == 0
    assert out.exists()
    run_result = _run_cli("run", str(out))
    assert run_result.returncode == 0
    assert run_result.stdout == "yo sup world\n"


def test_build_and_run_funnypak_multi_module(tmp_path):
    out = tmp_path / "main.funnypak"
    result = _run_cli("build", "examples/modules/main.funny", "-o", str(out))
    assert result.returncode == 0
    run_result = _run_cli("run", str(out))
    assert run_result.returncode == 0
    assert run_result.stdout.splitlines() == ["42", "4.0", "20", "6.28318", "QUIET"]


def test_build_needs_out_flag():
    result = _run_cli("build", "examples/hello.funny")
    assert result.returncode == 2


# -- xray ---------------------------------------------------------------


def test_xray_disassembles():
    result = _run_cli("xray", "examples/hello.funny")
    assert result.returncode == 0
    assert "CONST" in result.stdout
    assert "RETURN" in result.stdout


def test_xray_tokens():
    result = _run_cli("xray", "examples/hello.funny", "--tokens")
    assert result.returncode == 0
    assert "YO" in result.stdout


def test_xray_ast():
    result = _run_cli("xray", "examples/hello.funny", "--ast")
    assert result.returncode == 0
    assert result.stdout.startswith("(program")


def test_xray_pak(tmp_path):
    out = tmp_path / "main.funnypak"
    _run_cli("build", "examples/modules/main.funny", "-o", str(out))
    result = _run_cli("xray", str(out))
    assert result.returncode == 0
    assert "entry:" in result.stdout
    assert "module:" in result.stdout


# -- fmt ------------------------------------------------------------------


def test_fmt_check_passes_on_already_formatted_file():
    result = _run_cli("fmt", "examples/fizzbuzz.funny", "--check")
    assert result.returncode == 0


def test_fmt_check_fails_on_unformatted_file(tmp_path):
    f = tmp_path / "messy.funny"
    f.write_text("yo   x=1\nyap x\n", encoding="utf-8")
    result = _run_cli("fmt", str(f), "--check")
    assert result.returncode == 1


def test_fmt_rewrites_file_in_place(tmp_path):
    f = tmp_path / "messy.funny"
    f.write_text("yo   x=1\nyap x\n", encoding="utf-8")
    result = _run_cli("fmt", str(f))
    assert result.returncode == 0
    formatted = f.read_text(encoding="utf-8")
    assert formatted == "yo x = 1\nyap x\n"
    # running the formatted program must still behave identically
    run_result = _run_cli("run", str(f))
    assert run_result.stdout == "1\n"


def test_fmt_is_idempotent(tmp_path):
    f = tmp_path / "messy.funny"
    f.write_text("bet f(a,b){bounce a+b}\nyap f(1,2)\n", encoding="utf-8")
    _run_cli("fmt", str(f))
    first = f.read_text(encoding="utf-8")
    _run_cli("fmt", str(f))
    second = f.read_text(encoding="utf-8")
    assert first == second
    assert _run_cli("fmt", str(f), "--check").returncode == 0


# -- test -------------------------------------------------------------


def test_test_command_runs_lang_directory():
    result = _run_cli("test", "tests/lang")
    assert result.returncode == 0
    assert "passed" in result.stdout


def test_test_command_reports_failures(tmp_path):
    (tmp_path / "bad.funny").write_text('yap "hi"\n', encoding="utf-8")
    (tmp_path / "bad.expected").write_text("nope\n", encoding="utf-8")
    result = _run_cli("test", str(tmp_path))
    assert result.returncode == 1
    assert "FAIL" in result.stdout


# -- vibe (REPL) --------------------------------------------------------


def test_vibe_basic_session():
    result = _run_cli("vibe", input="yo x = 5\nx + 3\n.exit\n")
    assert result.returncode == 0
    assert "8" in result.stdout


def test_vibe_persists_globals_across_inputs():
    result = _run_cli("vibe", input="yo x = 10\nbet f() { bounce x * 2 }\nf()\n.exit\n")
    assert "20" in result.stdout


def test_vibe_help_and_clear():
    result = _run_cli("vibe", input=".help\n.clear\n.exit\n")
    assert result.returncode == 0
    assert "funny vibe" in result.stdout


def test_vibe_error_keeps_session_alive():
    result = _run_cli("vibe", input="yo x = 1 +\nx\n.exit\n")
    assert result.returncode == 0
    assert "1" in result.stdout  # session survived the syntax error and evaluated x


# -- stubs for later milestones ------------------------------------------


def test_yeet_without_out_flag_defaults_next_to_source(tmp_path):
    # `funny yeet` (M10) is now real; the slow end-to-end packaging tests
    # (a session-cached stub, actually running the .exe) live in
    # test_packager.py, which skips cleanly without PyInstaller.
    import shutil

    from funnylang.packager import pyinstaller_available

    if not pyinstaller_available():
        pytest.skip("PyInstaller isn't installed")
    src = tmp_path / "hello.funny"
    shutil.copyfile("examples/hello.funny", src)
    result = _run_cli("yeet", str(src))
    assert result.returncode == 0
    expected_exe = src.with_suffix(".exe" if sys.platform == "win32" else "")
    assert expected_exe.exists()


@pytest.mark.slow
def test_bootstrap_verify_reaches_fixed_point():
    # `funny bootstrap` is real as of M12 — see tests/test_bootstrap.py for
    # the full self-hosting fixed-point and cross-validation coverage; this
    # is just a smoke test that the CLI subcommand itself works end to end.
    result = _run_cli("bootstrap", "--verify")
    assert result.returncode == 0
    assert "stage3 and stage4 are byte-identical" in result.stdout


def test_bootstrap_needs_verify_flag():
    result = _run_cli("bootstrap")
    assert result.returncode == 1
