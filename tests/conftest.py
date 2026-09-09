"""Shared pytest fixtures/helpers for the FunnyLang test suite."""
from __future__ import annotations

from funnylang.ast_nodes import Program, dump_ast
from funnylang.chunk import CompiledUnit
from funnylang.compiler import Compiler
from funnylang.lexer import Lexer
from funnylang.parser import parse_source
from funnylang.resolver import resolve_program
from funnylang.source import SourceFile
from funnylang.tokens import Token, TokenKind


def pytest_sessionfinish(session, exitstatus):
    # An empty test suite (M0, before M1 lands) should not fail CI.
    if exitstatus == 5:  # NO_TESTS_COLLECTED
        session.exitstatus = 0


def make_source(src: str, path: str = "<test>") -> SourceFile:
    return SourceFile(path, src)


def lex(src: str, path: str = "<test>") -> list[Token]:
    """Tokenize `src` and return every token, EOF included."""
    return Lexer(make_source(src, path)).tokenize()


def lex_kinds(src: str) -> list[TokenKind]:
    """Tokenize `src` and return just the TokenKinds, EOF excluded — handy for
    compact assertions in tests."""
    return [t.kind for t in lex(src) if t.kind != TokenKind.EOF]


def parse_prog(src: str, path: str = "<test>") -> Program:
    return parse_source(make_source(src, path))


def dump_prog(src: str, path: str = "<test>") -> str:
    return dump_ast(parse_prog(src, path))


def parse_one(src: str):
    """Parse `src` as a program and return its single top-level statement's AST."""
    prog = parse_prog(src)
    assert len(prog.statements) == 1, f"expected exactly 1 statement, got {len(prog.statements)}"
    return prog.statements[0]


def resolve_prog(src: str, path: str = "<test>"):
    source = make_source(src, path)
    prog = parse_source(source)
    result = resolve_program(prog, source)
    return prog, result


def compile_prog(src: str, path: str = "<test>", fold_constants: bool = True) -> CompiledUnit:
    source = make_source(src, path)
    prog = parse_source(source)
    result = resolve_program(prog, source)
    return Compiler(result, source, fold_constants=fold_constants).compile_program(prog, path)


def entry_proto(unit: CompiledUnit):
    return unit.protos[unit.entry_proto]


def op_sequence(unit: CompiledUnit, proto=None):
    """The list of opcode names (no operands) in a proto's code, in order —
    for compact assertions in compiler tests."""
    from funnylang.opcodes import OPERANDS, Op

    proto = proto if proto is not None else entry_proto(unit)
    names = []
    code = proto.code
    ip = 0
    while ip < len(code):
        op = Op(code[ip])
        names.append(op.name)
        if op == Op.CLOSURE:
            const_idx = int.from_bytes(code[ip + 1:ip + 3], "big")
            tag, ref = unit.const_pool.entries[const_idx]
            n_upvals = unit.protos[ref].upvalue_count if tag == 5 else 0
            ip = ip + 3 + 2 * n_upvals
        else:
            ip += 1 + sum(OPERANDS[op])
    return names
