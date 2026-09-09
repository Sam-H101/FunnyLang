"""NATIVE_PLAN.md N1 acceptance: "differential fuzz: random doubles format
identically to Python's repr()." Drives native/tests/fuzz_numfmt and checks
every line's C-computed repr against Python's own repr() of the identical
bit pattern (reconstructed via struct.unpack, never by re-parsing decimal
text, so there's no double rounding to muddy the comparison).

Usage: python3 fuzz_numfmt_check.py [count] [seed]
"""
from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FUZZ_SRC = ROOT / "native" / "tests" / "fuzz_numfmt.c"
NUMFMT_SRC = ROOT / "native" / "numfmt.c"
BIGNUM_SRC = ROOT / "native" / "bignum.c"


def build_fuzzer(out_path: Path) -> None:
    cmd = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
           "-o", str(out_path), str(FUZZ_SRC), str(NUMFMT_SRC), str(BIGNUM_SRC), "-lm"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"build failed:\n{result.stdout}\n{result.stderr}")


def bits_to_double(hex_bits: str) -> float:
    return struct.unpack(">d", bytes.fromhex(hex_bits))[0]


def main() -> int:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 20_000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 54321

    fuzzer = ROOT / "build" / "fuzz_numfmt_check_bin"
    fuzzer.parent.mkdir(parents=True, exist_ok=True)
    build_fuzzer(fuzzer)

    result = subprocess.run([str(fuzzer), str(seed), str(count)], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"fuzzer exited {result.returncode}:\n{result.stderr}", file=sys.stderr)
        return 1

    lines = result.stdout.splitlines()
    failures = 0
    for line in lines:
        hex_bits, c_repr = line.split(" ", 1)
        v = bits_to_double(hex_bits)
        expected = repr(v)
        if c_repr != expected:
            print(f"MISMATCH: bits={hex_bits} value={v!r} C={c_repr!r} python={expected!r}", file=sys.stderr)
            failures += 1
            if failures >= 20:
                print("... stopping after 20 mismatches", file=sys.stderr)
                break

    print(f"checked {len(lines)} cases, {failures} mismatches")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
