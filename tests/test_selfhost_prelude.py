"""PLAN.md §M12 task 2: selfhost/prelude.funny's byte-buffer writers and
UTF-8 encoder, cross-checked against Python's own struct-based encoding
(the same encoding serializer.py uses) so a self-hosted emitter built on
top of these helpers produces spec-conformant §5.2 bytes."""
from __future__ import annotations

import shutil
import struct
from pathlib import Path

import pytest

PRELUDE = Path(__file__).resolve().parent.parent / "selfhost" / "prelude.funny"


def _run_against_prelude(tmp_path, body: str) -> str:
    from conftest import run_funny

    shutil.copyfile(PRELUDE, tmp_path / "prelude.funny")
    script = tmp_path / "check.funny"
    imports = (
        'gimme { new_buf, write_u8, write_u16, write_u32, write_u64, '
        'utf8_encode, write_str, write_int_magnitude, int_byte_len } '
        'from "prelude.funny"\n'
    )
    script.write_text(imports + body, encoding="utf-8")
    return run_funny(script.read_text(encoding="utf-8"), str(script))


def _expected_stash_repr(values: list[int]) -> str:
    return "[" + ", ".join(str(v) for v in values) + "]\n"


@pytest.mark.parametrize("value", [0, 1, 0x7F, 0x80, 0xFF])
def test_write_u8(tmp_path, value):
    out = _run_against_prelude(tmp_path, f"yo b = new_buf()\nwrite_u8(b, {value})\nyap b\n")
    assert out == _expected_stash_repr([value & 0xFF])


@pytest.mark.parametrize("value", [0, 0x1234, 0xFFFF, 0xABCD])
def test_write_u16(tmp_path, value):
    out = _run_against_prelude(tmp_path, f"yo b = new_buf()\nwrite_u16(b, {value})\nyap b\n")
    assert out == _expected_stash_repr(list(struct.pack(">H", value)))


@pytest.mark.parametrize("value", [0, 1, 0xDEADBEEF, 0xFFFFFFFF])
def test_write_u32(tmp_path, value):
    out = _run_against_prelude(tmp_path, f"yo b = new_buf()\nwrite_u32(b, {value})\nyap b\n")
    assert out == _expected_stash_repr(list(struct.pack(">I", value)))


def test_write_u64(tmp_path):
    value = 0x0102030405060708
    out = _run_against_prelude(tmp_path, f"yo b = new_buf()\nwrite_u64(b, {value})\nyap b\n")
    assert out == _expected_stash_repr(list(struct.pack(">Q", value)))


@pytest.mark.parametrize("s", ["", "a", "hello", "é", "中", "🎉", "a🎉中é"])
def test_utf8_encode_matches_python(tmp_path, s):
    literal = s.replace("\\", "\\\\").replace('"', '\\"')
    out = _run_against_prelude(tmp_path, f'yap utf8_encode("{literal}")\n')
    assert out == _expected_stash_repr(list(s.encode("utf-8")))


@pytest.mark.parametrize("s", ["", "hi", "hi🎉", "spread 🎉 out 中 more é"])
def test_write_str_matches_python(tmp_path, s):
    literal = s.replace("\\", "\\\\").replace('"', '\\"')
    out = _run_against_prelude(tmp_path, f'yo b = new_buf()\nwrite_str(b, "{literal}")\nyap b\n')
    encoded = s.encode("utf-8")
    expected = list(struct.pack(">I", len(encoded))) + list(encoded)
    assert out == _expected_stash_repr(expected)


def _expected_int_magnitude(value: int) -> list[int]:
    sign = 1 if value < 0 else 0
    mag = abs(value)
    n = (mag.bit_length() + 7) // 8
    return [sign] + list(struct.pack(">I", n)) + list(mag.to_bytes(n, "big"))


@pytest.mark.parametrize("value", [
    0, 1, -1, 255, 256, -300, 65535, 65536, -1234567890,
    123456789012345678901234567890, -123456789012345678901234567890,
])
def test_write_int_magnitude_matches_serializer(tmp_path, value):
    out = _run_against_prelude(tmp_path, f"yo b = new_buf()\nwrite_int_magnitude(b, {value})\nyap b\n")
    assert out == _expected_stash_repr(_expected_int_magnitude(value))


def test_int_byte_len_matches_bit_length(tmp_path):
    body = "\n".join(f"yap int_byte_len({m})" for m in [0, 1, 255, 256, 65535, 65536, 2**64])
    out = _run_against_prelude(tmp_path, body + "\n")
    expected = "".join(f"{(m.bit_length() + 7) // 8}\n" for m in [0, 1, 255, 256, 65535, 65536, 2**64])
    assert out == expected
