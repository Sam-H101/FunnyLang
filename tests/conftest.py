"""Shared pytest fixtures/helpers for the FunnyLang test suite."""
from __future__ import annotations

from funnylang.ast_nodes import Program, dump_ast
from funnylang.lexer import Lexer
from funnylang.parser import parse_source
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
