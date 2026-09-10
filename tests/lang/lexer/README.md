# Lexer goldens

`.funny` files here are **not run**. Their `.expected` opens with
`!XRAY --tokens`, so `funny test` compares the token dump instead — the same
thing `tests/test_lexer.py` asserted on when it compared `Token.kind`,
`Token.value` and `Token.span`, except the golden checks all three at once and
checks the positions too, which the pytest version mostly did not.

Every one of these was diffed against `funnylang.lexer`'s own token stream
before it was committed (`build/n4/token_oracle.py`), so they carry the
reference implementation's authority rather than the native lexer's opinion of
itself. That check is only possible while the Python implementation still
exists, which is why N11 runs last.

## `crlf_newline.funny` is exempt from the LF rule

`.gitattributes` forces `*.funny` to LF, and this one file is listed as
`-text` so it keeps its CRLF line endings. They *are* the test: the lexer has
to fold `\r\n` into a single NEWLINE token, and a checkout that rewrote the
file would leave the test passing while testing nothing.
