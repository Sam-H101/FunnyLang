#include "mafs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "da_string.h"
#include "vm.h"

/* M_PI/M_E are POSIX/BSD extensions to <math.h>, not standard C11 -- with
   -std=c11 (strict mode) glibc doesn't expose them, and MSVC never does
   without _USE_MATH_DEFINES. Defining our own avoids depending on either. */
#define FUNNY_PI 3.14159265358979323846
#define FUNNY_E 2.71828182845904523536

static bool check_num(VM *vm, Value v, const char *fnName) {
    if (IS_NUM(v)) return true;
    vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a numba, not a %s.", fnName, vm_type_name(v));
    return false;
}

static double as_double(Value v) {
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    if (IS_INT(v)) return (double)AS_INT(v);
    return bignum_to_double(AS_BIGNUM(v));
}

/* A double too large/precise to matter exactly here goes through a
   decimal string + bignum parse, the same round-trip native/builtins.c's
   own to_int uses -- consistent, if not perfectly exact for astronomical
   magnitudes (an inherent double-precision limit, not something this
   milestone tries to solve). */
static Value double_to_numba(GC *gc, double d) {
    if (d >= -9.2233720368547758e18 && d <= 9.2233720368547758e18) return INT_VAL((int64_t)d);
    char buf[64];
    snprintf(buf, sizeof buf, "%.0f", d);
    size_t i = 0, len = strlen(buf);
    bool negative = buf[0] == '-';
    if (negative) i = 1;
    ObjBignum *n = bignum_from_digits(buf + i, (int)(len - i), 10, negative);
    int64_t asInt;
    if (bignum_to_int64(n, &asInt)) {
        bignum_free(n);
        return INT_VAL(asInt);
    }
    gc_track(gc, (Obj *)n, sizeof(ObjBignum));
    return OBJ_VAL(n);
}

static Value m_sqrt(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "sqrt")) return GHOST_VAL;
    double x = as_double(a[0]);
    if (x < 0) {
        vm_throw_native(vm, "MathAintMathin", "sqrt of a negative numba.");
        return GHOST_VAL;
    }
    return FLOAT_VAL(sqrt(x));
}

static Value numba_abs(VM *vm, Value x) {
    if (IS_FLOAT(x)) return FLOAT_VAL(fabs(AS_FLOAT(x)));
    if (IS_BIGNUM(x)) return bignum_is_zero(AS_BIGNUM(x)) || AS_BIGNUM(x)->sign > 0 ? x : OBJ_VAL(bignum_abs(AS_BIGNUM(x)));
    int64_t i = AS_INT(x);
    if (i >= 0) return x;
    if (i == INT64_MIN) {
        ObjBignum *n = bignum_from_int64(i);
        ObjBignum *r = bignum_abs(n);
        bignum_free(n);
        int64_t asInt;
        if (bignum_to_int64(r, &asInt)) {
            bignum_free(r);
            return INT_VAL(asInt);
        }
        gc_track(&vm->gc, (Obj *)r, sizeof(ObjBignum));
        return OBJ_VAL(r);
    }
    return INT_VAL(-i);
}

static Value m_abs(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "abs")) return GHOST_VAL;
    return numba_abs(vm, a[0]);
}

static Value m_is_float(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "is_float")) return GHOST_VAL;
    return BOOL_VAL(IS_FLOAT(a[0]));
}

/* The inverse of float_to_bits, added for the same reason it was (PLAN.md
   §16, M12): selfhost/emitter.funny writes a float constant's raw 8 bytes,
   and NATIVE_PLAN.md N8's disassembler has to read them back. Nothing in
   the language gives FunnyLang bit-level access to a float, so decoding
   IEEE-754 by hand in the self-hosting subset would be fragile and
   pointless when both runtimes can just reinterpret the bits. */
static Value m_bits_to_float(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "bits_to_float")) return GHOST_VAL;
    uint64_t bits;
    if (IS_INT(a[0]) && AS_INT(a[0]) >= 0) {
        bits = (uint64_t)AS_INT(a[0]);
    } else if (IS_BIGNUM(a[0]) && AS_BIGNUM(a[0])->sign >= 0 && AS_BIGNUM(a[0])->count <= 2) {
        const ObjBignum *n = AS_BIGNUM(a[0]);
        bits = n->count > 0 ? (uint64_t)n->limbs[0] : 0;
        if (n->count == 2) bits |= (uint64_t)n->limbs[1] << 32;
    } else {
        /* A float lands here too: check_num accepts one, but a bit pattern
           is an integer by definition, and the Python side rejects it the
           same way rather than letting struct raise an OverflowError no
           FunnyLang program could catch. */
        vm_throw_native(vm, "MathAintMathin", "'bits_to_float' needs an unsigned 64-bit bit pattern.");
        return GHOST_VAL;
    }
    double d;
    memcpy(&d, &bits, sizeof d);
    return FLOAT_VAL(d);
}

static Value m_float_to_bits(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "float_to_bits")) return GHOST_VAL;
    double d = as_double(a[0]);
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    if (bits <= (uint64_t)INT64_MAX) return INT_VAL((int64_t)bits);
    ObjBignum *n = bignum_new(2);
    n->limbs[0] = (uint32_t)(bits & 0xFFFFFFFFu);
    n->limbs[1] = (uint32_t)(bits >> 32);
    n->count = n->limbs[1] ? 2 : 1;
    n->sign = 1;
    gc_track(&vm->gc, (Obj *)n, sizeof(ObjBignum));
    return OBJ_VAL(n);
}

static Value numba_floor(VM *vm, Value x) {
    if (!IS_FLOAT(x)) return x; /* already whole */
    return double_to_numba(&vm->gc, floor(AS_FLOAT(x)));
}

static Value numba_ceil(VM *vm, Value x) {
    if (!IS_FLOAT(x)) return x;
    return double_to_numba(&vm->gc, ceil(AS_FLOAT(x)));
}

static Value m_floor(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "floor")) return GHOST_VAL;
    return numba_floor(vm, a[0]);
}

static Value m_ceil(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "ceil")) return GHOST_VAL;
    return numba_ceil(vm, a[0]);
}

/* Python's own round(): banker's rounding (half-to-even), and
   round(x, 0-or-omitted) returns an int while round(x, nonzero) keeps
   x's own type. A plain int/bignum `x` with digits >= 0 is returned
   unchanged (nothing to round); digits < 0 goes through a double
   round-trip -- an accepted precision simplification for that one rare
   combination (rounding a huge exact integer to a power of ten). */
static Value numba_round(VM *vm, Value x, Value digitsV) {
    int64_t digits = IS_GHOST(digitsV) ? 0 : AS_INT(digitsV);
    if (!IS_FLOAT(x) && digits >= 0) return x;
    double v = as_double(x);
    double scale = pow(10.0, (double)digits);
    double scaled = v * scale;
    double floorVal = floor(scaled);
    double diff = scaled - floorVal;
    double rounded;
    if (diff < 0.5) rounded = floorVal;
    else if (diff > 0.5) rounded = floorVal + 1.0;
    else rounded = (fmod(floorVal, 2.0) == 0.0) ? floorVal : floorVal + 1.0;
    double result = rounded / scale;
    if (digits == 0) return double_to_numba(&vm->gc, result);
    return FLOAT_VAL(result);
}

static Value m_round(VM *vm, Value *a, int argc) {
    if (!check_num(vm, a[0], "round")) return GHOST_VAL;
    return numba_round(vm, a[0], argc > 1 ? a[1] : GHOST_VAL);
}

static bool numeric_less(Value a, Value b) {
    /* Comparing through `double` for a mixed bignum/float pair is a
       known, deliberate simplification already established elsewhere in
       this codebase (value.c's own value_equal_narrow) -- consistent,
       not a new gap. */
    return as_double(a) < as_double(b);
}

static Value m_min(VM *vm, Value *a, int argc) {
    for (int i = 0; i < argc; i++) {
        if (!check_num(vm, a[i], "min")) return GHOST_VAL;
    }
    Value best = a[0];
    for (int i = 1; i < argc; i++) {
        if (numeric_less(a[i], best)) best = a[i];
    }
    return best;
}

static Value m_max(VM *vm, Value *a, int argc) {
    for (int i = 0; i < argc; i++) {
        if (!check_num(vm, a[i], "max")) return GHOST_VAL;
    }
    Value best = a[0];
    for (int i = 1; i < argc; i++) {
        if (numeric_less(best, a[i])) best = a[i];
    }
    return best;
}

static Value m_pow(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "pow") || !check_num(vm, a[1], "pow")) return GHOST_VAL;
    return vm_numeric_pow(vm, a[0], a[1]);
}

static Value m_log(VM *vm, Value *a, int argc) {
    if (!check_num(vm, a[0], "log")) return GHOST_VAL;
    double x = as_double(a[0]);
    if (x <= 0) {
        vm_throw_native(vm, "MathAintMathin", "log of a non-positive numba.");
        return GHOST_VAL;
    }
    double base = FUNNY_E;
    if (argc > 1) {
        if (!check_num(vm, a[1], "log")) return GHOST_VAL;
        base = as_double(a[1]);
    }
    return FLOAT_VAL(log(x) / log(base));
}

static Value m_log2(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "log2")) return GHOST_VAL;
    double x = as_double(a[0]);
    if (x <= 0) {
        vm_throw_native(vm, "MathAintMathin", "log2 of a non-positive numba.");
        return GHOST_VAL;
    }
    return FLOAT_VAL(log2(x));
}

static Value m_log10(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "log10")) return GHOST_VAL;
    double x = as_double(a[0]);
    if (x <= 0) {
        vm_throw_native(vm, "MathAintMathin", "log10 of a non-positive numba.");
        return GHOST_VAL;
    }
    return FLOAT_VAL(log10(x));
}

static Value m_exp(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "exp")) return GHOST_VAL;
    return FLOAT_VAL(exp(as_double(a[0])));
}

static Value m_sin(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "sin")) return GHOST_VAL;
    return FLOAT_VAL(sin(as_double(a[0])));
}

static Value m_cos(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "cos")) return GHOST_VAL;
    return FLOAT_VAL(cos(as_double(a[0])));
}

static Value m_tan(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "tan")) return GHOST_VAL;
    return FLOAT_VAL(tan(as_double(a[0])));
}

static Value m_atan2(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "atan2") || !check_num(vm, a[1], "atan2")) return GHOST_VAL;
    return FLOAT_VAL(atan2(as_double(a[0]), as_double(a[1])));
}

static Value m_hypot(VM *vm, Value *a, int argc) {
    double sumSq = 0;
    for (int i = 0; i < argc; i++) {
        if (!check_num(vm, a[i], "hypot")) return GHOST_VAL;
        double d = as_double(a[i]);
        sumSq += d * d;
    }
    return FLOAT_VAL(sqrt(sumSq));
}

static Value m_clamp(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "clamp") || !check_num(vm, a[1], "clamp") || !check_num(vm, a[2], "clamp")) return GHOST_VAL;
    Value x = a[0], lo = a[1], hi = a[2];
    Value clampedLow = numeric_less(x, lo) ? lo : x;
    return numeric_less(hi, clampedLow) ? hi : clampedLow;
}

static Value m_sign(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "sign")) return GHOST_VAL;
    double x = as_double(a[0]);
    return INT_VAL((x > 0) - (x < 0));
}

static int64_t gcd64(int64_t a, int64_t b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

static Value m_gcd(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "gcd") || !check_num(vm, a[1], "gcd")) return GHOST_VAL;
    return INT_VAL(gcd64((int64_t)as_double(a[0]), (int64_t)as_double(a[1])));
}

static Value m_lcm(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "lcm") || !check_num(vm, a[1], "lcm")) return GHOST_VAL;
    int64_t x = (int64_t)as_double(a[0]), y = (int64_t)as_double(a[1]);
    if (x == 0 || y == 0) return INT_VAL(0);
    int64_t g = gcd64(x, y);
    int64_t absX = x < 0 ? -x : x, absY = y < 0 ? -y : y;
    return INT_VAL((absX / g) * absY);
}

static Value m_is_prime(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "is_prime")) return GHOST_VAL;
    int64_t n = (int64_t)as_double(a[0]);
    if (n < 2) return BOOL_VAL(false);
    if (n == 2 || n == 3) return BOOL_VAL(true);
    if (n % 2 == 0) return BOOL_VAL(false);
    for (int64_t i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

static Value m_factorial(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_num(vm, a[0], "factorial")) return GHOST_VAL;
    int64_t n = (int64_t)as_double(a[0]);
    if (n < 0) {
        vm_throw_native(vm, "MathAintMathin", "factorial of a negative numba.");
        return GHOST_VAL;
    }
    ObjBignum *result = bignum_from_int64(1);
    for (int64_t i = 2; i <= n; i++) {
        ObjBignum *factor = bignum_from_int64(i);
        ObjBignum *next = bignum_mul(result, factor);
        bignum_free(result);
        bignum_free(factor);
        result = next;
    }
    int64_t asInt;
    if (bignum_to_int64(result, &asInt)) {
        bignum_free(result);
        return INT_VAL(asInt);
    }
    gc_track(&vm->gc, (Obj *)result, sizeof(ObjBignum));
    return OBJ_VAL(result);
}

/* -- numba's own instance methods (bound via GET_PROP, e.g. `(5.5).floor()`) */

static Value nm_to_yap(VM *vm, Value *a, int argc) {
    (void)argc;
    char *disp = vm_value_to_display(vm, a[0]);
    ObjString *r = string_new(&vm->gc, disp, (uint32_t)strlen(disp));
    free(disp);
    return OBJ_VAL(r);
}

static Value nm_abs(VM *vm, Value *a, int argc) {
    (void)argc;
    return numba_abs(vm, a[0]);
}

static Value nm_floor(VM *vm, Value *a, int argc) {
    (void)argc;
    return numba_floor(vm, a[0]);
}

static Value nm_ceil(VM *vm, Value *a, int argc) {
    (void)argc;
    return numba_ceil(vm, a[0]);
}

static Value nm_round(VM *vm, Value *a, int argc) {
    return numba_round(vm, a[0], argc > 1 ? a[1] : GHOST_VAL);
}

static Value nm_is_whole(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)argc;
    if (!IS_FLOAT(a[0])) return BOOL_VAL(true);
    double d = AS_FLOAT(a[0]);
    return BOOL_VAL(d == trunc(d));
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} MafsEntry;

static const MafsEntry NUMBA_METHOD_TABLE[] = {
    {"to_yap", nm_to_yap, 0, 0},
    {"abs", nm_abs, 0, 0},
    {"floor", nm_floor, 0, 0},
    {"ceil", nm_ceil, 0, 0},
    {"round", nm_round, 0, 1},
    {"is_whole", nm_is_whole, 0, 0},
};
#define NUMBA_METHOD_TABLE_COUNT (int)(sizeof(NUMBA_METHOD_TABLE) / sizeof(NUMBA_METHOD_TABLE[0]))

NativeMethodFn numba_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < NUMBA_METHOD_TABLE_COUNT; i++) {
        if (strcmp(NUMBA_METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = NUMBA_METHOD_TABLE[i].minArity;
            *outMaxArity = NUMBA_METHOD_TABLE[i].maxArity;
            return NUMBA_METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}

static const MafsEntry MAFS_FUNCTIONS[] = {
    {"sqrt", m_sqrt, 1, 1},
    {"abs", m_abs, 1, 1},
    {"is_float", m_is_float, 1, 1},
    {"float_to_bits", m_float_to_bits, 1, 1},
    {"bits_to_float", m_bits_to_float, 1, 1},
    {"floor", m_floor, 1, 1},
    {"ceil", m_ceil, 1, 1},
    {"round", m_round, 1, 2},
    {"min", m_min, 1, 255},
    {"max", m_max, 1, 255},
    {"pow", m_pow, 2, 2},
    {"log", m_log, 1, 2},
    {"log2", m_log2, 1, 1},
    {"log10", m_log10, 1, 1},
    {"exp", m_exp, 1, 1},
    {"sin", m_sin, 1, 1},
    {"cos", m_cos, 1, 1},
    {"tan", m_tan, 1, 1},
    {"atan2", m_atan2, 2, 2},
    {"hypot", m_hypot, 1, 255},
    {"clamp", m_clamp, 3, 3},
    {"sign", m_sign, 1, 1},
    {"gcd", m_gcd, 2, 2},
    {"lcm", m_lcm, 2, 2},
    {"is_prime", m_is_prime, 1, 1},
    {"factorial", m_factorial, 1, 1},
};
#define MAFS_FUNCTIONS_COUNT (int)(sizeof(MAFS_FUNCTIONS) / sizeof(MAFS_FUNCTIONS[0]))

Value mafs_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < MAFS_FUNCTIONS_COUNT; i++) {
        const MafsEntry *e = &MAFS_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    /* A plain local array, deliberately not `static const`: sqrt(5.0) is
       a function call, not a constant expression, so it can't initialize
       a static/file-scope array portably -- a local one has no such
       restriction. */
    const struct {
        const char *name;
        double value;
    } constants[] = {
        {"skibidi_pi", FUNNY_PI},
        {"e", FUNNY_E},
        {"phi", (1.0 + sqrt(5.0)) / 2.0},
        {"infinity", HUGE_VAL},
        {"nan", NAN},
    };
    for (int i = 0; i < (int)(sizeof(constants) / sizeof(constants[0])); i++) {
        ObjString *name = string_new(&vm->gc, constants[i].name, (uint32_t)strlen(constants[i].name));
        groupchat_set(&vm->gc, members, OBJ_VAL(name), FLOAT_VAL(constants[i].value));
    }
    ObjString *moduleName = string_new(&vm->gc, "mafs", 4);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
