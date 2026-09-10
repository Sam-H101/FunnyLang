"""NATIVE_PLAN.md N8 task 6: the CLI, in FunnyLang.

`selfhost/cli.funny` owns argument parsing and every subcommand.
`native/main.c` is now a loader: it sets up the console, reads the two
diagnostic flags (they configure the *runtime's* renderer, so they have to
apply before anything runs), and hands argv to the CLI bundle.

The bar is N8's own acceptance line — every subcommand's output byte-identical
to the Python CLI's — so most of these run both and diff.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent


def _native(binary: Path, *args: str, cwd: Path = ROOT, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(binary), *args], capture_output=True, text=True,
                          encoding="utf-8", cwd=cwd, timeout=900, **kw)


def _python(*args: str, cwd: Path = ROOT, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-m", "funnylang", *args], capture_output=True,
                          text=True, encoding="utf-8", cwd=cwd, timeout=900, **kw)


SAME_STDOUT = [
    ("run examples/hello.funny", ["run", "examples/hello.funny"]),
    ("run examples/fizzbuzz.funny", ["run", "examples/fizzbuzz.funny"]),
    ("run a multi-module program", ["run", "examples/modules/main.funny"]),
    ("run a .funnyc", None),  # filled in by the test that needs a compiled file
    ("xray", ["xray", "examples/hello.funny"]),
    ("xray --tokens", ["xray", "examples/hello.funny", "--tokens"]),
    ("xray --ast", ["xray", "examples/hello.funny", "--ast"]),
    # `examples`, not `tests/lang`: the latter now holds goldens using
    # `.expected` directives (!ARGS, !EXIT, !DIAG, !XRAY) that only the native
    # runner understands, because the Python one is being deleted in N11 and
    # did not grow them. Comparing there would compare a feature against its
    # absence. test_native_test_runner.py does the real `funny test`
    # differential, over a staged corpus of the goldens both can express; this
    # entry only needs to prove the *subcommand* dispatches identically.
    ("test examples", ["test", "examples"]),
    ("test examples", ["test", "examples"]),
    ("fmt --check", ["fmt", "examples/fizzbuzz.funny", "--check"]),
]


@pytest.mark.parametrize("label,args", [(l, a) for l, a in SAME_STDOUT if a is not None])
def test_subcommand_output_matches_the_python_cli(native_binary, label, args):
    mine = _native(native_binary, *args)
    theirs = _python(*args)
    assert mine.stdout == theirs.stdout, label
    assert mine.returncode == theirs.returncode, label


def test_bare_file_is_a_native_only_shorthand(native_binary):
    """`funny x.funny` with no subcommand. The Python CLI's argparse requires
    one, so this is compared against its `run` instead."""
    mine = _native(native_binary, "examples/hello.funny")
    theirs = _python("run", "examples/hello.funny")
    assert mine.stdout == theirs.stdout
    assert mine.returncode == theirs.returncode == 0


def test_build_produces_identical_bytes(native_binary, tmp_path):
    mine, theirs = tmp_path / "mine.funnypak", tmp_path / "theirs.funnypak"
    a = _native(native_binary, "build", "examples/modules/main.funny", "-o", str(mine))
    b = _python("build", "examples/modules/main.funny", "-o", str(theirs))
    assert a.returncode == b.returncode == 0
    assert mine.read_bytes() == theirs.read_bytes()
    assert a.stdout.replace(str(mine), "OUT") == b.stdout.replace(str(theirs), "OUT")


def test_build_a_single_module_to_funnyc(native_binary, tmp_path):
    mine, theirs = tmp_path / "mine.funnyc", tmp_path / "theirs.funnyc"
    a = _native(native_binary, "build", "examples/hello.funny", "-o", str(mine))
    b = _python("build", "examples/hello.funny", "-o", str(theirs))
    assert a.returncode == b.returncode == 0
    assert mine.read_bytes() == theirs.read_bytes()
    assert mine.read_bytes()[:5] == b"FUNNY", "a one-module .funnyc, not a bundle"


def test_running_compiled_output_matches(native_binary, tmp_path):
    compiled = tmp_path / "hello.funnyc"
    assert _native(native_binary, "build", "examples/hello.funny", "-o", str(compiled)).returncode == 0
    mine = _native(native_binary, "run", str(compiled))
    theirs = _python("run", str(compiled))
    assert mine.stdout == theirs.stdout == "yo sup world\n"


def test_program_arguments_reach_the_program(native_binary, tmp_path):
    src = tmp_path / "args.funny"
    src.write_text("yap the_args()\n", encoding="utf-8", newline="")
    mine = _native(native_binary, "run", str(src), "one", "two")
    theirs = _python("run", str(src), "--", "one", "two")
    assert mine.stdout == theirs.stdout == '["one", "two"]\n'


def test_an_uncaught_error_renders_the_same_diagnostic(native_binary, tmp_path):
    """Including the source snippet — which means the compiled program has to
    carry its own *path* as its source name. Bundling a single file would
    replace that with a canonical key and silently cost every one-file
    program its snippet."""
    src = tmp_path / "boom.funny"
    src.write_text('chuck "boom"\n', encoding="utf-8", newline="")
    mine = _native(native_binary, "run", str(src))
    theirs = _python("run", str(src))
    assert mine.returncode == theirs.returncode == 1
    assert mine.stderr == theirs.stderr
    assert "chuck \"boom\"" in mine.stderr, "the snippet is part of the diagnostic"


def test_computer_explode_exits_69(native_binary, tmp_path):
    src = tmp_path / "kaboom.funny"
    src.write_text("gimme computer\ncomputer.explode()\n", encoding="utf-8", newline="")
    mine = _native(native_binary, "run", str(src))
    theirs = _python("run", str(src))
    assert mine.returncode == theirs.returncode == 69


def test_dip_propagates_its_exit_code(native_binary, tmp_path):
    src = tmp_path / "bye.funny"
    src.write_text("dip(7)\n", encoding="utf-8", newline="")
    assert _native(native_binary, "run", str(src)).returncode == 7
    assert _python("run", str(src)).returncode == 7


def test_serious_flag_reaches_the_runtime(native_binary, tmp_path):
    """`--serious` is read by main.c, not by cli.funny: it configures the
    runtime's own renderer, and has to apply to a program the CLI runs in a
    nested VM."""
    src = tmp_path / "boom.funny"
    src.write_text('chuck "boom"\n', encoding="utf-8", newline="")
    funny = _native(native_binary, "run", str(src))
    serious = _native(native_binary, "--serious", "run", str(src))
    assert "FUNNYLANG MOMENT" in funny.stderr
    assert "FUNNYLANG ERROR" in serious.stderr
    assert "skill issue." not in serious.stderr, "serious mode prints the message, not the roast"


def test_global_flags_do_not_reach_the_program(native_binary, tmp_path):
    src = tmp_path / "args.funny"
    src.write_text("yap the_args()\n", encoding="utf-8", newline="")
    result = _native(native_binary, "--serious", "--no-color", "run", str(src), "kept")
    assert result.stdout == '["kept"]\n'


def test_version_works_even_with_a_broken_toolchain(native_binary, tmp_path):
    """`--version` is answered by the C loader on purpose: it is the one
    command someone runs when their install is broken, so it must not depend
    on the toolchain that might be what's broken.

    Since N10 task 1 the toolchain is compiled *into* the binary, so the only
    way to break it is to point FUNNY_CLI at something unreadable — which is
    also the one thing that used to be a missing-sidecar error."""
    import os

    env = {**os.environ, "FUNNY_CLI": str(tmp_path / "nope.funnypak")}
    result = _native(native_binary, "--version", env=env)
    assert result.returncode == 0
    assert re.fullmatch(r"funny \d+\.\d+\.\d+ \(bytecode v\d+\)\n", result.stdout), result.stdout

    broken = _native(native_binary, "run", "examples/hello.funny", env=env)
    assert broken.returncode == 1
    assert "FUNNY_CLI is set to" in broken.stderr
    assert "couldn't be read" in broken.stderr


def test_the_embedded_toolchain_needs_nothing_beside_it(native_binary, tmp_path):
    """N10 task 1's actual acceptance: the binary alone. Copied somewhere
    with no `bootstrap/`, no `selfhost/` and nothing else, it still compiles
    and runs a program."""
    import shutil

    solo = tmp_path / ("funny.exe" if os.name == "nt" else "funny")
    shutil.copy2(native_binary, solo)
    solo.chmod(0o755)
    (tmp_path / "hi.funny").write_text('yap "solo"\n', encoding="utf-8", newline="")

    for d in ("bootstrap", "selfhost", "funnylang"):
        assert not (tmp_path / d).exists()

    result = subprocess.run([str(solo), "run", "hi.funny"], capture_output=True, text=True,
                            encoding="utf-8", cwd=tmp_path, timeout=300)
    assert result.returncode == 0, result.stderr
    assert result.stdout == "solo\n"


def test_the_version_string_matches_the_one_in_the_cli(native_binary):
    """Two copies of a version string is exactly the kind of thing that
    drifts, so main.c's #define and cli.funny's constant are compared."""
    from_binary = _native(native_binary, "--version").stdout.strip()
    cli_src = (ROOT / "selfhost" / "cli.funny").read_text(encoding="utf-8")
    version = re.search(r'VERSION = "([^"]+)"', cli_src).group(1)
    bytecode = re.search(r"BYTECODE_VERSION = (\d+)", cli_src).group(1)
    assert from_binary == f"funny {version} (bytecode v{bytecode})"


def test_yeet_makes_a_standalone_binary(native_binary, native_stub_binary, tmp_path):
    import os

    out = tmp_path / "hello_yeeted"
    env = {**os.environ, "FUNNY_STUB": str(native_stub_binary)}
    result = _native(native_binary, "yeet", "examples/hello.funny", "-o", str(out), env=env)
    assert result.returncode == 0, result.stderr
    assert "yeeted" in result.stdout and "no cap" in result.stdout
    assert out.exists()

    ran = subprocess.run([str(out)], capture_output=True, text=True, encoding="utf-8", timeout=60)
    assert ran.returncode == 0, ran.stderr
    assert ran.stdout == "yo sup world\n"


def test_yeet_creates_the_output_directory(native_binary, native_stub_binary, tmp_path):
    """The M13 packager fix, kept: `-o` into a directory that doesn't exist
    yet creates it rather than failing on the write."""
    import os

    out = tmp_path / "nested" / "deeper" / "prog"
    env = {**os.environ, "FUNNY_STUB": str(native_stub_binary)}
    assert _native(native_binary, "yeet", "examples/hello.funny", "-o", str(out), env=env).returncode == 0
    assert out.exists()


def test_usage_when_given_nothing(native_binary):
    result = _native(native_binary)
    assert result.returncode == 0
    assert "FunnyLang (native)" in result.stdout
    assert "usage: funny" in result.stdout


def test_a_missing_file_is_reported(native_binary):
    result = _native(native_binary, "run", "definitely_not_here.funny")
    assert result.returncode == 1
    assert "couldn't read" in result.stderr


def test_time_flag_reports_both_phases(native_binary):
    result = _native(native_binary, "--time", "run", "examples/hello.funny")
    assert result.returncode == 0
    assert re.search(r"compiled in \d+ms, ran in \d+ms\. blazingly fast \(probably\)", result.stdout)


def test_vibes_flag_prints_a_quip(native_binary):
    result = _native(native_binary, "--vibes", "run", "examples/hello.funny")
    assert result.returncode == 0
    assert re.search(r"\[(lexing|compiling|linking|running)\] ", result.stdout)
