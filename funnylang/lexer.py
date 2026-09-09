"""`.funny` source text -> a flat token stream (PLAN.md §M1)."""
from __future__ import annotations

from .errors import LexerSaidNah
from .source import SourceFile, Span
from .tokens import KEYWORDS, OPERATORS_1, OPERATORS_2, OPERATORS_3, Token, TokenKind

# Common emoji code-point ranges. Python's `str.isalpha()` already covers every
# non-emoji script (including CJK), so this only needs to plug the emoji gap —
# `regex`'s \p{Emoji} isn't available without a third-party dependency, which
# PLAN.md forbids for the core.
_EMOJI_RANGES: tuple[tuple[int, int], ...] = (
    (0x2600, 0x27BF),  # misc symbols, dingbats
    (0x2190, 0x21FF),  # arrows
    (0x2300, 0x23FF),  # misc technical
    (0x25A0, 0x25FF),  # geometric shapes
    (0x2B00, 0x2BFF),  # misc symbols and arrows
    (0x1F1E6, 0x1F1FF),  # regional indicators (flag letters)
    (0x1F300, 0x1F5FF),  # misc symbols and pictographs
    (0x1F600, 0x1F64F),  # emoticons
    (0x1F680, 0x1F6FF),  # transport and map
    (0x1F700, 0x1F77F),  # alchemical
    (0x1F900, 0x1F9FF),  # supplemental symbols and pictographs
    (0x1FA00, 0x1FA6F),  # chess symbols / symbols and pictographs extended-A
    (0x1FA70, 0x1FAFF),  # symbols and pictographs extended-A
    (0xFE0F, 0xFE0F),  # variation selector-16 (emoji presentation)
    (0x200D, 0x200D),  # zero-width joiner (emoji sequences)
)


def is_emoji_char(ch: str) -> bool:
    cp = ord(ch)
    return any(lo <= cp <= hi for lo, hi in _EMOJI_RANGES)


def _is_ident_start(ch: str) -> bool:
    return ch == "_" or ch.isalpha() or is_emoji_char(ch)


def _is_ident_continue(ch: str) -> bool:
    return ch == "_" or ch.isalnum() or is_emoji_char(ch)


_SIMPLE_ESCAPES = {
    "n": "\n",
    "t": "\t",
    "r": "\r",
    "\\": "\\",
    '"': '"',
    "'": "'",
    "0": "\0",
    "{": "{",
    "`": "`",  # AGENT CHOICE (PLAN.md §16): lets templates contain a literal backtick.
}

_HEX_DIGITS = set("0123456789abcdefABCDEF")
_DEC_DIGITS = set("0123456789")


class Lexer:
    """Scans one SourceFile into Tokens. Call `tokenize()` for the whole file."""

    def __init__(self, source: SourceFile):
        self.source = source
        self.text = source.text
        self.n = len(self.text)
        self.pos = 0

    # -- public API ---------------------------------------------------

    def tokenize(self) -> list[Token]:
        tokens: list[Token] = []
        while True:
            tok = self.next_token()
            if tok.kind == TokenKind.NEWLINE:
                if not tokens or tokens[-1].kind == TokenKind.NEWLINE:
                    continue
            tokens.append(tok)
            if tok.kind == TokenKind.EOF:
                break
        return tokens

    def next_token(self) -> Token:
        while True:
            tok = self._scan_one()
            if tok is not None:
                return tok

    # -- helpers --------------------------------------------------------

    def _peek(self, offset: int = 0) -> str:
        i = self.pos + offset
        return self.text[i] if 0 <= i < self.n else ""

    def _span(self, start: int) -> Span:
        return self.source.make_span(start, self.pos)

    def _token(self, kind: TokenKind, start: int, text: str | None = None, value=None) -> Token:
        span = self._span(start)
        return Token(kind, self.text[start:self.pos] if text is None else text, span, value)

    def _error(self, start: int, message: str, roast: str | None = None) -> LexerSaidNah:
        return LexerSaidNah(
            message,
            span=self._span(start),
            source=self.source,
            roast=roast or "what even IS that character. i'm not doing this.",
        )

    # -- top-level dispatch ----------------------------------------------

    def _scan_one(self) -> Token | None:
        if self.pos >= self.n:
            return self._token(TokenKind.EOF, self.pos, "")

        ch = self.text[self.pos]

        if ch == "\r":
            start = self.pos
            self.pos += 1
            if self._peek() == "\n":
                self.pos += 1
            return self._token(TokenKind.NEWLINE, start, "\n")
        if ch == "\n":
            start = self.pos
            self.pos += 1
            return self._token(TokenKind.NEWLINE, start, "\n")

        if ch in " \t":
            self.pos += 1
            return None

        if ch == "/" and self._peek(1) == "/":
            self._skip_line_comment()
            return None
        if ch == "/" and self._peek(1) == "*":
            self._skip_block_comment()
            return None

        if ch in _DEC_DIGITS:
            return self._scan_number()

        if ch in ("'", '"'):
            return self._scan_string(ch)

        if ch == "`":
            return self._scan_template()

        if _is_ident_start(ch):
            return self._scan_ident()

        return self._scan_operator()

    def _skip_line_comment(self) -> None:
        while self.pos < self.n and self.text[self.pos] not in "\r\n":
            self.pos += 1

    def _skip_block_comment(self) -> None:
        start = self.pos
        self.pos += 2  # consume '/*'
        depth = 1
        while depth > 0:
            if self.pos >= self.n:
                raise self._error(
                    start,
                    "you opened a /* comment and never closed it.",
                    "an unclosed /* comment. i'm still waiting.",
                )
            if self.text[self.pos] == "/" and self._peek(1) == "*":
                depth += 1
                self.pos += 2
            elif self.text[self.pos] == "*" and self._peek(1) == "/":
                depth -= 1
                self.pos += 2
            else:
                self.pos += 1

    # -- numbers ----------------------------------------------------------

    def _scan_number(self) -> Token:
        start = self.pos
        if self.text[self.pos] == "0" and self._peek(1) in ("x", "X"):
            return self._scan_radix_int(start, 2, _HEX_DIGITS, 16, "hex")
        if self.text[self.pos] == "0" and self._peek(1) in ("b", "B"):
            return self._scan_radix_int(start, 2, set("01"), 2, "binary")
        if self.text[self.pos] == "0" and self._peek(1) in ("o", "O"):
            return self._scan_radix_int(start, 2, set("01234567"), 8, "octal")
        return self._scan_decimal(start)

    def _scan_radix_int(self, start: int, prefix_len: int, digits: set, base: int, name: str) -> Token:
        self.pos += prefix_len
        digits_start = self.pos
        while self.pos < self.n and (self.text[self.pos] in digits or self.text[self.pos] == "_"):
            self.pos += 1
        raw_digits = self.text[digits_start:self.pos].replace("_", "")
        if not raw_digits:
            raise self._error(start, f"that's a '{self.text[start:self.pos]}' with no {name} digits after it.")
        value = int(raw_digits, base)
        return self._token(TokenKind.INT, start, value=value)

    def _scan_decimal(self, start: int) -> Token:
        is_float = False
        while self.pos < self.n and (self.text[self.pos] in _DEC_DIGITS or self.text[self.pos] == "_"):
            self.pos += 1
        if self.text[self.pos:self.pos + 1] == "." and self._peek(1) in _DEC_DIGITS:
            is_float = True
            self.pos += 1
            while self.pos < self.n and (self.text[self.pos] in _DEC_DIGITS or self.text[self.pos] == "_"):
                self.pos += 1
        if self.text[self.pos:self.pos + 1] in ("e", "E"):
            exp_len = 2 if self._peek(1) in ("+", "-") else 1
            if self._peek(exp_len) in _DEC_DIGITS:
                is_float = True
                self.pos += exp_len
                while self.pos < self.n and self.text[self.pos] in _DEC_DIGITS:
                    self.pos += 1
        raw = self.text[start:self.pos]
        cleaned = raw.replace("_", "")
        try:
            value = float(cleaned) if is_float else int(cleaned)
        except ValueError as exc:
            raise self._error(start, f"'{raw}' is not a number i understand.") from exc
        return self._token(TokenKind.FLOAT if is_float else TokenKind.INT, start, value=value)

    # -- strings ------------------------------------------------------------

    def _scan_escape(self) -> str:
        bs_pos = self.pos
        self.pos += 1  # consume backslash
        if self.pos >= self.n:
            raise self._error(bs_pos, "this string ends mid-escape. finish your sentence.")
        ch = self.text[self.pos]
        if ch in _SIMPLE_ESCAPES:
            self.pos += 1
            return _SIMPLE_ESCAPES[ch]
        if ch == "u":
            self.pos += 1
            if self._peek() != "{":
                raise self._error(bs_pos, r"\u escapes need braces, like \u{1F480}.")
            self.pos += 1
            hex_start = self.pos
            while self.pos < self.n and self.text[self.pos] != "}":
                self.pos += 1
            if self.pos >= self.n:
                raise self._error(bs_pos, r"that \u{...} escape never closes its brace.")
            hex_digits = self.text[hex_start:self.pos]
            self.pos += 1  # consume '}'
            if not hex_digits or any(c not in _HEX_DIGITS for c in hex_digits):
                raise self._error(bs_pos, f"'{hex_digits}' isn't a hex codepoint.")
            try:
                return chr(int(hex_digits, 16))
            except (ValueError, OverflowError) as exc:
                raise self._error(bs_pos, f"U+{hex_digits} isn't a real codepoint.") from exc
        raise self._error(
            bs_pos,
            f"'\\{ch}' isn't an escape sequence i recognize.",
            f"'\\{ch}' isn't a real escape. i'm not doing this.",
        )

    def _scan_string(self, quote: str) -> Token:
        start = self.pos
        if self.text[self.pos:self.pos + 3] == quote * 3:
            return self._scan_triple_string(start, quote)
        self.pos += 1
        chars: list[str] = []
        while True:
            if self.pos >= self.n:
                raise self._error(
                    start,
                    "this string never closes.",
                    "you opened a string and just... left it open. rude.",
                )
            ch = self.text[self.pos]
            if ch == quote:
                self.pos += 1
                break
            if ch in "\r\n":
                raise self._error(
                    start,
                    "this string hits a newline before it closes.",
                    "strings don't just end whenever they feel like it. use \\n or a triple-quote.",
                )
            if ch == "\\":
                chars.append(self._scan_escape())
                continue
            chars.append(ch)
            self.pos += 1
        return self._token(TokenKind.STRING, start, value="".join(chars))

    def _scan_triple_string(self, start: int, quote: str) -> Token:
        self.pos += 3
        chars: list[str] = []
        while True:
            if self.pos >= self.n:
                raise self._error(
                    start,
                    "this triple-quoted string never closes.",
                    "you opened a \"\"\" and never gave it a matching \"\"\". i'm stuck in here forever.",
                )
            if self.text[self.pos:self.pos + 3] == quote * 3:
                self.pos += 3
                break
            ch = self.text[self.pos]
            if ch == "\\":
                chars.append(self._scan_escape())
                continue
            chars.append(ch)
            self.pos += 1
        return self._token(TokenKind.STRING, start, value="".join(chars))

    # -- templates --------------------------------------------------------

    def _scan_template(self) -> Token:
        start = self.pos
        self.pos += 1  # consume opening backtick
        parts: list[tuple] = []
        literal: list[str] = []
        while True:
            if self.pos >= self.n:
                raise self._error(
                    start,
                    "this template string never closes.",
                    "you opened a ` template and never closed it. it's still vibing, unterminated.",
                )
            ch = self.text[self.pos]
            if ch == "`":
                self.pos += 1
                break
            if ch == "\\":
                literal.append(self._scan_escape())
                continue
            if ch == "{":
                if literal:
                    parts.append(("str", "".join(literal)))
                    literal = []
                self.pos += 1  # consume '{'
                parts.append(("tokens", self._consume_template_expr(start)))
                continue
            literal.append(ch)
            self.pos += 1
        if literal:
            parts.append(("str", "".join(literal)))
        return self._token(TokenKind.TEMPLATE, start, value=parts)

    def _consume_template_expr(self, template_start: int) -> list[Token]:
        result: list[Token] = []
        depth = 1
        last: Token | None = None
        while True:
            tok = self.next_token()
            if tok.kind == TokenKind.EOF:
                raise self._error(
                    template_start,
                    "a `{...}` substitution in this template never closes.",
                    "that { in your template string is still waiting on its }.",
                )
            if tok.kind == TokenKind.LBRACE:
                depth += 1
            elif tok.kind == TokenKind.RBRACE:
                depth -= 1
                if depth == 0:
                    last = tok
                    break
            if tok.kind == TokenKind.NEWLINE and (not result or result[-1].kind == TokenKind.NEWLINE):
                continue
            result.append(tok)
            last = tok
        end_span = last.span if last is not None else self._span(self.pos)
        result.append(Token(TokenKind.EOF, "", end_span, None))
        return result

    # -- identifiers / keywords --------------------------------------------

    def _scan_ident(self) -> Token:
        start = self.pos
        self.pos += 1
        while self.pos < self.n and _is_ident_continue(self.text[self.pos]):
            self.pos += 1
        text = self.text[start:self.pos]
        kind = KEYWORDS.get(text, TokenKind.IDENT)
        value = text if kind == TokenKind.IDENT else None
        return self._token(kind, start, text=text, value=value)

    # -- operators ----------------------------------------------------------

    def _scan_operator(self) -> Token:
        start = self.pos
        three = self.text[self.pos:self.pos + 3]
        if three in OPERATORS_3:
            self.pos += 3
            return self._token(OPERATORS_3[three], start, text=three)
        two = self.text[self.pos:self.pos + 2]
        if two in OPERATORS_2:
            self.pos += 2
            return self._token(OPERATORS_2[two], start, text=two)
        one = self.text[self.pos:self.pos + 1]
        if one in OPERATORS_1:
            self.pos += 1
            return self._token(OPERATORS_1[one], start, text=one)
        self.pos += 1
        raise self._error(start, f"i don't know what '{one}' is supposed to mean here.")


def lex(source: SourceFile) -> list[Token]:
    return Lexer(source).tokenize()
