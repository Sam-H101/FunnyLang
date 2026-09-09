"""The FunnyError hierarchy and diagnostic renderer (PLAN.md §4)."""
from __future__ import annotations

import os
import sys
from typing import Any

from .source import Span

# Sample roasts from PLAN.md §4.1's table — used whenever a raise site
# doesn't supply its own more specific one.
DEFAULT_ROASTS: dict[str, str] = {
    "LexerSaidNah": "what even IS that character. i'm not doing this.",
    "ParserHadAStroke": "i read this three times. it's still not code.",
    "WhoDis": "who? never heard of them.",
    "TypeVibeMismatch": "those two do NOT have the same energy.",
    "MathAintMathin": "you divided by zero. the universe said no.",
    "OutOfPocket": "that's straight up out of pocket.",
    "KeyGhosted": "that key left the group chat.",
    "GhostError": "you're talking to a ghost, king.",
    "NotACallableRizz": "that thing has no call rizz whatsoever.",
    "WrongNumberOfHomies": "wrong number of homies. awkward.",
    "TooDeepBro": "you recursed way too deep. touch grass.",
    "ImportSkillIssue": "can't find it. did you make it up?",
    "ImmutableVibes": "it's deadass. it doesn't change. like your ex's opinion of you.",
    "SkillIssue": "skill issue.",
    "ComputerExploded": "it's over. exit code 69.",
}


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
        self.roast = roast or DEFAULT_ROASTS.get(self.flavor, message)
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


class ImportSkillIssue(FunnyError):
    flavor = "ImportSkillIssue"


class ComputerExploded(FunnyError):
    """Raised by `computer.explode()` — exits 69, never a Python traceback."""

    flavor = "ComputerExploded"


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


# ---------------------------------------------------------------------------
# Diagnostic rendering (PLAN.md §4.2)
# ---------------------------------------------------------------------------

# Errors with no runtime call stack — the same renderer, minus "stack of
# shame" (PLAN.md §4.2: "Compile-time errors ... use the same renderer with
# no stack of shame").
_COMPILE_TIME_FLAVORS = frozenset({"LexerSaidNah", "ParserHadAStroke", "WhoDis", "ImmutableVibes"})

_RED = "\x1b[31m"
_BOLD = "\x1b[1m"
_DIM = "\x1b[2m"
_RESET = "\x1b[0m"


def _is_serious() -> bool:
    return os.environ.get("FUNNY_SERIOUS") == "1"


def _use_color(color: bool | None) -> bool:
    if color is not None:
        return color
    try:
        return sys.stdout.isatty()
    except Exception:
        return False


def _c(text: str, code: str, color: bool) -> str:
    return f"{code}{text}{_RESET}" if color else text


def _render_snippet(source, span: Span, color: bool) -> list[str]:
    line = span.line
    try:
        total = source.num_lines()
    except Exception:
        return []
    start_line = max(1, line - 2)
    end_line = min(total, line + 1)
    width = len(str(end_line))
    out = []
    for n in range(start_line, end_line + 1):
        text = source.line_text(n)
        out.append(f"     {str(n).rjust(width)} │ {text}")
        if n == line:
            caret_col = max(span.col - 1, 0)
            caret = " " * caret_col + _c("^ this right here", _RED, color)
            out.append(f"     {' ' * width} │ {caret}")
    return out


def render_diagnostic(err: FunnyError, *, color: bool | None = None) -> str:
    """Renders one FunnyError in the exact §4.2 shape."""
    serious = _is_serious()
    use_color = _use_color(color)
    lines: list[str] = []

    header = "FUNNYLANG ERROR" if serious else "💀💀💀 FUNNYLANG MOMENT 💀💀💀"
    lines.append(_c(header, _BOLD, use_color))
    lines.append("")

    loc = ""
    if err.span is not None:
        path = getattr(err.source, "path", None) or "<unknown>"
        loc = f"{path}:{err.span.line}:{err.span.col}"
    flavor_str = _c(err.flavor, _RED + _BOLD, use_color)
    lines.append(f"  {flavor_str}  ──  {loc}" if loc else f"  {flavor_str}")

    if err.span is not None and err.source is not None:
        lines.append("")
        lines.extend(_render_snippet(err.source, err.span, use_color))

    lines.append("")
    body = err.message if serious else (err.roast or err.message)
    for body_line in body.splitlines() or [""]:
        lines.append(f"  {body_line}")

    if err.hint:
        lines.append("")
        fix_label = "fix:" if serious else "💡 skill issue fix:"
        lines.append(f"  {fix_label}  {err.hint}")

    if err.frames and err.flavor not in _COMPILE_TIME_FLAVORS:
        lines.append("")
        lines.append("  stack trace:" if serious else "  🥞 stack of shame:")
        for frame_line in err.frames:
            lines.append(f"       {frame_line}")

    return "\n".join(lines) + "\n"


def render_parse_error_bundle(bundle: ParseErrorBundle, *, color: bool | None = None) -> str:
    """Renders up to 5 syntax errors, then a summary line for the rest
    (PLAN.md §4.2: "reports up to 5 syntax errors, then ... and {n} more.")."""
    shown = bundle.errors[:5]
    parts = [render_diagnostic(e, color=color) for e in shown]
    remaining = len(bundle.errors) - len(shown)
    if remaining > 0:
        parts.append(f"... and {remaining} more. i'll stop.\n")
    return "\n".join(parts)
