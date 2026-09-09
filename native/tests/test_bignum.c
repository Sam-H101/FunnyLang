/* native/tests/test_bignum.c -- NATIVE_PLAN.md N1 acceptance: plain
 * assertion-based unit tests for bignum.c, focused on the edge cases that
 * are easy to get wrong by construction (Knuth D borrow propagation,
 * single-limb divisors, the INT64_MIN corner, sign/zero handling) rather
 * than random coverage -- that's fuzz_bignum.c's job, cross-checked
 * against Python's own `int`.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bignum.h"

static ObjBignum *from_dec(const char *s) {
    bool neg = (s[0] == '-');
    const char *digits = neg ? s + 1 : s;
    return bignum_from_digits(digits, (int)strlen(digits), 10, neg);
}

static void assert_dec(const ObjBignum *n, const char *expected) {
    char *s = bignum_to_decimal_string(n);
    if (strcmp(s, expected) != 0) {
        fprintf(stderr, "expected %s, got %s\n", expected, s);
        assert(0 && "decimal mismatch");
    }
    free(s);
}

static void test_from_int64_roundtrip(void) {
    int64_t cases[] = {0, 1, -1, 42, -42, INT64_MAX, INT64_MIN, INT64_MIN + 1};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ObjBignum *n = bignum_from_int64(cases[i]);
        int64_t back;
        assert(bignum_to_int64(n, &back));
        assert(back == cases[i]);
        bignum_free(n);
    }
}

static void test_decimal_parse_and_format(void) {
    const char *cases[] = {
        "0", "1", "-1", "123456789", "-123456789",
        "99999999999999999999999999999999",
        "-99999999999999999999999999999999",
        "18446744073709551616", /* 2^64 */
        "340282366920938463463374607431768211456", /* 2^128 */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ObjBignum *n = from_dec(cases[i]);
        assert_dec(n, cases[i]);
        bignum_free(n);
    }
}

static void test_add_sub_basic(void) {
    ObjBignum *a = from_dec("99999999999999999999");
    ObjBignum *b = from_dec("1");
    ObjBignum *sum = bignum_add(a, b);
    assert_dec(sum, "100000000000000000000"); /* carry propagates through every limb */
    ObjBignum *back = bignum_sub(sum, b);
    assert_dec(back, "99999999999999999999");
    bignum_free(a); bignum_free(b); bignum_free(sum); bignum_free(back);

    ObjBignum *x = from_dec("5");
    ObjBignum *y = from_dec("7");
    ObjBignum *diff = bignum_sub(x, y);
    assert_dec(diff, "-2"); /* result sign flips when |b| > |a| */
    bignum_free(x); bignum_free(y); bignum_free(diff);
}

static void test_mul_basic(void) {
    ObjBignum *a = from_dec("123456789012345678901234567890");
    ObjBignum *b = from_dec("987654321098765432109876543210");
    ObjBignum *p = bignum_mul(a, b);
    /* cross-checked against Python: 123456789012345678901234567890 *
       987654321098765432109876543210 */
    assert_dec(p, "121932631137021795226185032733622923332237463801111263526900");
    bignum_free(a); bignum_free(b); bignum_free(p);
}

static void assert_divmod_floor(const char *as, const char *bs, const char *qs, const char *rs) {
    ObjBignum *a = from_dec(as);
    ObjBignum *b = from_dec(bs);
    ObjBignum *q, *r;
    assert(bignum_divmod_floor(a, b, &q, &r));
    assert_dec(q, qs);
    assert_dec(r, rs);
    bignum_free(a); bignum_free(b); bignum_free(q); bignum_free(r);
}

static void test_divmod_floor_sign_cases(void) {
    /* PLAN.md's `\` `%` are floor division/modulo (Python semantics): the
       remainder's sign always matches the divisor's, never the dividend's. */
    assert_divmod_floor("7", "2", "3", "1");
    assert_divmod_floor("-7", "2", "-4", "1");
    assert_divmod_floor("7", "-2", "-4", "-1");
    assert_divmod_floor("-7", "-2", "3", "-1");
    assert_divmod_floor("6", "3", "2", "0");
    assert_divmod_floor("0", "5", "0", "0");
}

static void test_divmod_single_limb_divisor(void) {
    /* Exercises mag_divmod_small's own code path (bn==1) directly. */
    ObjBignum *a = from_dec("100000000000000000000000000000001");
    ObjBignum *b = from_dec("7");
    ObjBignum *q, *r;
    assert(bignum_divmod_floor(a, b, &q, &r));
    /* 100000000000000000000000000000001 = 7 * 14285714285714285714285714285714 + 3 */
    assert_dec(q, "14285714285714285714285714285714");
    assert_dec(r, "3");
    bignum_free(a); bignum_free(b); bignum_free(q); bignum_free(r);
}

static void test_divmod_knuth_multi_limb(void) {
    /* Forces the Algorithm D path (bn>=2), including an adversarial qhat
       that's initially off by one and needs the add-back correction --
       constructed so the top two divisor limbs are large (near 2^32) and
       the dividend's leading limbs make the naive two-limb quotient
       estimate overshoot. */
    ObjBignum *a = from_dec("123456789012345678901234567890123456789012345678901234567890");
    ObjBignum *b = from_dec("98765432109876543210987654321");
    ObjBignum *q, *r;
    assert(bignum_divmod_floor(a, b, &q, &r));
    /* cross-checked against Python's divmod() for the same operands */
    assert_dec(q, "1249999988609375000142382812499");
    assert_dec(r, "46440971104644097110464409711");
    /* q*b + r must reconstruct a exactly, regardless of whether the
       expected strings above are themselves correct -- a self-check that
       doesn't depend on hand-copied numbers. */
    ObjBignum *check = bignum_mul(q, b);
    ObjBignum *reconstructed = bignum_add(check, r);
    assert(bignum_compare(reconstructed, a) == 0);
    bignum_free(a); bignum_free(b); bignum_free(q); bignum_free(r);
    bignum_free(check); bignum_free(reconstructed);
}

static void test_pow(void) {
    ObjBignum *base = from_dec("2");
    ObjBignum *p = bignum_pow(base, 100);
    assert_dec(p, "1267650600228229401496703205376");
    bignum_free(base); bignum_free(p);

    ObjBignum *zero_exp_base = from_dec("12345");
    ObjBignum *one = bignum_pow(zero_exp_base, 0);
    assert_dec(one, "1");
    bignum_free(zero_exp_base); bignum_free(one);
}

static void test_shifts(void) {
    ObjBignum *a = from_dec("1");
    ObjBignum *shifted = bignum_shl(a, 100);
    assert_dec(shifted, "1267650600228229401496703205376"); /* 2^100 */
    ObjBignum *back = bignum_shr(shifted, 100);
    assert_dec(back, "1");
    bignum_free(a); bignum_free(shifted); bignum_free(back);

    /* Python: -5 >> 1 == -3 (floors toward -infinity, not toward zero) */
    ObjBignum *neg5 = from_dec("-5");
    ObjBignum *r = bignum_shr(neg5, 1);
    assert_dec(r, "-3");
    bignum_free(neg5); bignum_free(r);

    /* Python: -1 >> 1 == -1 */
    ObjBignum *negOne = from_dec("-1");
    ObjBignum *r2 = bignum_shr(negOne, 1);
    assert_dec(r2, "-1");
    bignum_free(negOne); bignum_free(r2);
}

static void test_bitwise_two2s_complement_meaning(void) {
    /* Python: 5 & 3 == 1, 5 | 2 == 7, 5 ^ 1 == 4, ~5 == -6 */
    ObjBignum *five = from_dec("5");
    ObjBignum *three = from_dec("3");
    ObjBignum *two = from_dec("2");
    ObjBignum *one = from_dec("1");

    ObjBignum *r1 = bignum_band(five, three);
    assert_dec(r1, "1");
    ObjBignum *r2 = bignum_bor(five, two);
    assert_dec(r2, "7");
    ObjBignum *r3 = bignum_bxor(five, one);
    assert_dec(r3, "4");
    ObjBignum *r4 = bignum_bnot(five);
    assert_dec(r4, "-6");

    /* Python: -1 & 5 == 5 (an infinite run of 1-bits acts as identity for AND) */
    ObjBignum *negOne = from_dec("-1");
    ObjBignum *r5 = bignum_band(negOne, five);
    assert_dec(r5, "5");

    /* Python: -5 & -3 == -7 */
    ObjBignum *negFive = from_dec("-5");
    ObjBignum *negThree = from_dec("-3");
    ObjBignum *r6 = bignum_band(negFive, negThree);
    assert_dec(r6, "-7");

    bignum_free(five); bignum_free(three); bignum_free(two); bignum_free(one);
    bignum_free(r1); bignum_free(r2); bignum_free(r3); bignum_free(r4);
    bignum_free(negOne); bignum_free(r5);
    bignum_free(negFive); bignum_free(negThree); bignum_free(r6);
}

static void test_zero_has_no_sign(void) {
    ObjBignum *a = from_dec("5");
    ObjBignum *b = from_dec("-5");
    ObjBignum *sum = bignum_add(a, b);
    assert(bignum_is_zero(sum));
    assert(sum->sign == 0); /* never "-0" */
    assert_dec(sum, "0");
    bignum_free(a); bignum_free(b); bignum_free(sum);
}

static void test_hex_binary_octal_parsing(void) {
    ObjBignum *hex = bignum_from_digits("ff", 2, 16, false);
    assert_dec(hex, "255");
    bignum_free(hex);

    ObjBignum *bin = bignum_from_digits("1010", 4, 2, false);
    assert_dec(bin, "10");
    bignum_free(bin);

    ObjBignum *oct = bignum_from_digits("17", 2, 8, false);
    assert_dec(oct, "15");
    bignum_free(oct);
}

int main(void) {
    test_from_int64_roundtrip();
    test_decimal_parse_and_format();
    test_add_sub_basic();
    test_mul_basic();
    test_divmod_floor_sign_cases();
    test_divmod_single_limb_divisor();
    test_divmod_knuth_multi_limb();
    test_pow();
    test_shifts();
    test_bitwise_two2s_complement_meaning();
    test_zero_has_no_sign();
    test_hex_binary_octal_parsing();
    printf("test_bignum: all tests passed\n");
    return 0;
}
