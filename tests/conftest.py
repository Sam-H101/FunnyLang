"""Shared pytest fixtures/helpers for the FunnyLang test suite."""
from __future__ import annotations

from funnylang.lexer import Lexer
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
