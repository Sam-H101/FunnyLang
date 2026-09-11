#include "rizz.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bignum.h"
#include "error.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "platform.h"
#include "stash.h"
#include "da_string.h"

#define IS_INT_LIKE(v) (IS_INT(v) || IS_BOOL(v))

static int64_t as_int64_like(Value v) {
    return IS_BOOL(v) ? (AS_BOOL(v) ? 1 : 0) : AS_INT(v);
}

/* -- xoshiro256** (public domain, D. Blackman & S. Vigna) ---------------- */

static uint64_t g_state[4];
static bool g_seeded = false;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rizz_seed(uint64_t seed) {
    uint64_t sm = seed;
    for (int i = 0; i < 4; i++) g_state[i] = splitmix64(&sm);
    g_seeded = true;
}

static uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t rizz_next_u64(void) {
    if (!g_seeded) {
        /* Self-seeded from the current time the first time anything asks
           for randomness without an explicit rizz.seed(n) first --
           unpredictable is the point here, not reproducible. */
        rizz_seed((uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&g_state);
    }
    uint64_t *s = g_state;
    uint64_t result = rotl64(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return result;
}

double rizz_next_double(void) {
    return (double)(rizz_next_u64() >> 11) * (1.0 / 9007199254740992.0); /* / 2^53 */
}

/* -- funnylang/stdlib/rizz.py's own functions ---------------------------- */

static Value m_roll(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_INT_LIKE(a[0]) || !IS_INT_LIKE(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'roll' needs whole numbas.");
        return GHOST_VAL;
    }
    int64_t lo = as_int64_like(a[0]), hi = as_int64_like(a[1]);
    if (lo > hi) {
        int64_t t = lo;
        lo = hi;
        hi = t;
    }
    uint64_t range = (uint64_t)(hi - lo) + 1;
    return INT_VAL(lo + (int64_t)(rizz_next_u64() % range));
}

static Value m_float_roll(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return FLOAT_VAL(rizz_next_double());
}

static Value m_pick(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'pick' needs a stash, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *s = (ObjStash *)AS_OBJ(a[0]);
    if (s->count == 0) {
        vm_throw_native(vm, "TypeVibeMismatch", "'pick' on an empty stash.");
        return GHOST_VAL;
    }
    return s->items[rizz_next_u64() % (uint64_t)s->count];
}

static Value m_shuffle(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'shuffle' needs a stash, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *s = (ObjStash *)AS_OBJ(a[0]);
    for (int i = s->count - 1; i > 0; i--) {
        int j = (int)(rizz_next_u64() % (uint64_t)(i + 1));
        Value tmp = s->items[i];
        s->items[i] = s->items[j];
        s->items[j] = tmp;
    }
    return a[0];
}

static Value m_coinflip(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return BOOL_VAL(rizz_next_double() < 0.5);
}

static Value m_seed(VM *vm, Value *a, int argc) {
    if (argc == 0 || IS_GHOST(a[0])) {
        g_seeded = false; /* re-seed unpredictably next use, matching random.seed(None) */
        return GHOST_VAL;
    }
    uint64_t seed;
    if (IS_INT_LIKE(a[0])) {
        seed = (uint64_t)as_int64_like(a[0]);
    } else if (IS_FLOAT(a[0])) {
        double d = AS_FLOAT(a[0]);
        memcpy(&seed, &d, sizeof seed);
    } else {
        vm_throw_native(vm, "TypeVibeMismatch", "'seed' needs a numba, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    rizz_seed(seed);
    return GHOST_VAL;
}

static Value m_uuid(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    uint64_t hi = rizz_next_u64(), lo = rizz_next_u64();
    /* UUIDv4: version nibble fixed to 4, variant bits fixed to 10xx. */
    unsigned char b[16];
    memcpy(b, &hi, 8);
    memcpy(b + 8, &lo, 8);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    char buf[37];
    snprintf(buf, sizeof buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
              b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return OBJ_VAL(string_new(&vm->gc, buf, 36));
}

/* rizz.entropy(n) -- n bytes from the operating system's CSPRNG, as 2n
   lowercase hex digits. Everything else in this module is xoshiro256**
   seeded from the clock: fine for dice, useless for anything an attacker
   would like to guess. Session ids and CSRF tokens come from here. */
static Value m_entropy(VM *vm, Value *a, int argc) {
    int64_t n = 32;
    if (argc > 0 && !IS_GHOST(a[0])) {
        if (!IS_INT(a[0])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'entropy' needs a numba of bytes, not a %s.",
                            vm_type_name(a[0]));
            return GHOST_VAL;
        }
        n = AS_INT(a[0]);
    }
    if (n < 1 || n > 1024) {
        vm_throw_native(vm, "OutOfPocket", "'entropy' hands out 1 to 1024 bytes at a time, not %lld.", (long long)n);
        return GHOST_VAL;
    }
    unsigned char bytes[1024];
    if (!platform_random_bytes(bytes, (size_t)n)) {
        vm_throw_native(vm, "SkillIssue", "the operating system wouldn't hand over any randomness.");
        return GHOST_VAL;
    }
    static const char HEX[] = "0123456789abcdef";
    char hex[2049];
    for (int64_t i = 0; i < n; i++) {
        hex[2 * i] = HEX[bytes[i] >> 4];
        hex[2 * i + 1] = HEX[bytes[i] & 0x0F];
    }
    hex[2 * n] = '\0';
    return OBJ_VAL(string_new(&vm->gc, hex, (uint32_t)(2 * n)));
}

static Value m_gamble(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_NUM(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'gamble' needs a numba, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    double odds = IS_FLOAT(a[0]) ? AS_FLOAT(a[0]) : (IS_INT(a[0]) ? (double)AS_INT(a[0]) : bignum_to_double(AS_BIGNUM(a[0])));
    return BOOL_VAL(rizz_next_double() < odds);
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} RizzEntry;

static const RizzEntry RIZZ_FUNCTIONS[] = {
    {"roll", m_roll, 2, 2},
    {"float_roll", m_float_roll, 0, 0},
    {"pick", m_pick, 1, 1},
    {"shuffle", m_shuffle, 1, 1},
    {"coinflip", m_coinflip, 0, 0},
    {"seed", m_seed, 0, 1},
    {"uuid", m_uuid, 0, 0},
    {"gamble", m_gamble, 1, 1},
    {"entropy", m_entropy, 0, 1},
};
#define RIZZ_FUNCTIONS_COUNT (int)(sizeof(RIZZ_FUNCTIONS) / sizeof(RIZZ_FUNCTIONS[0]))

Value rizz_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < RIZZ_FUNCTIONS_COUNT; i++) {
        const RizzEntry *e = &RIZZ_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "rizz", 4);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
