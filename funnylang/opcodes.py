"""The bytecode instruction set (PLAN.md §5.1 — FROZEN, never renumber)."""
from __future__ import annotations

from enum import IntEnum


class Op(IntEnum):
    NOP = 0
    CONST = 1
    GHOST = 2
    FAX = 3
    CAP = 4
    POP = 5
    DUP = 6
    SWAP = 7
    GET_LOCAL = 8
    SET_LOCAL = 9
    GET_GLOBAL = 10
    SET_GLOBAL = 11
    DEF_GLOBAL = 12
    GET_UPVAL = 13
    SET_UPVAL = 14
    CLOSE_UPVAL = 15
    GET_PROP = 16
    SET_PROP = 17
    GET_PROP_SAFE = 18
    GET_INDEX = 19
    SET_INDEX = 20
    GET_SLICE = 21
    ADD = 22
    SUB = 23
    MUL = 24
    DIV = 25
    IDIV = 26
    MOD = 27
    POW = 28
    NEG = 29
    NOT = 30
    BNOT = 31
    BAND = 32
    BOR = 33
    BXOR = 34
    SHL = 35
    SHR = 36
    EQ = 37
    NEQ = 38
    LT = 39
    LE = 40
    GT = 41
    GE = 42
    IN = 43
    JUMP = 44
    JUMP_IF_FALSE = 45
    JUMP_IF_TRUE = 46
    JUMP_IF_FALSE_KEEP = 47
    JUMP_IF_TRUE_KEEP = 48
    JUMP_IF_GHOST_KEEP = 49
    LOOP = 50
    CALL = 51
    INVOKE = 52
    INVOKE_OG = 53
    CLOSURE = 54
    RETURN = 55
    BUILD_STASH = 56
    BUILD_GROUPCHAT = 57
    BUILD_STRING = 58
    YAP = 59
    CHUCK = 60
    TRY_PUSH = 61
    TRY_POP = 62
    SQUAD = 63
    METHOD = 64
    INHERIT = 65
    IMPORT = 66
    EXPORT = 67
    ITER_NEW = 68
    ITER_NEXT = 69
    HALT = 70
    # Added in M11 (PLAN.md §16): wide-offset (u32) counterparts of JUMP/LOOP,
    # emitted only when a function body is so large a plain u16 offset can't
    # reach. Never emitted by ordinary programs.
    JUMP_LONG = 71
    LOOP_LONG = 72
    # Added in M15 (PLAN.md §3.10, §16): pointers. PTR_* forms a `pointa`
    # place-reference (never a raw address); DEREF/SET_DEREF read/write
    # through one. PTR_LOCAL/PTR_UPVAL's second operand is a redundant name
    # string (an AGENT CHOICE — see §16) since locals/upvalues are otherwise
    # purely positional and §3.10 promises `p.where()`/`to_yap(p)` a name.
    PTR_LOCAL = 73
    PTR_GLOBAL = 74
    PTR_UPVAL = 75
    PTR_INDEX = 76
    PTR_PROP = 77
    DEREF = 78
    SET_DEREF = 79


# Fixed operand byte-widths, in order. This is the single source of truth for
# both the disassembler and the VM's decoder — neither hardcodes widths.
# CLOSURE is the one variable-length exception (PLAN.md §5.1 note): its fixed
# prefix is one u16 (a const-pool index holding a `proto_ref`), followed by a
# `n * (u8 isLocal, u8 index)` tail whose length depends on that proto's
# upvalue_count — callers must special-case it (see disasm.py / vm.py).
OPERANDS: dict[Op, tuple[int, ...]] = {
    Op.NOP: (),
    Op.CONST: (2,),
    Op.GHOST: (),
    Op.FAX: (),
    Op.CAP: (),
    Op.POP: (),
    Op.DUP: (),
    Op.SWAP: (),
    Op.GET_LOCAL: (1,),
    Op.SET_LOCAL: (1,),
    Op.GET_GLOBAL: (2,),
    Op.SET_GLOBAL: (2,),
    Op.DEF_GLOBAL: (2,),
    Op.GET_UPVAL: (1,),
    Op.SET_UPVAL: (1,),
    Op.CLOSE_UPVAL: (),
    Op.GET_PROP: (2,),
    Op.SET_PROP: (2,),
    Op.GET_PROP_SAFE: (2,),
    Op.GET_INDEX: (),
    Op.SET_INDEX: (),
    Op.GET_SLICE: (),
    Op.ADD: (),
    Op.SUB: (),
    Op.MUL: (),
    Op.DIV: (),
    Op.IDIV: (),
    Op.MOD: (),
    Op.POW: (),
    Op.NEG: (),
    Op.NOT: (),
    Op.BNOT: (),
    Op.BAND: (),
    Op.BOR: (),
    Op.BXOR: (),
    Op.SHL: (),
    Op.SHR: (),
    Op.EQ: (),
    Op.NEQ: (),
    Op.LT: (),
    Op.LE: (),
    Op.GT: (),
    Op.GE: (),
    Op.IN: (),
    Op.JUMP: (2,),
    Op.JUMP_IF_FALSE: (2,),
    Op.JUMP_IF_TRUE: (2,),
    Op.JUMP_IF_FALSE_KEEP: (2,),
    Op.JUMP_IF_TRUE_KEEP: (2,),
    Op.JUMP_IF_GHOST_KEEP: (2,),
    Op.LOOP: (2,),
    Op.CALL: (1,),
    Op.INVOKE: (2, 1),
    Op.INVOKE_OG: (2, 1),
    Op.CLOSURE: None,  # special-cased (see module docstring)
    Op.RETURN: (),
    Op.BUILD_STASH: (2,),
    Op.BUILD_GROUPCHAT: (2,),
    Op.BUILD_STRING: (2,),
    Op.YAP: (1, 1),
    Op.CHUCK: (),
    Op.TRY_PUSH: (2, 2),
    Op.TRY_POP: (),
    Op.SQUAD: (2,),
    Op.METHOD: (2,),
    Op.INHERIT: (),
    Op.IMPORT: (2, 1),
    Op.EXPORT: (2,),
    Op.ITER_NEW: (),
    Op.ITER_NEXT: (2,),
    Op.HALT: (),
    Op.JUMP_LONG: (4,),
    Op.LOOP_LONG: (4,),
    Op.PTR_LOCAL: (1, 2),
    Op.PTR_GLOBAL: (2,),
    Op.PTR_UPVAL: (1, 2),
    Op.PTR_INDEX: (),
    Op.PTR_PROP: (2,),
    Op.DEREF: (),
    Op.SET_DEREF: (),
}
