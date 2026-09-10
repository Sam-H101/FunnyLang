# Error and diagnostic goldens

Two kinds live here.

`!ERROR <Flavor>` asserts which error escapes, at compile time or at run time
— `funny test` catches both in the same place, so a parse error, a resolve
error and a runtime error are all written the same way.

`!DIAG` asserts the **rendered** diagnostic, byte for byte: the caret column,
the hint, the roast, whether there is a stack of shame. That is what a person
actually reads, and none of it is checked by the flavor alone.

## These are compared against the reference, not captured

Every `!DIAG` golden here was checked against `python -m funnylang run` on the
same file (`build/n4/diagcmp.sh`) and is byte-identical to it. Three things
had to change before that was true, and all three were real gaps rather than
test plumbing:

- **The front end carried no source path.** `parse_source(src)` took text and
  nothing else, and `oops()` had no file argument, so a compile-time error
  named no file at all — the renderer attributed it to whatever unit was
  running (the toolchain) and dropped the caret line entirely, because it had
  no source to quote. A path now threads through the lexer, parser, resolver
  and bundler. Note the bundler keeps *two* names per module: the canonical
  key the loader needs, and a real path for diagnostics.
- **`oops()` could not set a roast or a hint.** In funny mode the rendered
  body is the *roast*, not the message, so every self-hosted compile error
  rendered with its flavor's generic default. `funnylang/parser.py` and
  `resolver.py` override the roast on exactly four errors between them; those
  four now match.
- **The self-hosted resolver had no "did you mean".** `suggest_name` and
  `levenshtein` simply did not exist on this side, so a `WhoDis` never
  suggested the name you probably meant. Ported, tie-break included — the
  *first* candidate at the best distance wins, so the order the reference
  walks scopes in is part of the behaviour.

## These goldens are anchored to the repository root

A `!DIAG` body contains the path the diagnostic printed, and that path is the
one `funny test` was given — so these pass under `funny test tests/lang` (and
`funny test .`) from the repository root, and would not under
`cd tests/lang && funny test .`, which would render `errors/x.funny` instead.
That is how CI, `docs/NATIVE.md` and the release checks all invoke it. Worth
knowing before moving one of these files.
