/* native/tests/fuzz_bignum.c -- NATIVE_PLAN.md N1 acceptance: "differential
 * fuzz: random bignum ops match Python's int exactly." This program does
 * the C half only -- it prints one line per random operation (operands and
 * the C result), all in decimal. tests/native/fuzz_bignum_check.py drives
 * it, recomputes each case with Python's own `int`, and diffs.
 *
 * Usage: fuzz_bignum <seed> <count>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bignum.h"

/* A small, deterministic, portable PRNG (xorshift64*) -- deliberately not
   libc rand()/srand(), whose sequence and quality vary by platform/libc,
   which would make "same seed" not actually mean "same test cases" across
   the OSes this project cares about. */
static uint64_t rng_state;

static uint64_t next_rand(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static int rand_range(int lo, int hi) {
    return lo + (int)(next_rand() % (uint64_t)(hi - lo + 1));
}

/* A random decimal digit string, `digitCount` digits (first digit nonzero
   unless digitCount==1), optionally negative. */
static ObjBignum *random_bignum(int maxDigits, bool allowNegative) {
    int digitCount = rand_range(1, maxDigits);
    char *digits = (char *)malloc((size_t)digitCount + 1);
    /* leading digit nonzero unless this is the single-digit case (0..9) */
    digits[0] = (char)('0' + rand_range(digitCount > 1 ? 1 : 0, 9));
    for (int i = 1; i < digitCount; i++) {
        digits[i] = (char)('0' + rand_range(0, 9));
    }
    digits[digitCount] = '\0';
    bool negative = allowNegative && rand_range(0, 1) == 1 && !(digitCount == 1 && digits[0] == '0');
    ObjBignum *n = bignum_from_digits(digits, digitCount, 10, negative);
    free(digits);
    return n;
}

static void print_case(const char *op, const ObjBignum *a, const ObjBignum *b,
                        const ObjBignum *result, const ObjBignum *result2) {
    char *as = bignum_to_decimal_string(a);
    char *rs = bignum_to_decimal_string(result);
    if (b && result2) {
        char *bs = bignum_to_decimal_string(b);
        char *r2s = bignum_to_decimal_string(result2);
        printf("%s %s %s %s %s\n", op, as, bs, rs, r2s);
        free(bs);
        free(r2s);
    } else if (b) {
        char *bs = bignum_to_decimal_string(b);
        printf("%s %s %s %s\n", op, as, bs, rs);
        free(bs);
    } else {
        printf("%s %s %s\n", op, as, rs);
    }
    free(as);
    free(rs);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <seed> <count>\n", argv[0]);
        return 2;
    }
    rng_state = (uint64_t)strtoull(argv[1], NULL, 10);
    if (rng_state == 0) rng_state = 0x9E3779B97F4A7C15ULL; /* xorshift needs a nonzero seed */
    long count = strtol(argv[2], NULL, 10);

    for (long i = 0; i < count; i++) {
        int op = rand_range(0, 9);
        int maxDigits = rand_range(1, 40); /* spans single-limb through many-limb */
        ObjBignum *a = random_bignum(maxDigits, true);

        switch (op) {
            case 0: { /* add */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                ObjBignum *r = bignum_add(a, b);
                print_case("add", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 1: { /* sub */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                ObjBignum *r = bignum_sub(a, b);
                print_case("sub", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 2: { /* mul */
                ObjBignum *b = random_bignum(rand_range(1, 25), true);
                ObjBignum *r = bignum_mul(a, b);
                print_case("mul", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 3: { /* divmod (floor) */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                if (bignum_is_zero(b)) { bignum_free(b); bignum_free(a); i--; continue; }
                ObjBignum *q, *r;
                bignum_divmod_floor(a, b, &q, &r);
                print_case("divmod", a, b, q, r);
                bignum_free(b); bignum_free(q); bignum_free(r);
                break;
            }
            case 4: { /* shl, small shift to keep sizes sane */
                int64_t shift = rand_range(0, 96);
                ObjBignum *r = bignum_shl(a, shift);
                char shiftBuf[32];
                snprintf(shiftBuf, sizeof shiftBuf, "%lld", (long long)shift);
                ObjBignum *shiftAsBignum = bignum_from_digits(shiftBuf, (int)strlen(shiftBuf), 10, false);
                print_case("shl", a, shiftAsBignum, r, NULL);
                bignum_free(shiftAsBignum); bignum_free(r);
                break;
            }
            case 5: { /* shr */
                int64_t shift = rand_range(0, 96);
                ObjBignum *r = bignum_shr(a, shift);
                char shiftBuf[32];
                snprintf(shiftBuf, sizeof shiftBuf, "%lld", (long long)shift);
                ObjBignum *shiftAsBignum = bignum_from_digits(shiftBuf, (int)strlen(shiftBuf), 10, false);
                print_case("shr", a, shiftAsBignum, r, NULL);
                bignum_free(shiftAsBignum); bignum_free(r);
                break;
            }
            case 6: { /* band */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                ObjBignum *r = bignum_band(a, b);
                print_case("band", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 7: { /* bor */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                ObjBignum *r = bignum_bor(a, b);
                print_case("bor", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 8: { /* bxor */
                ObjBignum *b = random_bignum(rand_range(1, 40), true);
                ObjBignum *r = bignum_bxor(a, b);
                print_case("bxor", a, b, r, NULL);
                bignum_free(b); bignum_free(r);
                break;
            }
            case 9: { /* bnot */
                ObjBignum *r = bignum_bnot(a);
                print_case("bnot", a, NULL, r, NULL);
                bignum_free(r);
                break;
            }
        }
        bignum_free(a);
    }
    return 0;
}
