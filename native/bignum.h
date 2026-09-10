/* native/bignum.h -- NATIVE_PLAN.md N1 task 5: arbitrary-precision integers.
 *
 * Sign-magnitude, base 2^32, little-endian limbs (limbs[0] is least
 * significant). This is the heap representation `numba` promotes to when a
 * fixnum (int64_t) operation would overflow -- PLAN.md's `numba` has no
 * size limit, and the C runtime must reproduce that exactly, including
 * every operator's behavior on negative numbers (Python's `int` semantics:
 * floor division/modulo, arithmetic-shift-style `>>` on negatives, and
 * two's-complement-*meaning* bitwise ops despite the sign-magnitude
 * storage).
 *
 * Deliberately GC-agnostic: a Bignum is malloc'd/freed on its own (see
 * bignum_new/bignum_free) so this file's correctness is testable in
 * isolation, exactly as NATIVE_PLAN.md §5's testing strategy wants
 * ("C unit tests for the components with no Python counterpart worth
 * diffing"). object.c's `Obj` header is embedded so that once a real GC
 * heap exists (gc.c), a Bignum already looks like every other object to
 * it -- no separate object kind needs bolting on later.
 */
#ifndef FUNNY_BIGNUM_H
#define FUNNY_BIGNUM_H

#include <stdbool.h>
#include <stdint.h>

#include "object.h"

typedef struct {
    Obj obj;
    int sign;         /* -1, 0, or +1. 0 means the value is exactly zero,
                          and then `count` is always 0 -- there is exactly
                          one representation of zero, never "-0". */
    uint32_t *limbs;   /* little-endian base-2^32 digits, no leading
                          (most-significant) zero limbs. */
    int count;         /* limbs in use */
    int capacity;      /* limbs allocated */
} ObjBignum;

/* -- construction / destruction ------------------------------------------ */

ObjBignum *bignum_new(int capacity);
void bignum_free(ObjBignum *n);
ObjBignum *bignum_from_int64(int64_t v);
ObjBignum *bignum_copy(const ObjBignum *a);

/* Parses a base-10/16/2/8 string (optional leading '-', for base 10 only --
 * PLAN.md's other bases are always unsigned literals) into a fresh Bignum.
 * `len` is the digit-substring length (no base prefix, no sign). Returns
 * NULL on an empty or invalid digit string. */
ObjBignum *bignum_from_digits(const char *digits, int len, int base, bool negative);

/* Decimal string, malloc'd, NUL-terminated, caller frees. Always the
 * shortest exact representation ("0" for zero, a leading '-' for negative,
 * never a leading zero digit otherwise) -- this is what PLAN.md's `to_yap`
 * on a numba ultimately bottoms out to for anything too big to be a
 * fixnum. */
char *bignum_to_decimal_string(const ObjBignum *n);

/* -- predicates / comparison ---------------------------------------------- */

bool bignum_is_zero(const ObjBignum *n);
/* -1 if a<b, 0 if a==b, +1 if a>b -- signed comparison. */
int bignum_compare(const ObjBignum *a, const ObjBignum *b);
/* Exact int64 round-trip: true and *out set if `n` fits, false otherwise
   (the fixnum/bignum boundary the VM's Value layer promotes/demotes at). */
bool bignum_to_int64(const ObjBignum *n, int64_t *out);
double bignum_to_double(const ObjBignum *n);

/* -- arithmetic (all return a freshly allocated result) ------------------- */

ObjBignum *bignum_negate(const ObjBignum *a);
ObjBignum *bignum_abs(const ObjBignum *a);
ObjBignum *bignum_add(const ObjBignum *a, const ObjBignum *b);
ObjBignum *bignum_sub(const ObjBignum *a, const ObjBignum *b);
ObjBignum *bignum_mul(const ObjBignum *a, const ObjBignum *b);

/* Floor division + matching modulo, exactly like Python's `//`/`%` (which
 * is what PLAN.md's `\ %` are specified to be -- see PLAN.md §16's floor-
 * division note): the remainder always has the same sign as `b` (or is
 * zero), never the sign of `a`. `*q_out`/`*r_out` are freshly allocated.
 * Returns false (no allocation performed) if `b` is zero -- division by
 * zero is the VM/opcode layer's `MathAintMathin` to raise, not this file's
 * problem to solve. */
bool bignum_divmod_floor(const ObjBignum *a, const ObjBignum *b, ObjBignum **q_out, ObjBignum **r_out);

/* Truncating division + matching modulo (sign of `a`), i.e. C/hardware
 * semantics -- exposed because floor-division is built on top of it. */
bool bignum_divmod_trunc(const ObjBignum *a, const ObjBignum *b, ObjBignum **q_out, ObjBignum **r_out);

/* Non-negative integer exponent only (`exp < 0` is a caller error -- Python
 * `int ** negative_int` produces a *float*, which is a Value-layer
 * decision, not this file's). Repeated squaring. */
ObjBignum *bignum_pow(const ObjBignum *base, int64_t exp);

/* Two's-complement-*meaning* bitwise ops on sign-magnitude storage, exactly
 * matching Python's arbitrary-precision `& | ^ ~` on negative operands
 * (conceptually as if each operand were sign-extended infinitely to the
 * left). */
ObjBignum *bignum_band(const ObjBignum *a, const ObjBignum *b);
ObjBignum *bignum_bor(const ObjBignum *a, const ObjBignum *b);
ObjBignum *bignum_bxor(const ObjBignum *a, const ObjBignum *b);
ObjBignum *bignum_bnot(const ObjBignum *a);

/* `shift >= 0` required (a negative shift is the caller's `MathAintMathin`
 * to raise, matching PLAN.md's `<<`/`>>` semantics -- see funnylang/vm.py's
 * `_bitwise`). `bignum_shr` is an arithmetic (floor) shift: shifting a
 * negative number right rounds toward negative infinity, same as Python. */
ObjBignum *bignum_shl(const ObjBignum *a, int64_t shift);
ObjBignum *bignum_shr(const ObjBignum *a, int64_t shift);

#endif /* FUNNY_BIGNUM_H */
