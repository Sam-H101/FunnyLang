/* native/tests/fuzz_numfmt.c -- NATIVE_PLAN.md N1 acceptance: "differential
 * fuzz: random doubles format identically to Python's repr()." Prints one
 * line per random double: its raw bit pattern (hex, so the Python side can
 * reconstruct the *exact* double with no text-parsing precision loss) and
 * numfmt_repr's own output. tests/native/fuzz_numfmt_check.py drives this
 * and compares against Python's repr() of the same bit pattern.
 *
 * Usage: fuzz_numfmt <seed> <count>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../numfmt.h"

static uint64_t rng_state;

static uint64_t next_rand(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* Biases toward "interesting" bit patterns: exponent bits skewed toward the
   extremes (subnormal/max) as well as uniformly random, not just uniformly
   random 64 bits (which would rarely land near the boundaries that are
   easiest to get wrong). */
static uint64_t random_double_bits(void) {
    int mode = (int)(next_rand() % 4);
    uint64_t sign = (next_rand() & 1) << 63;
    uint64_t mantissa = next_rand() & 0xFFFFFFFFFFFFFull;
    uint64_t exponent;
    switch (mode) {
        case 0: exponent = next_rand() % 2047; break;              /* fully random */
        case 1: exponent = 0; break;                                /* subnormal range */
        case 2: exponent = 2046; break;                             /* near max normal */
        default: exponent = 1023 + (int)(next_rand() % 21) - 10; break; /* near 1.0 */
    }
    return sign | (exponent << 52) | mantissa;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <seed> <count>\n", argv[0]);
        return 2;
    }
    rng_state = (uint64_t)strtoull(argv[1], NULL, 10);
    if (rng_state == 0) rng_state = 0x9E3779B97F4A7C15ULL;
    long count = strtol(argv[2], NULL, 10);

    for (long i = 0; i < count; i++) {
        uint64_t bits = random_double_bits();
        double v;
        memcpy(&v, &bits, sizeof v);
        if (v != v) continue; /* skip NaN: infinitely many bit patterns, one repr */
        char *s = numfmt_repr(v);
        printf("%016llx %s\n", (unsigned long long)bits, s);
        free(s);
    }
    return 0;
}
