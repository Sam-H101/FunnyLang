# Error and diagnostic goldens

Two kinds live here.

`!ERROR <Flavor>` asserts which error escapes, at compile time or at run time
— `funny test` catches both in the same place, so a parse error, a resolve
error and a runtime error are all written the same way.

`!DIAG` asserts the **rendered** diagnostic, byte for byte: the caret column,
the hint, the roast, whether there is a stack of shame. That is what a person
actually reads, and none of it is checked by the flavor alone.

## Known gap: `!DIAG` on a *compile-time* error names the wrong file

A runtime `!DIAG` golden works completely — see `caret_points_at_column`. A
compile-time one renders the right flavor, message and roast but attributes
them to `cli.funny`, the toolchain that was running the parser, instead of the
file being parsed.

The cause is upstream of the diagnostic: `selfhost/parser.funny` builds its
errors with `oops(flavor, message, line, col)`, and neither `oops` nor
`parse_source(src)` takes a source *path* — the parser is handed text and
nothing else. The position is correct; only the file is not. It surfaces here
because `sus.render_diag` renders the raw error object, where `funny run`
prints `couldn't compile <path>: <message>` and supplies the path itself.

Fixing it means threading a path through `parse_source` and adding a file
argument to `oops`, which also buys a caret line for parse errors that
`funny run` does not print today. Worth doing on its own; not smuggled into
the change that found it.

Until then the four address-of and assignment-target parse errors are pinned
by flavor here and by *message* only in `funnylang/parser.py`, which is being
deleted. The wording is copied verbatim into `selfhost/parser.funny` with a
comment saying so.
