"""Human-readable bytecode dump (PLAN.md §M4 task 5)."""
from __future__ import annotations

import json

from .chunk import TAG_BOOL, TAG_FLOAT, TAG_GHOST, TAG_INT, TAG_PROTO_REF, TAG_STRING, CompiledUnit, FunctionProto
from .opcodes import OPERANDS, Op

_NAMED_CONST_OPS = {
    Op.GET_GLOBAL, Op.SET_GLOBAL, Op.DEF_GLOBAL, Op.GET_PROP, Op.SET_PROP,
    Op.GET_PROP_SAFE, Op.INVOKE, Op.INVOKE_OG, Op.EXPORT,
    Op.PTR_GLOBAL, Op.PTR_PROP, Op.PTR_LOCAL, Op.PTR_UPVAL,
}

# Which fixed operand of a _NAMED_CONST_OPS opcode is the const-pool index —
# 0 for everything except PTR_LOCAL/PTR_UPVAL, whose *second* operand is the
# (redundant, display-only) name const; their first is the slot/upvalue index.
_NAMED_CONST_OPERAND_INDEX = {
    Op.PTR_LOCAL: 1,
    Op.PTR_UPVAL: 1,
}


def _const_repr(unit: CompiledUnit, idx: int) -> str:
    tag, value = unit.const_pool.entries[idx]
    if tag == TAG_GHOST:
        return "ghost"
    if tag == TAG_BOOL:
        return "fax" if value else "cap"
    if tag == TAG_INT:
        return str(value)
    if tag == TAG_FLOAT:
        return repr(value)
    if tag == TAG_STRING:
        return json.dumps(value)
    if tag == TAG_PROTO_REF:
        return f"<proto #{value}>"
    return "?"  # pragma: no cover


def disassemble_proto(unit: CompiledUnit, proto: FunctionProto, label: str) -> str:
    out = [f"== {label} (arity {proto.arity}) =="]
    code = proto.code
    ip = 0
    last_shown_line = None
    while ip < len(code):
        op = Op(code[ip])
        current_line = None
        for entry in proto.lines:
            if entry.code_offset > ip:
                break
            current_line = entry.line
        marker = f"line {current_line}" if current_line is not None and current_line != last_shown_line else ""
        if marker:
            last_shown_line = current_line
        pos = ip + 1
        parts: list[str] = []
        comment = ""
        if op == Op.CLOSURE:
            const_idx = int.from_bytes(code[pos:pos + 2], "big")
            pos += 2
            parts.append(str(const_idx))
            tag, ref = unit.const_pool.entries[const_idx]
            target = unit.protos[ref] if tag == TAG_PROTO_REF else None
            n_upvals = target.upvalue_count if target is not None else 0
            upvals = []
            for _ in range(n_upvals):
                is_local, uv_idx = code[pos], code[pos + 1]
                pos += 2
                upvals.append(f"{'local' if is_local else 'upval'}:{uv_idx}")
            comment = f"; proto #{ref}" + (f" [{', '.join(upvals)}]" if upvals else "")
        else:
            widths = OPERANDS[op]
            for w in widths:
                val = int.from_bytes(code[pos:pos + w], "big")
                parts.append(str(val))
                pos += w
            if op == Op.CONST or op in _NAMED_CONST_OPS:
                const_pos = _NAMED_CONST_OPERAND_INDEX.get(op, 0)
                comment = f"; {_const_repr(unit, int(parts[const_pos]))}"
        offset_str = f"{ip:04d}"
        operand_str = " ".join(parts)
        line = f"{offset_str}  {marker:<8}{op.name:<14}{operand_str}"
        if comment:
            line += f"    {comment}"
        out.append(line.rstrip())
        ip = pos
    return "\n".join(out)


def disassemble(unit: CompiledUnit) -> str:
    sections = []
    for i, proto in enumerate(unit.protos):
        label = proto.name if proto.name == "<script>" else f"bet {proto.name}"
        marker = " (entry)" if i == unit.entry_proto else ""
        sections.append(disassemble_proto(unit, proto, label + marker))
    return "\n\n".join(sections) + "\n"
