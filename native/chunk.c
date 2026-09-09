/* native/chunk.c -- see chunk.h. Byte-for-byte mirror of
 * funnylang/serializer.py's load_funnyc(): same field order, same widths,
 * same big-endian convention, so a .funnyc the Python compiler produced
 * loads identically here.
 */
#include "chunk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "gc.h"
#include "string.h"

#define MAGIC_LEN 6
static const uint8_t MAGIC_FUNNYC[MAGIC_LEN] = {'F', 'U', 'N', 'N', 'Y', 0};
#define BYTECODE_VERSION 2

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    bool truncated;
} Reader;

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    memcpy(r, s, n);
    return r;
}

static const uint8_t *r_raw(Reader *r, size_t n) {
    if (r->truncated || r->pos + n > r->len) {
        r->truncated = true;
        return NULL;
    }
    const uint8_t *p = r->data + r->pos;
    r->pos += n;
    return p;
}

static uint8_t r_u8(Reader *r) {
    const uint8_t *p = r_raw(r, 1);
    return p ? p[0] : 0;
}

static uint16_t r_u16(Reader *r) {
    const uint8_t *p = r_raw(r, 2);
    if (!p) return 0;
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t r_u32(Reader *r) {
    const uint8_t *p = r_raw(r, 4);
    if (!p) return 0;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Returns a malloc'd, NUL-terminated copy; *outLen excludes the NUL. */
static char *r_str(Reader *r, uint32_t *outLen) {
    uint32_t n = r_u32(r);
    const uint8_t *p = r_raw(r, n);
    if (!p) {
        if (outLen) *outLen = 0;
        return dup_str("");
    }
    char *s = (char *)malloc((size_t)n + 1);
    memcpy(s, p, n);
    s[n] = '\0';
    if (outLen) *outLen = n;
    return s;
}

static double r_double_be(Reader *r) {
    const uint8_t *p = r_raw(r, 8);
    if (!p) return 0.0;
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) bits = (bits << 8) | p[i];
    double v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

static ObjBignum *r_int_magnitude(Reader *r) {
    uint8_t sign = r_u8(r);
    uint32_t n = r_u32(r);
    const uint8_t *p = r_raw(r, n);
    if (!p) return bignum_from_int64(0);
    /* magnitude is big-endian bytes; bignum_from_digits wants decimal
       digits, so build the value byte-by-byte instead (base-256 digits
       via the same "multiply running value by base, add digit" approach
       bignum_from_digits itself uses -- just base 256 here). */
    ObjBignum *acc = bignum_from_int64(0);
    ObjBignum *base = bignum_from_int64(256);
    for (uint32_t i = 0; i < n; i++) {
        ObjBignum *scaled = bignum_mul(acc, base);
        bignum_free(acc);
        ObjBignum *digit = bignum_from_int64(p[i]);
        acc = bignum_add(scaled, digit);
        bignum_free(scaled);
        bignum_free(digit);
    }
    bignum_free(base);
    if (sign && !bignum_is_zero(acc)) {
        ObjBignum *neg = bignum_negate(acc);
        bignum_free(acc);
        acc = neg;
    }
    return acc;
}

static char *fmt_error(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return dup_str(buf);
}

static void free_proto(FunctionProto *p) {
    free(p->name);
    free(p->code);
    free(p->lines);
}

CompiledUnit *chunk_load_funnyc(const uint8_t *data, size_t len, GC *gc, char **err) {
    *err = NULL;
    Reader r = {data, len, 0, false};

    const uint8_t *magic = r_raw(&r, MAGIC_LEN);
    if (r.truncated || memcmp(magic, MAGIC_FUNNYC, MAGIC_LEN) != 0) {
        *err = dup_str("this isn't a .funnyc file.");
        return NULL;
    }
    uint16_t version = r_u16(&r);
    if (version != BYTECODE_VERSION) {
        *err = fmt_error("bytecode version %u != %d.", version, BYTECODE_VERSION);
        return NULL;
    }
    r_u16(&r); /* flags -- informational only, same as the Python loader */

    CompiledUnit *unit = (CompiledUnit *)calloc(1, sizeof(CompiledUnit));
    uint32_t sourceNameLen;
    unit->sourceName = r_str(&r, &sourceNameLen);

    unit->constCount = r_u32(&r);
    unit->consts = (ConstEntry *)calloc(unit->constCount, sizeof(ConstEntry));
    for (uint32_t i = 0; i < unit->constCount && !r.truncated; i++) {
        uint8_t tag = r_u8(&r);
        ConstEntry *entry = &unit->consts[i];
        switch (tag) {
            case CTAG_GHOST:
                entry->tag = CTAG_GHOST;
                entry->value = GHOST_VAL;
                break;
            case CTAG_BOOL:
                entry->tag = CTAG_BOOL;
                entry->value = BOOL_VAL(r_u8(&r) != 0);
                break;
            case CTAG_INT: {
                entry->tag = CTAG_INT;
                ObjBignum *big = r_int_magnitude(&r);
                int64_t asInt;
                if (bignum_to_int64(big, &asInt)) {
                    entry->value = INT_VAL(asInt);
                    bignum_free(big);
                } else {
                    gc_track(gc, (Obj *)big, sizeof(ObjBignum));
                    entry->value = OBJ_VAL(big);
                }
                break;
            }
            case CTAG_FLOAT:
                entry->tag = CTAG_FLOAT;
                entry->value = FLOAT_VAL(r_double_be(&r));
                break;
            case CTAG_STRING: {
                entry->tag = CTAG_STRING;
                uint32_t sLen;
                char *s = r_str(&r, &sLen);
                ObjString *str = string_new(gc, s, sLen);
                free(s);
                entry->value = OBJ_VAL(str);
                break;
            }
            case CTAG_PROTO_REF:
                entry->tag = CTAG_PROTO_REF;
                entry->protoRef = r_u32(&r);
                break;
            default:
                *err = fmt_error("unknown constant tag %u in this bytecode.", tag);
                free(unit->sourceName);
                free(unit->consts);
                free(unit);
                return NULL;
        }
    }

    unit->protoCount = r_u32(&r);
    unit->protos = (FunctionProto *)calloc(unit->protoCount, sizeof(FunctionProto));
    for (uint32_t i = 0; i < unit->protoCount && !r.truncated; i++) {
        FunctionProto *p = &unit->protos[i];
        uint32_t nameLen;
        p->name = r_str(&r, &nameLen);
        p->arity = r_u8(&r);
        p->defaultCount = r_u8(&r);
        p->isVariadic = r_u8(&r) != 0;
        p->upvalueCount = r_u8(&r);
        p->maxStack = r_u16(&r);
        p->localCount = r_u8(&r);
        uint32_t codeLen = r_u32(&r);
        const uint8_t *code = r_raw(&r, codeLen);
        if (code) {
            p->code = (uint8_t *)malloc(codeLen);
            memcpy(p->code, code, codeLen);
            p->codeLen = codeLen;
        }
        p->lineCount = r_u32(&r);
        if (!r.truncated) {
            p->lines = (LineEntry *)calloc(p->lineCount, sizeof(LineEntry));
            for (uint32_t j = 0; j < p->lineCount && !r.truncated; j++) {
                p->lines[j].codeOffset = r_u32(&r);
                p->lines[j].line = r_u32(&r);
                p->lines[j].col = r_u32(&r);
            }
        }
    }

    unit->entryProto = r_u32(&r);

    if (r.truncated) {
        *err = dup_str("this bytecode file is truncated or corrupt.");
        for (uint32_t i = 0; i < unit->protoCount; i++) free_proto(&unit->protos[i]);
        free(unit->protos);
        free(unit->consts);
        free(unit->sourceName);
        free(unit);
        return NULL;
    }

    return unit;
}

void chunk_free_unit(CompiledUnit *unit) {
    if (!unit) return;
    for (uint32_t i = 0; i < unit->protoCount; i++) free_proto(&unit->protos[i]);
    free(unit->protos);
    free(unit->consts);
    free(unit->sourceName);
    free(unit);
}

void chunk_line_for_offset(const FunctionProto *proto, uint32_t offset, uint32_t *lineOut, uint32_t *colOut) {
    uint32_t line = 0, col = 0;
    for (uint32_t i = 0; i < proto->lineCount; i++) {
        if (proto->lines[i].codeOffset > offset) break;
        line = proto->lines[i].line;
        col = proto->lines[i].col;
    }
    *lineOut = line;
    *colOut = col;
}
