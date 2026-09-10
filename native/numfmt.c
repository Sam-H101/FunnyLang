/* native/numfmt.c -- see numfmt.h for the AGENT CHOICE this makes (exact
 * bignum arithmetic + round-trip verification, not table-driven Ryu).
 *
 * The core idea, a well-established alternative to Dragon4/Ryu-style
 * algorithms: a double's *exact* value is a rational number (mantissa *
 * 2^exponent, computed exactly as a bignum fraction). For increasing
 * precisions P = 1, 2, 3, ..., round that exact value to P significant
 * decimal digits (round-half-to-even) and check whether parsing the
 * result back (via libc's strtod, itself correctly-rounded) reproduces
 * the identical bit pattern. The first P that round-trips is provably the
 * *shortest* one: the set of P-digit decimals that round-trip to a given
 * double is a contiguous range (round-to-nearest-double has contiguous
 * preimages), so if any of them round-trips, the closest one to the true
 * value -- which is exactly what round-half-to-even produces -- does too.
 * P=17 is guaranteed to round-trip for every double, so the loop always
 * terminates.
 */
#include "numfmt.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    memcpy(r, s, n);
    return r;
}

typedef struct {
    ObjBignum *num;
    ObjBignum *den;
} ExactFraction;

/* Decomposes a positive, finite, nonzero double into its exact value as
   num/den (den is always a power of 2, or 1). */
static ExactFraction double_to_fraction(double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    int biasedExp = (int)((bits >> 52) & 0x7FFu);
    uint64_t mantissaBits = bits & 0xFFFFFFFFFFFFFull;

    int64_t mantissa;
    int exp2;
    if (biasedExp == 0) {
        /* subnormal */
        mantissa = (int64_t)mantissaBits;
        exp2 = -1074;
    } else {
        mantissa = (int64_t)(mantissaBits | (1ull << 52));
        exp2 = biasedExp - 1075;
    }

    ExactFraction f;
    ObjBignum *mBig = bignum_from_int64(mantissa);
    ObjBignum *two = bignum_from_int64(2);
    if (exp2 >= 0) {
        ObjBignum *pw = bignum_pow(two, exp2);
        f.num = bignum_mul(mBig, pw);
        f.den = bignum_from_int64(1);
        bignum_free(pw);
        bignum_free(mBig);
    } else {
        ObjBignum *pw = bignum_pow(two, -exp2);
        f.num = mBig;
        f.den = pw;
    }
    bignum_free(two);
    return f;
}

/* compares num/den against 10^exp (num, den > 0, any sign of exp) */
static int compare_to_power_of_ten(const ObjBignum *num, const ObjBignum *den, int exp) {
    ObjBignum *ten = bignum_from_int64(10);
    ObjBignum *lhs, *rhs;
    if (exp >= 0) {
        ObjBignum *pw = bignum_pow(ten, exp);
        lhs = bignum_copy(num);
        rhs = bignum_mul(den, pw);
        bignum_free(pw);
    } else {
        ObjBignum *pw = bignum_pow(ten, -exp);
        lhs = bignum_mul(num, pw);
        rhs = bignum_copy(den);
        bignum_free(pw);
    }
    bignum_free(ten);
    int cmp = bignum_compare(lhs, rhs);
    bignum_free(lhs);
    bignum_free(rhs);
    return cmp;
}

static int bit_length(const ObjBignum *n) {
    if (n->count == 0) return 0;
    int bits = (n->count - 1) * 32;
    uint32_t top = n->limbs[n->count - 1];
    while (top) {
        bits++;
        top >>= 1;
    }
    return bits;
}

/* The decimal exponent E such that 10^E <= num/den < 10^(E+1). The
   starting guess comes from bit lengths, not bignum_to_double(num) /
   bignum_to_double(den): a subnormal double's denominator is up to 2^1074,
   which overflows a double to +Inf, sending the old log10-based guess to
   -infinity (an out-of-range double-to-int conversion -- undefined
   behavior -- that in practice produced a garbage huge-magnitude guess,
   which then took the correction loop below effectively forever to walk
   back to the real answer). Bit lengths never overflow like that. */
static int compute_dec_exp(const ObjBignum *num, const ObjBignum *den) {
    int bitDiff = bit_length(num) - bit_length(den);
    int guess = (int)floor((double)bitDiff * 0.30102999566398119521); /* log10(2) */
    while (compare_to_power_of_ten(num, den, guess) < 0) guess--;
    while (compare_to_power_of_ten(num, den, guess + 1) >= 0) guess++;
    return guess;
}

/* round(a/b), round-half-to-even; a, b > 0. */
static ObjBignum *round_div(const ObjBignum *a, const ObjBignum *b) {
    ObjBignum *q, *r;
    bignum_divmod_trunc(a, b, &q, &r); /* a,b >= 0, so trunc == floor */
    ObjBignum *twiceR = bignum_add(r, r);
    int cmp = bignum_compare(twiceR, b);
    bignum_free(twiceR);
    bool roundUp;
    if (cmp > 0) {
        roundUp = true;
    } else if (cmp < 0) {
        roundUp = false;
    } else {
        roundUp = (q->count > 0) && (q->limbs[0] & 1u); /* tie: round to even */
    }
    bignum_free(r);
    if (!roundUp) return q;
    ObjBignum *one = bignum_from_int64(1);
    ObjBignum *q2 = bignum_add(q, one);
    bignum_free(one);
    bignum_free(q);
    return q2;
}

/* Writes exactly `p` decimal digits (no sign, no point) of num/den, rounded
   to nearest (ties to even), into digitsOut[0..p-1] (+NUL at [p]).
   *decExpOut starts as the caller's best estimate of the decimal exponent
   and is corrected if rounding carried out of range (e.g. 9.99->10.0). */
static void round_to_p_digits(const ObjBignum *num, const ObjBignum *den, int decExp, int p,
                               char *digitsOut, int *decExpOut) {
    int k = p - 1 - decExp; /* scale so floor/round(value * 10^k) has ~p digits */
    ObjBignum *ten = bignum_from_int64(10);
    ObjBignum *scaledNum, *scaledDen;
    if (k >= 0) {
        ObjBignum *pw = bignum_pow(ten, k);
        scaledNum = bignum_mul(num, pw);
        scaledDen = bignum_copy(den);
        bignum_free(pw);
    } else {
        ObjBignum *pw = bignum_pow(ten, -k);
        scaledNum = bignum_copy(num);
        scaledDen = bignum_mul(den, pw);
        bignum_free(pw);
    }
    bignum_free(ten);

    ObjBignum *digits = round_div(scaledNum, scaledDen);
    bignum_free(scaledNum);
    bignum_free(scaledDen);

    char *s = bignum_to_decimal_string(digits);
    bignum_free(digits);
    int len = (int)strlen(s);
    if (len > p) {
        /* rounding carried out of range (e.g. 999 -> 1000): the extra
           digit(s) are trailing zeros: drop them, bump the exponent. */
        decExp += (len - p);
        len = p;
    }
    int pad = p - len; /* defensive: compute_dec_exp being exact should make this 0 */
    for (int i = 0; i < pad; i++) digitsOut[i] = '0';
    memcpy(digitsOut + pad, s, (size_t)len);
    digitsOut[p] = '\0';
    free(s);
    *decExpOut = decExp;
}

/* Python repr()'s fixed-vs-scientific threshold: fixed for a decimal
   exponent in [-4, 15], scientific otherwise (empirically confirmed
   against CPython -- see NATIVE_PLAN.md §9's numfmt entry). */
static char *format_digits(const char *digits, int p, int decExp, bool negative) {
    char buf[64];
    int pos = 0;
    if (negative) buf[pos++] = '-';

    if (decExp >= -4 && decExp <= 15) {
        if (decExp >= 0) {
            int intDigits = decExp + 1;
            if (p <= intDigits) {
                memcpy(buf + pos, digits, (size_t)p);
                pos += p;
                for (int i = p; i < intDigits; i++) buf[pos++] = '0';
                buf[pos++] = '.';
                buf[pos++] = '0';
            } else {
                memcpy(buf + pos, digits, (size_t)intDigits);
                pos += intDigits;
                buf[pos++] = '.';
                memcpy(buf + pos, digits + intDigits, (size_t)(p - intDigits));
                pos += (p - intDigits);
            }
        } else {
            buf[pos++] = '0';
            buf[pos++] = '.';
            for (int i = 0; i < (-decExp - 1); i++) buf[pos++] = '0';
            memcpy(buf + pos, digits, (size_t)p);
            pos += p;
        }
    } else {
        buf[pos++] = digits[0];
        if (p > 1) {
            buf[pos++] = '.';
            memcpy(buf + pos, digits + 1, (size_t)(p - 1));
            pos += (p - 1);
        }
        buf[pos++] = 'e';
        buf[pos++] = (decExp >= 0) ? '+' : '-';
        int absExp = decExp >= 0 ? decExp : -decExp;
        char expBuf[8];
        int expLen = snprintf(expBuf, sizeof expBuf, "%d", absExp);
        if (expLen < 2) buf[pos++] = '0';
        memcpy(buf + pos, expBuf, (size_t)expLen);
        pos += expLen;
    }
    buf[pos] = '\0';
    return dup_str(buf);
}

char *numfmt_repr(double v) {
    if (isnan(v)) return dup_str("nan");
    if (isinf(v)) return dup_str(v > 0 ? "inf" : "-inf");

    bool negative = signbit(v) != 0;
    double av = negative ? -v : v;
    if (av == 0.0) return dup_str(negative ? "-0.0" : "0.0");

    ExactFraction f = double_to_fraction(av);
    int decExp = compute_dec_exp(f.num, f.den);

    char *result = NULL;
    for (int p = 1; p <= 17; p++) {
        char digits[20];
        int candDecExp = decExp;
        round_to_p_digits(f.num, f.den, decExp, p, digits, &candDecExp);
        char *formatted = format_digits(digits, p, candDecExp, negative);
        double parsed = strtod(formatted, NULL);
        uint64_t parsedBits, vBits;
        memcpy(&parsedBits, &parsed, sizeof parsedBits);
        memcpy(&vBits, &v, sizeof vBits);
        if (parsedBits == vBits) {
            result = formatted;
            break;
        }
        free(formatted);
    }

    bignum_free(f.num);
    bignum_free(f.den);
    /* Unreachable in practice -- 17 significant digits always round-trips
       any IEEE-754 double -- but never return NULL from here regardless. */
    return result != NULL ? result : dup_str(negative ? "-0.0" : "0.0");
}
