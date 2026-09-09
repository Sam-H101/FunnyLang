"""In-progress bytecode buffers and their frozen, serializable form
(PLAN.md §M4 / §5.2). One ConstPool is shared by every function in a
compiled file; each function gets its own Chunk (bytecode-in-progress) which
`finish()`es into a FunctionProto."""
from __future__ import annotations

import bisect
from dataclasses import dataclass, field

from .opcodes import OPERANDS, Op
from .source import Span

# Conditional jump opcodes (PLAN.md §5.1) — all forward-only, all exactly 3
# bytes short-form (1 opcode + u16 offset), all compare-and-branch with
# distinct pop/keep semantics that a relaxation pass must never disturb.
_COND_JUMP_OPS = frozenset({
    Op.JUMP_IF_FALSE, Op.JUMP_IF_TRUE, Op.JUMP_IF_FALSE_KEEP,
    Op.JUMP_IF_TRUE_KEEP, Op.JUMP_IF_GHOST_KEEP,
})

# Constant-pool tags (PLAN.md §5.2).
TAG_GHOST = 0
TAG_BOOL = 1
TAG_INT = 2
TAG_FLOAT = 3
TAG_STRING = 4
TAG_PROTO_REF = 5


class ConstPool:
    """Deduped by (tag, value) — an int 1 and a float 1.0 are distinct
    constants (PLAN.md §M12 byte-exactness discipline calls this out
    explicitly), so tags are always part of the dedupe key."""

    def __init__(self):
        self.entries: list[tuple[int, object]] = []
        self._index: dict[tuple[int, object], int] = {}

    def _add(self, tag: int, value) -> int:
        key = (tag, value)
        existing = self._index.get(key)
        if existing is not None:
            return existing
        idx = len(self.entries)
        self.entries.append(key)
        self._index[key] = idx
        return idx

    def add_ghost(self) -> int:
        return self._add(TAG_GHOST, None)

    def add_bool(self, value: bool) -> int:
        return self._add(TAG_BOOL, bool(value))

    def add_int(self, value: int) -> int:
        return self._add(TAG_INT, int(value))

    def add_float(self, value: float) -> int:
        return self._add(TAG_FLOAT, float(value))

    def add_string(self, value: str) -> int:
        return self._add(TAG_STRING, value)

    def add_proto_ref(self, proto_index: int) -> int:
        return self._add(TAG_PROTO_REF, int(proto_index))

    def __len__(self) -> int:
        return len(self.entries)


@dataclass(frozen=True)
class LineEntry:
    code_offset: int
    line: int
    col: int


@dataclass(frozen=True)
class FunctionProto:
    name: str
    arity: int
    default_count: int
    is_variadic: bool
    upvalue_count: int
    max_stack: int
    local_count: int
    code: bytes
    lines: tuple  # tuple[LineEntry, ...]

    def line_for_offset(self, offset: int) -> tuple[int, int]:
        """(line, col) of the instruction at `offset`, via the last LineEntry
        at or before it."""
        best = (0, 0)
        for entry in self.lines:
            if entry.code_offset > offset:
                break
            best = (entry.line, entry.col)
        return best


@dataclass
class _JumpSite:
    """One JUMP/LOOP/conditional-jump instruction, tracked so `Chunk.finish()`
    can retroactively widen it to a *_LONG form if its u16 offset overflowed
    (PLAN.md §16 — M11's 50k-line stress test found this)."""
    opcode_pos: int
    op: Op
    is_loop: bool
    target: int | None = None  # absolute offset in the *original* buffer


@dataclass
class CompiledUnit:
    source_name: str
    const_pool: ConstPool
    protos: list  # list[FunctionProto]
    entry_proto: int


class Chunk:
    """One function's bytecode, being built. Shares `const_pool` with every
    other function compiled in the same file."""

    def __init__(self, name: str, arity: int, default_count: int, is_variadic: bool, const_pool: ConstPool):
        self.name = name
        self.arity = arity
        self.default_count = default_count
        self.is_variadic = is_variadic
        self.const_pool = const_pool
        self.code = bytearray()
        self.lines: list[LineEntry] = []
        self.upvalue_count = 0
        self.local_count = 0
        self.stack_depth = 0
        self.max_stack = 0
        self._jump_sites: dict[int, _JumpSite] = {}  # keyed by opcode_pos
        self._needs_relax = False

    # -- stack bookkeeping (used for FunctionProto.max_stack) -------------

    def push(self, n: int = 1) -> None:
        self.stack_depth += n
        if self.stack_depth > self.max_stack:
            self.max_stack = self.stack_depth

    def pop(self, n: int = 1) -> None:
        self.stack_depth -= n
        if self.stack_depth < 0:
            raise AssertionError(f"compiler bug: stack underflow in {self.name!r}")

    # -- raw emission -------------------------------------------------------

    def _mark_line(self, span: Span | None) -> None:
        if span is None:
            return
        offset = len(self.code)
        if self.lines and self.lines[-1].line == span.line and self.lines[-1].col == span.col:
            return
        self.lines.append(LineEntry(offset, span.line, span.col))

    def _write_u8(self, value: int) -> None:
        if not (0 <= value <= 0xFF):
            raise AssertionError(f"compiler bug: u8 operand out of range: {value}")
        self.code.append(value)

    def _write_u16(self, value: int) -> None:
        if not (0 <= value <= 0xFFFF):
            raise OverflowError("your function is too long. seek help.")
        self.code += value.to_bytes(2, "big")

    def emit(self, op: Op, *operands: int, span: Span | None = None) -> int:
        """Writes `op` and its fixed-width operands. Returns the offset the
        instruction started at."""
        widths = OPERANDS[op]
        if widths is None:
            raise AssertionError(f"{op.name} has variable-width operands; use a dedicated emit_* method")
        if len(operands) != len(widths):
            raise AssertionError(f"compiler bug: {op.name} expects {len(widths)} operand(s), got {len(operands)}")
        self._mark_line(span)
        start = len(self.code)
        self.code.append(int(op))
        for value, width in zip(operands, widths):
            if width == 1:
                self._write_u8(value)
            elif width == 2:
                self._write_u16(value)
            else:  # pragma: no cover - no opcode currently uses another width
                raise AssertionError(f"unsupported operand width {width}")
        return start

    def emit_closure(self, proto_const_idx: int, upvalues, span: Span | None = None) -> int:
        self._mark_line(span)
        start = len(self.code)
        self.code.append(int(Op.CLOSURE))
        self._write_u16(proto_const_idx)
        for uv in upvalues:
            self._write_u8(1 if uv.is_local else 0)
            self._write_u8(uv.index)
        return start

    # -- jump patching ------------------------------------------------------

    def emit_jump(self, op: Op, span: Span | None = None) -> int:
        opcode_pos = len(self.code)
        self.emit(op, 0, span=span)
        self._jump_sites[opcode_pos] = _JumpSite(opcode_pos=opcode_pos, op=op, is_loop=False)
        return len(self.code) - 2

    def patch_jump(self, site: int) -> None:
        target = len(self.code)
        self._jump_sites[site - 1].target = target
        offset = target - (site + 2)
        if 0 <= offset <= 0xFFFF:
            self.code[site:site + 2] = offset.to_bytes(2, "big")
        else:
            # Too far for a u16 offset — leave a placeholder; finish()'s
            # relaxation pass rewrites this whole instruction as a JUMP_LONG
            # (or a short-jump-over-JUMP_LONG trampoline, for conditionals).
            self.code[site:site + 2] = (0xFFFF).to_bytes(2, "big")
            self._needs_relax = True

    def emit_loop(self, loop_start: int, span: Span | None = None) -> None:
        self._mark_line(span)
        opcode_pos = len(self.code)
        self.code.append(int(Op.LOOP))
        offset = len(self.code) + 2 - loop_start
        if 0 <= offset <= 0xFFFF:
            self.code += offset.to_bytes(2, "big")
        else:
            self.code += (0xFFFF).to_bytes(2, "big")
            self._needs_relax = True
        self._jump_sites[opcode_pos] = _JumpSite(opcode_pos=opcode_pos, op=Op.LOOP, is_loop=True, target=loop_start)

    @property
    def next_offset(self) -> int:
        return len(self.code)

    # -- finalize -----------------------------------------------------------

    def finish(self) -> FunctionProto:
        if self._needs_relax:
            code, lines = self._relax()
        else:
            code, lines = bytes(self.code), tuple(self.lines)
        return FunctionProto(
            name=self.name,
            arity=self.arity,
            default_count=self.default_count,
            is_variadic=self.is_variadic,
            upvalue_count=self.upvalue_count,
            max_stack=self.max_stack,
            local_count=self.local_count,
            code=code,
            lines=lines,
        )

    def _relax(self) -> tuple[bytes, tuple]:
        """Rewrites every JUMP/LOOP/conditional-jump whose u16 offset
        overflowed into a wide form, via a standard assembler-style
        fixed-point relaxation (PLAN.md §16, M11).

        Unconditional JUMP/LOOP simply become JUMP_LONG/LOOP_LONG (u32
        offset). A conditional (JUMP_IF_FALSE and friends) can't itself grow
        a wider offset without becoming a different opcode with different
        pop/keep semantics, so instead it keeps its original opcode and a
        *tiny* fixed offset that jumps over a 3-byte unconditional JUMP,
        landing on a JUMP_LONG that carries the real (now-far) target:

            COND_OP  +3   ; taken -> lands right on JUMP_LONG below
            JUMP     +5   ; not taken -> skips JUMP_LONG, into the body
            JUMP_LONG <target>
        """
        sites = sorted(self._jump_sites.values(), key=lambda s: s.opcode_pos)
        starts = [s.opcode_pos for s in sites]
        n = len(sites)
        is_long = [False] * n

        def size_of(i: int) -> int:
            if not is_long[i]:
                return 3
            return 11 if sites[i].op in _COND_JUMP_OPS else 5

        def build_prefix() -> list[int]:
            cum = []
            total = 0
            for i in range(n):
                total += size_of(i) - 3
                cum.append(total)
            return cum

        def shift(p: int, cum: list[int]) -> int:
            idx = bisect.bisect_right(starts, p - 3)
            return p + (cum[idx - 1] if idx > 0 else 0)

        while True:
            cum = build_prefix()
            changed = False
            for i, s in enumerate(sites):
                if is_long[i]:
                    continue
                new_opcode_pos = shift(s.opcode_pos, cum)
                new_target = shift(s.target, cum)
                instr_end = new_opcode_pos + 3
                offset = (instr_end - new_target) if s.is_loop else (new_target - instr_end)
                if not (0 <= offset <= 0xFFFF):
                    is_long[i] = True
                    changed = True
            if not changed:
                break

        cum = build_prefix()
        new_code = bytearray()
        pos = 0
        for i, s in enumerate(sites):
            new_code += self.code[pos:s.opcode_pos]
            new_opcode_pos = len(new_code)
            new_target = shift(s.target, cum)
            if not is_long[i]:
                instr_end = new_opcode_pos + 3
                offset = (instr_end - new_target) if s.is_loop else (new_target - instr_end)
                new_code.append(int(s.op))
                new_code += offset.to_bytes(2, "big")
            elif s.is_loop:
                offset = (new_opcode_pos + 5) - new_target
                new_code.append(int(Op.LOOP_LONG))
                new_code += offset.to_bytes(4, "big")
            elif s.op == Op.JUMP:
                offset = new_target - (new_opcode_pos + 5)
                new_code.append(int(Op.JUMP_LONG))
                new_code += offset.to_bytes(4, "big")
            else:
                new_code.append(int(s.op))
                new_code += (3).to_bytes(2, "big")
                new_code.append(int(Op.JUMP))
                new_code += (5).to_bytes(2, "big")
                offset = new_target - (new_opcode_pos + 11)
                new_code.append(int(Op.JUMP_LONG))
                new_code += offset.to_bytes(4, "big")
            pos = s.opcode_pos + 3
        new_code += self.code[pos:]

        new_lines = tuple(
            LineEntry(shift(entry.code_offset, cum), entry.line, entry.col)
            for entry in self.lines
        )
        return bytes(new_code), new_lines
