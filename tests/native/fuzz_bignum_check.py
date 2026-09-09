"""NATIVE_PLAN.md N1 acceptance: "differential fuzz: random bignum ops match
Python's int exactly." Drives native/tests/fuzz_bignum (built separately by
this script -- it's not part of build.sh's own `funny` target, see its own
docstring) and checks every line's C-computed result against Python's own
arbitrary-precision `int`, which is what PLAN.md's `numba` is specified to
match byte for byte (see PLAN.md M12/M15's own differential-testing
discipline, extended here to the native runtime).

Usage: python3 fuzz_bignum_check.py [count] [seed]
Defaults to a modest count for a quick local run; CI (or a manual "make
sure this is really solid" pass) should pass a much larger count -- the
plan's own acceptance bar is 1,000,000.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FUZZ_SRC = ROOT / "native" / "tests" / "fuzz_bignum.c"
BIGNUM_SRC = ROOT / "native" / "bignum.c"


def build_fuzzer(out_path: Path) -> None:
    cc = "cc"
    cmd = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
           "-o", str(out_path), str(FUZZ_SRC), str(BIGNUM_SRC), "-lm"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"build failed:\n{result.stdout}\n{result.stderr}")


def check(line: str) -> str | None:
    """Returns an error description, or None if the line checks out."""
    parts = line.split()
    op = parts[0]
    if op == "add":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a + b
    elif op == "sub":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a - b
    elif op == "mul":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a * b
    elif op == "band":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a & b
    elif op == "bor":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a | b
    elif op == "bxor":
        a, b, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a ^ b
    elif op == "bnot":
        a, expected = int(parts[1]), int(parts[2])
        actual = ~a
    elif op == "shl":
        a, n, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a << n
    elif op == "shr":
        a, n, expected = int(parts[1]), int(parts[2]), int(parts[3])
        actual = a >> n
    elif op == "divmod":
        a, b, expectedQ, expectedR = int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4])
        actualQ, actualR = divmod(a, b)
        if actualQ != expectedQ or actualR != expectedR:
            return f"{line.strip()}: python divmod({a},{b}) = ({actualQ},{actualR})"
        return None
    else:
        return f"unknown op in line: {line!r}"
    if actual != expected:
        return f"{line.strip()}: python {op} = {actual}"
    return None


def main() -> int:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20_000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 12345

    fuzzer = ROOT / "build" / "fuzz_bignum_check_bin"
    fuzzer.parent.mkdir(parents=True, exist_ok=True)
    build_fuzzer(fuzzer)

    result = subprocess.run(
        [str(fuzzer), str(seed), str(count)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"fuzzer exited {result.returncode}:\n{result.stderr}", file=sys.stderr)
        return 1

    lines = result.stdout.splitlines()
    failures = 0
    for line in lines:
        err = check(line)
        if err is not None:
            print("MISMATCH:", err, file=sys.stderr)
            failures += 1
            if failures >= 20:
                print("... stopping after 20 mismatches", file=sys.stderr)
                break

    print(f"checked {len(lines)} cases, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
