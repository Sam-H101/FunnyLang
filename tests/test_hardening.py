"""PLAN.md §M11 tasks 4-6: deep/self-referential structures don't recurse
forever, unicode identifiers/strings/paths work end to end, and large
programs compile fast without tripping a 16-bit jump-offset overflow."""
from __future__ import annotations

import subprocess
import sys
import time

import pytest

from conftest import compile_prog, run_funny

# -- task 4: self-referential structures print `[...]` / `{...}`, not stack overflow --


def test_self_referential_stash_prints_ellipsis():
    out = run_funny("yo s = [1, 2, 3]\ns[0] = s\nyap s\n")
    assert out == "[[...], 2, 3]\n"


def test_self_referential_groupchat_prints_ellipsis():
    out = run_funny('yo g = {"a": 1}\ng["self"] = g\nyap g\n')
    assert out == '{"a": 1, "self": {...}}\n'


def test_mutually_referential_stashes_print_ellipsis():
    src = (
        "yo a = [1]\n"
        "yo b = [2]\n"
        "a[0] = b\n"
        "b[0] = a\n"
        "yap a\n"
    )
    out = run_funny(src)
    assert out == "[[[...]]]\n"


# -- task 5: unicode identifiers, strings, and file paths ----------------


def test_unicode_identifier_and_string_end_to_end():
    src = 'yo café_🎉 = "héllo 中文 world"\nyap café_🎉\n'
    assert run_funny(src) == "héllo 中文 world\n"


def test_unicode_file_path_runs_via_cli(tmp_path):
    f = tmp_path / "héllo_🎉_中文.funny"
    f.write_text('yap "sup"\n', encoding="utf-8")
    result = subprocess.run(
        [sys.executable, "-m", "funnylang", "run", str(f)],
        capture_output=True, text=True, encoding="utf-8",
    )
    assert result.returncode == 0
    assert result.stdout == "sup\n"


# -- task 6: large programs compile fast, no jump-offset overflow --------


@pytest.mark.slow
def test_50k_flat_statements_compiles_under_10s():
    src = "\n".join(f"yap {i}" for i in range(50_000)) + "\n"
    start = time.perf_counter()
    compile_prog(src, "big_flat.funny")
    assert time.perf_counter() - start < 10.0


@pytest.mark.slow
def test_single_huge_if_block_forces_jump_long():
    # A single `sus` body of 50k statements makes JUMP_IF_FALSE's offset
    # exceed 0xFFFF -- this used to raise OverflowError before JUMP_LONG.
    lines = ["yo flag = fax", "sus (flag) {"]
    lines += [f"    yap {i}" for i in range(50_000)]
    lines.append("}")
    src = "\n".join(lines) + "\n"

    start = time.perf_counter()
    unit = compile_prog(src, "big_if.funny")
    elapsed = time.perf_counter() - start
    assert elapsed < 10.0
    assert len(unit.protos[unit.entry_proto].code) > 0xFFFF

    out = run_funny(src, "big_if.funny")
    lines_out = out.splitlines()
    assert len(lines_out) == 50_000
    assert lines_out[0] == "0"
    assert lines_out[-1] == "49999"


@pytest.mark.slow
def test_single_huge_if_block_false_branch_skips_whole_body():
    lines = ["yo flag = cap", "sus (flag) {"]
    lines += [f"    yap {i}" for i in range(50_000)]
    lines.append("}")
    lines.append('yap "done"')
    src = "\n".join(lines) + "\n"
    assert run_funny(src, "big_if_false.funny") == "done\n"


@pytest.mark.slow
def test_huge_while_loop_body_forces_loop_long():
    lines = ["yo i = 0", "yo total = 0", "bruh (i < 2) {"]
    lines += [f"    total = total + {i}" for i in range(15_000)]
    lines.append("    i = i + 1")
    lines.append("}")
    lines.append("yap total")
    src = "\n".join(lines) + "\n"
    out = run_funny(src, "big_loop.funny")
    assert out.strip() == str(sum(range(15_000)) * 2)


@pytest.mark.slow
def test_bail_out_of_huge_loop_body_widens_forward_jump():
    lines = ["yo i = 0", "yo total = 0", "bruh (fax) {"]
    lines.append("    sus (i == 3) { bail }")
    lines += ["    total = total + 1" for _ in range(15_000)]
    lines.append("    i = i + 1")
    lines.append("}")
    lines.append("yap total")
    lines.append("yap i")
    src = "\n".join(lines) + "\n"
    out = run_funny(src, "big_break.funny")
    assert out.strip().splitlines() == ["45000", "3"]
