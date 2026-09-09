"""The FunnyError hierarchy (PLAN.md §4).

This module grows with the milestones: each phase adds the exception classes
it needs to raise. M6 finishes the job with `render_diagnostic`, a roast/hint
for every flavor, and `FUNNY_SERIOUS` support.
"""
from __future__ import annotations

from typing import Any

from .source import Span


class FunnyError(Exception):
    """Base of every FunnyLang diagnostic. `flavor` is the class-level error name
    shown to users (PLAN.md §4.1); `roast` is the comedic explanation, `hint` is
    an optional one-line fix suggestion, `span` locates the offending source."""

    flavor: str = "FunnyError"

    def __init__(
        self,
        message: str,
        *,
        span: Span | None = None,
        source: Any = None,
        roast: str | None = None,
        hint: str | None = None,
        frames: list[str] | None = None,
    ):
        super().__init__(message)
        self.message = message
        self.span = span
        self.source = source
        self.roast = roast
        self.hint = hint
        self.frames = frames or []

    def __str__(self) -> str:
        return f"{self.flavor}: {self.message}"


class LexerSaidNah(FunnyError):
    """Unrecognized character, or an unterminated string/comment/template."""

    flavor = "LexerSaidNah"


class ParserHadAStroke(FunnyError):
    """A syntax error."""

    flavor = "ParserHadAStroke"


class ParseErrorBundle(Exception):
    """Raised once parsing finishes with one or more ParserHadAStroke errors
    collected via error recovery. `errors` holds every one that was found;
    the diagnostic renderer (M6) shows the first 5 and summarizes the rest."""

    def __init__(self, errors: list[ParserHadAStroke]):
        self.errors = errors
        super().__init__(f"{len(errors)} syntax error(s)")
