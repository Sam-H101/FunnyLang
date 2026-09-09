/* native/numfmt.h -- NATIVE_PLAN.md N1 task 6: float formatting that must
 * reproduce Python's repr(float) exactly (byte for byte), since that's
 * what PLAN.md's `to_display`/`_format_float` already delegates to for
 * every non-whole-number `numba`, and the differential-testing discipline
 * this whole plan runs on means the C runtime has to match it precisely,
 * not just "look about right."
 *
 * AGENT CHOICE (NATIVE_PLAN.md §9): this is *not* a hand-rolled Ryu
 * implementation, despite the plan's own text suggesting Ryu specifically.
 * What N1's acceptance criterion actually requires is exact behavioral
 * agreement with Python's repr() -- not any particular algorithm -- so
 * this instead reuses the freshly-built, extensively fuzz-tested bignum.c
 * to do exact rational arithmetic: represent the double as an exact
 * fraction, generate the correctly-rounded (round-half-to-even) decimal
 * digits at successively longer precisions, and stop at the first
 * precision whose formatted string round-trips (via strtod) back to the
 * identical bit pattern -- provably the shortest such representation
 * (see numfmt.c's own comment for why). Slower than table-driven Ryu, but
 * its correctness rests entirely on bignum.c's, which is already proven
 * out; revisit for performance post-2.0 if profiling ever says this
 * matters (formatting is not on any hot path PLAN.md's own perf targets,
 * §10, actually exercise).
 */
#ifndef FUNNY_NUMFMT_H
#define FUNNY_NUMFMT_H

/* Malloc'd, NUL-terminated, caller frees. Exactly matches CPython's
 * repr(float): "nan", "inf"/"-inf", "0.0"/"-0.0", fixed notation
 * ("100.0", "0.0001") for a decimal exponent in [-4, 15], scientific
 * ("1e+16", "1e-05") outside that range, with the shortest digit sequence
 * that round-trips back to the exact same double. */
char *numfmt_repr(double v);

#endif /* FUNNY_NUMFMT_H */
