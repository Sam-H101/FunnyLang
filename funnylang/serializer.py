"""`.funnyc` and `.funnypak` binary I/O (PLAN.md §5.2, §5.3)."""
from __future__ import annotations

import struct

from . import BYTECODE_VERSION
from .chunk import (
    TAG_BOOL, TAG_FLOAT, TAG_GHOST, TAG_INT, TAG_PROTO_REF, TAG_STRING,
    CompiledUnit, ConstPool, FunctionProto, LineEntry,
)
from .errors import FunnyError

MAGIC_FUNNYC = b"FUNNY\x00"
MAGIC_FUNNYPAK = b"FUNNYPAK\x00"

FLAG_HAS_DEBUG_INFO = 0x1


class BytecodeVersionMismatch(FunnyError):
    flavor = "BytecodeVersionMismatch"


class _Writer:
    def __init__(self):
        self.buf = bytearray()

    def u8(self, v: int) -> "_Writer":
        self.buf += struct.pack(">B", v)
        return self

    def u16(self, v: int) -> "_Writer":
        self.buf += struct.pack(">H", v)
        return self

    def u32(self, v: int) -> "_Writer":
        self.buf += struct.pack(">I", v)
        return self

    def raw(self, data: bytes) -> "_Writer":
        self.buf += data
        return self

    def str_(self, s: str) -> "_Writer":
        encoded = s.encode("utf-8")
        self.u32(len(encoded))
        self.raw(encoded)
        return self

    def bytes_(self, data: bytes) -> "_Writer":
        self.u32(len(data))
        self.raw(data)
        return self

    def bytes8(self) -> bytes:
        return bytes(self.buf)


class _Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def raw(self, n: int) -> bytes:
        chunk = self.data[self.pos:self.pos + n]
        if len(chunk) != n:
            raise BytecodeVersionMismatch(
                "this bytecode file is truncated or corrupt.",
                roast="this bytecode ends mid-sentence. did you even finish writing it?",
            )
        self.pos += n
        return chunk

    def u8(self) -> int:
        return struct.unpack(">B", self.raw(1))[0]

    def u16(self) -> int:
        return struct.unpack(">H", self.raw(2))[0]

    def u32(self) -> int:
        return struct.unpack(">I", self.raw(4))[0]

    def str_(self) -> str:
        n = self.u32()
        return self.raw(n).decode("utf-8")

    def bytes_(self) -> bytes:
        n = self.u32()
        return self.raw(n)


def _write_int_magnitude(w: _Writer, value: int) -> None:
    sign = 1 if value < 0 else 0
    magnitude = abs(value)
    n_bytes = (magnitude.bit_length() + 7) // 8
    payload = magnitude.to_bytes(n_bytes, "big")
    w.u8(sign)
    w.u32(len(payload))
    w.raw(payload)


def _read_int_magnitude(r: _Reader) -> int:
    sign = r.u8()
    n = r.u32()
    magnitude = int.from_bytes(r.raw(n), "big")
    return -magnitude if sign else magnitude


def _write_const(w: _Writer, tag: int, value) -> None:
    w.u8(tag)
    if tag == TAG_GHOST:
        pass
    elif tag == TAG_BOOL:
        w.u8(1 if value else 0)
    elif tag == TAG_INT:
        _write_int_magnitude(w, value)
    elif tag == TAG_FLOAT:
        w.raw(struct.pack(">d", value))
    elif tag == TAG_STRING:
        w.str_(value)
    elif tag == TAG_PROTO_REF:
        w.u32(value)
    else:  # pragma: no cover
        raise AssertionError(f"unknown const tag {tag}")


def _read_const(r: _Reader) -> tuple[int, object]:
    tag = r.u8()
    if tag == TAG_GHOST:
        return tag, None
    if tag == TAG_BOOL:
        return tag, bool(r.u8())
    if tag == TAG_INT:
        return tag, _read_int_magnitude(r)
    if tag == TAG_FLOAT:
        return tag, struct.unpack(">d", r.raw(8))[0]
    if tag == TAG_STRING:
        return tag, r.str_()
    if tag == TAG_PROTO_REF:
        return tag, r.u32()
    raise BytecodeVersionMismatch(
        f"unknown constant tag {tag} in this bytecode.",
        roast="this bytecode is from a different era. recompile it.",
    )


def _write_proto(w: _Writer, proto: FunctionProto) -> None:
    w.str_(proto.name)
    w.u8(proto.arity)
    w.u8(proto.default_count)
    w.u8(1 if proto.is_variadic else 0)
    w.u8(proto.upvalue_count)
    w.u16(proto.max_stack)
    w.u8(proto.local_count)
    w.bytes_(proto.code)
    w.u32(len(proto.lines))
    for entry in proto.lines:
        w.u32(entry.code_offset)
        w.u32(entry.line)
        w.u32(entry.col)


def _read_proto(r: _Reader) -> FunctionProto:
    name = r.str_()
    arity = r.u8()
    default_count = r.u8()
    is_variadic = bool(r.u8())
    upvalue_count = r.u8()
    max_stack = r.u16()
    local_count = r.u8()
    code = r.bytes_()
    line_count = r.u32()
    lines = tuple(LineEntry(r.u32(), r.u32(), r.u32()) for _ in range(line_count))
    return FunctionProto(
        name=name, arity=arity, default_count=default_count, is_variadic=is_variadic,
        upvalue_count=upvalue_count, max_stack=max_stack, local_count=local_count,
        code=code, lines=lines,
    )


def dump_funnyc(unit: CompiledUnit, *, has_debug_info: bool = True) -> bytes:
    w = _Writer()
    w.raw(MAGIC_FUNNYC)
    w.u16(BYTECODE_VERSION)
    w.u16(FLAG_HAS_DEBUG_INFO if has_debug_info else 0)
    w.str_(unit.source_name)
    w.u32(len(unit.const_pool.entries))
    for tag, value in unit.const_pool.entries:
        _write_const(w, tag, value)
    w.u32(len(unit.protos))
    for proto in unit.protos:
        if has_debug_info:
            _write_proto(w, proto)
        else:
            _write_proto(w, FunctionProto(
                proto.name, proto.arity, proto.default_count, proto.is_variadic,
                proto.upvalue_count, proto.max_stack, proto.local_count, proto.code, (),
            ))
    w.u32(unit.entry_proto)
    return w.bytes8()


def load_funnyc(data: bytes) -> CompiledUnit:
    r = _Reader(data)
    magic = r.raw(len(MAGIC_FUNNYC))
    if magic != MAGIC_FUNNYC:
        raise BytecodeVersionMismatch(
            "this isn't a .funnyc file.",
            roast="that's not FunnyLang bytecode. that's just... bytes.",
        )
    version = r.u16()
    if version != BYTECODE_VERSION:
        raise BytecodeVersionMismatch(
            f"bytecode version {version} != {BYTECODE_VERSION}.",
            roast="this bytecode is from a different era. recompile it.",
        )
    r.u16()  # flags (currently informational only)
    source_name = r.str_()
    const_count = r.u32()
    pool = ConstPool()
    for _ in range(const_count):
        tag, value = _read_const(r)
        pool.entries.append((tag, value))
        pool._index[(tag, value)] = len(pool.entries) - 1
    proto_count = r.u32()
    protos = [_read_proto(r) for _ in range(proto_count)]
    entry_proto = r.u32()
    return CompiledUnit(source_name, pool, protos, entry_proto)


def dump_funnypak(modules: dict, entry_name: str) -> bytes:
    """`modules`: dict[logical_name -> CompiledUnit]."""
    w = _Writer()
    w.raw(MAGIC_FUNNYPAK)
    w.u16(BYTECODE_VERSION)
    w.u32(len(modules))
    for name, unit in modules.items():
        w.str_(name)
        w.bytes_(dump_funnyc(unit))
    w.str_(entry_name)
    return w.bytes8()


def load_funnypak(data: bytes) -> tuple[dict, str]:
    r = _Reader(data)
    magic = r.raw(len(MAGIC_FUNNYPAK))
    if magic != MAGIC_FUNNYPAK:
        raise BytecodeVersionMismatch(
            "this isn't a .funnypak file.",
            roast="that's not a FunnyLang bundle. that's just... bytes.",
        )
    version = r.u16()
    if version != BYTECODE_VERSION:
        raise BytecodeVersionMismatch(
            f"bytecode version {version} != {BYTECODE_VERSION}.",
            roast="this bytecode is from a different era. recompile it.",
        )
    module_count = r.u32()
    modules = {}
    for _ in range(module_count):
        name = r.str_()
        modules[name] = load_funnyc(r.bytes_())
    entry_name = r.str_()
    return modules, entry_name
