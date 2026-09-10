#include "builtins.h"

#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "error.h"
#include "gc.h"
#include "groupchat.h"
#include "squad.h"
#include "stash.h"
#include "string.h"
#include "vm.h"

/* -- combo() -------------------------------------------------------- */

ObjCombo *combo_new(GC *gc, Value fns) {
    ObjCombo *c = (ObjCombo *)malloc(sizeof(ObjCombo));
    c->obj.type = OBJ_COMBO;
    c->obj.marked = false;
    c->obj.size = 0;
    c->obj.next = NULL;
    c->fns = fns;
    gc_track(gc, (Obj *)c, sizeof(ObjCombo));
    return c;
}

Value combo_call(VM *vm, ObjCombo *combo, Value arg) {
    ObjStash *fns = (ObjStash *)AS_OBJ(combo->fns);
    Value value = arg;
    for (int i = 0; i < fns->count && !vm->hadError; i++) {
        Value callArgs[1] = {value};
        value = vm_call_value(vm, fns->items[i], callArgs, 1);
    }
    return vm->hadError ? GHOST_VAL : value;
}

/* -- the rest of funnylang/stdlib/builtins.py ------------------------ */

static ObjBignum *bignum_owned_of(Value v, bool *owned) {
    if (IS_BIGNUM(v)) {
        *owned = false;
        return AS_BIGNUM(v);
    }
    *owned = true;
    return bignum_from_int64(IS_BOOL(v) ? (AS_BOOL(v) ? 1 : 0) : AS_INT(v));
}

static Value m_how_thicc(VM *vm, Value *a, int argc) {
    (void)argc;
    Value x = a[0];
    if (IS_OBJ(x) && AS_OBJ(x)->type == OBJ_STASH) return INT_VAL(((ObjStash *)AS_OBJ(x))->count);
    if (IS_OBJ(x) && AS_OBJ(x)->type == OBJ_GROUPCHAT) return INT_VAL(((ObjGroupChat *)AS_OBJ(x))->count);
    if (IS_STRING(x)) return INT_VAL(AS_STRING(x)->codepointCount);
    if (IS_OBJ(x) && AS_OBJ(x)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(x);
        ObjClosure *method = squad_find_method(inst->squad, "how_thicc");
        if (method != NULL) {
            Value args[1] = {x};
            return vm_call_value(vm, OBJ_VAL(method), args, 1);
        }
        vm_throw_native(vm, "WhoDis", "'%s' doesn't do 'how_thicc'.", inst->squad->name->chars);
        return GHOST_VAL;
    }
    vm_throw_native(vm, "TypeVibeMismatch", "a %s doesn't have a length.", vm_type_name(x));
    return GHOST_VAL;
}

static Value m_what_is_it(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *name = vm_type_name(a[0]);
    return OBJ_VAL(string_new(&vm->gc, name, (uint32_t)strlen(name)));
}

static Value m_to_yap(VM *vm, Value *a, int argc) {
    (void)argc;
    size_t len;
    char *disp = vm_value_to_display_len(vm, a[0], &len);
    ObjString *r = string_new(&vm->gc, disp, (uint32_t)len);
    free(disp);
    return OBJ_VAL(r);
}

/* Shared by to_numba/to_int: strips ASCII whitespace, detects a 0x/0b/0o
   prefix (else base 10), and tries a strict bignum parse of the whole
   remaining string via bignum_from_digits (N1) -- exactly matching
   Python's own `int(s, 0)`, arbitrary-precision included, since a
   fixnum-only parse would silently truncate a numba literal too big for
   int64. Returns NULL (no allocation) if the string isn't a valid
   integer in that base at all -- the caller falls back to a float parse. */
static ObjBignum *parse_int_bignum(const char *s, size_t len, bool *outNegative) {
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    size_t end = len;
    while (end > i && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) end--;
    bool negative = false;
    if (i < end && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        i++;
    }
    int base = 10;
    if (end - i >= 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (end - i >= 2 && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) {
        base = 2;
        i += 2;
    } else if (end - i >= 2 && s[i] == '0' && (s[i + 1] == 'o' || s[i + 1] == 'O')) {
        base = 8;
        i += 2;
    }
    *outNegative = negative;
    return bignum_from_digits(s + i, (int)(end - i), base, negative);
}

static Value bignum_to_numba(GC *gc, ObjBignum *n) {
    int64_t asInt;
    if (bignum_to_int64(n, &asInt)) {
        bignum_free(n);
        return INT_VAL(asInt);
    }
    gc_track(gc, (Obj *)n, sizeof(ObjBignum));
    return OBJ_VAL(n);
}

/* Exposed (builtins.h) so yapper.c's own `to_numba` instance method --
   the same underlying Python function in funnylang/stdlib/builtins.py,
   just re-exported there as `yapper._to_numba` -- can call the identical
   logic instead of duplicating it. */
Value to_numba_value(VM *vm, Value x) {
    if (IS_BOOL(x)) return INT_VAL(AS_BOOL(x) ? 1 : 0);
    if (IS_NUM(x)) return x;
    if (IS_STRING(x)) {
        ObjString *s = AS_STRING(x);
        bool negative;
        ObjBignum *n = parse_int_bignum(s->chars, s->byteLen, &negative);
        if (n != NULL) return bignum_to_numba(&vm->gc, n);
        char *endPtr;
        double d = strtod(s->chars, &endPtr);
        if (endPtr != s->chars) {
            while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\n' || *endPtr == '\r') endPtr++;
            if (*endPtr == '\0') return FLOAT_VAL(d);
        }
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' isn't a numba.", s->chars);
        return GHOST_VAL;
    }
    vm_throw_native(vm, "TypeVibeMismatch", "can't turn a %s into a numba.", vm_type_name(x));
    return GHOST_VAL;
}

static Value m_to_numba(VM *vm, Value *a, int argc) {
    (void)argc;
    return to_numba_value(vm, a[0]);
}

static Value m_to_int(VM *vm, Value *a, int argc) {
    (void)argc;
    Value x = a[0];
    if (IS_BOOL(x)) return INT_VAL(AS_BOOL(x) ? 1 : 0);
    if (IS_INT(x) || IS_BIGNUM(x)) return x;
    if (IS_FLOAT(x)) {
        double d = AS_FLOAT(x);
        if (d >= -9.2233720368547758e18 && d <= 9.2233720368547758e18) return INT_VAL((int64_t)d);
        /* Too large for int64 -- go through a decimal string and a bignum
           parse rather than reinventing double->bignum truncation here. */
        char buf[64];
        snprintf(buf, sizeof buf, "%.0f", d);
        bool negative;
        ObjBignum *n = parse_int_bignum(buf, strlen(buf), &negative);
        return bignum_to_numba(&vm->gc, n);
    }
    if (IS_STRING(x)) {
        ObjString *s = AS_STRING(x);
        char *endPtr;
        double d = strtod(s->chars, &endPtr);
        if (endPtr != s->chars) {
            while (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\n' || *endPtr == '\r') endPtr++;
            if (*endPtr == '\0') return INT_VAL((int64_t)d);
        }
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' isn't a numba.", s->chars);
        return GHOST_VAL;
    }
    vm_throw_native(vm, "TypeVibeMismatch", "can't turn a %s into a numba.", vm_type_name(x));
    return GHOST_VAL;
}

static Value m_sheesh(VM *vm, Value *a, int argc) {
    (void)argc;
    char *r = vm_value_to_repr(vm, a[0]);
    fputs(r, vm->out);
    fputc('\n', vm->out);
    free(r);
    return a[0];
}

static Value m_no_cap(VM *vm, Value *a, int argc) {
    if (value_is_truthy(a[0])) return GHOST_VAL;
    if (argc > 1 && IS_STRING(a[1])) {
        vm_throw_native(vm, "SkillIssue", "%s", AS_STRING(a[1])->chars);
    } else if (argc > 1) {
        char *disp = vm_value_to_display(vm, a[1]);
        vm_throw_native(vm, "SkillIssue", "%s", disp);
        free(disp);
    } else {
        vm_throw_native(vm, "SkillIssue", "assertion failed. couldn't be you.");
    }
    return GHOST_VAL;
}

static Value m_ask(VM *vm, Value *a, int argc) {
    if (argc > 0 && !IS_GHOST(a[0])) {
        size_t len;
        char *disp = vm_value_to_display_len(vm, a[0], &len);
        fwrite(disp, 1, len, vm->out);
        free(disp);
        fflush(vm->out);
    }
    char buf[4096];
    if (fgets(buf, sizeof buf, stdin) == NULL) return OBJ_VAL(string_new(&vm->gc, "", 0));
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
    return OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)len));
}

static Value m_dip(VM *vm, Value *a, int argc) {
    int64_t code = 0;
    if (argc > 0 && !IS_GHOST(a[0])) code = IS_BOOL(a[0]) ? (AS_BOOL(a[0]) ? 1 : 0) : AS_INT(a[0]);
    vm_request_exit(vm, code);
    return GHOST_VAL;
}

static Value m_the_args(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    /* A fresh copy every call (funnylang/stdlib/builtins.py's own
       `Stash(list(...))`) -- Stash has reference semantics, and two
       separate the_args() calls must not alias each other's mutations. */
    ObjStash *src = (ObjStash *)AS_OBJ(vm->programArgs);
    return OBJ_VAL(stash_new(&vm->gc, src->items, src->count));
}

static Value m_combo(VM *vm, Value *a, int argc) {
    ObjStash *fns = stash_new(&vm->gc, a, argc);
    return OBJ_VAL(combo_new(&vm->gc, OBJ_VAL(fns)));
}

static Value m_identity(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)argc;
    return a[0];
}

static Value m_range_stash(VM *vm, Value *a, int argc) {
    Value startV = GHOST_VAL, stopV;
    Value stepV = (argc > 2 && !IS_GHOST(a[2])) ? a[2] : INT_VAL(1);
    if (argc > 1 && !IS_GHOST(a[1])) {
        startV = a[0];
        stopV = a[1];
    } else {
        startV = INT_VAL(0);
        stopV = a[0];
    }
    if (!(IS_NUM(startV) && IS_NUM(stopV) && IS_NUM(stepV))) {
        vm_throw_native(vm, "TypeVibeMismatch", "range_stash needs numbas.");
        return GHOST_VAL;
    }
    bool useFloat = IS_FLOAT(startV) || IS_FLOAT(stopV) || IS_FLOAT(stepV);
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    if (useFloat) {
        /* Mirrors Python's own dynamic typing exactly, including the part
           that's easy to miss: `n` starts as whatever type `start` itself
           is (often a plain int, e.g. range_stash(0, 1, 0.25)'s `0`), and
           only *becomes* a float once a float `step` is actually added to
           it -- not upfront just because *some* argument here is a float.
           `stop`'s own type never matters beyond the comparison. */
        double stop = IS_FLOAT(stopV) ? AS_FLOAT(stopV) : (IS_INT(stopV) ? (double)AS_INT(stopV) : bignum_to_double(AS_BIGNUM(stopV)));
        bool stepIsFloat = IS_FLOAT(stepV);
        double stepf = stepIsFloat ? AS_FLOAT(stepV) : (IS_INT(stepV) ? (double)AS_INT(stepV) : bignum_to_double(AS_BIGNUM(stepV)));
        if (stepf == 0.0) {
            vm_throw_native(vm, "TypeVibeMismatch", "range_stash step can't be 0.");
            gc_pop_temp(&vm->gc);
            return GHOST_VAL;
        }
        bool nIsFloat = IS_FLOAT(startV);
        double nf = nIsFloat ? AS_FLOAT(startV) : 0.0;
        int64_t ni = nIsFloat ? 0 : (IS_INT(startV) ? AS_INT(startV) : (int64_t)bignum_to_double(AS_BIGNUM(startV)));
        for (;;) {
            double cmp = nIsFloat ? nf : (double)ni;
            if (stepf > 0 ? !(cmp < stop) : !(cmp > stop)) break;
            stash_push(&vm->gc, out, nIsFloat ? FLOAT_VAL(nf) : INT_VAL(ni));
            if (nIsFloat) {
                nf += stepf;
            } else if (stepIsFloat) {
                nf = (double)ni + stepf;
                nIsFloat = true;
            } else {
                ni += (int64_t)stepf;
            }
        }
    } else {
        bool oa, ob, oc;
        ObjBignum *bStart = bignum_owned_of(startV, &oa);
        ObjBignum *bStop = bignum_owned_of(stopV, &ob);
        ObjBignum *bStep = bignum_owned_of(stepV, &oc);
        if (bignum_is_zero(bStep)) {
            if (oa) bignum_free(bStart);
            if (ob) bignum_free(bStop);
            if (oc) bignum_free(bStep);
            vm_throw_native(vm, "TypeVibeMismatch", "range_stash step can't be 0.");
            gc_pop_temp(&vm->gc);
            return GHOST_VAL;
        }
        bool stepPositive = bStep->sign >= 0;
        ObjBignum *n = bignum_copy(bStart);
        while (stepPositive ? bignum_compare(n, bStop) < 0 : bignum_compare(n, bStop) > 0) {
            stash_push(&vm->gc, out, bignum_to_numba(&vm->gc, bignum_copy(n)));
            ObjBignum *next = bignum_add(n, bStep);
            bignum_free(n);
            n = next;
        }
        bignum_free(n);
        if (oa) bignum_free(bStart);
        if (ob) bignum_free(bStop);
        if (oc) bignum_free(bStep);
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_zip_em(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH && IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "zip_em needs two stashes.");
        return GHOST_VAL;
    }
    ObjStash *x = (ObjStash *)AS_OBJ(a[0]);
    ObjStash *y = (ObjStash *)AS_OBJ(a[1]);
    int n = x->count < y->count ? x->count : y->count;
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    for (int i = 0; i < n; i++) {
        Value pair[2] = {x->items[i], y->items[i]};
        stash_push(&vm->gc, out, OBJ_VAL(stash_new(&vm->gc, pair, 2)));
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_enumerate_em(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "enumerate_em needs a stash.");
        return GHOST_VAL;
    }
    ObjStash *x = (ObjStash *)AS_OBJ(a[0]);
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    for (int i = 0; i < x->count; i++) {
        Value pair[2] = {INT_VAL(i), x->items[i]};
        stash_push(&vm->gc, out, OBJ_VAL(stash_new(&vm->gc, pair, 2)));
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value deep_clone_value(VM *vm, Value v) {
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(v);
        ObjStash *out = stash_new(&vm->gc, NULL, 0);
        gc_push_temp(&vm->gc, OBJ_VAL(out));
        for (int i = 0; i < s->count; i++) stash_push(&vm->gc, out, deep_clone_value(vm, s->items[i]));
        gc_pop_temp(&vm->gc);
        return OBJ_VAL(out);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(v);
        ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
        gc_push_temp(&vm->gc, OBJ_VAL(out));
        for (int i = 0; i < g->count; i++) groupchat_set(&vm->gc, out, g->entries[i].key, deep_clone_value(vm, g->entries[i].value));
        gc_pop_temp(&vm->gc);
        return OBJ_VAL(out);
    }
    return v;
}

static Value m_deep_clone(VM *vm, Value *a, int argc) {
    (void)argc;
    return deep_clone_value(vm, a[0]);
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} BuiltinEntry;

static const BuiltinEntry BUILTIN_TABLE[] = {
    {"how_thicc", m_how_thicc, 1, 1},
    {"what_is_it", m_what_is_it, 1, 1},
    {"to_yap", m_to_yap, 1, 1},
    {"to_numba", m_to_numba, 1, 1},
    {"to_int", m_to_int, 1, 1},
    {"sheesh", m_sheesh, 1, 1},
    {"no_cap", m_no_cap, 1, 2},
    {"ask", m_ask, 0, 1},
    {"dip", m_dip, 0, 1},
    {"the_args", m_the_args, 0, 0},
    {"combo", m_combo, 0, 255},
    {"identity", m_identity, 1, 1},
    {"range_stash", m_range_stash, 1, 3},
    {"zip_em", m_zip_em, 2, 2},
    {"enumerate_em", m_enumerate_em, 1, 1},
    {"deep_clone", m_deep_clone, 1, 1},
};
#define BUILTIN_TABLE_COUNT (int)(sizeof(BUILTIN_TABLE) / sizeof(BUILTIN_TABLE[0]))

void builtins_install(VM *vm) {
    for (int i = 0; i < BUILTIN_TABLE_COUNT; i++) {
        const BuiltinEntry *e = &BUILTIN_TABLE[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        vm_define_builtin(vm, name, OBJ_VAL(fn));
    }
}
