/* native/vm.c -- see vm.h. Arithmetic semantics mirror funnylang/vm.py's
 * _add/_mul/_check_num2/_bitwise/_compare exactly (PLAN.md §3.2/§4.1),
 * extended with fixnum<->bignum promotion, which Python's own unbounded
 * int never needed. Booleans are never numeric for + - * or / (PLAN.md
 * §16), but *are* int-like for bitwise ops (Python's bool is a subtype of
 * int, and funnylang/vm.py's _is_int_like inherits that) -- IS_INT_LIKE
 * below exists specifically to keep that asymmetry correct.
 *
 * N3 adds a real call stack: frames, closures, upvalues, try/catch/
 * finally unwinding, error objects, and pointers to variables. The
 * dispatch loop re-fetches "the current frame" at the top of every
 * iteration (see vm_run) because CALL/RETURN/a caught-exception jump can
 * all change which frame is current between one opcode and the next --
 * exactly funnylang/vm.py's own `while True: frame = self.frames[-1]; ...`
 * structure, ported rather than reinvented.
 */
#include "vm.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "builtins.h"
#include "error.h"
#include "groupchat.h"
#include "iterator.h"
#include "mafs.h"
#include "modules.h"
#include "numfmt.h"
#include "opcodes.h"
#include "pointa.h"
#include "squad.h"
#include "stash.h"
#include "string.h"
#include "yapper.h"

#define IS_INT_LIKE(v) (IS_INT(v) || IS_BOOL(v))

/* Forward declaration: vm_throw itself is defined much further down (it
   needs current_frame/build_trace, defined in the "-- errors --" section
   below), but the display/repr helpers and the public vm_throw_native
   wrapper -- both placed early, right after dup_str, since every other
   helper in this file wants them -- need to call it. */
static void vm_throw(VM *vm, const char *flavor, const char *message);
/* Same, but carrying N6's site-specific roast/hint (NULL for either means
   "use the flavor's default"). */
static void vm_throw_rich(VM *vm, const char *flavor, const char *roast, const char *hint, const char *message);

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    memcpy(r, s, n);
    return r;
}

static int64_t as_int64_like(Value v) {
    return IS_BOOL(v) ? (AS_BOOL(v) ? 1 : 0) : AS_INT(v);
}

/* Naive O(n*m) substring search -- `memmem` is a GNU extension, not
   portable to every C compiler this project targets (MSVC in particular;
   see bignum.h's own note on the same tradeoff for __builtin_*_overflow),
   and needle/haystack lengths here are yapstring-sized, not a hot path
   worth a smarter algorithm yet. An empty needle is contained in anything,
   matching Python's `"" in s`. */
static bool bytes_contains(const char *haystack, uint32_t hLen, const char *needle, uint32_t nLen) {
    if (nLen == 0) return true;
    if (nLen > hLen) return false;
    for (uint32_t i = 0; i + nLen <= hLen; i++) {
        if (memcmp(haystack + i, needle, nLen) == 0) return true;
    }
    return false;
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

static const char *type_name_of(Value v) {
    if (IS_GHOST(v)) return "ghost";
    if (IS_BOOL(v)) return "boolski";
    if (IS_INT(v) || IS_FLOAT(v) || IS_BIGNUM(v)) return "numba";
    if (IS_STRING(v)) return "yapstring";
    if (IS_OBJ(v)) {
        switch (AS_OBJ(v)->type) {
            case OBJ_CLOSURE:
            case OBJ_BOUND_NATIVE:
            case OBJ_BOUND_METHOD:
            case OBJ_NATIVE_FN:
            case OBJ_COMBO: return "bet";
            case OBJ_ERROR: return "error";
            case OBJ_POINTA: return "pointa";
            case OBJ_STASH: return "stash";
            case OBJ_GROUPCHAT: return "groupchat";
            case OBJ_ITERATOR: return "iterator";
            case OBJ_SQUAD: return "squad";
            case OBJ_INSTANCE: return ((ObjInstance *)AS_OBJ(v))->squad->name->chars;
            case OBJ_MODULE: return "module";
            default: return "object";
        }
    }
    return "object";
}

/* -- display/repr, with self-reference guarding (PLAN.md M11's own fix,
   ported here: a stash/groupchat that (directly or transitively) contains
   itself prints "[...]"/"{...}" for the cyclic occurrence instead of
   recursing forever) -- mirrors funnylang/values.py's to_display/to_repr,
   whose `_seen` parameter is a frozenset of Python object ids; `seen`
   here is the equivalent small pointer stack. */
typedef struct {
    Obj **items;
    int count;
    int capacity;
} SeenStack;

static bool seen_contains(const SeenStack *s, Obj *o) {
    for (int i = 0; i < s->count; i++) {
        if (s->items[i] == o) return true;
    }
    return false;
}

static void seen_push(SeenStack *s, Obj *o) {
    if (s->count == s->capacity) {
        s->capacity = s->capacity < 8 ? 8 : s->capacity * 2;
        s->items = (Obj **)realloc(s->items, (size_t)s->capacity * sizeof(Obj *));
    }
    s->items[s->count++] = o;
}

/* Matches Python's json.dumps(..., ensure_ascii=True) -- the default,
   and what to_repr's own `json.dumps(v)` call uses -- exactly: every
   non-ASCII codepoint becomes \uXXXX, or a UTF-16 surrogate pair for
   anything above U+FFFF (a 4-byte UTF-8 sequence), since JSON strings
   are defined over UTF-16 code units. Buffer sized for the worst case
   (every byte its own 1-byte codepoint needing "\u00XX", 6 chars) --
   still a safe upper bound now that multi-byte codepoints are decoded
   and re-encoded as (at most 2) 6-char escapes each, using fewer bytes
   of output per input byte than the all-ASCII worst case. */
static char *json_quote_string(const char *chars, uint32_t len) {
    char *buf = (char *)malloc((size_t)len * 6 + 3);
    size_t o = 0;
    buf[o++] = '"';
    uint32_t i = 0;
    while (i < len) {
        uint32_t seqLen = utf8_seq_len(chars, len, i);
        if (seqLen == 1) {
            unsigned char c = (unsigned char)chars[i];
            switch (c) {
                case '"': buf[o++] = '\\'; buf[o++] = '"'; break;
                case '\\': buf[o++] = '\\'; buf[o++] = '\\'; break;
                case '\n': buf[o++] = '\\'; buf[o++] = 'n'; break;
                case '\r': buf[o++] = '\\'; buf[o++] = 'r'; break;
                case '\t': buf[o++] = '\\'; buf[o++] = 't'; break;
                default:
                    if (c < 0x20 || c >= 0x80) {
                        /* c >= 0x80 here only from a malformed/truncated
                           leading byte (utf8_seq_len's own fallback) --
                           shouldn't arise (see string.h's own note), but
                           escaping it by its raw byte value rather than
                           passing it through unescaped is the least
                           surprising thing to do with it. */
                        o += (size_t)snprintf(buf + o, 7, "\\u%04x", c);
                    } else {
                        buf[o++] = (char)c;
                    }
            }
        } else {
            uint32_t cp = utf8_decode_cp(chars, seqLen, i);
            if (cp > 0xFFFF) {
                uint32_t v = cp - 0x10000;
                o += (size_t)snprintf(buf + o, 7, "\\u%04x", 0xD800 + (v >> 10));
                o += (size_t)snprintf(buf + o, 7, "\\u%04x", 0xDC00 + (v & 0x3FF));
            } else {
                o += (size_t)snprintf(buf + o, 7, "\\u%04x", cp);
            }
        }
        i += seqLen;
    }
    buf[o++] = '"';
    buf[o] = '\0';
    return buf;
}

static char *display_value_rec(VM *vm, Value v, SeenStack *seen);

static char *repr_value_rec(VM *vm, Value v, SeenStack *seen) {
    if (IS_STRING(v)) return json_quote_string(AS_STRING(v)->chars, AS_STRING(v)->byteLen);
    return display_value_rec(vm, v, seen);
}

static char *join_with_commas(char **parts, int n) {
    size_t len = 0;
    for (int i = 0; i < n; i++) len += strlen(parts[i]) + (i > 0 ? 2 : 0);
    char *buf = (char *)malloc(len + 1);
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            buf[o++] = ',';
            buf[o++] = ' ';
        }
        size_t pl = strlen(parts[i]);
        memcpy(buf + o, parts[i], pl);
        o += pl;
        free(parts[i]);
    }
    buf[o] = '\0';
    return buf;
}

static char *display_value_rec(VM *vm, Value v, SeenStack *seen) {
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
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_POINTA) {
        ObjPointa *p = (ObjPointa *)AS_OBJ(v);
        char buf[128];
        snprintf(buf, sizeof buf, "pointa -> %s", p->label->chars);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_CLOSURE) {
        ObjClosure *c = (ObjClosure *)AS_OBJ(v);
        char buf[128];
        snprintf(buf, sizeof buf, "<bet %s/%d>", c->proto->name, c->proto->arity);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BOUND_NATIVE) {
        ObjBoundNative *bn = (ObjBoundNative *)AS_OBJ(v);
        char buf[128];
        snprintf(buf, sizeof buf, "<bet %s/native>", bn->name);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BOUND_METHOD) {
        ObjBoundMethod *bm = (ObjBoundMethod *)AS_OBJ(v);
        char buf[128];
        snprintf(buf, sizeof buf, "<bet %s/bound>", bm->method->proto->name);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_NATIVE_FN) {
        ObjNativeFn *nf = (ObjNativeFn *)AS_OBJ(v);
        char buf[128];
        snprintf(buf, sizeof buf, "<bet %s/native>", nf->name);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_COMBO) {
        return dup_str("<bet combo/native>");
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_MODULE) {
        ObjModule *m = (ObjModule *)AS_OBJ(v);
        char buf[160];
        snprintf(buf, sizeof buf, "<module %s>", m->name->chars);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_SQUAD) {
        ObjSquad *s = (ObjSquad *)AS_OBJ(v);
        char buf[160];
        snprintf(buf, sizeof buf, "<squad %s>", s->name->chars);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(v);
        ObjClosure *method = squad_find_method(inst->squad, "to_yap");
        if (method != NULL) {
            gc_push_temp(&vm->gc, v);
            Value args[1] = {v};
            Value result = vm_call_value(vm, OBJ_VAL(method), args, 1);
            gc_pop_temp(&vm->gc);
            if (vm->hadError) return dup_str(""); /* dispatch loop unwinds; this string is discarded */
            return display_value_rec(vm, result, seen);
        }
        char buf[160];
        snprintf(buf, sizeof buf, "<%s instance>", inst->squad->name->chars);
        return dup_str(buf);
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_ITERATOR) {
        return dup_str("<iterator>");
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(v);
        if (seen_contains(seen, (Obj *)s)) return dup_str("[...]");
        seen_push(seen, (Obj *)s);
        char **parts = (char **)malloc((size_t)(s->count == 0 ? 1 : s->count) * sizeof(char *));
        for (int i = 0; i < s->count; i++) parts[i] = repr_value_rec(vm, s->items[i], seen);
        seen->count--; /* pop -- this stash's own frame is done */
        char *inner = join_with_commas(parts, s->count);
        free(parts);
        size_t n = strlen(inner);
        char *buf = (char *)malloc(n + 3);
        buf[0] = '[';
        memcpy(buf + 1, inner, n);
        buf[n + 1] = ']';
        buf[n + 2] = '\0';
        free(inner);
        return buf;
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(v);
        if (seen_contains(seen, (Obj *)g)) return dup_str("{...}");
        seen_push(seen, (Obj *)g);
        char **parts = (char **)malloc((size_t)(g->count == 0 ? 1 : g->count) * sizeof(char *));
        for (int i = 0; i < g->count; i++) {
            char *k = repr_value_rec(vm, g->entries[i].key, seen);
            char *val = repr_value_rec(vm, g->entries[i].value, seen);
            size_t kn = strlen(k), vn = strlen(val);
            char *pair = (char *)malloc(kn + vn + 3);
            memcpy(pair, k, kn);
            pair[kn] = ':';
            pair[kn + 1] = ' ';
            memcpy(pair + kn + 2, val, vn);
            pair[kn + 2 + vn] = '\0';
            free(k);
            free(val);
            parts[i] = pair;
        }
        seen->count--;
        char *inner = join_with_commas(parts, g->count);
        free(parts);
        size_t n = strlen(inner);
        char *buf = (char *)malloc(n + 3);
        buf[0] = '{';
        memcpy(buf + 1, inner, n);
        buf[n + 1] = '}';
        buf[n + 2] = '\0';
        free(inner);
        return buf;
    }
    return dup_str("<obj>"); /* unreachable for N4's value set so far */
}

static char *value_to_display(VM *vm, Value v) {
    /* Rooted for the whole walk: an Instance found anywhere in this value
       (directly, or nested inside a Stash/GroupChat) may call back into
       FunnyLang via its `to_yap` magic method, which can trigger a
       collection -- exactly the hazard call_bound_native's own comment
       describes, and the same fix (root the top-level value; blacken_
       object already transitively protects everything reachable from it
       once it's marked). */
    gc_push_temp(&vm->gc, v);
    SeenStack seen = {0};
    char *r = display_value_rec(vm, v, &seen);
    free(seen.items);
    gc_pop_temp(&vm->gc);
    return r;
}

void vm_throw_native(VM *vm, const char *flavor, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    vm_throw(vm, flavor, buf);
}

void vm_throw_native_roast(VM *vm, const char *flavor, const char *roast, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    vm_throw_rich(vm, flavor, roast, NULL, buf);
}

const char *vm_type_name(Value v) { return type_name_of(v); }
char *vm_value_to_display(VM *vm, Value v) { return value_to_display(vm, v); }

char *vm_value_to_repr(VM *vm, Value v) {
    /* Same rooting reasoning as value_to_display's own wrapper -- an
       Instance anywhere in `v` (directly, or nested) may call back into
       FunnyLang via `to_yap`, which can trigger a collection. */
    gc_push_temp(&vm->gc, v);
    SeenStack seen = {0};
    char *r = repr_value_rec(vm, v, &seen);
    free(seen.items);
    gc_pop_temp(&vm->gc);
    return r;
}

/* dip()'s sentinel flavor -- an ObjError users never see printed, since
   an uncaught one is recognized and turned into a real process exit
   (vm_run/main.c) rather than reported like an ordinary crash, and a
   caught one is impossible (vm_unwind_to_handler refuses to search for a
   handler once it sees this). Not exposed in any public header -- only
   vm_request_exit/vm_is_system_exit ever need to know the actual string. */
#define SYSTEM_EXIT_FLAVOR "__SystemExit__"

void vm_request_exit(VM *vm, int64_t code) {
    if (vm->hadError) return; /* first error at this position wins, same rule vm_throw uses */
    ObjError *e = error_new(&vm->gc, SYSTEM_EXIT_FLAVOR, "", NULL, NULL, 0, 0, "", INT_VAL(code), NULL, 0);
    vm->pendingError = OBJ_VAL(e);
    vm->hadError = true;
}

bool vm_is_system_exit(Value errValue, int64_t *outCode) {
    if (!(IS_OBJ(errValue) && AS_OBJ(errValue)->type == OBJ_ERROR)) return false;
    ObjError *e = (ObjError *)AS_OBJ(errValue);
    if (strcmp(e->flavor->chars, SYSTEM_EXIT_FLAVOR) != 0) return false;
    *outCode = AS_INT(e->payload);
    return true;
}

/* Structural equality (funnylang/values.py's funny_eq, ported): unlike
   value_equal_narrow (value.c -- deliberately the "narrow" scalar-only
   piece), Stash/GroupChat compare by contents here, recursively, and two
   Instances compare via their squad's `same_energy` magic method if it
   has one (falling back to pointer identity otherwise, matching Python's
   `return a is b`). No cycle guard -- funny_eq doesn't have one either; a
   self-referential stash compared against itself (or another
   self-referential stash) recursing forever is a pre-existing property of
   the reference semantics this ports, not a new gap. */
static bool vm_value_equal_rec(VM *vm, Value a, Value b) {
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == OBJ_STASH && AS_OBJ(b)->type == OBJ_STASH) {
        ObjStash *sa = (ObjStash *)AS_OBJ(a), *sb = (ObjStash *)AS_OBJ(b);
        if (sa->count != sb->count) return false;
        for (int i = 0; i < sa->count; i++) {
            if (!vm_value_equal_rec(vm, sa->items[i], sb->items[i])) return false;
        }
        return true;
    }
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == OBJ_GROUPCHAT && AS_OBJ(b)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *ga = (ObjGroupChat *)AS_OBJ(a), *gb = (ObjGroupChat *)AS_OBJ(b);
        if (ga->count != gb->count) return false;
        for (int i = 0; i < ga->count; i++) {
            GroupChatEntry *e = groupchat_find(gb, ga->entries[i].key);
            if (e == NULL || !vm_value_equal_rec(vm, ga->entries[i].value, e->value)) return false;
        }
        return true;
    }
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == OBJ_INSTANCE && AS_OBJ(b)->type == OBJ_INSTANCE) {
        ObjInstance *ia = (ObjInstance *)AS_OBJ(a);
        ObjClosure *method = squad_find_method(ia->squad, "same_energy");
        if (method == NULL) return AS_OBJ(a) == AS_OBJ(b);
        Value args[2] = {a, b};
        Value result = vm_call_value(vm, OBJ_VAL(method), args, 2);
        if (vm->hadError) return false; /* discarded; the dispatch loop unwinds */
        return value_is_truthy(result);
    }
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == OBJ_POINTA && AS_OBJ(b)->type == OBJ_POINTA) {
        /* Place identity (PLAN.md §3.10): the same box, or the same
           container and key -- never the pointed-to value (that's what
           `*p == *q` is for). Matches funnylang/values.py's own funny_eq
           exactly. */
        ObjPointa *pa = (ObjPointa *)AS_OBJ(a), *pb = (ObjPointa *)AS_OBJ(b);
        if (pa->kind != pb->kind) return false;
        if (pa->kind == POINTA_CELL) return pa->cell == pb->cell;
        /* Only one globals table exists in the whole VM at this milestone's
           scope (no per-module namespaces yet -- that's N5's), so "same
           globals_dict" (funnylang/vm.py's own check) is always true here;
           the name alone decides. */
        if (pa->kind == POINTA_GLOBAL) return string_equal(pa->globalName, pb->globalName);
        /* "index"/"prop": container is whatever PTR_INDEX/PTR_PROP was
           applied to, unchecked at construction time -- it need not be an
           Obj at all (`&(5).whatever` is legal to *form*). Two non-Obj
           containers have no real identity to compare, so content
           equality is the closest analogue (matching CPython's `is` on
           interned small ints/ghost/bools in practice, which is what
           funnylang/vm.py's own `a.container is b.container` actually
           observes for those cases). */
        bool containerSame = IS_OBJ(pa->container) && IS_OBJ(pb->container)
                                  ? AS_OBJ(pa->container) == AS_OBJ(pb->container)
                                  : value_equal_narrow(pa->container, pb->container);
        return containerSame && vm_value_equal_rec(vm, pa->key, pb->key);
    }
    return value_equal_narrow(a, b);
}

bool vm_value_equal(VM *vm, Value a, Value b) {
    /* Rooted for the whole comparison -- same reasoning as
       value_to_display: a nested same_energy call can trigger a
       collection, and blacken_object transitively protects everything
       reachable from a and b once both are marked. */
    gc_push_temp(&vm->gc, a);
    gc_push_temp(&vm->gc, b);
    bool result = vm_value_equal_rec(vm, a, b);
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    return result;
}

ObjBoundNative *bound_native_new(GC *gc, Value receiver, NativeMethodFn fn, const char *name, int minArity, int maxArity) {
    ObjBoundNative *bn = (ObjBoundNative *)malloc(sizeof(ObjBoundNative));
    bn->obj.type = OBJ_BOUND_NATIVE;
    bn->obj.marked = false;
    bn->obj.size = 0;
    bn->obj.next = NULL;
    bn->receiver = receiver;
    bn->fn = fn;
    bn->name = name;
    bn->minArity = minArity;
    bn->maxArity = maxArity;
    gc_track(gc, (Obj *)bn, sizeof(ObjBoundNative));
    return bn;
}

ObjNativeFn *native_fn_new(GC *gc, NativeMethodFn fn, const char *name, int minArity, int maxArity) {
    ObjNativeFn *nf = (ObjNativeFn *)malloc(sizeof(ObjNativeFn));
    nf->obj.type = OBJ_NATIVE_FN;
    nf->obj.marked = false;
    nf->obj.size = 0;
    nf->obj.next = NULL;
    nf->fn = fn;
    nf->name = name;
    nf->minArity = minArity;
    nf->maxArity = maxArity;
    gc_track(gc, (Obj *)nf, sizeof(ObjNativeFn));
    return nf;
}

/* -- setup / teardown ------------------------------------------------- */

#define INITIAL_STACK_CAPACITY 256
#define INITIAL_GLOBALS_CAPACITY 8
#define INITIAL_FRAMES_CAPACITY 64
#define INITIAL_OPEN_UPVALUES_CAPACITY 8

void vm_init(VM *vm) {
    gc_init(&vm->gc);
    vm->stackCapacity = INITIAL_STACK_CAPACITY;
    vm->stack = (Value *)malloc((size_t)vm->stackCapacity * sizeof(Value));
    vm->stackCount = 0;

    vm->frameCapacity = INITIAL_FRAMES_CAPACITY;
    vm->frames = (Frame *)malloc((size_t)vm->frameCapacity * sizeof(Frame));
    vm->frameCount = 0;

    vm->openUpvalueCapacity = INITIAL_OPEN_UPVALUES_CAPACITY;
    vm->openUpvalues = (ObjUpvalue **)malloc((size_t)vm->openUpvalueCapacity * sizeof(ObjUpvalue *));
    vm->openUpvalueCount = 0;

    vm->builtinCapacity = INITIAL_GLOBALS_CAPACITY;
    vm->builtins = (GlobalEntry *)malloc((size_t)vm->builtinCapacity * sizeof(GlobalEntry));
    vm->builtinCount = 0;
    vm->programArgs = GHOST_VAL;

    vm->unit = NULL;
    vm->pak = NULL;
    vm->pakModuleCache = GHOST_VAL;
    vm->currentModuleName = NULL;
    vm->out = NULL;
    vm->currentFrameIndex = -1;
    vm->currentInstrStart = 0;
    vm->hadError = false;
    vm->pendingError = GHOST_VAL;
    vm->uncaughtError = GHOST_VAL;
}

void vm_destroy(VM *vm) {
    free(vm->stack);
    for (int i = 0; i < vm->frameCount; i++) frame_destroy(&vm->frames[i]);
    free(vm->frames);
    free(vm->openUpvalues);
    free(vm->builtins);
    gc_destroy(&vm->gc);
}

/* -- roots -------------------------------------------------------------- */

static void mark_vm_roots(GC *gc, void *userdata) {
    VM *vm = (VM *)userdata;
    for (int i = 0; i < vm->stackCount; i++) gc_mark_value(gc, vm->stack[i]);
    for (int i = 0; i < vm->frameCount; i++) gc_mark_object(gc, (Obj *)vm->frames[i].closure);
    for (int i = 0; i < vm->openUpvalueCount; i++) gc_mark_object(gc, (Obj *)vm->openUpvalues[i]);
    /* No separate loop for module globals: each live frame's closure is
       already marked above, and blacken_object's own OBJ_CLOSURE case
       marks moduleGlobals/moduleExports transitively from there. */
    for (int i = 0; i < vm->builtinCount; i++) {
        gc_mark_object(gc, (Obj *)vm->builtins[i].name);
        gc_mark_value(gc, vm->builtins[i].value);
    }
    gc_mark_value(gc, vm->programArgs);
    gc_mark_value(gc, vm->pendingError);
    gc_mark_value(gc, vm->uncaughtError);
    /* Every constant any live proto can CONST-push must stay reachable
       through the whole run, not just while its own proto is executing
       (a closure created early can still be sitting in a local when a
       collection happens much later) -- root the whole unit's pool. */
    if (vm->unit != NULL) {
        for (uint32_t i = 0; i < vm->unit->constCount; i++) {
            gc_mark_value(gc, vm->unit->consts[i].value);
        }
    }
    /* Same again for every module in a bundle: `vm->unit` is only whichever
       one is executing right now, but a closure from an already-imported
       module can still be called later and CONST-push out of its own
       pool. */
    if (vm->pak != NULL) {
        for (uint32_t m = 0; m < vm->pak->moduleCount; m++) {
            CompiledUnit *u = vm->pak->modules[m].unit;
            if (u == NULL) continue;
            for (uint32_t i = 0; i < u->constCount; i++) gc_mark_value(gc, u->consts[i].value);
        }
    }
    gc_mark_value(gc, vm->pakModuleCache);
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

/* Module globals (GET/SET/DEF_GLOBAL, PTR_GLOBAL) are *not* handled here
   any more -- they live on each closure's own `moduleGlobals` GroupChat
   (PLAN.md §3.8's per-module isolation, N5 task 3), read/written directly
   via groupchat_find/groupchat_set at each opcode's own dispatch site.
   Only the builtins table (a single, genuinely VM-wide namespace) is
   still this plain linear-scan GlobalEntry array. */

static GlobalEntry *builtins_find(VM *vm, ObjString *name) {
    for (int i = 0; i < vm->builtinCount; i++) {
        if (string_equal(vm->builtins[i].name, name)) return &vm->builtins[i];
    }
    return NULL;
}

void vm_define_builtin(VM *vm, ObjString *name, Value value) {
    if (vm->builtinCount == vm->builtinCapacity) {
        vm->builtinCapacity *= 2;
        vm->builtins = (GlobalEntry *)realloc(vm->builtins, (size_t)vm->builtinCapacity * sizeof(GlobalEntry));
    }
    vm->builtins[vm->builtinCount].name = name;
    vm->builtins[vm->builtinCount].value = value;
    vm->builtinCount++;
}

/* -- frames ----------------------------------------------------------------- */

static Frame *current_frame(VM *vm) {
    return &vm->frames[vm->frameCount - 1];
}

static void push_frame(VM *vm, ObjClosure *closure, int slotBase) {
    if (vm->frameCount == vm->frameCapacity) {
        vm->frameCapacity *= 2;
        vm->frames = (Frame *)realloc(vm->frames, (size_t)vm->frameCapacity * sizeof(Frame));
    }
    frame_init(&vm->frames[vm->frameCount], closure, slotBase);
    vm->frameCount++;
}

/* -- upvalues --------------------------------------------------------------- */

static ObjUpvalue *capture_upvalue(VM *vm, int slot) {
    for (int i = 0; i < vm->openUpvalueCount; i++) {
        if (!vm->openUpvalues[i]->closed && vm->openUpvalues[i]->slot == slot) return vm->openUpvalues[i];
    }
    ObjUpvalue *uv = upvalue_new(&vm->gc, vm, slot);
    if (vm->openUpvalueCount == vm->openUpvalueCapacity) {
        vm->openUpvalueCapacity *= 2;
        vm->openUpvalues = (ObjUpvalue **)realloc(vm->openUpvalues, (size_t)vm->openUpvalueCapacity * sizeof(ObjUpvalue *));
    }
    vm->openUpvalues[vm->openUpvalueCount++] = uv;
    return uv;
}

static void close_upvalues_from(VM *vm, int minSlot) {
    int kept = 0;
    for (int i = 0; i < vm->openUpvalueCount; i++) {
        ObjUpvalue *uv = vm->openUpvalues[i];
        if (!uv->closed && uv->slot >= minSlot) {
            upvalue_close(uv);
        } else {
            vm->openUpvalues[kept++] = uv;
        }
    }
    vm->openUpvalueCount = kept;
}

/* -- errors ----------------------------------------------------------------- */

static char **build_trace(VM *vm, int *countOut) {
    int n = vm->frameCount;
    char **lines = (char **)malloc((size_t)n * sizeof(char *));
    const char *path = vm->unit->sourceName ? vm->unit->sourceName : "<unknown>";
    /* funnylang/vm.py's _build_trace walks frames innermost-first (`reversed(self.frames)`). */
    for (int i = 0; i < n; i++) {
        Frame *f = &vm->frames[n - 1 - i];
        uint32_t lookupIp = (n - 1 - i == vm->currentFrameIndex) ? vm->currentInstrStart
                                                                  : (f->ip > 0 ? f->ip - 1 : 0);
        uint32_t line, col;
        chunk_line_for_offset(f->closure->proto, lookupIp, &line, &col);
        char buf[256];
        const char *name = f->closure->proto->name;
        if (strcmp(name, "<script>") == 0) {
            snprintf(buf, sizeof buf, "at <the big one>  %s:%u", path, line);
        } else {
            snprintf(buf, sizeof buf, "at %s()  %s:%u", name, path, line);
        }
        lines[i] = dup_str(buf);
    }
    *countOut = n;
    return lines;
}

static void free_trace(char **lines, int count) {
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

Value vm_stack_trace_stash(VM *vm) {
    if (vm->frameCount == 0) return OBJ_VAL(stash_new(&vm->gc, NULL, 0));
    int count;
    char **lines = build_trace(vm, &count);
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (int i = 0; i < count; i++) {
        stash_push(&vm->gc, s, OBJ_VAL(string_new(&vm->gc, lines[i], (uint32_t)strlen(lines[i]))));
    }
    gc_pop_temp(&vm->gc);
    free_trace(lines, count);
    return OBJ_VAL(s);
}

/* Builds a complete ObjError from the VM's *current* position
   (currentFrameIndex/currentInstrStart, refreshed at the top of every
   dispatch iteration) and sets it pending. Safe to call from deep inside
   an arithmetic helper -- see vm.h's own note on why those fields exist. */
/* N6: `roast`/`hint` NULL means "no site-specific text" -- error_new then
   falls back to PLAN.md §4.1's per-flavor default roast, and to no hint,
   exactly as funnylang/errors.py's FunnyError.__init__ does. Most throw
   sites want precisely that and keep calling vm_throw/vm_throw_fmt. */
static void vm_throw_rich(VM *vm, const char *flavor, const char *roast, const char *hint, const char *message) {
    if (vm->hadError) return; /* first error at this position wins */
    Frame *f = &vm->frames[vm->currentFrameIndex];
    uint32_t line, col;
    chunk_line_for_offset(f->closure->proto, vm->currentInstrStart, &line, &col);
    int traceCount;
    char **trace = build_trace(vm, &traceCount);
    ObjError *e = error_new(&vm->gc, flavor, message, roast, hint, line, col, vm->unit->sourceName, GHOST_VAL, trace,
                             traceCount);
    free_trace(trace, traceCount);
    vm->pendingError = OBJ_VAL(e);
    vm->hadError = true;
}

static void vm_throw(VM *vm, const char *flavor, const char *message) {
    vm_throw_rich(vm, flavor, NULL, NULL, message);
}

static void vm_throw_fmt(VM *vm, const char *flavor, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    vm_throw(vm, flavor, buf);
}

/* Like vm_throw_fmt, but with PLAN.md §4.1's site-specific roast (and,
   for the one case §4.2 calls out by name, a fix hint). Only the throw
   sites whose Python counterpart passes `roast=`/`hint=` use this. */
static void vm_throw_roast(VM *vm, const char *flavor, const char *roast, const char *hint, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    vm_throw_rich(vm, flavor, roast, hint, buf);
}

/* The one throw shape every arithmetic type mismatch shares. The roast
   normalises to "a numba and a yapstring" for that particular pair
   whichever order the operands arrived in (the *message* keeps the real
   order), and PLAN.md §4.2's own worked hint example is attached on `+`
   alone -- both quirks copied from funnylang/vm.py rather than tidied. */
static void vm_throw_same_energy(VM *vm, Value a, Value b, bool withAddHint) {
    const char *ta = type_name_of(a), *tb = type_name_of(b);
    bool numbaAndString = (strcmp(ta, "numba") == 0 && strcmp(tb, "yapstring") == 0) ||
                          (strcmp(ta, "yapstring") == 0 && strcmp(tb, "numba") == 0);
    char roast[256];
    if (numbaAndString) {
        snprintf(roast, sizeof roast, "a numba and a yapstring do NOT have the same energy.");
    } else {
        snprintf(roast, sizeof roast, "a %s and a %s do NOT have the same energy.", ta, tb);
    }
    const char *hint =
        (withAddHint && numbaAndString) ? "wrap the numba in to_yap(...) first, so both sides are yapstrings" : NULL;
    vm_throw_roast(vm, "TypeVibeMismatch", roast, hint, "a %s and a %s do NOT have the same energy.", ta, tb);
}

/* The remaining throw shapes whose Python counterpart passes an explicit
   roast. Each pairs §4.1's roast with the message the runtime already
   produced, so `e.message` (language-visible) is untouched and only the
   rendered diagnostic gains anything. */

static void vm_throw_too_deep(VM *vm) {
    char roast[128];
    snprintf(roast, sizeof roast, "you recursed %d deep. touch grass.", vm->frameCount);
    vm_throw_roast(vm, "TooDeepBro", roast, NULL, "recursed %d deep.", vm->frameCount);
}

static void vm_throw_out_of_pocket(VM *vm, long long index, int count) {
    char roast[192];
    snprintf(roast, sizeof roast, "index %lld on a stash of %d. that's straight up out of pocket.", index, count);
    vm_throw_roast(vm, "OutOfPocket", roast, NULL, "index %lld on a stash of %d.", index, count);
}

static void vm_throw_key_ghosted(VM *vm, const char *key) {
    char roast[512];
    snprintf(roast, sizeof roast, "key `%s` left the group chat.", key);
    vm_throw_roast(vm, "KeyGhosted", roast, NULL, "key '%s' not found.", key);
}

static void vm_throw_no_such_method(VM *vm, const char *owner, const char *name) {
    char roast[512];
    snprintf(roast, sizeof roast, "`%s` doesn't do `%s`. that's not its thing.", owner, name);
    vm_throw_roast(vm, "WhoDis", roast, NULL, "'%s' doesn't do '%s'.", owner, name);
}

static void vm_throw_wrong_homies(VM *vm, const char *name, const char *wantStr, int argc) {
    char roast[512];
    snprintf(roast, sizeof roast, "`%s` wanted %s args. you brought %d. awkward.", name, wantStr, argc);
    vm_throw_roast(vm, "WrongNumberOfHomies", roast, NULL, "'%s' wants %s args, got %d.", name, wantStr, argc);
}

/* Searches from the current (innermost) frame outward for a handler,
   exactly matching funnylang/vm.py's _unwind(err, base_frame_count): pop
   frames with no handlers of their own, unwinding upvalues/stack as each
   one is discarded; the first frame with a pending handler gets the error
   value pushed and its ip redirected. Never pops below `baseFrameCount`
   -- a nested vm_execute (a native method's callback calling back into
   FunnyLang, via vm_call_value) must not catch/unwind past the frame it
   started at, even if nothing in *its own* frames handles the error: the
   error stays pending (hadError/pendingError, set by the caller after this
   returns false) for the *outer* execution to retry unwinding at its own,
   lower base, exactly as the outer loop already does for any other
   uncaught error. Returns false if nothing between the current frame and
   baseFrameCount catches it. */
static bool vm_unwind_to_handler(VM *vm, Value errValue, int baseFrameCount) {
    /* dip()'s sentinel is never caught, at any level -- mirrors Python's
       SystemExit not being a FunnyError, so `except FunnyError` (the
       only thing that ever calls _unwind there) never even sees it;
       every `sketchy`/`regardless` on the way out is skipped the same
       way. See vm_request_exit's own comment. */
    int64_t ignoredCode;
    if (vm_is_system_exit(errValue, &ignoredCode)) return false;
    while (vm->frameCount > baseFrameCount) {
        Frame *frame = current_frame(vm);
        if (frame->handlerCount > 0) {
            Handler h = frame_pop_handler(frame);
            close_upvalues_from(vm, h.stackDepth);
            vm->stackCount = h.stackDepth;
            if (h.handlerIp != HANDLER_ABSENT) {
                push(vm, errValue);
                frame->ip = (uint32_t)h.handlerIp;
                return true;
            }
            if (h.finallyIp != HANDLER_ABSENT) {
                push(vm, errValue);
                frame->ip = (uint32_t)h.finallyIp;
                return true;
            }
            continue;
        }
        close_upvalues_from(vm, frame->slotBase);
        vm->stackCount = frame->slotBase;
        frame_destroy(frame);
        vm->frameCount--;
    }
    return false;
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

/* -- pointer arithmetic (PLAN.md §3.10) -------------------------------- */

/* `key`'s repr can call back into FunnyLang (an Instance `key`, or one
   nested inside a Stash/GroupChat `key`, with a `to_yap`), which can
   trigger a collection -- the caller must keep `container`/`key` rooted
   (e.g. gc_push_temp) for the whole call if they aren't already reachable
   some other way (still on vm->stack, or transitively via an already-
   rooted value). Pointer arithmetic's own callers are safe without this:
   the key they pass here is always a freshly computed INT_VAL, never
   anything that can carry a `to_yap`. */
static ObjString *vm_index_label(VM *vm, Value container, Value key) {
    bool isGroupchat = IS_OBJ(container) && AS_OBJ(container)->type == OBJ_GROUPCHAT;
    SeenStack seen = {0};
    char *keyRepr = repr_value_rec(vm, key, &seen);
    free(seen.items);
    size_t need = strlen(keyRepr) + 16;
    char *buf = (char *)malloc(need);
    snprintf(buf, need, "%s[%s]", isGroupchat ? "groupchat" : "stash", keyRepr);
    free(keyRepr);
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)strlen(buf));
    free(buf);
    return r;
}

/* Only a pointer into a Stash, at "index" kind, supports arithmetic --
   PLAN.md §3.10: a groupchat key has no ordinal, and a "prop"/cell/global
   pointa isn't addressing a sequence at all. Also requires a plain fixnum
   key: funnylang/vm.py's own _require_stash_pointa doesn't check this (a
   float-keyed stash pointer's arithmetic just silently produces a float-
   keyed result there, a Python quirk nothing relies on), but AS_INT on a
   non-int Value here would read the wrong union member -- a clean,
   explicit TypeVibeMismatch is the safer call, matching this whole
   milestone's "never a segfault/UB" mandate. */
static bool require_stash_pointa(VM *vm, ObjPointa *p, const char *verb) {
    if (p->kind != POINTA_INDEX || !(IS_OBJ(p->container) && AS_OBJ(p->container)->type == OBJ_STASH) || !IS_INT(p->key)) {
        vm_throw_fmt(vm, "TypeVibeMismatch", "can't %s a pointa that isn't into a stash.", verb);
        return false;
    }
    return true;
}

static Value vm_pointer_add(VM *vm, Value a, Value b) {
    bool aIsPtr = IS_OBJ(a) && AS_OBJ(a)->type == OBJ_POINTA;
    bool bIsPtr = IS_OBJ(b) && AS_OBJ(b)->type == OBJ_POINTA;
    if (aIsPtr && bIsPtr) {
        vm_throw(vm, "TypeVibeMismatch", "can't add two pointas together.");
        return GHOST_VAL;
    }
    Value ptrVal = aIsPtr ? a : b;
    Value nVal = aIsPtr ? b : a;
    if (!IS_INT_LIKE(nVal)) {
        vm_throw_same_energy(vm, a, b, false);
        return GHOST_VAL;
    }
    ObjPointa *ptr = (ObjPointa *)AS_OBJ(ptrVal);
    if (!require_stash_pointa(vm, ptr, "add to")) return GHOST_VAL;
    Value newKeyVal = INT_VAL(AS_INT(ptr->key) + as_int64_like(nVal));
    ObjString *label = vm_index_label(vm, ptr->container, newKeyVal);
    return OBJ_VAL(pointa_new_place(&vm->gc, POINTA_INDEX, ptr->container, newKeyVal, label));
}

static Value vm_pointer_sub(VM *vm, Value a, Value b) {
    bool aIsPtr = IS_OBJ(a) && AS_OBJ(a)->type == OBJ_POINTA;
    bool bIsPtr = IS_OBJ(b) && AS_OBJ(b)->type == OBJ_POINTA;
    if (aIsPtr && bIsPtr) {
        ObjPointa *pa = (ObjPointa *)AS_OBJ(a), *pb = (ObjPointa *)AS_OBJ(b);
        if (!require_stash_pointa(vm, pa, "subtract")) return GHOST_VAL;
        if (!require_stash_pointa(vm, pb, "subtract")) return GHOST_VAL;
        if (AS_OBJ(pa->container) != AS_OBJ(pb->container)) {
            vm_throw(vm, "TypeVibeMismatch", "can't subtract pointas into different stashes.");
            return GHOST_VAL;
        }
        return INT_VAL(AS_INT(pa->key) - AS_INT(pb->key));
    }
    if (aIsPtr && IS_INT_LIKE(b)) {
        ObjPointa *pa = (ObjPointa *)AS_OBJ(a);
        if (!require_stash_pointa(vm, pa, "subtract from")) return GHOST_VAL;
        Value newKeyVal = INT_VAL(AS_INT(pa->key) - as_int64_like(b));
        ObjString *label = vm_index_label(vm, pa->container, newKeyVal);
        return OBJ_VAL(pointa_new_place(&vm->gc, POINTA_INDEX, pa->container, newKeyVal, label));
    }
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't subtract a %s from a %s.", type_name_of(b), type_name_of(a));
    return GHOST_VAL;
}

/* -- binary arithmetic (ADD..MOD, POW) --------------------------------------- */

static Value vm_add(VM *vm, Value a, Value b) {
    if ((IS_OBJ(a) && AS_OBJ(a)->type == OBJ_POINTA) || (IS_OBJ(b) && AS_OBJ(b)->type == OBJ_POINTA)) {
        return vm_pointer_add(vm, a, b);
    }
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
    if (IS_OBJ(a) && IS_OBJ(b) && AS_OBJ(a)->type == OBJ_STASH && AS_OBJ(b)->type == OBJ_STASH) {
        ObjStash *sa = (ObjStash *)AS_OBJ(a), *sb = (ObjStash *)AS_OBJ(b);
        ObjStash *r = stash_new(&vm->gc, sa->items, sa->count);
        gc_push_temp(&vm->gc, OBJ_VAL(r));
        for (int i = 0; i < sb->count; i++) stash_push(&vm->gc, r, sb->items[i]);
        gc_pop_temp(&vm->gc);
        return OBJ_VAL(r);
    }
    vm_throw_same_energy(vm, a, b, true);
    return GHOST_VAL;
}

static Value vm_sub(VM *vm, Value a, Value b) {
    if ((IS_OBJ(a) && AS_OBJ(a)->type == OBJ_POINTA) || (IS_OBJ(b) && AS_OBJ(b)->type == OBJ_POINTA)) {
        return vm_pointer_sub(vm, a, b);
    }
    if (!(IS_NUM(a) && IS_NUM(b))) {
        vm_throw_same_energy(vm, a, b, false);
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

/* Stash*int / int*Stash repetition (funnylang/vm.py's own `_mul`): the
   count is required to be an int-like (fixnum or bool, per _is_int_like)
   -- a bignum-sized repeat count would never fit in memory anyway, so
   unlike arithmetic's fixnum->bignum promotion, this stays int64-only. */
static Value stash_repeat(VM *vm, ObjStash *s, int64_t n) {
    if (n <= 0) return OBJ_VAL(stash_new(&vm->gc, NULL, 0));
    ObjStash *r = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(r));
    for (int64_t i = 0; i < n; i++) {
        for (int j = 0; j < s->count; j++) stash_push(&vm->gc, r, s->items[j]);
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(r);
}

/* yapstring*int / int*yapstring repetition (funnylang/vm.py's own `_mul`)
   -- plain byte repetition: repeating a valid UTF-8 byte sequence N times
   is still valid UTF-8, so this needs no codepoint awareness at all. */
static Value string_repeat(VM *vm, ObjString *s, int64_t n) {
    if (n <= 0) return OBJ_VAL(string_new(&vm->gc, "", 0));
    size_t total = (size_t)s->byteLen * (size_t)n;
    char *buf = (char *)malloc(total > 0 ? total : 1);
    size_t o = 0;
    for (int64_t i = 0; i < n; i++) {
        memcpy(buf + o, s->chars, s->byteLen);
        o += s->byteLen;
    }
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)total);
    free(buf);
    return OBJ_VAL(r);
}

static Value vm_mul(VM *vm, Value a, Value b) {
    if (IS_OBJ(a) && AS_OBJ(a)->type == OBJ_STASH && IS_INT_LIKE(b)) {
        return stash_repeat(vm, (ObjStash *)AS_OBJ(a), as_int64_like(b));
    }
    if (IS_INT_LIKE(a) && IS_OBJ(b) && AS_OBJ(b)->type == OBJ_STASH) {
        return stash_repeat(vm, (ObjStash *)AS_OBJ(b), as_int64_like(a));
    }
    if (IS_STRING(a) && IS_INT_LIKE(b)) {
        return string_repeat(vm, AS_STRING(a), as_int64_like(b));
    }
    if (IS_INT_LIKE(a) && IS_STRING(b)) {
        return string_repeat(vm, AS_STRING(b), as_int64_like(a));
    }
    if (!(IS_NUM(a) && IS_NUM(b))) {
        vm_throw_same_energy(vm, a, b, false);
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
        vm_throw_same_energy(vm, a, b, false);
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        vm_throw(vm, "MathAintMathin", "division by zero.");
        return GHOST_VAL;
    }
    return FLOAT_VAL(value_to_double(a) / value_to_double(b));
}

static Value vm_idiv(VM *vm, Value a, Value b) {
    if (!(IS_NUM(a) && IS_NUM(b))) {
        vm_throw_same_energy(vm, a, b, false);
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        vm_throw(vm, "MathAintMathin", "division by zero.");
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
        vm_throw_same_energy(vm, a, b, false);
        return GHOST_VAL;
    }
    if (numeric_is_zero(b)) {
        vm_throw(vm, "MathAintMathin", "mod by zero.");
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
        vm_throw_same_energy(vm, a, b, false);
        return GHOST_VAL;
    }
    if (IS_FLOAT(a) || IS_FLOAT(b)) {
        double da = value_to_double(a), db = value_to_double(b);
        if (da == 0.0 && db < 0.0) {
            vm_throw(vm, "MathAintMathin", "0 to a negative power.");
            return GHOST_VAL;
        }
        return FLOAT_VAL(pow(da, db));
    }
    int64_t exp;
    if (IS_BIGNUM(b)) {
        if (!bignum_to_int64(AS_BIGNUM(b), &exp)) {
            vm_throw(vm, "MathAintMathin", "exponent too large.");
            return GHOST_VAL;
        }
    } else {
        exp = AS_INT(b);
    }
    if (exp < 0) {
        double da = value_to_double(a);
        if (da == 0.0) {
            vm_throw(vm, "MathAintMathin", "0 to a negative power.");
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

Value vm_numeric_pow(VM *vm, Value a, Value b) { return vm_pow(vm, a, b); }
Value vm_numeric_add(VM *vm, Value a, Value b) { return vm_add(vm, a, b); }

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
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't negate a %s.", type_name_of(a));
    return GHOST_VAL;
}

/* -- bitwise (ints/bools; bignum when either operand already is one) -------- */

static Value vm_bnot(VM *vm, Value a) {
    if (IS_INT_LIKE(a)) return INT_VAL(~as_int64_like(a));
    if (IS_BIGNUM(a)) return bignum_result(vm, bignum_bnot(AS_BIGNUM(a)));
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't bitwise-not a %s.", type_name_of(a));
    return GHOST_VAL;
}

typedef enum { BIT_AND, BIT_OR, BIT_XOR } BitOp;

static Value vm_bitwise(VM *vm, BitOp op, Value a, Value b) {
    if (!((IS_INT_LIKE(a) || IS_BIGNUM(a)) && (IS_INT_LIKE(b) || IS_BIGNUM(b)))) {
        vm_throw_fmt(vm, "TypeVibeMismatch", "bitwise ops need whole numbas, not a %s/%s.", type_name_of(a), type_name_of(b));
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
        vm_throw_fmt(vm, "TypeVibeMismatch", "bitwise ops need whole numbas, not a %s/%s.", type_name_of(a), type_name_of(b));
        return GHOST_VAL;
    }
    int64_t shiftAmt;
    if (IS_BIGNUM(b)) {
        if (!bignum_to_int64(AS_BIGNUM(b), &shiftAmt)) {
            vm_throw(vm, "MathAintMathin", "shift amount too large.");
            return GHOST_VAL;
        }
    } else {
        shiftAmt = as_int64_like(b);
    }
    if (shiftAmt < 0) {
        vm_throw(vm, "MathAintMathin", "negative shift amount.");
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
    bool aIsPtr = IS_OBJ(a) && AS_OBJ(a)->type == OBJ_POINTA;
    bool bIsPtr = IS_OBJ(b) && AS_OBJ(b)->type == OBJ_POINTA;
    if (aIsPtr || bIsPtr) {
        if (!(aIsPtr && bIsPtr)) {
            vm_throw_fmt(vm, "TypeVibeMismatch", "can't compare a %s and a %s.", type_name_of(a), type_name_of(b));
            return GHOST_VAL;
        }
        ObjPointa *pa = (ObjPointa *)AS_OBJ(a), *pb = (ObjPointa *)AS_OBJ(b);
        if (!require_stash_pointa(vm, pa, "compare")) return GHOST_VAL;
        if (!require_stash_pointa(vm, pb, "compare")) return GHOST_VAL;
        if (AS_OBJ(pa->container) != AS_OBJ(pb->container)) {
            vm_throw(vm, "TypeVibeMismatch", "can't compare pointas into different stashes.");
            return GHOST_VAL;
        }
        cmp = AS_INT(pa->key) < AS_INT(pb->key) ? -1 : (AS_INT(pa->key) > AS_INT(pb->key) ? 1 : 0);
    } else if (IS_NUM(a) && IS_NUM(b)) {
        cmp = compare_numeric(a, b);
    } else if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
        uint32_t minLen = sa->byteLen < sb->byteLen ? sa->byteLen : sb->byteLen;
        int c = minLen ? memcmp(sa->chars, sb->chars, minLen) : 0;
        cmp = c != 0 ? (c < 0 ? -1 : 1) : (sa->byteLen < sb->byteLen ? -1 : (sa->byteLen > sb->byteLen ? 1 : 0));
    } else {
        vm_throw_fmt(vm, "TypeVibeMismatch", "can't compare a %s and a %s.", type_name_of(a), type_name_of(b));
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

/* -- calls -------------------------------------------------------------- */

/* Shared by do_call and vm_call_value's closure path, so the two never
   drift out of sync on what counts as a valid call. */
static bool closure_arity_ok(VM *vm, FunctionProto *proto, int argc) {
    int arity = proto->arity;
    int minRequired = arity - proto->defaultCount;
    if (proto->isVariadic) {
        /* The "...rest" parameter needs a real Stash to bind -- Stash
           exists as of this milestone, but wiring variadic binding
           through is separate follow-up work, not yet done. Reject
           cleanly rather than silently mishandling it. */
        vm_throw(vm, "SkillIssue", "variadic functions aren't supported natively yet.");
        return false;
    }
    if (argc > arity || argc < minRequired) {
        const char *wantStr;
        char wantBuf[32];
        int want = arity - proto->defaultCount;
        if (proto->defaultCount) {
            snprintf(wantBuf, sizeof wantBuf, "at least %d", want);
            wantStr = wantBuf;
        } else {
            snprintf(wantBuf, sizeof wantBuf, "%d", want);
            wantStr = wantBuf;
        }
        vm_throw_wrong_homies(vm, proto->name, wantStr, argc);
        return false;
    }
    return true;
}

/* Calls an ObjBoundNative's underlying C function. `argStart` is where its
   (non-receiver) arguments begin on the VM stack; the receiver itself
   isn't there (GET_PROP already popped it when it bound this
   ObjBoundNative -- see the OP_GET_PROP case). Copies args onto the C
   stack, rather than passing a pointer into vm->stack, before calling:
   `fn` may call vm_call_value (stash's sort/glow_up/vibe_check/squish/
   any/all all do), which pushes onto vm->stack and can trigger a
   realloc -- a raw pointer into vm->stack would dangle the instant that
   happens, the same "container can realloc" hazard ARCHITECTURE.md
   already calls out for ObjUpvalue/ObjPointa. */
static void call_bound_native(VM *vm, ObjBoundNative *bn, int argc, int argStart) {
    if (argc < bn->minArity || argc > bn->maxArity) {
        char wantBuf[32];
        if (bn->minArity == bn->maxArity) snprintf(wantBuf, sizeof wantBuf, "%d", bn->minArity);
        else snprintf(wantBuf, sizeof wantBuf, "%d-%d", bn->minArity, bn->maxArity);
        vm_throw_wrong_homies(vm, bn->name, wantBuf, argc);
        return;
    }
    Value args[257]; /* argc is a uint8_t at the opcode level (max 255), +1 for the receiver */
    args[0] = bn->receiver;
    for (int i = 0; i < argc; i++) args[i + 1] = vm->stack[argStart + i];
    vm->stackCount = argStart - 1; /* drop the callee + args now, before calling */
    /* Root the receiver and every arg as GC temps for the whole call: once
       they're off vm->stack, mark_vm_roots can no longer see them, but a
       callback-taking method (sort/glow_up/vibe_check/squish/any/all) can
       trigger arbitrarily many collections via nested vm_call_value calls
       while it's still running -- without this, the receiver (e.g. the
       stash glow_up is iterating) could be swept out from under it. */
    for (int i = 0; i <= argc; i++) gc_push_temp(&vm->gc, args[i]);
    Value result = bn->fn(vm, args, argc + 1);
    for (int i = 0; i <= argc; i++) gc_pop_temp(&vm->gc);
    if (vm->hadError) return; /* the dispatch loop's top-of-loop check unwinds */
    push(vm, result);
}

/* Squad(...) construction (funnylang/vm.py's own `_construct`): a bare
   Instance, then (if the squad or one of its ancestors defines `spawn`)
   `spawn` is called on it with the given args -- found via find_method,
   deliberately, not a dedicated `.spawn` field, so a subclass with no
   `spawn` of its own inherits the nearest ancestor's (needed for
   `squad_multilevel_inheritance`-style chains, and matching the M9 fix
   this milestone is explicitly asked to port). A squad with no `spawn`
   anywhere in its chain still constructs fine -- args are just silently
   unused, exactly like Python's own version. Shared by do_call (args
   still sitting on vm->stack) and vm_call_value (args in a plain C
   array), so both go through the exact same construction logic. */
static Value construct_instance(VM *vm, ObjSquad *squad, Value *args, int argc) {
    ObjInstance *inst = instance_new(&vm->gc, squad);
    ObjClosure *spawnMethod = squad_find_method(squad, "spawn");
    if (spawnMethod != NULL) {
        Value full[257]; /* argc is a uint8_t at the opcode level (max 255), +1 for `me` */
        full[0] = OBJ_VAL(inst);
        for (int i = 0; i < argc; i++) full[i + 1] = args[i];
        /* Root the whole call, same hazard call_bound_native's own comment
           describes: `inst` and its constructor args are off vm->stack
           now, and spawn's own body can trigger collections. */
        for (int i = 0; i <= argc; i++) gc_push_temp(&vm->gc, full[i]);
        vm_call_value(vm, OBJ_VAL(spawnMethod), full, argc + 1); /* return value discarded, matching _construct */
        for (int i = 0; i <= argc; i++) gc_pop_temp(&vm->gc);
        if (vm->hadError) return GHOST_VAL;
    }
    return OBJ_VAL(inst);
}

static void do_construct(VM *vm, ObjSquad *squad, int argc, int argStart) {
    Value args[257];
    for (int i = 0; i < argc; i++) args[i] = vm->stack[argStart + i];
    vm->stackCount = argStart - 1; /* drop the callee (the Squad) + raw args */
    Value result = construct_instance(vm, squad, args, argc);
    if (vm->hadError) return;
    push(vm, result);
}

/* Forward declarations: do_invoke/vm_get_prop (just below) need to bind a
   pointa's own instance methods (.deref()/.set()/.valid()/.where()), whose
   bodies (further down) need vm_get_index/vm_set_index/vm_get_prop/
   vm_set_prop -- themselves defined after do_invoke/vm_get_prop in this
   file. Breaking the cycle here rather than reordering the whole file. */
static NativeMethodFn pointa_find_method(const char *name, int *outMinArity, int *outMaxArity);
static Value vm_get_prop(VM *vm, Value obj, ObjString *name);
static void vm_set_prop(VM *vm, Value obj, ObjString *name, Value value);
static Value vm_get_index(VM *vm, Value obj, Value key);
static void vm_set_index(VM *vm, Value obj, Value key, Value value);

/* Like call_bound_native, but for a plain ObjNativeFn: no receiver to
   prepend, args are exactly [argStart, argStart+argc). Same GC-rooting
   reasoning -- a native function (how_thicc's Instance dispatch, combo's
   captured calls, ...) can call back into FunnyLang, which can trigger a
   collection while its own args are off vm->stack. */
static void call_native_fn(VM *vm, ObjNativeFn *nf, int argc, int argStart) {
    if (argc < nf->minArity || argc > nf->maxArity) {
        char wantBuf[32];
        if (nf->minArity == nf->maxArity) snprintf(wantBuf, sizeof wantBuf, "%d", nf->minArity);
        else snprintf(wantBuf, sizeof wantBuf, "%d-%d", nf->minArity, nf->maxArity);
        vm_throw_wrong_homies(vm, nf->name, wantBuf, argc);
        return;
    }
    Value args[256];
    for (int i = 0; i < argc; i++) args[i] = vm->stack[argStart + i];
    vm->stackCount = argStart - 1;
    for (int i = 0; i < argc; i++) gc_push_temp(&vm->gc, args[i]);
    Value result = nf->fn(vm, args, argc);
    for (int i = 0; i < argc; i++) gc_pop_temp(&vm->gc);
    if (vm->hadError) return;
    push(vm, result);
}

static void call_combo(VM *vm, ObjCombo *combo, int argc, int argStart) {
    if (argc > 1) {
        vm_throw_fmt(vm, "WrongNumberOfHomies", "'combo' wants 0-1 args, got %d.", argc);
        return;
    }
    Value arg = argc > 0 ? vm->stack[argStart] : GHOST_VAL;
    gc_push_temp(&vm->gc, OBJ_VAL(combo));
    if (argc > 0) gc_push_temp(&vm->gc, arg);
    Value result = combo_call(vm, combo, arg);
    if (argc > 0) gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    vm->stackCount = argStart - 1;
    if (vm->hadError) return;
    push(vm, result);
}

static void do_call(VM *vm, int argc) {
    int argStart = vm->stackCount - argc;
    Value callee = vm->stack[argStart - 1];
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_BOUND_NATIVE) {
        call_bound_native(vm, (ObjBoundNative *)AS_OBJ(callee), argc, argStart);
        return;
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_NATIVE_FN) {
        call_native_fn(vm, (ObjNativeFn *)AS_OBJ(callee), argc, argStart);
        return;
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_COMBO) {
        call_combo(vm, (ObjCombo *)AS_OBJ(callee), argc, argStart);
        return;
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_SQUAD) {
        do_construct(vm, (ObjSquad *)AS_OBJ(callee), argc, argStart);
        return;
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_BOUND_METHOD) {
        ObjBoundMethod *bm = (ObjBoundMethod *)AS_OBJ(callee);
        /* Same receiver-prepend shift do_call already does for a bare
           closure, just with the receiver coming from `bm` instead of
           already sitting in the callee slot -- overwrite that slot with
           the receiver instead of shifting args down over it. */
        if (vm->frameCount >= VM_MAX_FRAMES) {
            vm_throw_too_deep(vm);
            return;
        }
        if (!closure_arity_ok(vm, bm->method->proto, argc + 1)) return;
        int arity = bm->method->proto->arity;
        vm->stack[argStart - 1] = bm->receiver;
        for (int i = argc + 1; i < arity; i++) push(vm, GHOST_VAL);
        push_frame(vm, bm->method, argStart - 1);
        return;
    }
    if (!(IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_CLOSURE)) {
        vm_throw_fmt(vm, "NotACallableRizz", "'%s' is not callable.", type_name_of(callee));
        return;
    }
    if (vm->frameCount >= VM_MAX_FRAMES) {
        vm_throw_too_deep(vm);
        return;
    }
    ObjClosure *closure = (ObjClosure *)AS_OBJ(callee);
    if (!closure_arity_ok(vm, closure->proto, argc)) return;
    int arity = closure->proto->arity;
    /* Pad missing (defaulted) args with ghost, matching funnylang/vm.py --
       the closure's own bytecode fills in the actual default expressions
       (compiler.py's _emit_param_defaults: "if this slot is still ghost,
       evaluate the default"). Padding first, then shifting everything
       (real args *and* padding) left by one slot, overwrites the callee
       slot in place instead of popping the callee+args and re-pushing a
       fresh copy the way funnylang/vm.py does -- same end state, one
       array move instead of a pop/extend pair. */
    for (int i = argc; i < arity; i++) push(vm, GHOST_VAL);
    memmove(&vm->stack[argStart - 1], &vm->stack[argStart], (size_t)(vm->stackCount - argStart) * sizeof(Value));
    vm->stackCount--; /* the callee slot is now the first arg slot */
    push_frame(vm, closure, argStart - 1);
}

/* Pushes a new frame calling `method` with `me` prepended to the argc
   already-on-the-stack args at [argStart, argStart+argc) -- shared by
   do_invoke's Instance fast path and do_invoke_og, both of which always
   know the receiver ahead of time and want the same "shift args down over
   the now-unneeded name/callee slot" shape do_call's plain-closure path
   uses. `argStart - 1` is the slot being reused as `me`'s slot 0. */
static void invoke_bound_closure(VM *vm, Value me, ObjClosure *method, int argc, int argStart) {
    if (vm->frameCount >= VM_MAX_FRAMES) {
        vm_throw_too_deep(vm);
        return;
    }
    if (!closure_arity_ok(vm, method->proto, argc + 1)) return;
    int arity = method->proto->arity;
    vm->stack[argStart - 1] = me;
    for (int i = argc + 1; i < arity; i++) push(vm, GHOST_VAL);
    push_frame(vm, method, argStart - 1);
}

/* INVOKE (name+argc known at the call site, e.g. `mystash.yeet_in(5)` or
   `instance.method(5)`): binds and calls a method directly, without
   materializing an intermediate ObjBoundNative/ObjBoundMethod first --
   mirrors funnylang/vm.py's own _do_invoke. An Instance field that
   shadows a method name (storing a plain closure under that name) is
   called with *no* implicit receiver, exactly like Python's version:
   only find_method's result gets `me` prepended. */
static void do_invoke(VM *vm, ObjString *name, int argc) {
    int argStart = vm->stackCount - argc;
    Value obj = vm->stack[argStart - 1];
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(obj);
        Value fieldVal;
        if (!instance_get_field(inst, name->chars, &fieldVal)) {
            ObjClosure *method = squad_find_method(inst->squad, name->chars);
            if (method == NULL) {
                vm_throw_no_such_method(vm, inst->squad->name->chars, name->chars);
                return;
            }
            invoke_bound_closure(vm, obj, method, argc, argStart);
            return;
        }
        /* Shadowed by a field: call whatever's stored there as a plain
           value, the same as if it had been read via GET_PROP then
           CALLed -- no receiver-prepend, matching Python exactly. Replace
           the receiver's own stack slot with the field's value first, so
           do_call reads the right callee. */
        vm->stack[argStart - 1] = fieldVal;
        do_call(vm, argc);
        return;
    }
    NativeMethodFn fn = NULL;
    int minArity = 0, maxArity = 0;
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_STASH) {
        fn = stash_find_method(name->chars, &minArity, &maxArity);
    } else if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_GROUPCHAT) {
        fn = groupchat_find_method(name->chars, &minArity, &maxArity);
    } else if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_POINTA) {
        fn = pointa_find_method(name->chars, &minArity, &maxArity);
    } else if (IS_NUM(obj)) {
        fn = numba_find_method(name->chars, &minArity, &maxArity);
    } else if (IS_STRING(obj)) {
        fn = yapstring_find_method(name->chars, &minArity, &maxArity);
    }
    if (fn == NULL) {
        if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_MODULE) {
            /* A module member, called directly (`mafs.sqrt(4)`) -- no
               receiver to prepend, matching vm.py's generic
               _get_prop-then-call fallback for a bare NativeFn/Closure
               result off any non-Instance receiver. Replace the
               receiver's own stack slot with the member's value first,
               so do_call reads the right callee (same trick as
               INVOKE's Instance-field-shadow path above). */
            ObjModule *mod = (ObjModule *)AS_OBJ(obj);
            GroupChatEntry *e = groupchat_find((ObjGroupChat *)AS_OBJ(mod->members), OBJ_VAL(name));
            if (e == NULL) {
                vm_throw_fmt(vm, "WhoDis", "'%s' isn't exported by module '%s'.", name->chars, mod->name->chars);
                return;
            }
            vm->stack[argStart - 1] = e->value;
            do_call(vm, argc);
            return;
        }
        if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_SQUAD) {
            /* A Squad's own method, called directly off the class itself
               (not an instance) -- no receiver to prepend, matching
               vm.py's generic _get_prop-then-call fallback for a bare
               Closure result. */
            ObjClosure *method = squad_find_method((ObjSquad *)AS_OBJ(obj), name->chars);
            if (method != NULL) {
                if (vm->frameCount >= VM_MAX_FRAMES) {
                    vm_throw_too_deep(vm);
                    return;
                }
                if (!closure_arity_ok(vm, method->proto, argc)) return;
                int arity = method->proto->arity;
                for (int i = argc; i < arity; i++) push(vm, GHOST_VAL);
                memmove(&vm->stack[argStart - 1], &vm->stack[argStart], (size_t)(vm->stackCount - argStart) * sizeof(Value));
                vm->stackCount--;
                push_frame(vm, method, argStart - 1);
                return;
            }
            vm_throw_no_such_method(vm, ((ObjSquad *)AS_OBJ(obj))->name->chars, name->chars);
            return;
        }
        if (IS_GHOST(obj)) {
            vm_throw_fmt(vm, "GhostError", "can't read '%s' off ghost.", name->chars);
        } else {
            vm_throw_fmt(vm, "WhoDis", "a %s doesn't have '%s' (yet).", type_name_of(obj), name->chars);
        }
        return;
    }
    if (argc < minArity || argc > maxArity) {
        char wantBuf[32];
        if (minArity == maxArity) snprintf(wantBuf, sizeof wantBuf, "%d", minArity);
        else snprintf(wantBuf, sizeof wantBuf, "%d-%d", minArity, maxArity);
        vm_throw_wrong_homies(vm, name->chars, wantBuf, argc);
        return;
    }
    Value args[257];
    args[0] = obj;
    for (int i = 0; i < argc; i++) args[i + 1] = vm->stack[argStart + i];
    vm->stackCount = argStart - 1;
    /* See call_bound_native's own comment: the receiver/args must stay
       GC-rooted for the whole call now that they're off vm->stack. */
    for (int i = 0; i <= argc; i++) gc_push_temp(&vm->gc, args[i]);
    Value result = fn(vm, args, argc + 1);
    for (int i = 0; i <= argc; i++) gc_pop_temp(&vm->gc);
    if (vm->hadError) return;
    push(vm, result);
}

/* `og`: resolves relative to the *defining* class of the currently
   executing method (frame->closure->homeSquad), never the receiver's own
   runtime class -- otherwise a super-call from a middle class in a 3+
   level hierarchy would re-invoke its own method forever instead of
   reaching the next class up (the M9 fix this milestone is explicitly
   asked to port; see squad_multilevel_inheritance.funny). */
static void do_invoke_og(VM *vm, ObjString *name, int argc) {
    int argStart = vm->stackCount - argc;
    Value me = vm->stack[argStart - 1];
    Frame *frame = current_frame(vm);
    ObjSquad *homeSquad = frame->closure->homeSquad;
    if (!(IS_OBJ(me) && AS_OBJ(me)->type == OBJ_INSTANCE) || homeSquad == NULL || homeSquad->superclass == NULL) {
        vm_throw(vm, "NotACallableRizz", "'og' has no superclass here.");
        return;
    }
    ObjClosure *method = squad_find_method(homeSquad->superclass, name->chars);
    if (method == NULL) {
        vm_throw_no_such_method(vm, homeSquad->superclass->name->chars, name->chars);
        return;
    }
    invoke_bound_closure(vm, me, method, argc, argStart);
}

/* -- properties / indexing / slicing (funnylang/vm.py's _get_prop,
   _get_index, _set_index, _get_slice, ported) ---------------------------- */

static Value vm_get_prop(VM *vm, Value obj, ObjString *name) {
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_ERROR) {
        Value fieldVal;
        if (error_get_field(&vm->gc, (ObjError *)AS_OBJ(obj), name->chars, &fieldVal)) {
            return fieldVal;
        }
        vm_throw_fmt(vm, "WhoDis", "error objects don't have '%s'.", name->chars);
        return GHOST_VAL;
    }
    if (IS_OBJ(obj) && (AS_OBJ(obj)->type == OBJ_STASH || AS_OBJ(obj)->type == OBJ_GROUPCHAT || AS_OBJ(obj)->type == OBJ_POINTA)) {
        int minArity, maxArity;
        NativeMethodFn fn;
        if (AS_OBJ(obj)->type == OBJ_STASH) fn = stash_find_method(name->chars, &minArity, &maxArity);
        else if (AS_OBJ(obj)->type == OBJ_GROUPCHAT) fn = groupchat_find_method(name->chars, &minArity, &maxArity);
        else fn = pointa_find_method(name->chars, &minArity, &maxArity);
        if (fn == NULL) {
            vm_throw_fmt(vm, "WhoDis", "a %s doesn't have '%s'.", type_name_of(obj), name->chars);
            return GHOST_VAL;
        }
        /* name->chars is safe to store long-term (not just for this call):
           it's part of the unit's constant pool, which mark_vm_roots keeps
           reachable for the whole run regardless of what else the GC
           collects -- see that function's own comment on exactly this
           point. */
        return OBJ_VAL(bound_native_new(&vm->gc, obj, fn, name->chars, minArity, maxArity));
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(obj);
        Value fieldVal;
        if (instance_get_field(inst, name->chars, &fieldVal)) {
            return fieldVal;
        }
        ObjClosure *method = squad_find_method(inst->squad, name->chars);
        if (method != NULL) {
            return OBJ_VAL(bound_method_new(&vm->gc, obj, method));
        }
        /* An unset field/unknown name on an Instance is ghost, not WhoDis
           -- deliberately different from every other type's GET_PROP,
           matching funnylang/vm.py's own _get_prop (`return GHOST`). */
        return GHOST_VAL;
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_SQUAD) {
        ObjSquad *squad = (ObjSquad *)AS_OBJ(obj);
        ObjClosure *method = squad_find_method(squad, name->chars);
        if (method == NULL) {
            vm_throw_no_such_method(vm, squad->name->chars, name->chars);
            return GHOST_VAL;
        }
        return OBJ_VAL(method);
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_MODULE) {
        ObjModule *mod = (ObjModule *)AS_OBJ(obj);
        GroupChatEntry *e = groupchat_find((ObjGroupChat *)AS_OBJ(mod->members), OBJ_VAL(name));
        if (e == NULL) {
            vm_throw_fmt(vm, "WhoDis", "'%s' isn't exported by module '%s'.", name->chars, mod->name->chars);
            return GHOST_VAL;
        }
        return e->value;
    }
    if (IS_NUM(obj)) {
        /* numba's own instance methods (mafs.c's NUMBA_METHODS,
           e.g. `(5.5).floor()`) -- boolski is never a numba (PLAN.md
           §16), and IS_NUM already excludes it (a separate Value tag),
           so this needs no extra !IS_BOOL check the way Python's own
           `isinstance(obj, (int,float)) and not isinstance(obj, bool)`
           does (Python's bool being an int subclass). */
        int minArity, maxArity;
        NativeMethodFn fn = numba_find_method(name->chars, &minArity, &maxArity);
        if (fn == NULL) {
            vm_throw_fmt(vm, "WhoDis", "a numba doesn't have '%s'.", name->chars);
            return GHOST_VAL;
        }
        return OBJ_VAL(bound_native_new(&vm->gc, obj, fn, name->chars, minArity, maxArity));
    }
    if (IS_STRING(obj)) {
        /* yapstring's own instance methods (yapper.c's YAPSTRING_METHOD_TABLE). */
        int minArity, maxArity;
        NativeMethodFn fn = yapstring_find_method(name->chars, &minArity, &maxArity);
        if (fn == NULL) {
            vm_throw_fmt(vm, "WhoDis", "a yapstring doesn't have '%s'.", name->chars);
            return GHOST_VAL;
        }
        return OBJ_VAL(bound_native_new(&vm->gc, obj, fn, name->chars, minArity, maxArity));
    }
    if (IS_GHOST(obj)) {
        vm_throw_fmt(vm, "GhostError", "can't read '%s' off ghost.", name->chars);
        return GHOST_VAL;
    }
    vm_throw_fmt(vm, "WhoDis", "a %s doesn't have '%s' (yet).", type_name_of(obj), name->chars);
    return GHOST_VAL;
}

static void vm_set_prop(VM *vm, Value obj, ObjString *name, Value value) {
    /* Instance fields (`me.x = ...`) are the only settable property in
       the whole language -- matches funnylang/vm.py's own _set_prop
       exactly (no method-dispatch magic here: field assignment is always
       a plain write, unlike GET_INDEX/SET_INDEX's get_it/set_it). */
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_INSTANCE) {
        instance_set_field(&vm->gc, (ObjInstance *)AS_OBJ(obj), name, value);
    } else if (IS_GHOST(obj)) {
        vm_throw_fmt(vm, "GhostError", "can't set '%s' on ghost.", name->chars);
    } else {
        vm_throw_fmt(vm, "TypeVibeMismatch", "can't set properties on a %s.", type_name_of(obj));
    }
}

static Value vm_get_index(VM *vm, Value obj, Value key) {
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(obj);
        if (!IS_INT_LIKE(key)) {
            vm_throw_fmt(vm, "TypeVibeMismatch", "can't index a stash with a %s.", type_name_of(key));
            return GHOST_VAL;
        }
        int64_t k = as_int64_like(key);
        int64_t idx = k < 0 ? k + s->count : k;
        if (idx < 0 || idx >= s->count) {
            vm_throw_out_of_pocket(vm, (long long)k, s->count);
            return GHOST_VAL;
        }
        return s->items[idx];
    }
    if (IS_STRING(obj)) {
        /* Codepoint indexing: `n`/`idx` are in codepoints throughout,
           matching Python's own str indexing exactly (len("héllo") == 5).
           isAscii gives an O(1) byte offset (index == offset); otherwise
           an O(n) walk translates the codepoint index into a byte offset
           -- exactly the tradeoff NATIVE_PLAN.md's own task asks for. */
        ObjString *str = AS_STRING(obj);
        if (!IS_INT_LIKE(key)) {
            vm_throw_fmt(vm, "TypeVibeMismatch", "can't index a yapstring with a %s.", type_name_of(key));
            return GHOST_VAL;
        }
        int64_t k = as_int64_like(key);
        int64_t n = str->codepointCount;
        int64_t idx = k < 0 ? k + n : k;
        if (idx < 0 || idx >= n) {
            vm_throw_fmt(vm, "OutOfPocket", "index %lld on a yapstring of length %lld.", (long long)k, (long long)n);
            return GHOST_VAL;
        }
        uint32_t byteStart = str->isAscii ? (uint32_t)idx : utf8_byte_offset_of(str->chars, str->byteLen, (uint32_t)idx);
        uint32_t seqLen = utf8_seq_len(str->chars, str->byteLen, byteStart);
        return OBJ_VAL(string_new(&vm->gc, str->chars + byteStart, seqLen));
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(obj);
        GroupChatEntry *e = groupchat_find(g, key);
        if (e == NULL) {
            char *disp = value_to_display(vm, key);
            vm_throw_key_ghosted(vm, disp);
            free(disp);
            return GHOST_VAL;
        }
        return e->value;
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(obj);
        ObjClosure *method = squad_find_method(inst->squad, "get_it");
        if (method == NULL) {
            vm_throw_no_such_method(vm, inst->squad->name->chars, "get_it");
            return GHOST_VAL;
        }
        /* obj/key are already off the FunnyLang stack by the time
           vm_get_index runs (OP_GET_INDEX pops both first) -- root them
           for the call, same hazard as call_bound_native's own comment. */
        gc_push_temp(&vm->gc, obj);
        gc_push_temp(&vm->gc, key);
        Value args[2] = {obj, key};
        Value result = vm_call_value(vm, OBJ_VAL(method), args, 2);
        gc_pop_temp(&vm->gc);
        gc_pop_temp(&vm->gc);
        return result;
    }
    if (IS_GHOST(obj)) {
        vm_throw(vm, "GhostError", "can't index ghost.");
        return GHOST_VAL;
    }
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't index a %s.", type_name_of(obj));
    return GHOST_VAL;
}

static void vm_set_index(VM *vm, Value obj, Value key, Value value) {
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(obj);
        if (!IS_INT_LIKE(key)) {
            vm_throw_fmt(vm, "TypeVibeMismatch", "can't index a stash with a %s.", type_name_of(key));
            return;
        }
        int64_t k = as_int64_like(key);
        int64_t idx = k < 0 ? k + s->count : k;
        if (idx < 0 || idx >= s->count) {
            vm_throw_out_of_pocket(vm, (long long)k, s->count);
            return;
        }
        s->items[idx] = value;
        return;
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_GROUPCHAT) {
        groupchat_set(&vm->gc, (ObjGroupChat *)AS_OBJ(obj), key, value);
        return;
    }
    if (IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(obj);
        ObjClosure *method = squad_find_method(inst->squad, "set_it");
        if (method == NULL) {
            vm_throw_no_such_method(vm, inst->squad->name->chars, "set_it");
            return;
        }
        /* obj/key/value are already off the FunnyLang stack (OP_SET_INDEX
           pops all three before calling here) -- root them for the call,
           same hazard as call_bound_native's own comment. */
        gc_push_temp(&vm->gc, obj);
        gc_push_temp(&vm->gc, key);
        gc_push_temp(&vm->gc, value);
        Value args[3] = {obj, key, value};
        vm_call_value(vm, OBJ_VAL(method), args, 3);
        gc_pop_temp(&vm->gc);
        gc_pop_temp(&vm->gc);
        gc_pop_temp(&vm->gc);
        return;
    }
    if (IS_STRING(obj)) {
        vm_throw(vm, "TypeVibeMismatch", "yapstring is immutable. make a new one.");
        return;
    }
    if (IS_GHOST(obj)) {
        vm_throw(vm, "GhostError", "can't index-assign ghost.");
        return;
    }
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't index-assign a %s.", type_name_of(obj));
}

/* Python's slice.indices(length) semantics, ported (CPython's own
   PySlice_AdjustIndices): a ghost start/stop defaults to "the end nearest
   what `step`'s direction would naturally start/stop at", and an
   out-of-range explicit bound clamps rather than errors. Must be resolved
   *after* step is known, since the defaults themselves depend on step's
   sign. */
static int64_t resolve_slice_bound(Value v, int64_t length, int64_t step, bool isStart) {
    if (IS_GHOST(v)) {
        if (step > 0) return isStart ? 0 : length;
        return isStart ? length - 1 : -1;
    }
    int64_t x = as_int64_like(v);
    if (x < 0) x += length;
    if (step > 0) {
        if (x < 0) x = 0;
        if (x > length) x = length;
    } else {
        if (x < -1) x = -1;
        if (x > length - 1) x = length - 1;
    }
    return x;
}

static bool slice_bounds_ok(Value startV, Value stopV, Value stepV) {
    return (IS_GHOST(startV) || IS_INT_LIKE(startV)) && (IS_GHOST(stopV) || IS_INT_LIKE(stopV)) &&
           (IS_GHOST(stepV) || IS_INT_LIKE(stepV));
}

static Value vm_get_slice(VM *vm, Value obj, Value startV, Value stopV, Value stepV) {
    if (!slice_bounds_ok(startV, stopV, stepV)) {
        vm_throw(vm, "TypeVibeMismatch", "slice bounds have to be numbas (or left out).");
        return GHOST_VAL;
    }
    bool isStash = IS_OBJ(obj) && AS_OBJ(obj)->type == OBJ_STASH;
    bool isString = IS_STRING(obj);
    if (!isStash && !isString) {
        if (IS_GHOST(obj)) {
            vm_throw(vm, "GhostError", "can't slice ghost.");
        } else {
            vm_throw_fmt(vm, "TypeVibeMismatch", "can't slice a %s.", type_name_of(obj));
        }
        return GHOST_VAL;
    }
    int64_t length = isStash ? ((ObjStash *)AS_OBJ(obj))->count : (int64_t)AS_STRING(obj)->codepointCount;
    int64_t step = IS_GHOST(stepV) ? 1 : as_int64_like(stepV);
    if (step == 0) {
        vm_throw(vm, "MathAintMathin", "slice step can't be zero.");
        return GHOST_VAL;
    }
    int64_t start = resolve_slice_bound(startV, length, step, true);
    int64_t stop = resolve_slice_bound(stopV, length, step, false);
    int64_t count = 0;
    if (step > 0) {
        for (int64_t i = start; i < stop; i += step) count++;
    } else {
        for (int64_t i = start; i > stop; i += step) count++;
    }
    if (isStash) {
        ObjStash *s = (ObjStash *)AS_OBJ(obj);
        Value *buf = count > 0 ? (Value *)malloc((size_t)count * sizeof(Value)) : NULL;
        int64_t n = 0;
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) buf[n++] = s->items[i];
        } else {
            for (int64_t i = start; i > stop; i += step) buf[n++] = s->items[i];
        }
        ObjStash *r = stash_new(&vm->gc, buf, (int)count);
        free(buf);
        return OBJ_VAL(r);
    }
    ObjString *str = AS_STRING(obj);
    if (str->isAscii) {
        /* Codepoint index == byte offset: identical to plain byte slicing. */
        char *buf = count > 0 ? (char *)malloc((size_t)count) : NULL;
        int64_t n = 0;
        if (step > 0) {
            for (int64_t i = start; i < stop; i += step) buf[n++] = str->chars[i];
        } else {
            for (int64_t i = start; i > stop; i += step) buf[n++] = str->chars[i];
        }
        ObjString *r = string_new(&vm->gc, buf, (uint32_t)count);
        free(buf);
        return OBJ_VAL(r);
    }
    /* Non-ASCII: a per-codepoint byte-offset table, built once in a
       single O(n) walk, turns the rest of this (potentially reversed,
       potentially strided) slice into O(m) lookups instead of re-walking
       from byte 0 for every codepoint in the result -- avoids the
       O(n*m) blowup a naive per-index utf8_byte_offset_of call in this
       loop would have (reversing a whole non-ASCII string is exactly
       the case that would hit it hardest). */
    uint32_t cpCount = str->codepointCount;
    uint32_t *offsets = (uint32_t *)malloc((size_t)(cpCount + 1) * sizeof(uint32_t));
    uint32_t bi = 0;
    for (uint32_t ci = 0; ci < cpCount; ci++) {
        offsets[ci] = bi;
        bi += utf8_seq_len(str->chars, str->byteLen, bi);
    }
    offsets[cpCount] = str->byteLen;
    char *buf = (char *)malloc((size_t)count * 4 + 1);
    size_t o = 0;
    if (step > 0) {
        for (int64_t i = start; i < stop; i += step) {
            uint32_t seqLen = offsets[i + 1] - offsets[i];
            memcpy(buf + o, str->chars + offsets[i], seqLen);
            o += seqLen;
        }
    } else {
        for (int64_t i = start; i > stop; i += step) {
            uint32_t seqLen = offsets[i + 1] - offsets[i];
            memcpy(buf + o, str->chars + offsets[i], seqLen);
            o += seqLen;
        }
    }
    free(offsets);
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)o);
    free(buf);
    return OBJ_VAL(r);
}

/* -- pointa deref/set + its own instance methods (PLAN.md §3.10) -------- */

/* Shared by DEREF and the `.deref()` method -- funnylang/vm.py's own
   _pointa_deref. */
static Value pointa_deref_value(VM *vm, ObjPointa *ptr) {
    if (ptr->kind == POINTA_CELL) return upvalue_get(ptr->cell);
    if (ptr->kind == POINTA_GLOBAL) {
        ObjGroupChat *mg = (ObjGroupChat *)AS_OBJ(ptr->moduleGlobals);
        GroupChatEntry *e = groupchat_find(mg, OBJ_VAL(ptr->globalName));
        if (e != NULL) return e->value;
        GlobalEntry *g = builtins_find(vm, ptr->globalName);
        if (g == NULL) {
            vm_throw_fmt(vm, "WhoDis", "'%s' isn't defined.", ptr->globalName->chars);
            return GHOST_VAL;
        }
        return g->value;
    }
    if (ptr->kind == POINTA_INDEX) return vm_get_index(vm, ptr->container, ptr->key);
    return vm_get_prop(vm, ptr->container, AS_STRING(ptr->key)); /* POINTA_PROP */
}

/* Shared by SET_DEREF and the `.set()` method -- funnylang/vm.py's own
   _pointa_set (which also returns `value`; callers that want that do it
   themselves, matching DEREF/SET_DEREF's existing push(vm, value)). */
static void pointa_set_value(VM *vm, ObjPointa *ptr, Value value) {
    if (ptr->kind == POINTA_CELL) {
        upvalue_set(ptr->cell, value);
    } else if (ptr->kind == POINTA_GLOBAL) {
        groupchat_set(&vm->gc, (ObjGroupChat *)AS_OBJ(ptr->moduleGlobals), OBJ_VAL(ptr->globalName), value);
    } else if (ptr->kind == POINTA_INDEX) {
        vm_set_index(vm, ptr->container, ptr->key, value);
    } else {
        vm_set_prop(vm, ptr->container, AS_STRING(ptr->key), value); /* POINTA_PROP */
    }
}

static Value m_pointa_deref(VM *vm, Value *a, int argc) {
    (void)argc;
    return pointa_deref_value(vm, (ObjPointa *)AS_OBJ(a[0]));
}

static Value m_pointa_set(VM *vm, Value *a, int argc) {
    (void)argc;
    pointa_set_value(vm, (ObjPointa *)AS_OBJ(a[0]), a[1]);
    return a[1];
}

static Value m_pointa_valid(VM *vm, Value *a, int argc) {
    (void)argc;
    pointa_deref_value(vm, (ObjPointa *)AS_OBJ(a[0]));
    if (vm->hadError) {
        /* funnylang/vm.py's own _pointa_valid: `try: deref(); return True
           except FunnyError: return False` -- the one place in the whole
           VM where an error is deliberately caught and swallowed rather
           than left pending for the dispatch loop to unwind. */
        vm->hadError = false;
        vm->pendingError = GHOST_VAL;
        return BOOL_VAL(false);
    }
    return BOOL_VAL(true);
}

static Value m_pointa_where(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)argc;
    ObjPointa *p = (ObjPointa *)AS_OBJ(a[0]);
    if (p->kind == POINTA_CELL || p->kind == POINTA_GLOBAL) return OBJ_VAL(p->label);
    return p->key; /* "index"/"prop": the raw key/field name, not the label */
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} PointaMethodEntry;

static const PointaMethodEntry POINTA_METHOD_TABLE[] = {
    {"deref", m_pointa_deref, 0, 0},
    {"set", m_pointa_set, 1, 1},
    {"valid", m_pointa_valid, 0, 0},
    {"where", m_pointa_where, 0, 0},
};
#define POINTA_METHOD_TABLE_COUNT (int)(sizeof(POINTA_METHOD_TABLE) / sizeof(POINTA_METHOD_TABLE[0]))

static NativeMethodFn pointa_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < POINTA_METHOD_TABLE_COUNT; i++) {
        if (strcmp(POINTA_METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = POINTA_METHOD_TABLE[i].minArity;
            *outMaxArity = POINTA_METHOD_TABLE[i].maxArity;
            return POINTA_METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}

/* -- iteration (NATIVE_PLAN.md N3 task 5's ITER_NEW/ITER_NEXT, ported
   here since N3 never actually implemented it -- see iterator.h's own
   note) -- mirrors funnylang/vm.py's _make_iter_gen. */
static ObjIterator *make_iterator(VM *vm, Value iterable) {
    if (IS_OBJ(iterable) && AS_OBJ(iterable)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(iterable);
        return iterator_new(&vm->gc, s->items, s->count);
    }
    if (IS_STRING(iterable)) {
        /* Codepoint-at-a-time, matching Python's own `iter(str)` exactly. */
        ObjString *str = AS_STRING(iterable);
        Value *chars = str->codepointCount > 0 ? (Value *)malloc((size_t)str->codepointCount * sizeof(Value)) : NULL;
        uint32_t bi = 0;
        for (uint32_t ci = 0; ci < str->codepointCount; ci++) {
            uint32_t seqLen = utf8_seq_len(str->chars, str->byteLen, bi);
            chars[ci] = OBJ_VAL(string_new(&vm->gc, str->chars + bi, seqLen));
            bi += seqLen;
        }
        ObjIterator *it = iterator_new(&vm->gc, chars, (int)str->codepointCount);
        free(chars);
        return it;
    }
    if (IS_OBJ(iterable) && AS_OBJ(iterable)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(iterable);
        Value *keys = g->count > 0 ? (Value *)malloc((size_t)g->count * sizeof(Value)) : NULL;
        for (int i = 0; i < g->count; i++) keys[i] = g->entries[i].key;
        ObjIterator *it = iterator_new(&vm->gc, keys, g->count);
        free(keys);
        return it;
    }
    if (IS_GHOST(iterable)) {
        vm_throw(vm, "GhostError", "can't iterate over ghost.");
        return NULL;
    }
    vm_throw_fmt(vm, "TypeVibeMismatch", "can't iterate over a %s.", type_name_of(iterable));
    return NULL;
}

/* -- the dispatch loop ---------------------------------------------------- */

static uint16_t read_u16(const uint8_t *code, uint32_t ip) {
    return (uint16_t)((code[ip] << 8) | code[ip + 1]);
}

static uint32_t read_u32(const uint8_t *code, uint32_t ip) {
    return ((uint32_t)code[ip] << 24) | ((uint32_t)code[ip + 1] << 16) | ((uint32_t)code[ip + 2] << 8) | code[ip + 3];
}

/* Runs frames until vm->frameCount drops back to `baseFrameCount` (a
   RETURN did it) or an error escapes past it (vm_unwind_to_handler found
   nothing to catch it within this execution's own frames) -- mirrors
   funnylang/vm.py's own `_run(base_frame_count)` exactly, including that
   it's reentrant: a native method's callback (stash's sort/glow_up/
   vibe_check/squish/any/all, via vm_call_value) calls this again with a
   freshly-raised base while the outer vm_execute call is still live
   further down the C stack, one real C stack frame per nesting level
   (ordinary FunnyLang recursion never does this -- see vm.h's own note on
   why CALL/RETURN alone never need a new C stack frame). On error escaping
   past `baseFrameCount`, returns VM_ERROR *without* clearing
   vm->hadError/pendingError -- the caller (an outer vm_execute, or vm_run
   at the true top level) notices hadError still set on its own next
   iteration and retries unwinding at its own, lower base, rather than
   this nested call silently swallowing or misattributing the error. */
static VmResult vm_execute(VM *vm, int baseFrameCount, Value *resultOut) {
    if (resultOut != NULL) *resultOut = GHOST_VAL;
    for (;;) {
        if (vm->hadError) {
            Value errValue = vm->pendingError;
            vm->hadError = false;
            vm->pendingError = GHOST_VAL;
            if (vm_unwind_to_handler(vm, errValue, baseFrameCount)) continue;
            vm->hadError = true; /* left pending for an outer vm_execute/vm_run to retry */
            vm->pendingError = errValue;
            return VM_ERROR;
        }
        if (vm->frameCount == baseFrameCount) return VM_OK; /* only possible on entry: 0 args to run */

        gc_maybe_collect(&vm->gc);

        vm->currentFrameIndex = vm->frameCount - 1;
        Frame *frame = current_frame(vm);
        const uint8_t *code = frame->closure->proto->code;
        ConstEntry *consts = frame->closure->unit->consts;
        vm->currentInstrStart = frame->ip;
        uint8_t op = code[frame->ip++];

        switch ((Op)op) {
            case OP_NOP:
                break;
            case OP_CONST: {
                uint16_t idx = read_u16(code, frame->ip);
                frame->ip += 2;
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
                push(vm, vm->stack[frame->slotBase + code[frame->ip++]]);
                break;
            case OP_SET_LOCAL:
                vm->stack[frame->slotBase + code[frame->ip++]] = peek(vm, 0);
                break;
            case OP_GET_GLOBAL: {
                /* This module's own globals first, then the always-in-
                   scope builtins -- matches funnylang/vm.py's own
                   _read_global exactly. */
                uint16_t idx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                ObjGroupChat *mg = (ObjGroupChat *)AS_OBJ(frame->closure->moduleGlobals);
                GroupChatEntry *e = groupchat_find(mg, OBJ_VAL(name));
                if (e != NULL) {
                    push(vm, e->value);
                    break;
                }
                GlobalEntry *g = builtins_find(vm, name);
                if (g == NULL) {
                    vm_throw_fmt(vm, "WhoDis", "'%s' isn't defined.", name->chars);
                    break;
                }
                push(vm, g->value);
                break;
            }
            case OP_SET_GLOBAL: {
                uint16_t idx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                groupchat_set(&vm->gc, (ObjGroupChat *)AS_OBJ(frame->closure->moduleGlobals), OBJ_VAL(name), peek(vm, 0));
                break;
            }
            case OP_DEF_GLOBAL: {
                uint16_t idx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                groupchat_set(&vm->gc, (ObjGroupChat *)AS_OBJ(frame->closure->moduleGlobals), OBJ_VAL(name), pop(vm));
                break;
            }
            case OP_GET_UPVAL:
                push(vm, upvalue_get(frame->closure->upvalues[code[frame->ip++]]));
                break;
            case OP_SET_UPVAL:
                upvalue_set(frame->closure->upvalues[code[frame->ip++]], peek(vm, 0));
                break;
            case OP_CLOSE_UPVAL:
                close_upvalues_from(vm, vm->stackCount - 1);
                pop(vm);
                break;
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
                vm->stack[vm->stackCount - 1] = BOOL_VAL(vm_value_equal(vm, a, b));
                break;
            }
            case OP_NEQ: {
                Value b = pop(vm);
                Value a = peek(vm, 0);
                vm->stack[vm->stackCount - 1] = BOOL_VAL(!vm_value_equal(vm, a, b));
                break;
            }
            case OP_IN: {
                /* funnylang/vm.py's own _contains: `a in b` -- b is the
                   container. Stash/GroupChat/yapstring only. `b` is
                   rooted for the loop's duration: it's already off the
                   FunnyLang stack by this point (popped above), and each
                   vm_value_equal call below can trigger a collection via a
                   nested same_energy dispatch -- the same hazard
                   call_bound_native's own comment describes. */
                Value b = pop(vm);
                Value a = peek(vm, 0);
                gc_push_temp(&vm->gc, b);
                bool result;
                if (IS_OBJ(b) && AS_OBJ(b)->type == OBJ_STASH) {
                    ObjStash *s = (ObjStash *)AS_OBJ(b);
                    result = false;
                    for (int i = 0; i < s->count; i++) {
                        if (vm_value_equal(vm, a, s->items[i])) { result = true; break; }
                    }
                } else if (IS_OBJ(b) && AS_OBJ(b)->type == OBJ_GROUPCHAT) {
                    ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(b);
                    result = false;
                    for (int i = 0; i < g->count; i++) {
                        if (vm_value_equal(vm, a, g->entries[i].key)) { result = true; break; }
                    }
                } else if (IS_STRING(b)) {
                    if (!IS_STRING(a)) {
                        vm_throw(vm, "TypeVibeMismatch", "can only check if a yapstring is 'in' another yapstring.");
                        gc_pop_temp(&vm->gc);
                        break;
                    }
                    ObjString *sa = AS_STRING(a), *sb = AS_STRING(b);
                    result = bytes_contains(sb->chars, sb->byteLen, sa->chars, sa->byteLen);
                } else {
                    vm_throw_fmt(vm, "TypeVibeMismatch", "can't check 'in' on a %s.", type_name_of(b));
                    gc_pop_temp(&vm->gc);
                    break;
                }
                gc_pop_temp(&vm->gc);
                vm->stack[vm->stackCount - 1] = BOOL_VAL(result);
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
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2 + off;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                Value cond = pop(vm);
                if (!value_is_truthy(cond)) frame->ip += off;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                Value cond = pop(vm);
                if (value_is_truthy(cond)) frame->ip += off;
                break;
            }
            case OP_JUMP_IF_FALSE_KEEP: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                if (!value_is_truthy(peek(vm, 0))) frame->ip += off;
                break;
            }
            case OP_JUMP_IF_TRUE_KEEP: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                if (value_is_truthy(peek(vm, 0))) frame->ip += off;
                break;
            }
            case OP_JUMP_IF_GHOST_KEEP: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                if (IS_GHOST(peek(vm, 0))) frame->ip += off;
                break;
            }
            case OP_LOOP: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2 - off;
                break;
            }
            case OP_JUMP_LONG: {
                uint32_t off = read_u32(code, frame->ip);
                frame->ip += 4 + off;
                break;
            }
            case OP_LOOP_LONG: {
                uint32_t off = read_u32(code, frame->ip);
                frame->ip += 4 - off;
                break;
            }
            case OP_CALL: {
                uint8_t argc = code[frame->ip++];
                do_call(vm, argc);
                break;
            }
            case OP_INVOKE: {
                uint16_t idx = read_u16(code, frame->ip);
                uint8_t argc = code[frame->ip + 2];
                frame->ip += 3;
                do_invoke(vm, AS_STRING(consts[idx].value), argc);
                break;
            }
            case OP_INVOKE_OG: {
                uint16_t idx = read_u16(code, frame->ip);
                uint8_t argc = code[frame->ip + 2];
                frame->ip += 3;
                do_invoke_og(vm, AS_STRING(consts[idx].value), argc);
                break;
            }
            case OP_CLOSURE: {
                uint16_t constIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                uint32_t protoIdx = consts[constIdx].protoRef;
                FunctionProto *proto = &frame->closure->unit->protos[protoIdx];
                ObjUpvalue *upvalues[256];
                for (int i = 0; i < proto->upvalueCount; i++) {
                    uint8_t isLocal = code[frame->ip];
                    uint8_t uvIdx = code[frame->ip + 1];
                    frame->ip += 2;
                    upvalues[i] = isLocal ? capture_upvalue(vm, frame->slotBase + uvIdx) : frame->closure->upvalues[uvIdx];
                }
                ObjClosure *c = closure_new(&vm->gc, proto, upvalues, proto->upvalueCount,
                                            frame->closure->moduleGlobals, frame->closure->moduleExports,
                                            frame->closure->unit);
                push(vm, OBJ_VAL(c));
                break;
            }
            case OP_RETURN: {
                Value retVal = pop(vm);
                close_upvalues_from(vm, frame->slotBase);
                vm->stackCount = frame->slotBase;
                frame_destroy(frame);
                vm->frameCount--;
                if (vm->frameCount == baseFrameCount) {
                    if (resultOut != NULL) *resultOut = retVal;
                    return VM_OK;
                }
                push(vm, retVal);
                break;
            }
            case OP_CHUCK: {
                Value payload = pop(vm);
                if (IS_OBJ(payload) && AS_OBJ(payload)->type == OBJ_ERROR) {
                    vm->pendingError = payload;
                } else {
                    char *display = value_to_display(vm, payload);
                    uint32_t line, col;
                    chunk_line_for_offset(frame->closure->proto, vm->currentInstrStart, &line, &col);
                    int traceCount;
                    char **trace = build_trace(vm, &traceCount);
                    ObjError *e = error_new(&vm->gc, "SkillIssue", display, NULL, NULL, line, col, vm->unit->sourceName, payload,
                                             trace, traceCount);
                    free_trace(trace, traceCount);
                    free(display);
                    vm->pendingError = OBJ_VAL(e);
                }
                vm->hadError = true;
                break;
            }
            case OP_TRY_PUSH: {
                uint16_t handlerOff = read_u16(code, frame->ip);
                uint16_t finallyOff = read_u16(code, frame->ip + 2);
                uint32_t base = frame->ip + 4;
                int handlerIp = handlerOff == 0xFFFF ? HANDLER_ABSENT : (int)(base + handlerOff);
                int finallyIp = finallyOff == 0xFFFF ? HANDLER_ABSENT : (int)(base + finallyOff);
                frame_push_handler(frame, handlerIp, finallyIp, vm->stackCount);
                frame->ip = base;
                break;
            }
            case OP_TRY_POP:
                frame_pop_handler(frame);
                break;
            case OP_IMPORT: {
                uint16_t idx = read_u16(code, frame->ip);
                uint8_t mode = code[frame->ip + 2];
                frame->ip += 3;
                ObjString *path = AS_STRING(consts[idx].value);
                push(vm, do_import(vm, path->chars, mode));
                break;
            }
            case OP_EXPORT: {
                uint16_t idx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[idx].value);
                groupchat_set(&vm->gc, (ObjGroupChat *)AS_OBJ(frame->closure->moduleExports), OBJ_VAL(name), peek(vm, 0));
                break;
            }
            case OP_ITER_NEW: {
                Value iterable = peek(vm, 0);
                ObjIterator *it = make_iterator(vm, iterable);
                if (it == NULL) break; /* error thrown; dispatch loop unwinds next iteration */
                vm->stack[vm->stackCount - 1] = OBJ_VAL(it);
                break;
            }
            case OP_ITER_NEXT: {
                uint16_t off = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjIterator *it = (ObjIterator *)AS_OBJ(peek(vm, 0));
                if (it->pos < it->count) {
                    push(vm, it->items[it->pos++]);
                } else {
                    frame->ip += off;
                }
                break;
            }
            case OP_SQUAD: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                push(vm, OBJ_VAL(squad_new(&vm->gc, name)));
                break;
            }
            case OP_METHOD: {
                /* compiler.py's own comment on this opcode: stack is
                   [..., squad, methodClosure] -- pop the closure, peek the
                   squad (left in place; SQUAD_DECL keeps building on it). */
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                Value methodVal = pop(vm);
                ObjClosure *method = (ObjClosure *)AS_OBJ(methodVal);
                ObjSquad *squad = (ObjSquad *)AS_OBJ(peek(vm, 0));
                method->homeSquad = squad;
                squad_add_method(&vm->gc, squad, name, method);
                break;
            }
            case OP_INHERIT: {
                /* Stack effect is "super class -> super class" (both kept,
                   unchanged) -- compiler.py's own SWAP+POP right after
                   this drops the now-unneeded superclass reference. */
                ObjSquad *sup = (ObjSquad *)AS_OBJ(peek(vm, 1));
                ObjSquad *sub = (ObjSquad *)AS_OBJ(peek(vm, 0));
                sub->superclass = sup;
                break;
            }
            case OP_PTR_LOCAL: {
                uint8_t slot = code[frame->ip];
                uint16_t nameIdx = read_u16(code, frame->ip + 1);
                frame->ip += 3;
                ObjUpvalue *cell = capture_upvalue(vm, frame->slotBase + slot);
                ObjString *label = AS_STRING(consts[nameIdx].value);
                push(vm, OBJ_VAL(pointa_new_cell(&vm->gc, cell, label)));
                break;
            }
            case OP_PTR_UPVAL: {
                uint8_t idx = code[frame->ip];
                uint16_t nameIdx = read_u16(code, frame->ip + 1);
                frame->ip += 3;
                ObjUpvalue *cell = frame->closure->upvalues[idx];
                ObjString *label = AS_STRING(consts[nameIdx].value);
                push(vm, OBJ_VAL(pointa_new_cell(&vm->gc, cell, label)));
                break;
            }
            case OP_PTR_GLOBAL: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                push(vm, OBJ_VAL(pointa_new_global(&vm->gc, frame->closure->moduleGlobals, name)));
                break;
            }
            case OP_PTR_INDEX: {
                /* No bounds/liveness check here, deliberately -- forming a
                   one-past-the-end (or entirely bogus) pointer is legal;
                   only DEREF/SET_DEREF (and pointer arithmetic) ever look
                   at what it addresses. Matches funnylang/vm.py's own
                   PTR_INDEX exactly. */
                Value key = pop(vm);
                Value obj = pop(vm);
                /* Both already off vm->stack, and vm_index_label's repr of
                   `key` can call back into FunnyLang (an Instance `key`,
                   or one nested inside a Stash/GroupChat `key`, with a
                   `to_yap`) -- root both for the call, same hazard
                   call_bound_native's own comment describes. */
                gc_push_temp(&vm->gc, obj);
                gc_push_temp(&vm->gc, key);
                ObjString *label = vm_index_label(vm, obj, key);
                gc_pop_temp(&vm->gc);
                gc_pop_temp(&vm->gc);
                push(vm, OBJ_VAL(pointa_new_place(&vm->gc, POINTA_INDEX, obj, key, label)));
                break;
            }
            case OP_PTR_PROP: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                Value obj = pop(vm);
                char buf[192];
                snprintf(buf, sizeof buf, "%s.%s", type_name_of(obj), name->chars);
                ObjString *label = string_new(&vm->gc, buf, (uint32_t)strlen(buf));
                push(vm, OBJ_VAL(pointa_new_place(&vm->gc, POINTA_PROP, obj, OBJ_VAL(name), label)));
                break;
            }
            case OP_DEREF: {
                Value p = peek(vm, 0);
                if (!(IS_OBJ(p) && AS_OBJ(p)->type == OBJ_POINTA)) {
                    if (IS_GHOST(p)) {
                        vm_throw(vm, "GhostError", "you dereferenced a ghost, king.");
                    } else {
                        vm_throw_fmt(vm, "TypeVibeMismatch", "can't dereference a %s.", type_name_of(p));
                    }
                    break;
                }
                vm->stack[vm->stackCount - 1] = pointa_deref_value(vm, (ObjPointa *)AS_OBJ(p));
                break;
            }
            case OP_SET_DEREF: {
                Value value = pop(vm);
                Value p = pop(vm);
                if (!(IS_OBJ(p) && AS_OBJ(p)->type == OBJ_POINTA)) {
                    if (IS_GHOST(p)) {
                        vm_throw(vm, "GhostError", "you dereferenced a ghost, king.");
                    } else {
                        vm_throw_fmt(vm, "TypeVibeMismatch", "can't dereference a %s.", type_name_of(p));
                    }
                    break;
                }
                pointa_set_value(vm, (ObjPointa *)AS_OBJ(p), value);
                push(vm, value);
                break;
            }
            case OP_GET_PROP: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                Value obj = pop(vm);
                push(vm, vm_get_prop(vm, obj, name));
                break;
            }
            case OP_GET_PROP_SAFE: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                Value obj = pop(vm);
                push(vm, IS_GHOST(obj) ? GHOST_VAL : vm_get_prop(vm, obj, name));
                break;
            }
            case OP_SET_PROP: {
                uint16_t nameIdx = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjString *name = AS_STRING(consts[nameIdx].value);
                Value value = pop(vm);
                Value obj = pop(vm);
                vm_set_prop(vm, obj, name, value);
                push(vm, value);
                break;
            }
            case OP_GET_INDEX: {
                Value key = pop(vm);
                Value obj = pop(vm);
                push(vm, vm_get_index(vm, obj, key));
                break;
            }
            case OP_SET_INDEX: {
                Value value = pop(vm);
                Value key = pop(vm);
                Value obj = pop(vm);
                vm_set_index(vm, obj, key, value);
                push(vm, value);
                break;
            }
            case OP_GET_SLICE: {
                Value step = pop(vm);
                Value stop = pop(vm);
                Value start = pop(vm);
                Value obj = pop(vm);
                push(vm, vm_get_slice(vm, obj, start, stop, step));
                break;
            }
            case OP_BUILD_STASH: {
                uint16_t n = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjStash *s = stash_new(&vm->gc, n ? &vm->stack[vm->stackCount - n] : NULL, n);
                vm->stackCount -= n;
                push(vm, OBJ_VAL(s));
                break;
            }
            case OP_BUILD_GROUPCHAT: {
                uint16_t n = read_u16(code, frame->ip);
                frame->ip += 2;
                ObjGroupChat *g = groupchat_new(&vm->gc, NULL, 0);
                gc_push_temp(&vm->gc, OBJ_VAL(g));
                int base = vm->stackCount - 2 * n;
                for (int i = 0; i < n; i++) {
                    groupchat_set(&vm->gc, g, vm->stack[base + 2 * i], vm->stack[base + 2 * i + 1]);
                }
                gc_pop_temp(&vm->gc);
                vm->stackCount -= 2 * n;
                push(vm, OBJ_VAL(g));
                break;
            }
            case OP_BUILD_STRING: {
                uint16_t n = read_u16(code, frame->ip);
                frame->ip += 2;
                int base = vm->stackCount - n;
                char *parts[256];
                size_t totalLen = 0;
                for (int i = 0; i < n; i++) {
                    parts[i] = value_to_display(vm, vm->stack[base + i]);
                    totalLen += strlen(parts[i]);
                }
                char *buf = (char *)malloc(totalLen + 1);
                size_t o = 0;
                for (int i = 0; i < n; i++) {
                    size_t pl = strlen(parts[i]);
                    memcpy(buf + o, parts[i], pl);
                    o += pl;
                    free(parts[i]);
                }
                buf[o] = '\0';
                vm->stackCount -= n;
                ObjString *r = string_new(&vm->gc, buf, (uint32_t)o);
                free(buf);
                push(vm, OBJ_VAL(r));
                break;
            }
            case OP_YAP: {
                uint8_t argc = code[frame->ip++];
                uint8_t newline = code[frame->ip++];
                char *parts[256];
                for (int i = argc - 1; i >= 0; i--) parts[i] = value_to_display(vm, pop(vm));
                for (int i = 0; i < argc; i++) {
                    if (i > 0) fputc(' ', vm->out);
                    fputs(parts[i], vm->out);
                    free(parts[i]);
                }
                if (newline) fputc('\n', vm->out);
                break;
            }
            case OP_HALT:
                /* Only the top-level script's own bytecode ever contains a
                   HALT -- a nested vm_execute (a callback closure, via
                   vm_call_value) always ends via RETURN instead. */
                return vm->hadError ? VM_ERROR : VM_OK;
            default:
                vm_throw_fmt(vm, "SkillIssue", "opcode %d not implemented until a later milestone.", op);
                break;
        }
    }
}

VmResult vm_run(VM *vm, CompiledUnit *unit, FILE *out) {
    vm->unit = unit;
    vm->out = out;
    vm->gc.markExternalRoots = mark_vm_roots;
    vm->gc.externalRootsUserdata = vm;

    /* A fresh, empty pair of namespaces -- matches funnylang/vm.py's own
       _make_entry_closure(unit) (module_globals=None, module_exports=None,
       each defaulting to a fresh empty dict). */
    Value freshGlobals = OBJ_VAL(groupchat_new(&vm->gc, NULL, 0));
    Value freshExports = OBJ_VAL(groupchat_new(&vm->gc, NULL, 0));
    FunctionProto *entryProto = &unit->protos[unit->entryProto];
    ObjClosure *entryClosure = closure_new(&vm->gc, entryProto, NULL, 0, freshGlobals, freshExports, unit);
    push_frame(vm, entryClosure, 0);

    VmResult result = vm_execute(vm, 0, NULL);
    if (result == VM_ERROR) vm->uncaughtError = vm->pendingError;
    return result;
}

Value vm_run_module(VM *vm, CompiledUnit *unit, const char *moduleName) {
    /* funnylang/vm.py's run_module: fresh namespaces, run the top level
       once, hand back a Module of whatever it flexed. The `vm->unit` swap
       is that function's own `self.source` swap -- it makes errors raised
       inside the imported module report *its* path, not the importer's. */
    Value freshGlobals = OBJ_VAL(groupchat_new(&vm->gc, NULL, 0));
    gc_push_temp(&vm->gc, freshGlobals);
    Value freshExports = OBJ_VAL(groupchat_new(&vm->gc, NULL, 0));
    gc_push_temp(&vm->gc, freshExports);
    FunctionProto *entryProto = &unit->protos[unit->entryProto];
    ObjClosure *entryClosure = closure_new(&vm->gc, entryProto, NULL, 0, freshGlobals, freshExports, unit);
    gc_push_temp(&vm->gc, OBJ_VAL(entryClosure));

    CompiledUnit *priorUnit = vm->unit;
    const char *priorModule = vm->currentModuleName;
    vm->unit = unit;
    vm->currentModuleName = moduleName;
    vm_call_value(vm, OBJ_VAL(entryClosure), NULL, 0);
    vm->unit = priorUnit;
    vm->currentModuleName = priorModule;

    Value result = GHOST_VAL;
    if (!vm->hadError) {
        ObjString *name = string_new(&vm->gc, moduleName, (uint32_t)strlen(moduleName));
        gc_push_temp(&vm->gc, OBJ_VAL(name));
        result = OBJ_VAL(module_new(&vm->gc, name, freshExports));
        gc_pop_temp(&vm->gc);
    }
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    return result;
}

VmResult vm_run_pak(VM *vm, CompiledPak *pak, FILE *out) {
    CompiledUnit *entry = chunk_pak_find(pak, pak->entryName);
    if (entry == NULL) {
        fprintf(stderr, "this bundle names '%s' as its entry, but doesn't contain it.\n", pak->entryName);
        return VM_ERROR;
    }
    vm->pak = pak;
    vm->pakModuleCache = OBJ_VAL(groupchat_new(&vm->gc, NULL, 0));
    vm->currentModuleName = pak->entryName;
    /* The entry runs through the ordinary path, so it gets the same fresh
       namespaces and the same error handling as a lone .funnyc; the only
       difference is that `pak` is now set, which is what makes a quoted
       `gimme` resolve instead of failing. */
    return vm_run(vm, entry, out);
}

Value vm_call_value(VM *vm, Value callee, Value *args, int argc) {
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_BOUND_NATIVE) {
        ObjBoundNative *bn = (ObjBoundNative *)AS_OBJ(callee);
        if (argc < bn->minArity || argc > bn->maxArity) {
            char wantBuf[32];
            if (bn->minArity == bn->maxArity) snprintf(wantBuf, sizeof wantBuf, "%d", bn->minArity);
            else snprintf(wantBuf, sizeof wantBuf, "%d-%d", bn->minArity, bn->maxArity);
            vm_throw_wrong_homies(vm, bn->name, wantBuf, argc);
            return GHOST_VAL;
        }
        Value full[257];
        full[0] = bn->receiver;
        for (int i = 0; i < argc; i++) full[i + 1] = args[i];
        return bn->fn(vm, full, argc + 1);
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_NATIVE_FN) {
        ObjNativeFn *nf = (ObjNativeFn *)AS_OBJ(callee);
        if (argc < nf->minArity || argc > nf->maxArity) {
            char wantBuf[32];
            if (nf->minArity == nf->maxArity) snprintf(wantBuf, sizeof wantBuf, "%d", nf->minArity);
            else snprintf(wantBuf, sizeof wantBuf, "%d-%d", nf->minArity, nf->maxArity);
            vm_throw_wrong_homies(vm, nf->name, wantBuf, argc);
            return GHOST_VAL;
        }
        return nf->fn(vm, args, argc);
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_COMBO) {
        ObjCombo *combo = (ObjCombo *)AS_OBJ(callee);
        if (argc > 1) {
            vm_throw_fmt(vm, "WrongNumberOfHomies", "'combo' wants 0-1 args, got %d.", argc);
            return GHOST_VAL;
        }
        return combo_call(vm, combo, argc > 0 ? args[0] : GHOST_VAL);
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_CLOSURE) {
        ObjClosure *closure = (ObjClosure *)AS_OBJ(callee);
        if (vm->frameCount >= VM_MAX_FRAMES) {
            vm_throw_too_deep(vm);
            return GHOST_VAL;
        }
        if (!closure_arity_ok(vm, closure->proto, argc)) return GHOST_VAL;
        int arity = closure->proto->arity;
        int baseFrameCount = vm->frameCount;
        int slotBase = vm->stackCount;
        for (int i = 0; i < argc; i++) push(vm, args[i]);
        for (int i = argc; i < arity; i++) push(vm, GHOST_VAL);
        push_frame(vm, closure, slotBase);
        Value result;
        vm_execute(vm, baseFrameCount, &result); /* VM_ERROR: vm->hadError stays set for the caller to notice */
        return result;
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_BOUND_METHOD) {
        ObjBoundMethod *bm = (ObjBoundMethod *)AS_OBJ(callee);
        Value full[257];
        full[0] = bm->receiver;
        for (int i = 0; i < argc; i++) full[i + 1] = args[i];
        return vm_call_value(vm, OBJ_VAL(bm->method), full, argc + 1);
    }
    if (IS_OBJ(callee) && AS_OBJ(callee)->type == OBJ_SQUAD) {
        return construct_instance(vm, (ObjSquad *)AS_OBJ(callee), args, argc);
    }
    vm_throw_fmt(vm, "NotACallableRizz", "'%s' is not callable.", type_name_of(callee));
    return GHOST_VAL;
}
