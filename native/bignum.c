/* native/bignum.c -- see bignum.h. Sign-magnitude, base 2^32 limbs.
 *
 * The one genuinely tricky algorithm here is multi-limb division
 * (mag_divmod_knuth), which implements Knuth's Algorithm D (TAOCP Vol 2,
 * 4.3.1), adapted for 32-bit limbs with 64-bit intermediate arithmetic
 * instead of Knuth's original 16-bit-oriented presentation. Single-limb
 * divisors go through the much simpler mag_divmod_small instead, since
 * Algorithm D's own normalization step assumes a divisor of at least two
 * limbs.
 */
#include "bignum.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -- construction / destruction ------------------------------------------ */

ObjBignum *bignum_new(int capacity) {
    ObjBignum *n = (ObjBignum *)malloc(sizeof(ObjBignum));
    if (!n) return NULL;
    n->obj.type = OBJ_BIGNUM;
    n->obj.marked = false;
    n->obj.size = 0; /* meaningful only once gc_track() tracks this object */
    n->obj.next = NULL;
    n->sign = 0;
    n->count = 0;
    n->capacity = capacity > 0 ? capacity : 1;
    n->limbs = (uint32_t *)calloc((size_t)n->capacity, sizeof(uint32_t));
    if (!n->limbs) {
        free(n);
        return NULL;
    }
    return n;
}

void bignum_free(ObjBignum *n) {
    if (!n) return;
    free(n->limbs);
    free(n);
}

static void trim(ObjBignum *n) {
    while (n->count > 0 && n->limbs[n->count - 1] == 0) {
        n->count--;
    }
    if (n->count == 0) {
        n->sign = 0;
    }
}

static bool ensure_capacity(ObjBignum *n, int needed) {
    if (needed <= n->capacity) return true;
    int newCap = n->capacity * 2;
    if (newCap < needed) newCap = needed;
    uint32_t *newLimbs = (uint32_t *)realloc(n->limbs, (size_t)newCap * sizeof(uint32_t));
    if (!newLimbs) return false;
    memset(newLimbs + n->capacity, 0, (size_t)(newCap - n->capacity) * sizeof(uint32_t));
    n->limbs = newLimbs;
    n->capacity = newCap;
    return true;
}

ObjBignum *bignum_copy(const ObjBignum *a) {
    ObjBignum *r = bignum_new(a->count > 0 ? a->count : 1);
    if (!r) return NULL;
    r->sign = a->sign;
    r->count = a->count;
    if (a->count > 0) memcpy(r->limbs, a->limbs, (size_t)a->count * sizeof(uint32_t));
    return r;
}

ObjBignum *bignum_from_int64(int64_t v) {
    ObjBignum *r = bignum_new(2);
    if (!r) return NULL;
    if (v == 0) return r;
    /* Safe magnitude-of-negative trick that avoids UB on INT64_MIN, whose
       positive counterpart doesn't fit in int64_t. */
    uint64_t mag = (v < 0) ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    r->sign = (v < 0) ? -1 : 1;
    r->limbs[0] = (uint32_t)(mag & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(mag >> 32);
    r->count = hi ? 2 : 1;
    r->limbs[1] = hi;
    return r;
}

/* -- predicates / comparison ---------------------------------------------- */

bool bignum_is_zero(const ObjBignum *n) {
    return n->sign == 0;
}

static int mag_compare(const uint32_t *a, int an, const uint32_t *b, int bn) {
    if (an != bn) return an < bn ? -1 : 1;
    for (int i = an - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

int bignum_compare(const ObjBignum *a, const ObjBignum *b) {
    if (a->sign != b->sign) return a->sign < b->sign ? -1 : (a->sign > b->sign ? 1 : 0);
    if (a->sign == 0) return 0;
    int cmp = mag_compare(a->limbs, a->count, b->limbs, b->count);
    return a->sign > 0 ? cmp : -cmp;
}

bool bignum_to_int64(const ObjBignum *n, int64_t *out) {
    if (n->sign == 0) {
        *out = 0;
        return true;
    }
    if (n->count > 2) return false;
    uint64_t mag = n->limbs[0];
    if (n->count == 2) mag |= ((uint64_t)n->limbs[1] << 32);
    if (n->sign > 0) {
        if (mag > (uint64_t)INT64_MAX) return false;
        *out = (int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX + 1u) return false;
        *out = (mag == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)mag;
    }
    return true;
}

double bignum_to_double(const ObjBignum *n) {
    double result = 0.0;
    for (int i = n->count - 1; i >= 0; i--) {
        result = result * 4294967296.0 + (double)n->limbs[i];
    }
    return n->sign < 0 ? -result : result;
}

/* -- magnitude-level arithmetic (sign handled by callers) ----------------- */

static ObjBignum *mag_add(const uint32_t *a, int an, const uint32_t *b, int bn) {
    if (an < bn) {
        const uint32_t *t = a; a = b; b = t;
        int tn = an; an = bn; bn = tn;
    }
    ObjBignum *r = bignum_new(an + 1);
    uint64_t carry = 0;
    int i = 0;
    for (; i < bn; i++) {
        uint64_t sum = (uint64_t)a[i] + b[i] + carry;
        r->limbs[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    for (; i < an; i++) {
        uint64_t sum = (uint64_t)a[i] + carry;
        r->limbs[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    if (carry) r->limbs[i++] = (uint32_t)carry;
    r->count = i;
    trim(r);
    return r;
}

/* requires mag(a) >= mag(b) */
static ObjBignum *mag_sub(const uint32_t *a, int an, const uint32_t *b, int bn) {
    ObjBignum *r = bignum_new(an > 0 ? an : 1);
    int64_t borrow = 0;
    int i = 0;
    for (; i < bn; i++) {
        int64_t diff = (int64_t)a[i] - (int64_t)b[i] - borrow;
        if (diff < 0) { diff += ((int64_t)1 << 32); borrow = 1; } else { borrow = 0; }
        r->limbs[i] = (uint32_t)diff;
    }
    for (; i < an; i++) {
        int64_t diff = (int64_t)a[i] - borrow;
        if (diff < 0) { diff += ((int64_t)1 << 32); borrow = 1; } else { borrow = 0; }
        r->limbs[i] = (uint32_t)diff;
    }
    r->count = an;
    trim(r);
    return r;
}

static ObjBignum *mag_mul(const uint32_t *a, int an, const uint32_t *b, int bn) {
    if (an == 0 || bn == 0) return bignum_new(1);
    ObjBignum *r = bignum_new(an + bn);
    r->count = an + bn; /* limbs already zeroed by bignum_new's calloc */
    for (int i = 0; i < an; i++) {
        if (a[i] == 0) continue;
        uint64_t carry = 0;
        for (int j = 0; j < bn; j++) {
            uint64_t prod = (uint64_t)a[i] * b[j] + r->limbs[i + j] + carry;
            r->limbs[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        int k = i + bn;
        while (carry) {
            uint64_t sum = (uint64_t)r->limbs[k] + carry;
            r->limbs[k] = (uint32_t)sum;
            carry = sum >> 32;
            k++;
        }
    }
    trim(r);
    return r;
}

/* -- public arithmetic ----------------------------------------------------- */

ObjBignum *bignum_negate(const ObjBignum *a) {
    ObjBignum *r = bignum_copy(a);
    r->sign = -r->sign;
    return r;
}

ObjBignum *bignum_abs(const ObjBignum *a) {
    ObjBignum *r = bignum_copy(a);
    if (r->sign < 0) r->sign = 1;
    return r;
}

ObjBignum *bignum_add(const ObjBignum *a, const ObjBignum *b) {
    if (a->sign == 0) return bignum_copy(b);
    if (b->sign == 0) return bignum_copy(a);
    if (a->sign == b->sign) {
        ObjBignum *r = mag_add(a->limbs, a->count, b->limbs, b->count);
        /* NOT bignum_is_zero(r): freshly out of mag_add, r->sign is still
           the bignum_new default of 0 regardless of whether the magnitude
           is actually zero -- that's exactly the question being asked
           here, so it has to be answered from r->count (trim()'s own
           source of truth), never from r->sign before this line sets it. */
        r->sign = (r->count == 0) ? 0 : a->sign;
        return r;
    }
    int cmp = mag_compare(a->limbs, a->count, b->limbs, b->count);
    if (cmp == 0) return bignum_new(1);
    if (cmp > 0) {
        ObjBignum *r = mag_sub(a->limbs, a->count, b->limbs, b->count);
        r->sign = (r->count == 0) ? 0 : a->sign;
        return r;
    }
    ObjBignum *r = mag_sub(b->limbs, b->count, a->limbs, a->count);
    r->sign = (r->count == 0) ? 0 : b->sign;
    return r;
}

ObjBignum *bignum_sub(const ObjBignum *a, const ObjBignum *b) {
    ObjBignum *negB = bignum_negate(b);
    ObjBignum *r = bignum_add(a, negB);
    bignum_free(negB);
    return r;
}

ObjBignum *bignum_mul(const ObjBignum *a, const ObjBignum *b) {
    if (a->sign == 0 || b->sign == 0) return bignum_new(1);
    ObjBignum *r = mag_mul(a->limbs, a->count, b->limbs, b->count);
    r->sign = (a->sign == b->sign) ? 1 : -1;
    return r;
}

/* -- division --------------------------------------------------------------
 * mag_divmod_small handles a single-limb divisor directly (also used as the
 * base case for decimal formatting and digit parsing). mag_divmod_knuth
 * handles a divisor of 2+ limbs via Algorithm D. mag_divmod dispatches.
 */

static uint32_t mag_divmod_small(const uint32_t *a, int an, uint32_t d, uint32_t *q) {
    uint64_t rem = 0;
    for (int i = an - 1; i >= 0; i--) {
        uint64_t cur = (rem << 32) | a[i];
        q[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    return (uint32_t)rem;
}

static int clz32(uint32_t x) {
    if (x == 0) return 32;
    int n = 0;
    while ((x & 0x80000000u) == 0) { x <<= 1; n++; }
    return n;
}

/* Shifts `a` (an limbs) left by 0<=s<32 bits into `out` (must have room for
   an+1 limbs). Returns the limb count actually used. */
static int shl_bits(const uint32_t *a, int an, int s, uint32_t *out) {
    if (s == 0) {
        memcpy(out, a, (size_t)an * sizeof(uint32_t));
        return an;
    }
    uint32_t carry = 0;
    for (int i = 0; i < an; i++) {
        out[i] = (a[i] << s) | carry;
        carry = a[i] >> (32 - s);
    }
    if (carry) {
        out[an] = carry;
        return an + 1;
    }
    return an;
}

/* Shifts `a` (an limbs) right by 0<=s<32 bits into `out` (an limbs). The
   topmost output limb gets no "bits from above" (there is nothing above
   the caller's own window) -- every call site here only ever needs that
   when the true value is guaranteed to fit in exactly `an` limbs already. */
static void shr_bits(const uint32_t *a, int an, int s, uint32_t *out) {
    if (s == 0) {
        memcpy(out, a, (size_t)an * sizeof(uint32_t));
        return;
    }
    for (int i = 0; i < an; i++) {
        uint32_t lo = a[i] >> s;
        uint32_t hi = (i + 1 < an) ? (a[i + 1] << (32 - s)) : 0;
        out[i] = lo | hi;
    }
}

/* Knuth TAOCP Vol 2, Algorithm D. Requires bn >= 2. */
static void mag_divmod_knuth(const uint32_t *a, int an, const uint32_t *b, int bn,
                              ObjBignum **q_out, ObjBignum **r_out) {
    int m = an - bn;
    if (m < 0) {
        ObjBignum *q = bignum_new(1);
        ObjBignum *r = bignum_new(an > 0 ? an : 1);
        if (an > 0) memcpy(r->limbs, a, (size_t)an * sizeof(uint32_t));
        r->count = an;
        trim(r);
        *q_out = q;
        *r_out = r;
        return;
    }

    int s = clz32(b[bn - 1]);

    /* Normalized divisor v = b << s: by definition of s (clz of b's top
       limb) this never overflows past bn limbs. Normalized dividend u = a
       << s needs an extra limb for whatever *does* overflow. */
    uint32_t *v = (uint32_t *)malloc((size_t)bn * sizeof(uint32_t));
    shl_bits(b, bn, s, v);

    uint32_t *u = (uint32_t *)calloc((size_t)(an + 1), sizeof(uint32_t));
    shl_bits(a, an, s, u);

    uint32_t *q = (uint32_t *)calloc((size_t)(m + 1), sizeof(uint32_t));
    uint64_t vTop = v[bn - 1];
    uint64_t vSecond = v[bn - 2]; /* bn >= 2 guaranteed by caller */

    for (int j = m; j >= 0; j--) {
        uint64_t numerator = ((uint64_t)u[j + bn] << 32) | u[j + bn - 1];
        uint64_t qhat = numerator / vTop;
        uint64_t rhat = numerator % vTop;
        if (qhat > 0xFFFFFFFFull) {
            qhat = 0xFFFFFFFFull;
            rhat = numerator - qhat * vTop;
        }
        while (rhat <= 0xFFFFFFFFull && qhat * vSecond > ((rhat << 32) | u[j + bn - 2])) {
            qhat--;
            rhat += vTop;
        }

        /* multiply-and-subtract: u[j..j+bn] -= qhat * v[0..bn-1] */
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (int i = 0; i < bn; i++) {
            uint64_t p = qhat * v[i] + carry;
            carry = p >> 32;
            int64_t sub = (int64_t)u[j + i] - (int64_t)(uint32_t)p - borrow;
            if (sub < 0) { sub += ((int64_t)1 << 32); borrow = 1; } else { borrow = 0; }
            u[j + i] = (uint32_t)sub;
        }
        int64_t topSub = (int64_t)u[j + bn] - (int64_t)carry - borrow;
        bool negResult = topSub < 0;
        u[j + bn] = (uint32_t)topSub;

        if (negResult) {
            /* qhat was exactly 1 too big: back off and add v once. */
            qhat--;
            uint64_t addCarry = 0;
            for (int i = 0; i < bn; i++) {
                uint64_t s2 = (uint64_t)u[j + i] + v[i] + addCarry;
                u[j + i] = (uint32_t)s2;
                addCarry = s2 >> 32;
            }
            u[j + bn] = (uint32_t)((uint64_t)u[j + bn] + addCarry);
        }
        q[j] = (uint32_t)qhat;
    }

    ObjBignum *qres = bignum_new(m + 1);
    memcpy(qres->limbs, q, (size_t)(m + 1) * sizeof(uint32_t));
    qres->count = m + 1;
    trim(qres);

    /* Denormalize the remainder: it is guaranteed to fit in exactly bn
       limbs already (it is < the normalized divisor v, which is bn limbs),
       so shr_bits's "nothing above" top limb is exactly right here. */
    ObjBignum *rres = bignum_new(bn);
    shr_bits(u, bn, s, rres->limbs);
    rres->count = bn;
    trim(rres);

    free(v);
    free(u);
    free(q);
    *q_out = qres;
    *r_out = rres;
}

static void mag_divmod(const uint32_t *a, int an, const uint32_t *b, int bn,
                        ObjBignum **q_out, ObjBignum **r_out) {
    if (bn == 1) {
        ObjBignum *q = bignum_new(an > 0 ? an : 1);
        uint32_t rem = an > 0 ? mag_divmod_small(a, an, b[0], q->limbs) : 0;
        q->count = an;
        trim(q);
        ObjBignum *r = bignum_new(1);
        if (rem != 0) {
            r->limbs[0] = rem;
            r->count = 1;
        }
        *q_out = q;
        *r_out = r;
        return;
    }
    mag_divmod_knuth(a, an, b, bn, q_out, r_out);
}

bool bignum_divmod_trunc(const ObjBignum *a, const ObjBignum *b, ObjBignum **q_out, ObjBignum **r_out) {
    if (bignum_is_zero(b)) return false;
    if (bignum_is_zero(a)) {
        *q_out = bignum_new(1);
        *r_out = bignum_new(1);
        return true;
    }
    ObjBignum *q, *r;
    mag_divmod(a->limbs, a->count, b->limbs, b->count, &q, &r);
    /* Same reasoning as bignum_add: q/r come straight out of mag_divmod
       with sign still at the bignum_new default, so the zero-check has to
       read count, not sign. */
    q->sign = (q->count == 0) ? 0 : (a->sign == b->sign ? 1 : -1);
    r->sign = (r->count == 0) ? 0 : a->sign;
    *q_out = q;
    *r_out = r;
    return true;
}

bool bignum_divmod_floor(const ObjBignum *a, const ObjBignum *b, ObjBignum **q_out, ObjBignum **r_out) {
    ObjBignum *q, *r;
    if (!bignum_divmod_trunc(a, b, &q, &r)) return false;
    if (!bignum_is_zero(r) && a->sign != b->sign) {
        ObjBignum *one = bignum_from_int64(1);
        ObjBignum *newQ = bignum_sub(q, one);
        ObjBignum *newR = bignum_add(r, b);
        bignum_free(one);
        bignum_free(q);
        bignum_free(r);
        q = newQ;
        r = newR;
    }
    *q_out = q;
    *r_out = r;
    return true;
}

/* -- power ------------------------------------------------------------------ */

ObjBignum *bignum_pow(const ObjBignum *base, int64_t exp) {
    ObjBignum *result = bignum_from_int64(1);
    ObjBignum *b = bignum_copy(base);
    uint64_t e = (uint64_t)exp;
    while (e > 0) {
        if (e & 1) {
            ObjBignum *tmp = bignum_mul(result, b);
            bignum_free(result);
            result = tmp;
        }
        e >>= 1;
        if (e > 0) {
            ObjBignum *tmp = bignum_mul(b, b);
            bignum_free(b);
            b = tmp;
        }
    }
    bignum_free(b);
    return result;
}

/* -- bitwise (two's-complement *meaning*, sign-magnitude storage) --------- */

static void to_twos_complement(const ObjBignum *n, int resultLen, uint32_t *out) {
    if (n->sign >= 0) {
        for (int i = 0; i < resultLen; i++) {
            out[i] = (i < n->count) ? n->limbs[i] : 0;
        }
        return;
    }
    /* out = ~(magnitude - 1), infinite-1-extended beyond the magnitude */
    uint32_t *mag = (uint32_t *)calloc((size_t)resultLen, sizeof(uint32_t));
    for (int i = 0; i < n->count; i++) mag[i] = n->limbs[i];
    int64_t borrow = 1;
    for (int i = 0; i < resultLen && borrow; i++) {
        int64_t d = (int64_t)mag[i] - borrow;
        if (d < 0) { d += ((int64_t)1 << 32); borrow = 1; } else { borrow = 0; }
        mag[i] = (uint32_t)d;
    }
    for (int i = 0; i < resultLen; i++) out[i] = ~mag[i];
    free(mag);
}

static ObjBignum *from_twos_complement(const uint32_t *bits, int len) {
    bool negative = (bits[len - 1] & 0x80000000u) != 0;
    ObjBignum *r = bignum_new(len);
    if (!negative) {
        memcpy(r->limbs, bits, (size_t)len * sizeof(uint32_t));
        r->count = len;
        r->sign = 1;
        trim(r);
        return r;
    }
    uint64_t carry = 1;
    for (int i = 0; i < len; i++) {
        uint32_t inv = ~bits[i];
        uint64_t sum = (uint64_t)inv + carry;
        r->limbs[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    r->count = len;
    r->sign = -1;
    trim(r);
    return r;
}

static ObjBignum *bitwise_op(const ObjBignum *a, const ObjBignum *b, char op) {
    int len = (a->count > b->count ? a->count : b->count) + 1;
    /* calloc, not malloc: GCC's -O2 uninitialized-value analysis can't
       always prove the fill loops below cover every element once this gets
       inlined into bitwise_op's three call sites, and warns (-Werror'd into
       a hard build failure) even though the loops always run len>=1 times
       here. Zeroing sidesteps the false positive for a trivial cost. */
    uint32_t *ta = (uint32_t *)calloc((size_t)len, sizeof(uint32_t));
    uint32_t *tb = (uint32_t *)calloc((size_t)len, sizeof(uint32_t));
    uint32_t *tr = (uint32_t *)calloc((size_t)len, sizeof(uint32_t));
    to_twos_complement(a, len, ta);
    to_twos_complement(b, len, tb);
    for (int i = 0; i < len; i++) {
        tr[i] = (op == '&') ? (ta[i] & tb[i]) : (op == '|') ? (ta[i] | tb[i]) : (ta[i] ^ tb[i]);
    }
    ObjBignum *r = from_twos_complement(tr, len);
    free(ta);
    free(tb);
    free(tr);
    return r;
}

ObjBignum *bignum_band(const ObjBignum *a, const ObjBignum *b) { return bitwise_op(a, b, '&'); }
ObjBignum *bignum_bor(const ObjBignum *a, const ObjBignum *b) { return bitwise_op(a, b, '|'); }
ObjBignum *bignum_bxor(const ObjBignum *a, const ObjBignum *b) { return bitwise_op(a, b, '^'); }

ObjBignum *bignum_bnot(const ObjBignum *a) {
    /* ~x == -x - 1 */
    ObjBignum *one = bignum_from_int64(1);
    ObjBignum *negA = bignum_negate(a);
    ObjBignum *r = bignum_sub(negA, one);
    bignum_free(one);
    bignum_free(negA);
    return r;
}

/* -- shifts ------------------------------------------------------------- */

ObjBignum *bignum_shl(const ObjBignum *a, int64_t shift) {
    if (shift == 0 || bignum_is_zero(a)) return bignum_copy(a);
    int limbShift = (int)(shift / 32);
    int bitShift = (int)(shift % 32);
    int newCount = a->count + limbShift + 1;
    ObjBignum *r = bignum_new(newCount);
    for (int i = 0; i < a->count; i++) r->limbs[i + limbShift] = a->limbs[i];
    if (bitShift > 0) {
        uint32_t carry = 0;
        for (int i = limbShift; i < newCount; i++) {
            uint32_t cur = r->limbs[i];
            r->limbs[i] = (cur << bitShift) | carry;
            carry = cur >> (32 - bitShift);
        }
    }
    r->count = newCount;
    r->sign = a->sign;
    trim(r);
    return r;
}

ObjBignum *bignum_shr(const ObjBignum *a, int64_t shift) {
    if (shift == 0 || bignum_is_zero(a)) return bignum_copy(a);
    if (a->sign > 0) {
        int limbShift = (int)(shift / 32);
        int bitShift = (int)(shift % 32);
        if (limbShift >= a->count) return bignum_new(1);
        int newCount = a->count - limbShift;
        ObjBignum *r = bignum_new(newCount);
        shr_bits(a->limbs + limbShift, newCount, bitShift, r->limbs);
        r->count = newCount;
        r->sign = 1;
        trim(r);
        return r;
    }
    /* Negative: Python's >> floors toward -infinity. x >> n == -((-x-1) >> n) - 1,
       reusing the simple positive-shift path above (well-known bit-trick). */
    ObjBignum *negA = bignum_negate(a);
    ObjBignum *one = bignum_from_int64(1);
    ObjBignum *negAMinus1 = bignum_sub(negA, one);
    ObjBignum *shifted = bignum_shr(negAMinus1, shift);
    ObjBignum *negShifted = bignum_negate(shifted);
    ObjBignum *result = bignum_sub(negShifted, one);
    bignum_free(negA);
    bignum_free(one);
    bignum_free(negAMinus1);
    bignum_free(shifted);
    bignum_free(negShifted);
    return result;
}

/* -- parsing / formatting -------------------------------------------------- */

ObjBignum *bignum_from_digits(const char *digits, int len, int base, bool negative) {
    if (len <= 0) return NULL;
    ObjBignum *r = bignum_new(1);
    for (int i = 0; i < len; i++) {
        char c = digits[i];
        int digitVal;
        if (c >= '0' && c <= '9') digitVal = c - '0';
        else if (c >= 'a' && c <= 'f') digitVal = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') digitVal = 10 + (c - 'A');
        else { bignum_free(r); return NULL; }
        if (digitVal >= base) { bignum_free(r); return NULL; }

        uint64_t carry = (uint64_t)digitVal;
        for (int j = 0; j < r->count; j++) {
            uint64_t v = (uint64_t)r->limbs[j] * (uint32_t)base + carry;
            r->limbs[j] = (uint32_t)v;
            carry = v >> 32;
        }
        while (carry) {
            if (!ensure_capacity(r, r->count + 1)) { bignum_free(r); return NULL; }
            r->limbs[r->count++] = (uint32_t)carry;
            carry >>= 32;
        }
    }
    trim(r);
    if (r->count > 0) r->sign = negative ? -1 : 1;
    return r;
}

char *bignum_to_decimal_string(const ObjBignum *n) {
    if (n->sign == 0) {
        char *s = (char *)malloc(2);
        s[0] = '0';
        s[1] = '\0';
        return s;
    }

    uint32_t *work = (uint32_t *)malloc((size_t)n->count * sizeof(uint32_t));
    memcpy(work, n->limbs, (size_t)n->count * sizeof(uint32_t));
    int workLen = n->count;

    /* Each chunk holds 9 decimal digits (< 10^9 < 2^32); worst case is
       roughly one chunk per limb, plus a little slack. */
    int chunkCap = n->count + 2;
    uint32_t *chunks = (uint32_t *)malloc((size_t)chunkCap * sizeof(uint32_t));
    int chunkCount = 0;

    while (workLen > 0) {
        uint32_t rem = mag_divmod_small(work, workLen, 1000000000u, work);
        while (workLen > 0 && work[workLen - 1] == 0) workLen--;
        chunks[chunkCount++] = rem;
    }
    if (chunkCount == 0) chunks[chunkCount++] = 0;

    char headBuf[16];
    snprintf(headBuf, sizeof headBuf, "%u", chunks[chunkCount - 1]);
    size_t total = (size_t)(n->sign < 0 ? 1 : 0) + strlen(headBuf) + (size_t)(chunkCount - 1) * 9;

    char *out = (char *)malloc(total + 1);
    char *p = out;
    if (n->sign < 0) *p++ = '-';
    memcpy(p, headBuf, strlen(headBuf));
    p += strlen(headBuf);
    for (int i = chunkCount - 2; i >= 0; i--) {
        p += snprintf(p, 10, "%09u", chunks[i]);
    }
    *p = '\0';

    free(work);
    free(chunks);
    return out;
}
