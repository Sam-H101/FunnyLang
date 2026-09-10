# Slow goldens

`funny test tests/slow`, separately from `funny test tests/lang`.

These generate and compile programs big enough to be worth several seconds
each, so keeping them out of the main corpus keeps the ordinary run fast.
`tests/test_hardening.py` marked its equivalents `@pytest.mark.slow` for the
same reason.

CI runs both directories. Nothing here is optional.

## `jump_widening`

A jump whose offset does not fit in 16 bits. A `sus` body of 12,000
statements pushes `JUMP_IF_FALSE`'s operand past `0xFFFF`; the same goes for a
`bruh` body's backwards `LOOP` and for a `bail` out of one.

This did not compile at all until N11. `selfhost/compiler.funny` had no
relaxation pass — `chunk_patch_jump` chucked *"your function is too long. seek
help."* — so the shipped toolchain could not compile a function body over
64 KB of bytecode, while the reference could. The pass is now ported from
`funnylang/chunk.py`, and `build/n4/relax_diff.sh` and `relax_nested.sh` check
it emits **byte-identical bytecode** to the reference for five shapes,
including one where widening an early jump pushes a later one over the limit
and the fixed-point loop has to run more than once.

The programs are generated rather than checked in: a 12,000-line fixture is
not something anyone should have to read or diff, and the interesting property
is the size, not the contents. A widened jump that lands in the wrong place
shows up immediately as wrong output.
