"""Source file tracking: byte/char offsets, line/column mapping, and spans."""
from __future__ import annotations

import bisect
from dataclasses import dataclass


@dataclass(frozen=True)
class Span:
    """A half-open [start, end) range of character offsets into a SourceFile,
    plus the 1-indexed line/column of `start` for quick diagnostic rendering."""

    start: int
    end: int
    line: int
    col: int

    def merge(self, other: "Span") -> "Span":
        """The smallest span covering both self and other (self is assumed earlier)."""
        return Span(self.start, max(self.end, other.end), self.line, self.col)


class SourceFile:
    """A named chunk of FunnyLang source text with fast line/column lookups."""

    def __init__(self, path: str, text: str):
        self.path = path
        self.text = text
        self.line_starts: list[int] = self._compute_line_starts(text)

    @staticmethod
    def _compute_line_starts(text: str) -> list[int]:
        starts = [0]
        for i, ch in enumerate(text):
            if ch == "\n":
                starts.append(i + 1)
        return starts

    def line_col(self, offset: int) -> tuple[int, int]:
        """1-indexed (line, col) for a character offset into `text`."""
        offset = max(0, min(offset, len(self.text)))
        line_idx = bisect.bisect_right(self.line_starts, offset) - 1
        line = line_idx + 1
        col = offset - self.line_starts[line_idx] + 1
        return line, col

    def line_text(self, line: int) -> str:
        """The raw text of a 1-indexed line number, without the trailing newline."""
        idx = line - 1
        if idx < 0 or idx >= len(self.line_starts):
            return ""
        start = self.line_starts[idx]
        end = self.line_starts[idx + 1] - 1 if idx + 1 < len(self.line_starts) else len(self.text)
        return self.text[start:end].rstrip("\r").rstrip("\n")

    def num_lines(self) -> int:
        return len(self.line_starts)

    def make_span(self, start: int, end: int) -> Span:
        line, col = self.line_col(start)
        return Span(start, end, line, col)
