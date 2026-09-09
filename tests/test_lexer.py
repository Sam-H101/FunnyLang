from __future__ import annotations

import pytest

from conftest import lex, lex_kinds
from funnylang.errors import LexerSaidNah
from funnylang.tokens import TokenKind as TK


def test_empty_source_is_just_eof():
    toks = lex("")
    assert [t.kind for t in toks] == [TK.EOF]


def test_integers():
    toks = lex("42")
    assert toks[0].kind == TK.INT
    assert toks[0].value == 42


def test_negative_number_is_minus_then_int():
    # unary minus is a parser concern, not a lexer concern
    assert lex_kinds("-7") == [TK.MINUS, TK.INT]


def test_underscore_separated_int():
    toks = lex("1_000_000")
    assert toks[0].value == 1_000_000


def test_hex_literal():
    toks = lex("0xFF")
    assert toks[0].kind == TK.INT
    assert toks[0].value == 255


def test_binary_literal():
    toks = lex("0b1010")
    assert toks[0].value == 10


def test_octal_literal():
    toks = lex("0o755")
    assert toks[0].value == 493


def test_float_basic():
    toks = lex("3.14")
    assert toks[0].kind == TK.FLOAT
    assert toks[0].value == pytest.approx(3.14)


def test_float_exponent():
    toks = lex("1e9")
    assert toks[0].kind == TK.FLOAT
    assert toks[0].value == pytest.approx(1e9)


def test_float_negative_exponent():
    toks = lex("2.5e-3")
    assert toks[0].kind == TK.FLOAT
    assert toks[0].value == pytest.approx(2.5e-3)


def test_dot_not_followed_by_digit_is_separate_token():
    # `5.abs` -> INT, DOT, IDENT (not a malformed float)
    assert lex_kinds("5.abs") == [TK.INT, TK.DOT, TK.IDENT]


def test_double_quoted_string():
    toks = lex('"hi"')
    assert toks[0].kind == TK.STRING
    assert toks[0].value == "hi"


def test_single_quoted_string():
    toks = lex("'hi'")
    assert toks[0].value == "hi"


def test_triple_quoted_multiline_string():
    toks = lex('"""multi\nline"""')
    assert toks[0].kind == TK.STRING
    assert toks[0].value == "multi\nline"


def test_string_escapes():
    toks = lex(r'"\n\t\r\\\"\'\0"')
    assert toks[0].value == "\n\t\r\\\"'\0"


def test_string_unicode_escape():
    toks = lex(r'"\u{1F480}"')
    assert toks[0].value == "\U0001F480"


def test_line_comment_is_skipped():
    assert lex_kinds("yo x // this is a comment\n") == [TK.YO, TK.IDENT, TK.NEWLINE]


def test_nested_block_comment_is_skipped():
    src = "yo /* outer /* inner */ still outer */ x = 1"
    assert lex_kinds(src) == [TK.YO, TK.IDENT, TK.EQ, TK.INT]


def test_consecutive_newlines_collapse_to_one():
    assert lex_kinds("yo x\n\n\nyo y") == [TK.YO, TK.IDENT, TK.NEWLINE, TK.YO, TK.IDENT]


def test_crlf_newline_is_one_token():
    assert lex_kinds("yo x\r\nyo y") == [TK.YO, TK.IDENT, TK.NEWLINE, TK.YO, TK.IDENT]


def test_leading_newlines_are_dropped():
    assert lex_kinds("\n\nyo x") == [TK.YO, TK.IDENT]


def test_all_keywords_lex_as_keywords():
    from funnylang.tokens import KEYWORDS

    for word, kind in KEYWORDS.items():
        toks = lex(word)
        assert toks[0].kind == kind, f"{word} lexed as {toks[0].kind}, expected {kind}"


def test_identifier_vs_keyword():
    assert lex_kinds("yo yolk") == [TK.YO, TK.IDENT]


def test_emoji_identifier():
    toks = lex("yo 🚀 = 1")
    assert toks[1].kind == TK.IDENT
    assert toks[1].text == "🚀"


def test_cjk_identifier():
    toks = lex("yo 変数 = 1")
    assert toks[1].kind == TK.IDENT
    assert toks[1].text == "変数"


@pytest.mark.parametrize(
    "src,kind",
    [
        ("+", TK.PLUS),
        ("-", TK.MINUS),
        ("*", TK.STAR),
        ("/", TK.SLASH),
        ("\\", TK.BACKSLASH),
        ("%", TK.PERCENT),
        ("**", TK.STAR_STAR),
        ("+=", TK.PLUS_EQ),
        ("-=", TK.MINUS_EQ),
        ("*=", TK.STAR_EQ),
        ("/=", TK.SLASH_EQ),
        ("%=", TK.PERCENT_EQ),
        ("**=", TK.STAR_STAR_EQ),
        ("||=", TK.PIPE_PIPE_EQ),
        ("=", TK.EQ),
        ("==", TK.EQ_EQ),
        ("!=", TK.BANG_EQ),
        ("<", TK.LT),
        ("<=", TK.LE),
        (">", TK.GT),
        (">=", TK.GE),
        ("<<", TK.LT_LT),
        (">>", TK.GT_GT),
        ("&", TK.AMP),
        ("|", TK.PIPE),
        ("^", TK.CARET),
        ("~", TK.TILDE),
        ("&&", TK.AMP_AMP),
        ("||", TK.PIPE_PIPE),
        ("!", TK.BANG),
        ("?", TK.QUESTION),
        ("??", TK.QUESTION_QUESTION),
        ("?.", TK.QUESTION_DOT),
        (":", TK.COLON),
        ("|>", TK.PIPE_GT),
        ("(", TK.LPAREN),
        (")", TK.RPAREN),
        ("[", TK.LBRACKET),
        ("]", TK.RBRACKET),
        ("{", TK.LBRACE),
        ("}", TK.RBRACE),
        (",", TK.COMMA),
        (".", TK.DOT),
        ("...", TK.ELLIPSIS),
        (";", TK.SEMICOLON),
        ("=>", TK.ARROW),
    ],
)
def test_every_operator(src, kind):
    assert lex_kinds(src) == [kind]


def test_maximal_munch_star_star_eq_not_star_star_then_eq():
    assert lex_kinds("**=") == [TK.STAR_STAR_EQ]


def test_variadic_ellipsis_before_ident():
    assert lex_kinds("...rest") == [TK.ELLIPSIS, TK.IDENT]


def test_template_plain_text_only():
    toks = lex("`hello`")
    assert toks[0].kind == TK.TEMPLATE
    assert toks[0].value == [("str", "hello")]


def test_template_single_substitution():
    toks = lex("`yo {name}`")
    parts = toks[0].value
    assert parts[0] == ("str", "yo ")
    assert parts[1][0] == "tokens"
    sub = parts[1][1]
    assert sub[0].kind == TK.IDENT
    assert sub[0].text == "name"
    assert sub[-1].kind == TK.EOF


def test_template_multiple_substitutions():
    toks = lex("`{a} and {b}`")
    parts = toks[0].value
    kinds = [p[0] for p in parts]
    assert kinds == ["tokens", "str", "tokens"]
    assert parts[1] == ("str", " and ")


def test_template_nested_braces_in_expr():
    toks = lex("`{ {1: 2} }`")
    parts = toks[0].value
    sub = parts[0][1]
    sub_kinds = [t.kind for t in sub if t.kind != TK.EOF]
    assert sub_kinds == [TK.LBRACE, TK.INT, TK.COLON, TK.INT, TK.RBRACE]


def test_template_escaped_brace():
    toks = lex(r"`\{not a sub}`")
    assert toks[0].value == [("str", "{not a sub}")]


def test_template_escaped_backtick():
    toks = lex(r"`a\`b`")
    assert toks[0].value == [("str", "a`b")]


def test_spans_report_correct_line_and_col():
    toks = lex("yo x\nyo y")
    # YO IDENT NEWLINE YO IDENT EOF -- second `yo` starts on line 2, column 1
    second_yo = toks[3]
    assert second_yo.kind == TK.YO
    assert second_yo.span.line == 2
    assert second_yo.span.col == 1


def test_span_col_mid_line():
    toks = lex("yo x")
    ident = toks[1]
    assert ident.span.line == 1
    assert ident.span.col == 4  # 1-indexed: y=1 o=2 ' '=3 x=4


# -- error cases -------------------------------------------------------


def test_error_unrecognized_character():
    with pytest.raises(LexerSaidNah):
        lex("yo x = @")


def test_error_unterminated_string():
    with pytest.raises(LexerSaidNah):
        lex('"never closes')


def test_error_unterminated_block_comment():
    with pytest.raises(LexerSaidNah):
        lex("/* never closes")


def test_error_unterminated_template():
    with pytest.raises(LexerSaidNah):
        lex("`never closes")


def test_error_bad_unicode_escape():
    with pytest.raises(LexerSaidNah):
        lex(r'"\u{ZZZZ}"')


def test_error_string_with_raw_newline():
    with pytest.raises(LexerSaidNah):
        lex('"line1\nline2"')


def test_error_has_span():
    try:
        lex("yo x = @")
    except LexerSaidNah as exc:
        assert exc.span is not None
        assert exc.span.line == 1
    else:
        pytest.fail("expected LexerSaidNah")


def test_full_program_lexes_without_error():
    src = (
        "// hello.funny\n"
        'yo greeting = "yo sup world"\n'
        "yap greeting\n"
    )
    kinds = lex_kinds(src)
    assert kinds == [
        TK.YO, TK.IDENT, TK.EQ, TK.STRING, TK.NEWLINE,
        TK.YAP, TK.IDENT, TK.NEWLINE,
    ]
