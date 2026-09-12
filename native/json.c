/* native/json.c -- see json.h. */
#include "json.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "blob.h"
#include "da_string.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "numfmt.h"
#include "stash.h"
#include "value.h"
#include "vm.h"

/* Deep enough for any document a person wrote, shallow enough that a body
   made of ten thousand open brackets cannot walk the C stack off the end of
   its own thread. The VM's own frame limit does not help here: this recurses
   in C, not in FunnyLang. */
#define MAX_DEPTH 512

/* -- a growable byte buffer ------------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    size_t want = b->cap == 0 ? 64 : b->cap;
    while (want < b->len + extra) want *= 2;
    b->data = (char *)realloc(b->data, want);
    b->cap = want;
}

static void buf_put(Buf *b, const char *s, size_t n) {
    if (n == 0) return;
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void buf_putc(Buf *b, char c) {
    buf_reserve(b, 1);
    b->data[b->len++] = c;
}

static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }

/* -- reading --------------------------------------------------------------- */

typedef struct {
    VM *vm;
    const char *src;
    uint32_t len;
    uint32_t i;
    bool failed;
} Parser;

/* Every failure is one SkillIssue naming where it gave up, and nothing
   half-built is ever handed back. The position is counted in characters
   rather than bytes: it is meant to be findable in the text somebody is
   looking at, and that text may not be ASCII. */
static void fail(Parser *p, const char *what) {
    if (p->failed) return;
    p->failed = true;
    uint32_t at = utf8_codepoint_count(p->src, p->i);
    vm_throw_native(p->vm, "SkillIssue", "that isn't JSON: %s (at character %u).", what, at);
}

static char peek(Parser *p) { return p->i < p->len ? p->src[p->i] : '\0'; }

static void skip_space(Parser *p) {
    while (p->i < p->len) {
        char c = p->src[p->i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        p->i++;
    }
}

static Value read_value(Parser *p, int depth);

static bool expect_word(Parser *p, const char *word) {
    size_t n = strlen(word);
    if (p->i + n > p->len || memcmp(p->src + p->i, word, n) != 0) {
        char what[64];
        snprintf(what, sizeof what, "expected '%s'", word);
        fail(p, what);
        return false;
    }
    p->i += (uint32_t)n;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* The four hex digits of a \u escape, starting at `at`. -1 on anything that
   is not four hex digits, with the failure already reported. */
static int32_t hex4(Parser *p, uint32_t at) {
    int32_t value = 0;
    for (int k = 0; k < 4; k++) {
        if (at + (uint32_t)k >= p->len) {
            fail(p, "a \\u escape needs four hex digits");
            return -1;
        }
        int d = hex_digit(p->src[at + k]);
        if (d < 0) {
            fail(p, "a \\u escape needs four hex digits");
            return -1;
        }
        value = value * 16 + d;
    }
    return value;
}

/* A JSON string, with its escapes resolved, as a fresh yapstring. */
static Value read_string(Parser *p) {
    Buf out = {NULL, 0, 0};
    uint32_t i = p->i + 1; /* past the opening quote */
    for (;;) {
        if (i >= p->len) {
            p->i = i;
            fail(p, "a string was never closed");
            free(out.data);
            return GHOST_VAL;
        }
        char ch = p->src[i];
        if (ch == '"') {
            p->i = i + 1;
            Value v = OBJ_VAL(string_new(&p->vm->gc, out.data != NULL ? out.data : "", (uint32_t)out.len));
            free(out.data);
            return v;
        }
        if (ch == '\\') {
            if (i + 1 >= p->len) {
                p->i = i;
                fail(p, "a string was never closed");
                free(out.data);
                return GHOST_VAL;
            }
            char esc = p->src[i + 1];
            if (esc == 'n') buf_putc(&out, '\n');
            else if (esc == 't') buf_putc(&out, '\t');
            else if (esc == 'r') buf_putc(&out, '\r');
            else if (esc == 'b') buf_putc(&out, '\b');
            else if (esc == 'f') buf_putc(&out, '\f');
            else if (esc == '"' || esc == '\\' || esc == '/') buf_putc(&out, esc);
            else if (esc == 'u') {
                p->i = i;
                int32_t cp = hex4(p, i + 2);
                if (cp < 0) {
                    free(out.data);
                    return GHOST_VAL;
                }
                i += 4;
                /* A surrogate pair is two escapes that spell one character. */
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 7 < p->len && p->src[i + 2] == '\\' &&
                    p->src[i + 3] == 'u') {
                    int32_t low = hex4(p, i + 4);
                    if (low < 0) {
                        free(out.data);
                        return GHOST_VAL;
                    }
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 6;
                    }
                }
                /* A lone half of a pair is not a character. U+FFFD is what
                   every other decoder puts there, and refusing the whole
                   document over one bad escape helps nobody. */
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                char enc[4];
                uint32_t n = utf8_encode_cp((uint32_t)cp, enc);
                buf_put(&out, enc, n);
            } else {
                p->i = i;
                char what[64];
                snprintf(what, sizeof what, "'\\%c' isn't an escape JSON has", esc);
                fail(p, what);
                free(out.data);
                return GHOST_VAL;
            }
            i += 2;
            continue;
        }
        if ((unsigned char)ch < 0x20) {
            p->i = i;
            fail(p, "a raw control character inside a string");
            free(out.data);
            return GHOST_VAL;
        }
        buf_putc(&out, ch);
        i++;
    }
}

/* JSON's number grammar exactly: an optional minus, digits, an optional
   fraction, an optional exponent. An integer stays an integer -- a numba is
   arbitrary-precision, so a JSON document full of large ids round-trips
   rather than silently becoming doubles. */
static Value read_number(Parser *p) {
    uint32_t start = p->i;
    uint32_t i = start;
    bool isFloat = false;
    bool negative = false;
    if (i < p->len && p->src[i] == '-') {
        negative = true;
        i++;
    }
    uint32_t digitsAt = i;
    while (i < p->len && p->src[i] >= '0' && p->src[i] <= '9') i++;
    if (i == digitsAt) {
        p->i = i;
        fail(p, "a number needs digits");
        return GHOST_VAL;
    }
    uint32_t intDigits = i - digitsAt;
    if (i < p->len && p->src[i] == '.') {
        isFloat = true;
        i++;
        uint32_t fracAt = i;
        while (i < p->len && p->src[i] >= '0' && p->src[i] <= '9') i++;
        if (i == fracAt) {
            p->i = i;
            fail(p, "a number needs digits after its '.'");
            return GHOST_VAL;
        }
    }
    if (i < p->len && (p->src[i] == 'e' || p->src[i] == 'E')) {
        isFloat = true;
        i++;
        if (i < p->len && (p->src[i] == '+' || p->src[i] == '-')) i++;
        uint32_t expAt = i;
        while (i < p->len && p->src[i] >= '0' && p->src[i] <= '9') i++;
        if (i == expAt) {
            p->i = i;
            fail(p, "a number needs digits in its exponent");
            return GHOST_VAL;
        }
    }
    p->i = i;

    size_t n = (size_t)(i - start);
    char *text = (char *)malloc(n + 1);
    memcpy(text, p->src + start, n);
    text[n] = '\0';

    Value out;
    if (isFloat) {
        out = FLOAT_VAL(strtod(text, NULL));
    } else {
        errno = 0;
        char *end = NULL;
        long long v = strtoll(text, &end, 10);
        if (errno == ERANGE) {
            /* Too large for a fixnum, so it becomes a bignum rather than
               losing its low digits to a double -- an id is exactly the kind
               of number that is both huge and exact. */
            ObjBignum *big = bignum_from_digits(p->src + start + (negative ? 1 : 0), (int)intDigits, 10, negative);
            gc_track(&p->vm->gc, (Obj *)big, sizeof(ObjBignum));
            out = OBJ_VAL(big);
        } else {
            out = INT_VAL((int64_t)v);
        }
    }
    free(text);
    return out;
}

static Value read_array(Parser *p, int depth) {
    ObjStash *out = stash_new(&p->vm->gc, NULL, 0);
    gc_push_temp(&p->vm->gc, OBJ_VAL(out));
    p->i++; /* past '[' */
    skip_space(p);
    if (peek(p) == ']') {
        p->i++;
        gc_pop_temp(&p->vm->gc);
        return OBJ_VAL(out);
    }
    for (;;) {
        skip_space(p);
        Value item = read_value(p, depth + 1);
        if (p->failed) break;
        gc_push_temp(&p->vm->gc, item);
        stash_push(&p->vm->gc, out, item);
        gc_pop_temp(&p->vm->gc);
        skip_space(p);
        char next = peek(p);
        p->i++;
        if (next == ']') {
            gc_pop_temp(&p->vm->gc);
            return OBJ_VAL(out);
        }
        if (next != ',') {
            p->i--;
            fail(p, "expected ',' or ']'");
            break;
        }
    }
    gc_pop_temp(&p->vm->gc);
    return GHOST_VAL;
}

static Value read_object(Parser *p, int depth) {
    ObjGroupChat *out = groupchat_new(&p->vm->gc, NULL, 0);
    gc_push_temp(&p->vm->gc, OBJ_VAL(out));
    p->i++; /* past '{' */
    skip_space(p);
    if (peek(p) == '}') {
        p->i++;
        gc_pop_temp(&p->vm->gc);
        return OBJ_VAL(out);
    }
    for (;;) {
        skip_space(p);
        if (peek(p) != '"') {
            fail(p, "expected a key in quotes");
            break;
        }
        Value key = read_string(p);
        if (p->failed) break;
        gc_push_temp(&p->vm->gc, key);
        skip_space(p);
        if (peek(p) != ':') {
            gc_pop_temp(&p->vm->gc);
            fail(p, "expected ':' after a key");
            break;
        }
        p->i++;
        skip_space(p);
        Value value = read_value(p, depth + 1);
        if (p->failed) {
            gc_pop_temp(&p->vm->gc);
            break;
        }
        gc_push_temp(&p->vm->gc, value);
        groupchat_set(&p->vm->gc, out, key, value);
        gc_pop_temp(&p->vm->gc);
        gc_pop_temp(&p->vm->gc);
        skip_space(p);
        char next = peek(p);
        p->i++;
        if (next == '}') {
            gc_pop_temp(&p->vm->gc);
            return OBJ_VAL(out);
        }
        if (next != ',') {
            p->i--;
            fail(p, "expected ',' or '}'");
            break;
        }
    }
    gc_pop_temp(&p->vm->gc);
    return GHOST_VAL;
}

static Value read_value(Parser *p, int depth) {
    if (depth > MAX_DEPTH) {
        char what[64];
        snprintf(what, sizeof what, "nested deeper than %d", MAX_DEPTH);
        fail(p, what);
        return GHOST_VAL;
    }
    char ch = peek(p);
    if (ch == '{') return read_object(p, depth);
    if (ch == '[') return read_array(p, depth);
    if (ch == '"') return read_string(p);
    if (ch == 't') return expect_word(p, "true") ? BOOL_VAL(true) : GHOST_VAL;
    if (ch == 'f') return expect_word(p, "false") ? BOOL_VAL(false) : GHOST_VAL;
    if (ch == 'n') return expect_word(p, "null") ? GHOST_VAL : GHOST_VAL;
    if (ch == '-' || (ch >= '0' && ch <= '9')) return read_number(p);
    if (ch == '\0') {
        fail(p, "it ended too soon");
        return GHOST_VAL;
    }
    char what[64];
    snprintf(what, sizeof what, "unexpected '%c'", ch);
    fail(p, what);
    return GHOST_VAL;
}

static Value m_parse(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_STRING(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'parse' needs a yapstring, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjString *text = AS_STRING(a[0]);
    Parser p = {vm, text->chars, text->byteLen, 0, false};
    skip_space(&p);
    Value v = read_value(&p, 0);
    if (p.failed) return GHOST_VAL;
    skip_space(&p);
    if (p.i < p.len) {
        fail(&p, "there's more after the end of the value");
        return GHOST_VAL;
    }
    return v;
}

/* -- writing --------------------------------------------------------------- */

/* Containers currently being written, innermost last. A structure that
   contains itself would otherwise recurse until the C stack gave out, and
   there is no honest JSON to write for it. */
typedef struct {
    Obj **items;
    int count;
    int capacity;
} Trail;

static bool trail_contains(const Trail *t, Obj *o) {
    for (int i = 0; i < t->count; i++) {
        if (t->items[i] == o) return true;
    }
    return false;
}

static void trail_push(Trail *t, Obj *o) {
    if (t->count == t->capacity) {
        t->capacity = t->capacity == 0 ? 8 : t->capacity * 2;
        t->items = (Obj **)realloc(t->items, (size_t)t->capacity * sizeof(Obj *));
    }
    t->items[t->count++] = o;
}

static void trail_pop(Trail *t) {
    if (t->count > 0) t->count--;
}

static void write_string(Buf *out, const char *chars, uint32_t byteLen) {
    buf_putc(out, '"');
    uint32_t i = 0;
    while (i < byteLen) {
        unsigned char c = (unsigned char)chars[i];
        if (c == '"') {
            buf_puts(out, "\\\"");
            i++;
        } else if (c == '\\') {
            buf_puts(out, "\\\\");
            i++;
        } else if (c == '\n') {
            buf_puts(out, "\\n");
            i++;
        } else if (c == '\r') {
            buf_puts(out, "\\r");
            i++;
        } else if (c == '\t') {
            buf_puts(out, "\\t");
            i++;
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            buf_puts(out, esc);
            i++;
        } else if (c == 0xE2 && i + 2 < byteLen && (unsigned char)chars[i + 1] == 0x80 &&
                   ((unsigned char)chars[i + 2] == 0xA8 || (unsigned char)chars[i + 2] == 0xA9)) {
            /* U+2028 and U+2029 are legal JSON and end a line in JavaScript,
               and a browser is usually the other reader. */
            buf_puts(out, (unsigned char)chars[i + 2] == 0xA8 ? "\\u2028" : "\\u2029");
            i += 3;
        } else {
            buf_putc(out, (char)c);
            i++;
        }
    }
    buf_putc(out, '"');
}

static bool write_value(VM *vm, Buf *out, Value v, const char *indent, int depth, Trail *trail);

static void write_newline_indent(Buf *out, const char *indent, int depth) {
    if (indent == NULL) return;
    buf_putc(out, '\n');
    for (int i = 0; i < depth; i++) buf_puts(out, indent);
}

static bool write_value(VM *vm, Buf *out, Value v, const char *indent, int depth, Trail *trail) {
    if (IS_GHOST(v)) {
        buf_puts(out, "null");
        return true;
    }
    if (IS_BOOL(v)) {
        buf_puts(out, AS_BOOL(v) ? "true" : "false");
        return true;
    }
    if (IS_INT(v)) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)AS_INT(v));
        buf_puts(out, buf);
        return true;
    }
    if (IS_BIGNUM(v)) {
        char *text = bignum_to_decimal_string(AS_BIGNUM(v));
        buf_puts(out, text);
        free(text);
        return true;
    }
    if (IS_FLOAT(v)) {
        double d = AS_FLOAT(v);
        /* JSON has no NaN and no infinity. `null` is what every other
           encoder writes rather than emitting something unparseable. */
        if (d != d || d == HUGE_VAL || d == -HUGE_VAL) {
            buf_puts(out, "null");
            return true;
        }
        char *text = numfmt_repr(d);
        buf_puts(out, text);
        free(text);
        return true;
    }
    if (IS_STRING(v)) {
        write_string(out, AS_STRING(v)->chars, AS_STRING(v)->byteLen);
        return true;
    }
    if (IS_BLOB(v)) {
        /* JSON has no bytes. base64 text is what everything else does, and
           refusing to write a value somebody put in a record is worse. It
           does not come back as a blob: nothing in the text says it was one. */
        ObjBlob *b = AS_BLOB(v);
        uint32_t n = 0;
        char *text = blob_base64_encode(b->bytes, b->byteLen, &n);
        write_string(out, text, n);
        free(text);
        return true;
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STASH) {
        ObjStash *s = (ObjStash *)AS_OBJ(v);
        if (trail_contains(trail, AS_OBJ(v))) {
            vm_throw_native(vm, "OutOfPocket", "that stash contains itself, so it can't be written as JSON.");
            return false;
        }
        if (s->count == 0) {
            buf_puts(out, "[]");
            return true;
        }
        trail_push(trail, AS_OBJ(v));
        buf_putc(out, '[');
        for (int i = 0; i < s->count; i++) {
            if (i > 0) buf_putc(out, ',');
            write_newline_indent(out, indent, depth + 1);
            if (!write_value(vm, out, s->items[i], indent, depth + 1, trail)) return false;
        }
        write_newline_indent(out, indent, depth);
        buf_putc(out, ']');
        trail_pop(trail);
        return true;
    }
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *g = (ObjGroupChat *)AS_OBJ(v);
        if (trail_contains(trail, AS_OBJ(v))) {
            vm_throw_native(vm, "OutOfPocket", "that groupchat contains itself, so it can't be written as JSON.");
            return false;
        }
        if (g->count == 0) {
            buf_puts(out, "{}");
            return true;
        }
        trail_push(trail, AS_OBJ(v));
        buf_putc(out, '{');
        for (int i = 0; i < g->count; i++) {
            if (i > 0) buf_putc(out, ',');
            write_newline_indent(out, indent, depth + 1);
            /* JSON keys are strings, so a numba or boolski key is written as
               the text it displays as -- the alternative is refusing to write
               a groupchat that is otherwise perfectly ordinary. */
            Value key = g->entries[i].key;
            if (IS_STRING(key)) {
                write_string(out, AS_STRING(key)->chars, AS_STRING(key)->byteLen);
            } else {
                size_t keyLen = 0;
                char *text = vm_value_to_display_len(vm, key, &keyLen);
                write_string(out, text, (uint32_t)keyLen);
                free(text);
            }
            buf_putc(out, ':');
            if (indent != NULL) buf_putc(out, ' ');
            if (!write_value(vm, out, g->entries[i].value, indent, depth + 1, trail)) return false;
        }
        write_newline_indent(out, indent, depth);
        buf_putc(out, '}');
        trail_pop(trail);
        return true;
    }
    /* Everything else -- an error, a bet, a squad instance -- as the text it
       displays as, quoted. Matching what the example's own writer did. */
    size_t len = 0;
    char *text = vm_value_to_display_len(vm, v, &len);
    write_string(out, text, (uint32_t)len);
    free(text);
    return true;
}

static Value m_spill(VM *vm, Value *a, int argc) {
    bool pretty = false;
    if (argc > 1 && !IS_GHOST(a[1])) {
        if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_GROUPCHAT)) {
            vm_throw_native(vm, "TypeVibeMismatch", "'spill' options need to be a groupchat, not a %s.",
                            vm_type_name(a[1]));
            return GHOST_VAL;
        }
        ObjGroupChat *opts = (ObjGroupChat *)AS_OBJ(a[1]);
        Value key = OBJ_VAL(string_new(&vm->gc, "pretty", 6));
        GroupChatEntry *e = groupchat_find(opts, key);
        pretty = e != NULL && value_is_truthy(e->value);
    }

    Buf out = {NULL, 0, 0};
    Trail trail = {NULL, 0, 0};
    gc_push_temp(&vm->gc, a[0]);
    bool ok = write_value(vm, &out, a[0], pretty ? "  " : NULL, 0, &trail);
    gc_pop_temp(&vm->gc);
    free(trail.items);
    if (!ok) {
        free(out.data);
        return GHOST_VAL;
    }
    /* Pretty output ends with a newline, because what it is for is a file a
       person opens -- and a file that does not end in one is a file every
       editor and every diff complains about. */
    if (pretty) buf_putc(&out, '\n');
    Value result = OBJ_VAL(string_new(&vm->gc, out.data != NULL ? out.data : "", (uint32_t)out.len));
    free(out.data);
    return result;
}

/* -- the module ------------------------------------------------------------ */

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} JsonEntry;

static const JsonEntry JSON_FUNCTIONS[] = {
    {"parse", m_parse, 1, 1},
    {"spill", m_spill, 1, 2},
};
#define JSON_FUNCTIONS_COUNT (int)(sizeof(JSON_FUNCTIONS) / sizeof(JSON_FUNCTIONS[0]))

Value json_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < JSON_FUNCTIONS_COUNT; i++) {
        const JsonEntry *e = &JSON_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "json", 4);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
