/* native/vm.c -- see vm.h. Arithmetic semantics mirror funnylang/vm.py's
 * _add/_mul/_check_num2/_bitwise/_compare exactly (PLAN.md §3.2/§4.1),
 * extended with fixnum<->bignum promotion, which Python's own unbounded
 * int never needed. Booleans are never numeric for + - * or / (PLAN.md §16),
 * but *are* int-like for bitwise ops (Python's bool is a subtype of int,
 * and funnylang/vm.py's _is_int_like inherits that) -- IS_INT_LIKE below
 * exists specifically to keep that asymmetry correct.
 */
#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "numfmt.h"
#include "opcodes.h"
#include "string.h"

#define IS_INT_LIKE(v) (IS_INT(v) || IS_BOOL(v))

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    memcpy(r, s, n);
    return r;
}

static int64_t as_int64_like(Value v) {
    return IS_BOOL(v) ? (AS_BOOL(v) ? 1 : 0) : AS_INT(v);
}

static double value_to_double(Value v) {
    if (IS_INT(v)) return (double)AS_INT(v);
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    if (IS_BIGNUM(v)) return bignum_to_double(AS_BIGNUM(v));
    return 0.0; /* unreachable when callers check IS_NUM first */
}

static ObjBignum *to_bignum_owned(Value v, bool *owned) {
    if (IS_BIGNUM(v)) {
        *owned = false;
        return AS_BIGNUM(v);
    }
    *owned = true;
    return bignum_from_int64(IS_BOOL(v) ? as_int64_like(v) : AS_INT(v));
}

static char *funny_format_float(double v) {
    if (isnan(v)) return dup_str("nan");
    if (isinf(v)) return dup_str(v > 0 ? "infinity" : "-infinity");
    if (v == trunc(v) && fabs(v) < 1e16) {
        char buf[32];
        snprintf(buf, sizeof buf, "%.1f", v);
        return dup_str(buf);
    }
    return numfmt_repr(v);
}

static char *value_to_display(Value v) {
    if (IS_GHOST(v)) return dup_str("ghost");
    if (IS_BOOL(v)) return dup_str(AS_BOOL(v) ? "fax" : "cap");
    if (IS_INT(v)) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)AS_INT(v));
        return dup_str(buf);
    }
    if (IS_BIGNUM(v)) return bignum_to_decimal_string(AS_BIGNUM(v));
    if (IS_FLOAT(v)) return funny_format_float(AS_FLOAT(v));
    if (IS_STRING(v)) return dup_str(AS_STRING(v)->chars);
    return dup_str("<obj>"); /* unreachable for N2's value set */
}

/* -- setup / teardown ------------------------------------------------- */

#define INITIAL_STACK_CAPACITY 256
#define INITIAL_GLOBALS_CAPACITY 8

void vm_init(VM *vm) {
    gc_init(&vm->gc);
    vm->stackCapacity = INITIAL_STACK_CAPACITY;
    vm->stack = (Value *)malloc((size_t)vm->stackCapacity * sizeof(Value));
    vm->stackCount = 0;
    vm->globalCapacity = INITIAL_GLOBALS_CAPACITY;
    vm->globals = (GlobalEntry *)malloc((size_t)vm->globalCapacity * sizeof(GlobalEntry));
    vm->globalCount = 0;
    vm->unit = NULL;
    vm->proto = NULL;
    vm->ip = 0;
    vm->out = NULL;
    vm->hadError = false;
    vm->errorFlavor = NULL;
    vm->errorMessage = NULL;
}

void vm_destroy(VM *vm) {
    free(vm->stack);
    free(vm->globals);
    free(vm->errorFlavor);
    free(vm->errorMessage);
    gc_destroy(&vm->gc);
}

/* -- roots -------------------------------------------------------------- */

static void mark_vm_roots(GC *gc, void *userdata) {
    VM *vm = (VM *)userdata;
    for (int i = 0; i < vm->stackCount; i++) gc_mark_value(gc, vm->stack[i]);
    for (int i = 0; i < vm->globalCount; i++) {
        gc_mark_object(gc, (Obj *)vm->globals[i].name);
        gc_mark_value(gc, vm->globals[i].value);
    }
    /* Every constant the running unit's proto can CONST-push is already
       reachable only through this unit for N2's single-proto scope; since
       the unit itself isn't a GC object (it's owned by the caller, freed
       separately via chunk_free_unit), its string/bignum constants must be
       rooted here explicitly or a mid-run collection could sweep a literal
       out from under a CONST that hasn't executed yet. */
    if (vm->unit != NULL) {
        for (uint32_t i = 0; i < vm->unit->constCount; i++) {
            gc_mark_value(gc, vm->unit->consts[i].value);
        }
    }
}

/* -- stack ---------------------------------------------------------------- */

static void push(VM *vm, Value v) {
    if (vm->stackCount == vm->stackCapacity) {
        vm->stackCapacity *= 2;
        vm->stack = (Value *)realloc(vm->stack, (size_t)vm->stackCapacity * sizeof(Value));
    }
    vm->stack[vm->stackCount++] = v;
}

static Value pop(VM *vm) {
    return vm->stack[--vm->stackCount];
}

static Value peek(VM *vm, int distance) {
    return vm->stack[vm->stackCount - 1 - distance];
}

/* -- globals -------------------------------------------------------------- */

static GlobalEntry *globals_find(VM *vm, ObjString *name) {
    for (int i = 0; i < vm->globalCount; i++) {
        if (string_equal(vm->globals[i].name, name)) return &vm->globals[i];
    }
    return NULL;
}

static void globals_set(VM *vm, ObjString *name, Value value) {
    GlobalEntry *existing = globals_find(vm, name);
    if (existing != NULL) {
        existing->value = value;
        return;
    }
    if (vm->globalCount == vm->globalCapacity) {
        vm->globalCapacity *= 2;
        vm->globals = (GlobalEntry *)realloc(vm->globals, (size_t)vm->globalCapacity * sizeof(GlobalEntry));
    }
    vm->globals[vm->globalCount].name = name;
    vm->globals[vm->globalCount].value = value;
    vm->globalCount++;
}

/* -- errors ----------------------------------------------------------------- */

static void runtime_error(VM *vm, const char *flavor, const char *fmt, ...) {
    if (vm->hadError) return; /* first error wins */
    vm->hadError = true;
    vm->errorFlavor = dup_str(flavor);
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    vm->errorMessage = dup_str(buf);
}

/* -- numeric helpers -------------------------------------------------------- */

static bool add_overflows_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return true;
    *out = a + b;
    return false;
}

static bool sub_overflows_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return true;
    *out = a - b;
    return false;
}

static bool mul_overflows_i64(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return false;
    }
    if (a == INT64_MIN || b == INT64_MIN) return true;
    int64_t absA = a < 0 ? -a : a;
    int64_t absB = b < 0 ? -b : b;
    if (absA > INT64_MAX / absB) return true;
    *out = a * b;
    return false;
}

static Value bignum_result(VM *vm, ObjBignum *r) {
    int64_t asInt;
    if (bignum_to_int64(r, &asInt)) {
        bignum_free(r);
        return INT_VAL(asInt);
    }
    gc_track(&vm->gc, (Obj *)r, sizeof(ObjBignum));
    return OBJ_VAL(r);
}

static double float_floor_div(double a, double b) {
    return floor(a / b);
}

static double float_floor_mod(double a, double b) {
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

/* -- binary arithmetic (ADD..MOD, POW) --------------------------------------- */

static Value vm_add(VM *vm, Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r;
        if (!add_overflows_i64(AS_INT(a), AS_INT(b), &r)) return INT_VAL(r);
    }
    if (IS_NUM(a) && IS_NUM(b)) {
        if (IS_FLOAT(a) || IS_FLOAT(b)) return FLOAT_VAL(value_to_double(a) + value_to_double(b));
        bool oa, ob;
        ObjBignum *ba = to_bignum_owned(a, &oa);
        ObjBignum *bb = to_bignum_owned(b, &ob);
        ObjBignum *r = bignum_add(ba, bb);
        if (oa) bignum_free(ba);
        if (ob) bignum_free(bb);
        return bignum_result(vm, r);
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a);
        ObjString *sb = AS_STRING(b);
        char *buf = (char *)malloc((size_t)sa->byteLen + sb->byteLen);
        memcpy(buf, sa->chars, sa->byteLen);
        memcpy(buf + sa->byteLen, sb->chars, sb->byteLen);
        ObjString *r = string_new(&vm->gc, buf, sa->byteLen + sb->byteLen);
        free(buf);
        return OBJ_VAL(r);
    }
    runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
    return GHOST_VAL;
}

static Value vm_sub(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r;
        if (!sub_overflows_i64(AS_INT(a), AS_INT(b), &r)) return INT_VAL(r);
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) return FLOAT_VAL(value_to_double(a) - value_to_double(b));
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    ObjBignum *r = bignum_sub(ba, bb);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return bignum_result(vm, r);
}

static Value vm_mul(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (IS_INT(a) && IS_INT(b)) {
        int64_t r;
        if (!mul_overflows_i64(AS_INT(a), AS_INT(b), &r)) return INT_VAL(r);
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) return FLOAT_VAL(value_to_double(a) * value_to_double(b));
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    ObjBignum *r = bignum_mul(ba, bb);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return bignum_result(vm, r);
}

static bool numeric_is_zero(Value v) {
    if (IS_INT(v)) return AS_INT(v) == 0;
    if (IS_FLOAT(v)) return AS_FLOAT(v) == 0.0;
    if (IS_BIGNUM(v)) return bignum_is_zero(AS_BIGNUM(v));
    return false;
}

static Value vm_div(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        runtime_error(vm, "MathAintMathin", "division by zero.");
        return GHOST_VAL;
    }
    return FLOAT_VAL(value_to_double(a) / value_to_double(b));
}

static Value vm_idiv(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        runtime_error(vm, "MathAintMathin", "division by zero.");
        return GHOST_VAL;
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) return FLOAT_VAL(float_floor_div(value_to_double(a), value_to_double(b)));
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    ObjBignum *q, *r;
    bignum_divmod_floor(ba, bb, &q, &r);
    bignum_free(r);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return bignum_result(vm, q);
}

static Value vm_mod(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        runtime_error(vm, "MathAintMathin", "mod by zero.");
        return GHOST_VAL;
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) return FLOAT_VAL(float_floor_mod(value_to_double(a), value_to_double(b)));
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    ObjBignum *q, *r;
    bignum_divmod_floor(ba, bb, &q, &r);
    bignum_free(q);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return bignum_result(vm, r);
}

static Value vm_pow(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        runtime_error(vm, "TypeVibeMismatch", "a value and a value do NOT have the same energy.");
        return GHOST_VAL;
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) {
        double da = value_to_double(a), db = value_to_double(b);
        if (da == 0.0 && db < 0.0) {
            runtime_error(vm, "MathAintMathin", "0 to a negative power.");
            return GHOST_VAL;
        }
        return FLOAT_VAL(pow(da, db));
    }
    int64_t exp;
    if (IS_BIGNUM(b)) {
        if (!bignum_to_int64(AS_BIGNUM(b), &exp)) {
            runtime_error(vm, "MathAintMathin", "exponent too large.");
            return GHOST_VAL;
        }
    } else {
        exp = AS_INT(b);
    }
    if (exp < 0) {
        double da = value_to_double(a);
        if (da == 0.0) {
            runtime_error(vm, "MathAintMathin", "0 to a negative power.");
            return GHOST_VAL;
        }
        return FLOAT_VAL(pow(da, (double)exp));
    }
    bool oa;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *r = bignum_pow(ba, exp);
    if (oa) bignum_free(ba);
    return bignum_result(vm, r);
}

/* -- unary -------------------------------------------------------------- */

static Value vm_neg(VM *vm, Value a) {
    if (IS_INT(a)) {
        if (AS_INT(a) != INT64_MIN) return INT_VAL(-AS_INT(a));
        ObjBignum *b = bignum_from_int64(AS_INT(a));
        ObjBignum *r = bignum_negate(b);
        bignum_free(b);
        return bignum_result(vm, r);
    }
    if (IS_FLOAT(a)) return FLOAT_VAL(-AS_FLOAT(a));
    if (IS_BIGNUM(a)) return bignum_result(vm, bignum_negate(AS_BIGNUM(a)));
    runtime_error(vm, "TypeVibeMismatch", "can't negate that.");
    return GHOST_VAL;
}

/* -- bitwise (ints/bools; bignum when either operand already is one) -------- */

static Value vm_bnot(VM *vm, Value a) {
    if (IS_INT_LIKE(a)) return INT_VAL(~as_int64_like(a));
    if (IS_BIGNUM(a)) return bignum_result(vm, bignum_bnot(AS_BIGNUM(a)));
    runtime_error(vm, "TypeVibeMismatch", "bitwise ops need whole numbas.");
    return GHOST_VAL;
}

typedef enum { BIT_AND, BIT_OR, BIT_XOR } BitOp;

static Value vm_bitwise(VM *vm, BitOp op, Value a, Value b) {
    if (!((IS_INT_LIKE(a) || IS_BIGNUM(a)) && (IS_INT_LIKE(b) || IS_BIGNUM(b)))) {
        runtime_error(vm, "TypeVibeMismatch", "bitwise ops need whole numbas.");
        return GHOST_VAL;
    }
    if (!IS_BIGNUM(a) && !IS_BIGNUM(b)) {
        int64_t ia = as_int64_like(a), ib = as_int64_like(b);
        int64_t r;
        switch (op) {
            case BIT_AND: r = ia & ib; break;
            case BIT_OR: r = ia | ib; break;
            default: r = ia ^ ib; break; /* BIT_XOR */
        }
        /* Python's bool overrides &/|/^ to stay bool when *both* operands
           are bool (True & False is False, not 0) -- but reverts to plain
           int the moment either side isn't (True & 5 is 1). vm.py's
           _bitwise inherits this for free from Python's own operators;
           the C VM has to replicate it explicitly. ~ never preserves bool
           in Python either (~True is -2), so vm_bnot doesn't need this. */
        if (IS_BOOL(a) && IS_BOOL(b)) return BOOL_VAL(r != 0);
        return INT_VAL(r);
    }
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    ObjBignum *r = op == BIT_AND ? bignum_band(ba, bb) : op == BIT_OR ? bignum_bor(ba, bb) : bignum_bxor(ba, bb);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return bignum_result(vm, r);
}

static Value vm_shift(VM *vm, bool isLeft, Value a, Value b) {
    if (!((IS_INT_LIKE(a) || IS_BIGNUM(a)) && (IS_INT_LIKE(b) || IS_BIGNUM(b)))) {
        runtime_error(vm, "TypeVibeMismatch", "bitwise ops need whole numbas.");
        return GHOST_VAL;
    }
    int64_t shiftAmt;
    if (IS_BIGNUM(b)) {
        if (!bignum_to_int64(AS_BIGNUM(b), &shiftAmt)) {
            runtime_error(vm, "MathAintMathin", "shift amount too large.");
            return GHOST_VAL;
        }
    } else {
        shiftAmt = as_int64_like(b);
    }
    if (shiftAmt < 0) {
        runtime_error(vm, "MathAintMathin", "negative shift amount.");
        return GHOST_VAL;
    }
    /* Always routed through bignum for both directions: uniformly correct
       and portable regardless of shift width, with no reliance on C's
       implementation-defined behavior for shifts >= 64 bits or shifting a
       negative value (bignum_shr already implements Python's floor-toward-
       -infinity semantics exactly -- see bignum.h). SHL can also grow
       past int64 range for shifts as small as ~64, so it needs the bignum
       path regardless; SHR alone would be safe as a native shift, but
       sharing this path keeps the logic in one place. */
    bool oa;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *r = isLeft ? bignum_shl(ba, shiftAmt) : bignum_shr(ba, shiftAmt);
    if (oa) bignum_free(ba);
    return bignum_result(vm, r);
}

/* -- comparisons -------------------------------------------------------- */

static int compare_numeric(Value a, Value b) {
    if (IS_FLOAT(a) || IS_FLOAT(b)) {
        double da = value_to_double(a), db = value_to_double(b);
        return da < db ? -1 : (da > db ? 1 : 0);
    }
    if (!IS_BIGNUM(a) && !IS_BIGNUM(b)) {
        int64_t ia = AS_INT(a), ib = AS_INT(b);
        return ia < ib ? -1 : (ia > ib ? 1 : 0);
    }
    bool oa, ob;
    ObjBignum *ba = to_bignum_owned(a, &oa);
    ObjBignum *bb = to_bignum_owned(b, &ob);
    int cmp = bignum_compare(ba, bb);
    if (oa) bignum_free(ba);
    if (ob) bignum_free(bb);
    return cmp;
}

typedef enum { CMP_LT, CMP_LE, CMP_GT, CMP_GE } CmpOp;

static Value vm_compare(VM *vm, CmpOp op, Value a, Value b) {
    int cmp;
    if (IS_NUM(a) && IS_NUM(b)) {
        cmp = compare_numeric(a, b);
    } else if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
        uint32_t minLen = sa->byteLen < sb->byteLen ? sa->byteLen : sb->byteLen;
        int c = minLen ? memcmp(sa->chars, sb->chars, minLen) : 0;
        /* UTF-8's byte ordering matches codepoint ordering for valid UTF-8,
           so a plain memcmp already gives the right answer for equal-
           length-prefix strings; a length tiebreak (like C strcmp on NUL-
           free buffers) covers the case where one is a prefix of the other. */
        cmp = c != 0 ? (c < 0 ? -1 : 1) : (sa->byteLen < sb->byteLen ? -1 : (sa->byteLen > sb->byteLen ? 1 : 0));
    } else {
        runtime_error(vm, "TypeVibeMismatch", "can't compare those.");
        return GHOST_VAL;
    }
    switch (op) {
        case CMP_LT: return BOOL_VAL(cmp < 0);
        case CMP_LE: return BOOL_VAL(cmp <= 0);
        case CMP_GT: return BOOL_VAL(cmp > 0);
        case CMP_GE: return BOOL_VAL(cmp >= 0);
    }
    return GHOST_VAL; /* unreachable */
}

/* -- the dispatch loop ---------------------------------------------------- */

static uint16_t read_u16(const uint8_t *code, uint32_t ip) {
    return (uint16_t)((code[ip] << 8) | code[ip + 1]);
}

static uint32_t read_u32(const uint8_t *code, uint32_t ip) {
    return ((uint32_t)code[ip] << 24) | ((uint32_t)code[ip + 1] << 16) | ((uint32_t)code[ip + 2] << 8) | code[ip + 3];
}

VmResult vm_run(VM *vm, CompiledUnit *unit, FILE *out) {
    vm->unit = unit;
    vm->proto = &unit->protos[unit->entryProto];
    vm->ip = 0;
    vm->out = out;
    vm->gc.markExternalRoots = mark_vm_roots;
    vm->gc.externalRootsUserdata = vm;

    const uint8_t *code = vm->proto->code;
    ConstEntry *consts = unit->consts;

    while (!vm->hadError) {
        gc_maybe_collect(&vm->gc);
        uint8_t op = code[vm->ip++];
        switch ((Op)op) {
            case OP_NOP:
                break;
            case OP_CONST: {
                uint16_t idx = read_u16(code, vm->ip);
                vm->ip += 2;
                push(vm, consts[idx].value);
                break;
            }
            case OP_GHOST:
                push(vm, GHOST_VAL);
                break;
            case OP_FAX:
                push(vm, BOOL_VAL(true));
                break;
            case OP_CAP:
                push(vm, BOOL_VAL(false));
                break;
            case OP_POP:
                pop(vm);
                break;
            case OP_DUP:
                push(vm, peek(vm, 0));
                break;
            case OP_SWAP: {
                Value tmp = vm->stack[vm->stackCount - 1];
                vm->stack[vm->stackCount - 1] = vm->stack[vm->stackCount - 2];
                vm->stack[vm->stackCount - 2] = tmp;
                break;
            }
            case OP_GET_LOCAL:
                push(vm, vm->stack[code[vm->ip++]]);
                break;
            case OP_SET_LOCAL:
                vm->stack[code[vm->ip++]] = peek(vm, 0);
                break;
            case OP_GET_GLOBAL: {
                uint16_t idx = read_u16(code, vm->ip);
                vm->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                GlobalEntry *g = globals_find(vm, name);
                if (g == NULL) {
                    runtime_error(vm, "WhoDis", "'%s' isn't defined.", name->chars);
                    break;
                }
                push(vm, g->value);
                break;
            }
            case OP_SET_GLOBAL: {
                uint16_t idx = read_u16(code, vm->ip);
                vm->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                globals_set(vm, name, peek(vm, 0));
                break;
            }
            case OP_DEF_GLOBAL: {
                uint16_t idx = read_u16(code, vm->ip);
                vm->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                globals_set(vm, name, pop(vm));
                break;
            }
            case OP_ADD: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_add(vm, a, b);
                break;
            }
            case OP_SUB: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_sub(vm, a, b);
                break;
            }
            case OP_MUL: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_mul(vm, a, b);
                break;
            }
            case OP_DIV: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_div(vm, a, b);
                break;
            }
            case OP_IDIV: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_idiv(vm, a, b);
                break;
            }
            case OP_MOD: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_mod(vm, a, b);
                break;
            }
            case OP_POW: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_pow(vm, a, b);
                break;
            }
            case OP_NEG:
                vm->stack[vm->stackCount - 1] = vm_neg(vm, peek(vm, 0));
                break;
            case OP_NOT:
                vm->stack[vm->stackCount - 1] = BOOL_VAL(!value_is_truthy(peek(vm, 0)));
                break;
            case OP_BNOT:
                vm->stack[vm->stackCount - 1] = vm_bnot(vm, peek(vm, 0));
                break;
            case OP_BAND: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_bitwise(vm, BIT_AND, a, b);
                break;
            }
            case OP_BOR: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_bitwise(vm, BIT_OR, a, b);
                break;
            }
            case OP_BXOR: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_bitwise(vm, BIT_XOR, a, b);
                break;
            }
            case OP_SHL: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_shift(vm, true, a, b);
                break;
            }
            case OP_SHR: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_shift(vm, false, a, b);
                break;
            }
            case OP_EQ: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = BOOL_VAL(value_equal_narrow(a, b));
                break;
            }
            case OP_NEQ: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = BOOL_VAL(!value_equal_narrow(a, b));
                break;
            }
            case OP_LT: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_compare(vm, CMP_LT, a, b);
                break;
            }
            case OP_LE: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_compare(vm, CMP_LE, a, b);
                break;
            }
            case OP_GT: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_compare(vm, CMP_GT, a, b);
                break;
            }
            case OP_GE: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = vm_compare(vm, CMP_GE, a, b);
                break;
            }
            case OP_JUMP: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2 + off;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2;
                Value cond = pop(vm);
                if (!value_is_truthy(cond)) vm->ip += off;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2;
                Value cond = pop(vm);
                if (value_is_truthy(cond)) vm->ip += off;
                break;
            }
            case OP_JUMP_IF_FALSE_KEEP: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2;
                if (!value_is_truthy(peek(vm, 0))) vm->ip += off;
                break;
            }
            case OP_JUMP_IF_TRUE_KEEP: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2;
                if (value_is_truthy(peek(vm, 0))) vm->ip += off;
                break;
            }
            case OP_JUMP_IF_GHOST_KEEP: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2;
                if (IS_GHOST(peek(vm, 0))) vm->ip += off;
                break;
            }
            case OP_LOOP: {
                uint16_t off = read_u16(code, vm->ip);
                vm->ip += 2 - off;
                break;
            }
            case OP_JUMP_LONG: {
                uint32_t off = read_u32(code, vm->ip);
                vm->ip += 4 + off;
                break;
            }
            case OP_LOOP_LONG: {
                uint32_t off = read_u32(code, vm->ip);
                vm->ip += 4 - off;
                break;
            }
            case OP_YAP: {
                uint8_t argc = code[vm->ip++];
                uint8_t newline = code[vm->ip++];
                char *parts[256];
                for (int i = argc - 1; i >= 0; i--) parts[i] = value_to_display(pop(vm));
                for (int i = 0; i < argc; i++) {
                    if (i > 0) fputc(' ', vm->out);
                    fputs(parts[i], vm->out);
                    free(parts[i]);
                }
                if (newline) fputc('\n', vm->out);
                break;
            }
            case OP_HALT:
                return vm->hadError ? VM_ERROR : VM_OK;
            case OP_RETURN:
                pop(vm); /* the script's own implicit return value; N2 has no caller to hand it to */
                return vm->hadError ? VM_ERROR : VM_OK;
            default:
                runtime_error(vm, "SkillIssue", "opcode %d not implemented until a later milestone.", op);
                break;
        }
    }
    return VM_ERROR;
}
