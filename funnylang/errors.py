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
        payload: Any = None,
    ):
        super().__init__(message)
        self.message = message
        self.span = span
        self.source = source
        self.roast = roast
        self.hint = hint
        self.frames = frames or []
        # Doubles as PLAN.md §3.9's `error` runtime value once caught by
        # `my_bad` — `payload` is whatever was `chuck`ed (SkillIssue only).
        self.payload = payload

    def __str__(self) -> str:
        return f"{self.flavor}: {self.message}"


class LexerSaidNah(FunnyError):
    """Unrecognized character, or an unterminated string/comment/template."""

    flavor = "LexerSaidNah"


class ParserHadAStroke(FunnyError):
    """A syntax error."""

    flavor = "ParserHadAStroke"


class WhoDis(FunnyError):
    """Reference to an identifier that resolves to nothing: not a local, not
    an upvalue, not a known global/builtin/stdlib/import binding."""

    flavor = "WhoDis"


class ImmutableVibes(FunnyError):
    """Assignment to a `deadass` (const) binding."""

    flavor = "ImmutableVibes"


class TypeVibeMismatch(FunnyError):
    flavor = "TypeVibeMismatch"


class MathAintMathin(FunnyError):
    flavor = "MathAintMathin"


class OutOfPocket(FunnyError):
    flavor = "OutOfPocket"


class KeyGhosted(FunnyError):
    flavor = "KeyGhosted"


class GhostError(FunnyError):
    flavor = "GhostError"


class NotACallableRizz(FunnyError):
    flavor = "NotACallableRizz"


class WrongNumberOfHomies(FunnyError):
    flavor = "WrongNumberOfHomies"


class TooDeepBro(FunnyError):
    flavor = "TooDeepBro"


class SkillIssue(FunnyError):
    flavor = "SkillIssue"


def levenshtein(a: str, b: str) -> int:
    if a == b:
        return 0
    if not a:
        return len(b)
    if not b:
        return len(a)
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, start=1):
        cur = [i] + [0] * len(b)
        for j, cb in enumerate(b, start=1):
            cost = 0 if ca == cb else 1
            cur[j] = min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
        prev = cur
    return prev[-1]


def suggest_name(name: str, candidates, max_distance: int = 2) -> str | None:
    """The closest candidate to `name` within `max_distance` edits, or None."""
    best = None
    best_dist = max_distance + 1
    for cand in candidates:
        if cand == name:
            continue
        d = levenshtein(name, cand)
        if d < best_dist:
            best, best_dist = cand, d
    return best


class ParseErrorBundle(Exception):
    """Raised once parsing finishes with one or more ParserHadAStroke errors
    collected via error recovery. `errors` holds every one that was found;
    the diagnostic renderer (M6) shows the first 5 and summarizes the rest."""

    def __init__(self, errors: list[ParserHadAStroke]):
        self.errors = errors
        super().__init__(f"{len(errors)} syntax error(s)")
