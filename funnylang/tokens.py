"""TokenKind enum, the Token dataclass, and the KEYWORDS table (frozen — see PLAN.md §3.3)."""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import Any

from .source import Span


class TokenKind(Enum):
    # literals
    INT = auto()
    FLOAT = auto()
    STRING = auto()
    TEMPLATE = auto()
    IDENT = auto()

    # arithmetic / assignment operators
    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    BACKSLASH = auto()  # floor division; see PLAN.md §16
    PERCENT = auto()
    STAR_STAR = auto()

    PLUS_EQ = auto()
    MINUS_EQ = auto()
    STAR_EQ = auto()
    SLASH_EQ = auto()
    PERCENT_EQ = auto()
    STAR_STAR_EQ = auto()
    PIPE_PIPE_EQ = auto()

    EQ = auto()
    EQ_EQ = auto()
    BANG_EQ = auto()

    LT = auto()
    LE = auto()
    GT = auto()
    GE = auto()
    LT_LT = auto()
    GT_GT = auto()

    AMP = auto()
    PIPE = auto()
    CARET = auto()
    TILDE = auto()
    AMP_AMP = auto()
    PIPE_PIPE = auto()
    BANG = auto()

    QUESTION = auto()
    QUESTION_QUESTION = auto()
    QUESTION_DOT = auto()
    COLON = auto()
    PIPE_GT = auto()

    LPAREN = auto()
    RPAREN = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    LBRACE = auto()
    RBRACE = auto()

    COMMA = auto()
    DOT = auto()
    ELLIPSIS = auto()
    SEMICOLON = auto()
    ARROW = auto()

    NEWLINE = auto()
    EOF = auto()

    # keywords (PLAN.md §3.3 — frozen)
    YO = auto()
    DEADASS = auto()
    YAP = auto()
    YEET = auto()
    MUMBLE = auto()
    BET = auto()
    LOWKEY = auto()
    BOUNCE = auto()
    SUS = auto()
    KINDA_SUS = auto()
    NAH = auto()
    BRUH = auto()
    GRIND = auto()
    FROM = auto()
    TO = auto()
    STEP = auto()
    IN = auto()
    BAIL = auto()
    NVM = auto()
    SKETCHY = auto()
    MY_BAD = auto()
    REGARDLESS = auto()
    CHUCK = auto()
    GIMME = auto()
    AS = auto()
    FLEX = auto()
    SQUAD = auto()
    INHERITS = auto()
    ME = auto()
    OG = auto()
    SPAWN = auto()
    FAX = auto()
    CAP = auto()
    GHOST = auto()
    FR = auto()
    ORR = auto()
    AINT = auto()
    SAME_ENERGY = auto()
    DIFF_ENERGY = auto()
    VIBE = auto()

    # reserved for future use — lexed as keywords, rejected by the parser
    VIBIN = auto()
    ASYNC_NGL = auto()
    AWAIT_FR = auto()
    YIELD_LOL = auto()
    MATCH_THIS = auto()
    WHEN = auto()


KEYWORDS: dict[str, TokenKind] = {
    "yo": TokenKind.YO,
    "deadass": TokenKind.DEADASS,
    "yap": TokenKind.YAP,
    "yeet": TokenKind.YEET,
    "mumble": TokenKind.MUMBLE,
    "bet": TokenKind.BET,
    "lowkey": TokenKind.LOWKEY,
    "bounce": TokenKind.BOUNCE,
    "sus": TokenKind.SUS,
    "kinda_sus": TokenKind.KINDA_SUS,
    "nah": TokenKind.NAH,
    "bruh": TokenKind.BRUH,
    "grind": TokenKind.GRIND,
    "from": TokenKind.FROM,
    "to": TokenKind.TO,
    "step": TokenKind.STEP,
    "in": TokenKind.IN,
    "bail": TokenKind.BAIL,
    "nvm": TokenKind.NVM,
    "sketchy": TokenKind.SKETCHY,
    "my_bad": TokenKind.MY_BAD,
    "regardless": TokenKind.REGARDLESS,
    "chuck": TokenKind.CHUCK,
    "gimme": TokenKind.GIMME,
    "as": TokenKind.AS,
    "flex": TokenKind.FLEX,
    "squad": TokenKind.SQUAD,
    "inherits": TokenKind.INHERITS,
    "me": TokenKind.ME,
    "og": TokenKind.OG,
    "spawn": TokenKind.SPAWN,
    "fax": TokenKind.FAX,
    "cap": TokenKind.CAP,
    "ghost": TokenKind.GHOST,
    "fr": TokenKind.FR,
    "orr": TokenKind.ORR,
    "aint": TokenKind.AINT,
    "same_energy": TokenKind.SAME_ENERGY,
    "diff_energy": TokenKind.DIFF_ENERGY,
    "vibe": TokenKind.VIBE,
    "vibin": TokenKind.VIBIN,
    "async_ngl": TokenKind.ASYNC_NGL,
    "await_fr": TokenKind.AWAIT_FR,
    "yield_lol": TokenKind.YIELD_LOL,
    "match_this": TokenKind.MATCH_THIS,
    "when": TokenKind.WHEN,
}

# Reserved-for-future keywords: lexed fine, but the parser rejects them.
RESERVED_FUTURE: frozenset[str] = frozenset(
    {"vibin", "async_ngl", "await_fr", "yield_lol", "match_this", "when"}
)

# Maximal-munch operator tables, longest lexeme first.
OPERATORS_3: dict[str, TokenKind] = {
    "**=": TokenKind.STAR_STAR_EQ,
    "||=": TokenKind.PIPE_PIPE_EQ,
    "...": TokenKind.ELLIPSIS,
}

OPERATORS_2: dict[str, TokenKind] = {
    "+=": TokenKind.PLUS_EQ,
    "-=": TokenKind.MINUS_EQ,
    "*=": TokenKind.STAR_EQ,
    "/=": TokenKind.SLASH_EQ,
    "%=": TokenKind.PERCENT_EQ,
    "==": TokenKind.EQ_EQ,
    "!=": TokenKind.BANG_EQ,
    "<=": TokenKind.LE,
    ">=": TokenKind.GE,
    "<<": TokenKind.LT_LT,
    ">>": TokenKind.GT_GT,
    "&&": TokenKind.AMP_AMP,
    "||": TokenKind.PIPE_PIPE,
    "**": TokenKind.STAR_STAR,
    "??": TokenKind.QUESTION_QUESTION,
    "|>": TokenKind.PIPE_GT,
    "=>": TokenKind.ARROW,
    "?.": TokenKind.QUESTION_DOT,
}

OPERATORS_1: dict[str, TokenKind] = {
    "=": TokenKind.EQ,
    "+": TokenKind.PLUS,
    "-": TokenKind.MINUS,
    "*": TokenKind.STAR,
    "/": TokenKind.SLASH,
    "\\": TokenKind.BACKSLASH,
    "%": TokenKind.PERCENT,
    "<": TokenKind.LT,
    ">": TokenKind.GT,
    "!": TokenKind.BANG,
    "~": TokenKind.TILDE,
    "&": TokenKind.AMP,
    "|": TokenKind.PIPE,
    "^": TokenKind.CARET,
    "?": TokenKind.QUESTION,
    ":": TokenKind.COLON,
    ".": TokenKind.DOT,
    ",": TokenKind.COMMA,
    ";": TokenKind.SEMICOLON,
    "(": TokenKind.LPAREN,
    ")": TokenKind.RPAREN,
    "[": TokenKind.LBRACKET,
    "]": TokenKind.RBRACKET,
    "{": TokenKind.LBRACE,
    "}": TokenKind.RBRACE,
}


@dataclass
class Token:
    kind: TokenKind
    text: str
    span: Span
    value: Any = None

    def __repr__(self) -> str:  # pragma: no cover - debug convenience
        v = f" {self.value!r}" if self.value is not None else ""
        return f"Token({self.kind.name}, {self.text!r}{v}, {self.span.line}:{self.span.col})"
