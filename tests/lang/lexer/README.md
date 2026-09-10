# Lexer goldens

`.funny` files here are **not run**. Their `.expected` opens with
`!XRAY --tokens`, so `funny test` compares the token dump instead — the same
thing `tests/test_lexer.py` asserted on when it compared `Token.kind`,
`Token.value` and `Token.span`, except the golden checks all three at once and
checks the positions too, which the pytest version mostly did not.

## Known gap: non-ASCII letters are not yet identifier characters

`yo 変数 = 1` lexes in the Python implementation and is rejected natively with
`LexerSaidNah`. It is not a lexer bug — `selfhost/lexer.funny` asks
`yapper.is_letter`, and the native `yapper.is_letter` is ASCII-only, a choice
logged in `native/yapper.h` and deferred from N4 because it needs Unicode
category tables that do not exist yet. `native/unicode_tbl.c` and its
generator are `NATIVE_PLAN.md` N11 task 3.

So `yapper.is_letter("中")` is `cap` natively and `fax` in Python, and every
non-ASCII identifier follows from that. Nothing in the 1,313-test pytest suite
notices, because that suite runs the *Python* VM; nothing in
`tests/native/programs/` notices either, because none of them classify a
non-ASCII character.

When the tables land, `tests/lang/lexer/identifiers_unicode.funny` and the
`yapper.is_letter` stdlib golden go in together and this section comes out.
