/* native/rizz.h -- funnylang/stdlib/rizz.py: randomness (`gimme rizz`).
 *
 * AGENT CHOICE (NATIVE_PLAN.md's own N5 task 6 line, quoted directly):
 * "rizz uses xoshiro256** rather than reproducing CPython's Mersenne
 * Twister stream. rizz.seed(n) is reproducible within the C VM; cross-VM
 * stream equality was never specified and no golden file depends on it."
 * Every function here (and stash.shuffle_it, which shares this same
 * generator for consistency rather than keeping a second one) is
 * therefore deliberately not tested for exact output in the differential
 * suite -- only for producing a value of the right shape/range, the same
 * established exception as examples/chaos.funny's own randomness.
 *
 * The generator's state lives in the VM (RUNTIME_PLAN.md R0): each
 * `interns` worker has its own, so threads never share one, and a seed
 * gives the same stream on any VM.
 */
#ifndef FUNNY_RIZZ_H
#define FUNNY_RIZZ_H

#include <stdint.h>

#include "value.h"
#include "vm.h"

struct VM;

/* Reseeds `vm`'s generator deterministically from `seed`. Called by
   rizz.seed(n); a VM that never calls it is seeded from the OS on first
   use. */
void rizz_seed(struct VM *vm, uint64_t seed);
/* The next raw 64 bits from `vm`'s xoshiro256** state. */
uint64_t rizz_next_u64(struct VM *vm);
/* A uniform double in [0, 1), via the top 53 bits of one rizz_next_u64(). */
double rizz_next_double(struct VM *vm);

Value rizz_build(struct VM *vm);

#endif /* FUNNY_RIZZ_H */
