#include "stash.h"

#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "error.h"
#include "gc.h"
#include "string.h"

#define INITIAL_STASH_CAPACITY 4

ObjStash *stash_new(GC *gc, const Value *items, int count) {
    ObjStash *s = (ObjStash *)malloc(sizeof(ObjStash));
    s->obj.type = OBJ_STASH;
    s->obj.marked = false;
    s->obj.size = 0;
    s->obj.next = NULL;
    s->count = count;
    s->capacity = count > 0 ? count : INITIAL_STASH_CAPACITY;
    s->items = (Value *)malloc((size_t)s->capacity * sizeof(Value));
    if (count > 0) memcpy(s->items, items, (size_t)count * sizeof(Value));
    gc_track(gc, (Obj *)s, sizeof(ObjStash));
    return s;
}

void stash_push(GC *gc, ObjStash *s, Value v) {
    (void)gc;
    if (s->count == s->capacity) {
        s->capacity = s->capacity < INITIAL_STASH_CAPACITY ? INITIAL_STASH_CAPACITY : s->capacity * 2;
        s->items = (Value *)realloc(s->items, (size_t)s->capacity * sizeof(Value));
    }
    s->items[s->count++] = v;
}

/* -- shared helpers --------------------------------------------------- */

static ObjStash *as_stash(VM *vm, Value v, const char *fnName, bool *ok) {
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STASH) {
        *ok = true;
        return (ObjStash *)AS_OBJ(v);
    }
    *ok = false;
    vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a stash, not a %s.", fnName, vm_type_name(v));
    return NULL;
}

static bool norm_index(VM *vm, int64_t i, int n, int *outIdx) {
    int64_t idx = i < 0 ? i + n : i;
    if (idx < 0 || idx >= n) {
        vm_throw_native(vm, "OutOfPocket", "index %lld on a stash of %d.", (long long)i, n);
        return false;
    }
    *outIdx = (int)idx;
    return true;
}

static bool arg_is_int(Value v) {
    return IS_INT(v);
}

/* -- methods (funnylang/stdlib/stash.py, ported) ----------------------- */

static Value m_how_thicc(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "how_thicc", &ok);
    if (!ok) return GHOST_VAL;
    return INT_VAL(s->count);
}

static Value m_yeet_in(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "yeet_in", &ok);
    if (!ok) return GHOST_VAL;
    stash_push(&vm->gc, s, a[1]);
    return a[0];
}

static Value m_yoink(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "yoink", &ok);
    if (!ok) return GHOST_VAL;
    if (s->count == 0) {
        vm_throw_native(vm, "OutOfPocket", "yoink on an empty stash.");
        return GHOST_VAL;
    }
    return s->items[--s->count];
}

static Value m_yoink_at(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "yoink_at", &ok);
    if (!ok) return GHOST_VAL;
    if (!arg_is_int(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'yoink_at' needs a whole numba index.");
        return GHOST_VAL;
    }
    int idx;
    if (!norm_index(vm, AS_INT(a[1]), s->count, &idx)) return GHOST_VAL;
    Value v = s->items[idx];
    memmove(&s->items[idx], &s->items[idx + 1], (size_t)(s->count - idx - 1) * sizeof(Value));
    s->count--;
    return v;
}

static Value m_insert(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "insert", &ok);
    if (!ok) return GHOST_VAL;
    if (!arg_is_int(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'insert' needs a whole numba index.");
        return GHOST_VAL;
    }
    int64_t i = AS_INT(a[1]);
    int n = s->count;
    int64_t idx = i < 0 ? i + n : i;
    if (idx < 0) idx = 0;
    if (idx > n) idx = n;
    stash_push(&vm->gc, s, GHOST_VAL); /* grow by one, value overwritten below */
    memmove(&s->items[idx + 1], &s->items[idx], (size_t)(n - idx) * sizeof(Value));
    s->items[idx] = a[2];
    return a[0];
}

static Value m_contains(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "contains", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < s->count; i++) {
        if (value_equal_narrow(a[1], s->items[i])) return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value m_index_of(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "index_of", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < s->count; i++) {
        if (value_equal_narrow(a[1], s->items[i])) return INT_VAL(i);
    }
    return INT_VAL(-1);
}

static Value m_slice(VM *vm, Value *a, int argc) {
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "slice", &ok);
    if (!ok) return GHOST_VAL;
    int n = s->count;
    int64_t start = (argc > 1 && !IS_GHOST(a[1])) ? AS_INT(a[1]) : 0;
    int64_t stop = (argc > 2 && !IS_GHOST(a[2])) ? AS_INT(a[2]) : n;
    if (start < 0) start += n;
    if (stop < 0) stop += n;
    if (start < 0) start = 0;
    if (stop > n) stop = n;
    if (start > stop) start = stop;
    ObjStash *r = stash_new(&vm->gc, s->items + start, (int)(stop - start));
    return OBJ_VAL(r);
}

static Value m_reverse(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "reverse", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0, j = s->count - 1; i < j; i++, j--) {
        Value tmp = s->items[i];
        s->items[i] = s->items[j];
        s->items[j] = tmp;
    }
    return a[0];
}

/* Default ordering (funnylang/stdlib/stash.py's `_default_sort_key`):
   numbers sort before everything else; within a tier, compare directly.
   Comparing two non-numeric, non-string values (e.g. two stashes) isn't
   meaningfully orderable -- raises TypeVibeMismatch rather than either
   crashing or silently picking an arbitrary order. */
static bool default_less(VM *vm, Value a, Value b, bool *hadErr) {
    bool aNum = IS_NUM(a), bNum = IS_NUM(b);
    if (aNum != bNum) return aNum; /* numbers first */
    if (aNum && bNum) {
        if (IS_FLOAT(a) || IS_FLOAT(b) || (!IS_BIGNUM(a) && !IS_BIGNUM(b))) {
            double da = IS_FLOAT(a) ? AS_FLOAT(a) : (IS_INT(a) ? (double)AS_INT(a) : bignum_to_double(AS_BIGNUM(a)));
            double db = IS_FLOAT(b) ? AS_FLOAT(b) : (IS_INT(b) ? (double)AS_INT(b) : bignum_to_double(AS_BIGNUM(b)));
            return da < db;
        }
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
        uint32_t minLen = sa->byteLen < sb->byteLen ? sa->byteLen : sb->byteLen;
        int c = minLen ? memcmp(sa->chars, sb->chars, minLen) : 0;
        return c != 0 ? c < 0 : sa->byteLen < sb->byteLen;
    }
    vm_throw_native(vm, "TypeVibeMismatch", "can't compare a %s and a %s.", vm_type_name(a), vm_type_name(b));
    *hadErr = true;
    return false;
}

/* A plain stable insertion sort: N4's stash lengths are test-program-sized
   (correctness first, same reasoning as the linear-scan globals/groupchat
   tables -- see NATIVE_PLAN.md §9), and it makes propagating a mid-sort
   error (the comparator itself threw, or the callback did) trivial to get
   right, unlike threading an abort signal through libc qsort's callback. */
static void insertion_sort(VM *vm, Value *items, int n, Value cmpFn, bool *hadErr) {
    for (int i = 1; i < n && !*hadErr && !vm->hadError; i++) {
        Value key = items[i];
        int j = i - 1;
        for (;;) {
            if (j < 0) break;
            bool shouldMove;
            if (IS_GHOST(cmpFn)) {
                shouldMove = default_less(vm, key, items[j], hadErr);
                if (*hadErr || vm->hadError) return;
            } else {
                Value args[2] = {items[j], key};
                Value result = vm_call_value(vm, cmpFn, args, 2);
                if (vm->hadError) return;
                shouldMove = IS_INT(result) ? AS_INT(result) > 0 : value_is_truthy(result);
            }
            if (!shouldMove) break;
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

static Value m_sort(VM *vm, Value *a, int argc) {
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "sort", &ok);
    if (!ok) return GHOST_VAL;
    Value cmpFn = (argc > 1 && !IS_GHOST(a[1])) ? a[1] : GHOST_VAL;
    bool hadErr = false;
    insertion_sort(vm, s->items, s->count, cmpFn, &hadErr);
    return a[0];
}

static Value m_join(VM *vm, Value *a, int argc) {
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "join", &ok);
    if (!ok) return GHOST_VAL;
    const char *sep = "";
    uint32_t sepLen = 0;
    if (argc > 1 && IS_STRING(a[1])) {
        sep = AS_STRING(a[1])->chars;
        sepLen = AS_STRING(a[1])->byteLen;
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (int i = 0; i < s->count; i++) {
        char *piece = vm_value_to_display(s->items[i]);
        size_t pieceLen = strlen(piece);
        size_t addLen = pieceLen + (i > 0 ? sepLen : 0);
        if (len + addLen > cap) {
            cap = (len + addLen) * 2 + 16;
            buf = (char *)realloc(buf, cap);
        }
        if (i > 0) {
            memcpy(buf + len, sep, sepLen);
            len += sepLen;
        }
        memcpy(buf + len, piece, pieceLen);
        len += pieceLen;
        free(piece);
    }
    ObjString *r = string_new(&vm->gc, buf ? buf : "", (uint32_t)len);
    free(buf);
    return OBJ_VAL(r);
}

static Value m_glow_up(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "glow_up", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *r = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(r));
    for (int i = 0; i < s->count && !vm->hadError; i++) {
        Value args[1] = {s->items[i]};
        Value v = vm_call_value(vm, a[1], args, 1);
        if (vm->hadError) break;
        stash_push(&vm->gc, r, v);
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(r);
}

static Value m_vibe_check(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "vibe_check", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *r = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(r));
    for (int i = 0; i < s->count && !vm->hadError; i++) {
        Value args[1] = {s->items[i]};
        Value v = vm_call_value(vm, a[1], args, 1);
        if (vm->hadError) break;
        if (value_is_truthy(v)) stash_push(&vm->gc, r, s->items[i]);
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(r);
}

static Value m_squish(VM *vm, Value *a, int argc) {
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "squish", &ok);
    if (!ok) return GHOST_VAL;
    Value acc;
    int start = 0;
    if (argc > 2) {
        acc = a[2];
    } else {
        if (s->count == 0) {
            vm_throw_native(vm, "TypeVibeMismatch", "'squish' on an empty stash needs an initial value.");
            return GHOST_VAL;
        }
        acc = s->items[0];
        start = 1;
    }
    for (int i = start; i < s->count && !vm->hadError; i++) {
        Value args[2] = {acc, s->items[i]};
        acc = vm_call_value(vm, a[1], args, 2);
    }
    return vm->hadError ? GHOST_VAL : acc;
}

static Value m_any(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "any", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < s->count && !vm->hadError; i++) {
        Value args[1] = {s->items[i]};
        Value v = vm_call_value(vm, a[1], args, 1);
        if (vm->hadError) return GHOST_VAL;
        if (value_is_truthy(v)) return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value m_all(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "all", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < s->count && !vm->hadError; i++) {
        Value args[1] = {s->items[i]};
        Value v = vm_call_value(vm, a[1], args, 1);
        if (vm->hadError) return GHOST_VAL;
        if (!value_is_truthy(v)) return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

static Value m_first(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "first", &ok);
    if (!ok) return GHOST_VAL;
    return s->count > 0 ? s->items[0] : GHOST_VAL;
}

static Value m_last(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "last", &ok);
    if (!ok) return GHOST_VAL;
    return s->count > 0 ? s->items[s->count - 1] : GHOST_VAL;
}

static Value m_clone(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "clone", &ok);
    if (!ok) return GHOST_VAL;
    return OBJ_VAL(stash_new(&vm->gc, s->items, s->count));
}

static Value m_clear(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "clear", &ok);
    if (!ok) return GHOST_VAL;
    s->count = 0;
    return a[0];
}

static Value m_extend(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjStash *s = as_stash(vm, a[0], "extend", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *other = as_stash(vm, a[1], "extend", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < other->count; i++) stash_push(&vm->gc, s, other->items[i]);
    return a[0];
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} StashMethodEntry;

static const StashMethodEntry METHOD_TABLE[] = {
    {"how_thicc", m_how_thicc, 0, 0},
    {"yeet_in", m_yeet_in, 1, 1},
    {"yoink", m_yoink, 0, 0},
    {"yoink_at", m_yoink_at, 1, 1},
    {"insert", m_insert, 2, 2},
    {"contains", m_contains, 1, 1},
    {"index_of", m_index_of, 1, 1},
    {"slice", m_slice, 0, 2},
    {"reverse", m_reverse, 0, 0},
    {"sort", m_sort, 0, 1},
    {"join", m_join, 0, 1},
    {"glow_up", m_glow_up, 1, 1},
    {"vibe_check", m_vibe_check, 1, 1},
    {"squish", m_squish, 1, 2},
    {"any", m_any, 1, 1},
    {"all", m_all, 1, 1},
    {"first", m_first, 0, 0},
    {"last", m_last, 0, 0},
    {"clone", m_clone, 0, 0},
    {"clear", m_clear, 0, 0},
    {"extend", m_extend, 1, 1},
};
#define METHOD_TABLE_COUNT (int)(sizeof(METHOD_TABLE) / sizeof(METHOD_TABLE[0]))

NativeMethodFn stash_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < METHOD_TABLE_COUNT; i++) {
        if (strcmp(METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = METHOD_TABLE[i].minArity;
            *outMaxArity = METHOD_TABLE[i].maxArity;
            return METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}
