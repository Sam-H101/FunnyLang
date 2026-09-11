/* native/blob.c -- see blob.h. */
#include "blob.h"

#include <stdlib.h>
#include <string.h>

#include "da_string.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "stash.h"
#include "vm.h"

ObjBlob *blob_new(GC *gc, const uint8_t *bytes, uint32_t len) {
    ObjBlob *b = (ObjBlob *)malloc(sizeof(ObjBlob));
    b->obj.type = OBJ_BLOB;
    b->obj.marked = false;
    b->obj.size = 0;
    b->obj.next = NULL;
    b->bytes = (uint8_t *)malloc((size_t)len + 1);
    if (len > 0) memcpy(b->bytes, bytes, len);
    b->bytes[len] = '\0';
    b->byteLen = len;
    gc_track(gc, (Obj *)b, sizeof(ObjBlob) + (size_t)len + 1);
    return b;
}

bool blob_equal(const ObjBlob *a, const ObjBlob *b) {
    if (a == b) return true;
    if (a->byteLen != b->byteLen) return false;
    return memcmp(a->bytes, b->bytes, a->byteLen) == 0;
}

/* -- hex and base64 ------------------------------------------------------ */

static const char HEX_DIGITS[] = "0123456789abcdef";
static const char B64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *blob_hex_encode(const uint8_t *bytes, uint32_t len) {
    char *out = (char *)malloc((size_t)len * 2 + 1);
    for (uint32_t i = 0; i < len; i++) {
        out[2 * i] = HEX_DIGITS[bytes[i] >> 4];
        out[2 * i + 1] = HEX_DIGITS[bytes[i] & 0x0F];
    }
    out[2 * (size_t)len] = '\0';
    return out;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint8_t *blob_hex_decode(const char *text, uint32_t len, uint32_t *outLen) {
    if (len % 2 != 0) return NULL;
    uint8_t *out = (uint8_t *)malloc(len / 2 + 1);
    for (uint32_t i = 0; i < len; i += 2) {
        int hi = hex_value(text[i]), lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            return NULL;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *outLen = len / 2;
    return out;
}

char *blob_base64_encode(const uint8_t *bytes, uint32_t len, uint32_t *outLen) {
    uint32_t groups = (len + 2) / 3;
    uint32_t n = groups * 4;
    char *out = (char *)malloc((size_t)n + 1);
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i += 3) {
        uint32_t bits = (uint32_t)bytes[i] << 16;
        uint32_t have = 1;
        if (i + 1 < len) {
            bits |= (uint32_t)bytes[i + 1] << 8;
            have = 2;
        }
        if (i + 2 < len) {
            bits |= bytes[i + 2];
            have = 3;
        }
        out[o++] = B64_ALPHABET[(bits >> 18) & 0x3F];
        out[o++] = B64_ALPHABET[(bits >> 12) & 0x3F];
        out[o++] = have >= 2 ? B64_ALPHABET[(bits >> 6) & 0x3F] : '=';
        out[o++] = have >= 3 ? B64_ALPHABET[bits & 0x3F] : '=';
    }
    out[o] = '\0';
    *outLen = o;
    return out;
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Strict on purpose: no whitespace skipping, no missing padding, no
   characters outside the alphabet. Base64 that arrives from somewhere else
   is usually being used as an integrity-ish check by whoever sent it, and
   quietly accepting a mangled version of it helps nobody. */
uint8_t *blob_base64_decode(const char *text, uint32_t len, uint32_t *outLen) {
    if (len % 4 != 0) return NULL;
    if (len == 0) {
        *outLen = 0;
        return (uint8_t *)malloc(1);
    }
    uint32_t pad = 0;
    if (text[len - 1] == '=') pad++;
    if (len >= 2 && text[len - 2] == '=') pad++;
    uint8_t *out = (uint8_t *)malloc((size_t)(len / 4) * 3 + 1);
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) {
            char c = text[i + k];
            if (c == '=') {
                /* Padding is only ever the last one or two characters. */
                bool lastGroup = i + 4 == len;
                if (!lastGroup || k < 2 || (k == 2 && text[i + 3] != '=')) {
                    free(out);
                    return NULL;
                }
                v[k] = 0;
                continue;
            }
            v[k] = b64_value(c);
            if (v[k] < 0) {
                free(out);
                return NULL;
            }
        }
        uint32_t bits = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12) | ((uint32_t)v[2] << 6) | (uint32_t)v[3];
        out[o++] = (uint8_t)((bits >> 16) & 0xFF);
        out[o++] = (uint8_t)((bits >> 8) & 0xFF);
        out[o++] = (uint8_t)(bits & 0xFF);
    }
    *outLen = o - pad;
    return out;
}

/* -- argument checking --------------------------------------------------- */

static ObjBlob *check_blob(VM *vm, Value v, const char *fnName) {
    if (!IS_BLOB(v)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a blob, not a %s.", fnName, vm_type_name(v));
        return NULL;
    }
    return AS_BLOB(v);
}

static ObjString *check_str(VM *vm, Value v, const char *fnName) {
    if (!IS_STRING(v)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a yapstring, not a %s.", fnName, vm_type_name(v));
        return NULL;
    }
    return AS_STRING(v);
}

/* -- constructors -------------------------------------------------------- */

static Value m_of(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'of' needs a stash of numbas 0-255, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *s = (ObjStash *)AS_OBJ(a[0]);
    uint8_t *buf = (uint8_t *)malloc(s->count > 0 ? (size_t)s->count : 1);
    for (int i = 0; i < s->count; i++) {
        /* No silent `& 0xFF`, unlike filez.write_bytes: that one is keeping
           faith with an older function's documented behaviour, while this is
           a new constructor, and 300 quietly becoming 44 is a bug nobody
           finds for a week. */
        if (!IS_INT(s->items[i]) || AS_INT(s->items[i]) < 0 || AS_INT(s->items[i]) > 255) {
            free(buf);
            vm_throw_native(vm, "TypeVibeMismatch", "'of' needs a stash of numbas 0-255.");
            return GHOST_VAL;
        }
        buf[i] = (uint8_t)AS_INT(s->items[i]);
    }
    Value out = OBJ_VAL(blob_new(&vm->gc, buf, (uint32_t)s->count));
    free(buf);
    return out;
}

static Value m_from_yap(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjString *s = check_str(vm, a[0], "from_yap");
    if (!s) return GHOST_VAL;
    return OBJ_VAL(blob_new(&vm->gc, (const uint8_t *)s->chars, s->byteLen));
}

static Value m_from_hex(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjString *s = check_str(vm, a[0], "from_hex");
    if (!s) return GHOST_VAL;
    uint32_t len = 0;
    uint8_t *bytes = blob_hex_decode(s->chars, s->byteLen, &len);
    if (bytes == NULL) {
        vm_throw_native(vm, "SkillIssue", "that isn't hex -- it needs an even number of 0-9a-f digits.");
        return GHOST_VAL;
    }
    Value out = OBJ_VAL(blob_new(&vm->gc, bytes, len));
    free(bytes);
    return out;
}

static Value m_from_base64(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjString *s = check_str(vm, a[0], "from_base64");
    if (!s) return GHOST_VAL;
    uint32_t len = 0;
    uint8_t *bytes = blob_base64_decode(s->chars, s->byteLen, &len);
    if (bytes == NULL) {
        vm_throw_native(vm, "SkillIssue", "that isn't base64 -- check the padding and the alphabet.");
        return GHOST_VAL;
    }
    Value out = OBJ_VAL(blob_new(&vm->gc, bytes, len));
    free(bytes);
    return out;
}

/* -- conversions --------------------------------------------------------- */

static Value m_to_yap(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "to_yap");
    if (!b) return GHOST_VAL;
    /* Lossy, deliberately: bytes off a socket are not always UTF-8, and the
       alternative to U+FFFD is refusing to show someone their own data. */
    return OBJ_VAL(string_new_utf8_lossy(&vm->gc, (const char *)b->bytes, b->byteLen));
}

static Value m_to_hex(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "to_hex");
    if (!b) return GHOST_VAL;
    char *hex = blob_hex_encode(b->bytes, b->byteLen);
    Value out = OBJ_VAL(string_new(&vm->gc, hex, b->byteLen * 2));
    free(hex);
    return out;
}

static Value m_to_base64(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "to_base64");
    if (!b) return GHOST_VAL;
    uint32_t len = 0;
    char *text = blob_base64_encode(b->bytes, b->byteLen, &len);
    Value out = OBJ_VAL(string_new(&vm->gc, text, len));
    free(text);
    return out;
}

static Value m_to_stash(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "to_stash");
    if (!b) return GHOST_VAL;
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (uint32_t i = 0; i < b->byteLen; i++) stash_push(&vm->gc, s, INT_VAL(b->bytes[i]));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(s);
}

static Value m_how_thicc(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "how_thicc");
    if (!b) return GHOST_VAL;
    return INT_VAL(b->byteLen);
}

/* -- searching ----------------------------------------------------------- */

/* Where `needle` starts in `hay`, or -1. A zero-length needle is at 0, which
   is what every other index_of in the runtime says. */
static int64_t find_bytes(const uint8_t *hay, uint32_t hayLen, const uint8_t *needle, uint32_t needleLen) {
    if (needleLen == 0) return 0;
    if (needleLen > hayLen) return -1;
    for (uint32_t i = 0; i + needleLen <= hayLen; i++) {
        if (memcmp(hay + i, needle, needleLen) == 0) return (int64_t)i;
    }
    return -1;
}

static Value m_starts_with(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "starts_with");
    if (!b) return GHOST_VAL;
    ObjBlob *p = check_blob(vm, a[1], "starts_with");
    if (!p) return GHOST_VAL;
    if (p->byteLen > b->byteLen) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(b->bytes, p->bytes, p->byteLen) == 0);
}

static Value m_ends_with(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "ends_with");
    if (!b) return GHOST_VAL;
    ObjBlob *p = check_blob(vm, a[1], "ends_with");
    if (!p) return GHOST_VAL;
    if (p->byteLen > b->byteLen) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(b->bytes + (b->byteLen - p->byteLen), p->bytes, p->byteLen) == 0);
}

static Value m_index_of(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "index_of");
    if (!b) return GHOST_VAL;
    ObjBlob *n = check_blob(vm, a[1], "index_of");
    if (!n) return GHOST_VAL;
    return INT_VAL(find_bytes(b->bytes, b->byteLen, n->bytes, n->byteLen));
}

static Value m_contains(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "contains");
    if (!b) return GHOST_VAL;
    ObjBlob *n = check_blob(vm, a[1], "contains");
    if (!n) return GHOST_VAL;
    return BOOL_VAL(find_bytes(b->bytes, b->byteLen, n->bytes, n->byteLen) >= 0);
}

/* -- split and join ------------------------------------------------------ */

static Value m_split(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *b = check_blob(vm, a[0], "split");
    if (!b) return GHOST_VAL;
    ObjBlob *sep = check_blob(vm, a[1], "split");
    if (!sep) return GHOST_VAL;
    if (sep->byteLen == 0) {
        vm_throw_native(vm, "SkillIssue", "can't split on an empty blob -- that separator is everywhere.");
        return GHOST_VAL;
    }
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    uint32_t start = 0;
    for (uint32_t i = 0; i + sep->byteLen <= b->byteLen;) {
        if (memcmp(b->bytes + i, sep->bytes, sep->byteLen) == 0) {
            Value piece = OBJ_VAL(blob_new(&vm->gc, b->bytes + start, i - start));
            gc_push_temp(&vm->gc, piece);
            stash_push(&vm->gc, out, piece);
            gc_pop_temp(&vm->gc);
            i += sep->byteLen;
            start = i;
        } else {
            i++;
        }
    }
    Value last = OBJ_VAL(blob_new(&vm->gc, b->bytes + start, b->byteLen - start));
    gc_push_temp(&vm->gc, last);
    stash_push(&vm->gc, out, last);
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

/* The receiver is the separator, matching `yapper`'s own join: `sep.join(parts)`. */
static Value m_join(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjBlob *sep = check_blob(vm, a[0], "join");
    if (!sep) return GHOST_VAL;
    if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'join' needs a stash of blobs, not a %s.", vm_type_name(a[1]));
        return GHOST_VAL;
    }
    ObjStash *parts = (ObjStash *)AS_OBJ(a[1]);
    size_t total = 0;
    for (int i = 0; i < parts->count; i++) {
        if (!IS_BLOB(parts->items[i])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'join' needs a stash of blobs, found a %s.",
                            vm_type_name(parts->items[i]));
            return GHOST_VAL;
        }
        total += AS_BLOB(parts->items[i])->byteLen;
        if (i > 0) total += sep->byteLen;
    }
    uint8_t *buf = (uint8_t *)malloc(total > 0 ? total : 1);
    size_t o = 0;
    for (int i = 0; i < parts->count; i++) {
        if (i > 0 && sep->byteLen > 0) {
            memcpy(buf + o, sep->bytes, sep->byteLen);
            o += sep->byteLen;
        }
        ObjBlob *p = AS_BLOB(parts->items[i]);
        memcpy(buf + o, p->bytes, p->byteLen);
        o += p->byteLen;
    }
    Value out = OBJ_VAL(blob_new(&vm->gc, buf, (uint32_t)o));
    free(buf);
    return out;
}

/* -- the tables ---------------------------------------------------------- */

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} BlobEntry;

/* Arities exclude the receiver, the same convention stash.h and yapper.h
   use for their own method tables. */
static const BlobEntry BLOB_METHOD_TABLE[] = {
    {"to_yap", m_to_yap, 0, 0},
    {"to_hex", m_to_hex, 0, 0},
    {"to_base64", m_to_base64, 0, 0},
    {"to_stash", m_to_stash, 0, 0},
    {"how_thicc", m_how_thicc, 0, 0},
    {"starts_with", m_starts_with, 1, 1},
    {"ends_with", m_ends_with, 1, 1},
    {"index_of", m_index_of, 1, 1},
    {"contains", m_contains, 1, 1},
    {"split", m_split, 1, 1},
    {"join", m_join, 1, 1},
};
#define BLOB_METHOD_TABLE_COUNT (int)(sizeof(BLOB_METHOD_TABLE) / sizeof(BLOB_METHOD_TABLE[0]))

/* Constructors only exist on the module: there is no blob to call them on. */
static const BlobEntry BLOB_CONSTRUCTORS[] = {
    {"of", m_of, 1, 1},
    {"from_yap", m_from_yap, 1, 1},
    {"from_hex", m_from_hex, 1, 1},
    {"from_base64", m_from_base64, 1, 1},
};
#define BLOB_CONSTRUCTORS_COUNT (int)(sizeof(BLOB_CONSTRUCTORS) / sizeof(BLOB_CONSTRUCTORS[0]))

NativeMethodFn blob_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < BLOB_METHOD_TABLE_COUNT; i++) {
        if (strcmp(BLOB_METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = BLOB_METHOD_TABLE[i].minArity;
            *outMaxArity = BLOB_METHOD_TABLE[i].maxArity;
            return BLOB_METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}

Value blob_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < BLOB_CONSTRUCTORS_COUNT; i++) {
        const BlobEntry *e = &BLOB_CONSTRUCTORS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    /* Every method again as a free function, with the blob as an explicit
       first argument (+1 on the receiver-excluded arities) -- `stash` and
       `yapper` both do this, and a program that prefers one form should not
       have to switch styles for one type. */
    for (int i = 0; i < BLOB_METHOD_TABLE_COUNT; i++) {
        const BlobEntry *e = &BLOB_METHOD_TABLE[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity + 1, e->maxArity + 1);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "blob", 4);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
