from __future__ import annotations

import pytest

from conftest import compile_prog
from funnylang.chunk import CompiledUnit, ConstPool, FunctionProto, LineEntry
from funnylang.disasm import disassemble
from funnylang.errors import FunnyError
from funnylang.serializer import (
    MAGIC_FUNNYC, MAGIC_FUNNYPAK, dump_funnyc, dump_funnypak, load_funnyc, load_funnypak,
)


def _roundtrip(unit: CompiledUnit) -> CompiledUnit:
    return load_funnyc(dump_funnyc(unit))


def test_roundtrip_hello_world():
    unit = compile_prog('yo greeting = "yo sup world"\nyap greeting\n')
    unit2 = _roundtrip(unit)
    assert disassemble(unit) == disassemble(unit2)


def test_roundtrip_preserves_entry_proto():
    unit = compile_prog("bet f() { bounce 1 }\nyap f()\n")
    unit2 = _roundtrip(unit)
    assert unit2.entry_proto == unit.entry_proto


def test_roundtrip_every_const_tag():
    pool = ConstPool()
    pool.add_ghost()
    pool.add_bool(True)
    pool.add_bool(False)
    pool.add_int(0)
    pool.add_int(-1)
    pool.add_int(10 ** 200)  # a 500-bit-ish big int
    pool.add_int(-(10 ** 200))
    pool.add_float(0.0)
    pool.add_float(-3.5)
    pool.add_float(float("inf"))
    pool.add_float(float("-inf"))
    pool.add_string("")
    pool.add_string("hello 🍕")
    pool.add_proto_ref(0)
    proto = FunctionProto("<script>", 0, 0, False, 0, 1, 0, bytes([2, 55]), ())  # GHOST; RETURN
    unit = CompiledUnit("<test>", pool, [proto], 0)
    unit2 = _roundtrip(unit)
    assert unit2.const_pool.entries == unit.const_pool.entries


def test_int_and_float_stay_distinct_after_roundtrip():
    pool = ConstPool()
    pool.add_int(1)
    pool.add_float(1.0)
    proto = FunctionProto("<script>", 0, 0, False, 0, 1, 0, bytes([2, 55]), ())
    unit = CompiledUnit("<test>", pool, [proto], 0)
    unit2 = _roundtrip(unit)
    assert (2, 1) in unit2.const_pool.entries
    assert (3, 1.0) in unit2.const_pool.entries


def test_nan_float_roundtrips():
    pool = ConstPool()
    idx = pool.add_float(float("nan"))
    proto = FunctionProto("<script>", 0, 0, False, 0, 1, 0, bytes([2, 55]), ())
    unit = CompiledUnit("<test>", pool, [proto], 0)
    unit2 = _roundtrip(unit)
    tag, value = unit2.const_pool.entries[idx]
    assert value != value  # nan != nan


def test_roundtrip_with_debug_info_off_drops_lines():
    unit = compile_prog("yo x = 1\nyap x\n")
    data = dump_funnyc(unit, has_debug_info=False)
    unit2 = load_funnyc(data)
    assert all(p.lines == () for p in unit2.protos)


def test_bad_magic_raises():
    with pytest.raises(FunnyError):
        load_funnyc(b"NOTFUNNY" + b"\x00" * 20)


def test_truncated_file_raises():
    unit = compile_prog("yap 1\n")
    data = dump_funnyc(unit)
    with pytest.raises(FunnyError):
        load_funnyc(data[:10])


def test_version_mismatch_raises():
    unit = compile_prog("yap 1\n")
    data = bytearray(dump_funnyc(unit))
    # version field sits right after the 6-byte magic
    data[6] = 0xFF
    data[7] = 0xFF
    with pytest.raises(FunnyError):
        load_funnyc(bytes(data))


def test_funnypak_roundtrip():
    unit_a = compile_prog("yap 1\n")
    unit_b = compile_prog("yap 2\n")
    data = dump_funnypak({"main": unit_a, "helper": unit_b}, "main")
    modules, entry_name = load_funnypak(data)
    assert entry_name == "main"
    assert set(modules) == {"main", "helper"}
    assert disassemble(modules["main"]) == disassemble(unit_a)
    assert disassemble(modules["helper"]) == disassemble(unit_b)


def test_funnypak_bad_magic_raises():
    with pytest.raises(FunnyError):
        load_funnypak(b"NOTAPAK\x00" + b"\x00" * 20)


def test_hello_disassembly_matches_golden(tmp_path):
    unit = compile_prog(open("examples/hello.funny", encoding="utf-8").read(), path="examples/hello.funny")
    golden = open("tests/golden/hello.disasm", encoding="utf-8").read()
    assert disassemble(unit) == golden
