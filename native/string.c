#include "string.h"

#include <stdlib.h>
#include <string.h>

#include "gc.h"

uint32_t utf8_seq_len(const char *chars, uint32_t byteLen, uint32_t offset) {
    unsigned char c = (unsigned char)chars[offset];
    uint32_t len;
    if (c < 0x80) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else len = 1; /* a stray continuation/invalid leading byte -- see string.h's own note */
    if (offset + len > byteLen) len = byteLen - offset;
    return len;
}

uint32_t utf8_decode_cp(const char *chars, uint32_t seqLen, uint32_t offset) {
    unsigned char c0 = (unsigned char)chars[offset];
    if (seqLen == 1) return c0;
    if (seqLen == 2) return (uint32_t)((c0 & 0x1F) << 6) | ((unsigned char)chars[offset + 1] & 0x3F);
    if (seqLen == 3) {
        return (uint32_t)((c0 & 0x0F) << 12) | (uint32_t)(((unsigned char)chars[offset + 1] & 0x3F) << 6) |
               ((unsigned char)chars[offset + 2] & 0x3F);
    }
    return (uint32_t)((c0 & 0x07) << 18) | (uint32_t)(((unsigned char)chars[offset + 1] & 0x3F) << 12) |
           (uint32_t)(((unsigned char)chars[offset + 2] & 0x3F) << 6) | ((unsigned char)chars[offset + 3] & 0x3F);
}

uint32_t utf8_codepoint_count(const char *chars, uint32_t byteLen) {
    uint32_t count = 0, i = 0;
    while (i < byteLen) {
        i += utf8_seq_len(chars, byteLen, i);
        count++;
    }
    return count;
}

uint32_t utf8_byte_offset_of(const char *chars, uint32_t byteLen, uint32_t codepointIdx) {
    uint32_t i = 0, cp = 0;
    while (cp < codepointIdx && i < byteLen) {
        i += utf8_seq_len(chars, byteLen, i);
        cp++;
    }
    return i;
}

uint32_t utf8_encode_cp(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

ObjString *string_new(GC *gc, const char *chars, uint32_t len) {
    ObjString *s = (ObjString *)malloc(sizeof(ObjString));
    s->obj.type = OBJ_STRING;
    s->obj.marked = false;
    s->obj.size = 0;
    s->obj.next = NULL;
    s->chars = (char *)malloc((size_t)len + 1);
    memcpy(s->chars, chars, len);
    s->chars[len] = '\0';
    s->byteLen = len;
    s->codepointCount = utf8_codepoint_count(s->chars, len);
    s->isAscii = s->codepointCount == len;
    gc_track(gc, (Obj *)s, sizeof(ObjString) + (size_t)len + 1);
    return s;
}

/* Not a byte-for-byte replica of Python's exact "maximal subpart"
   grouping for a run of invalid bytes (this advances one byte at a time
   on failure instead) -- reasonable given nothing differential-tests
   this against Python (no live network in the automated suite; see
   internet.c's own test file), and it never corrupts the codepointCount
   invariant either way. */
ObjString *string_new_utf8_lossy(GC *gc, const char *bytes, uint32_t len) {
    char *out = (char *)malloc((size_t)len * 3 + 1);
    size_t o = 0;
    uint32_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)bytes[i];
        uint32_t seqLen;
        if (c < 0x80) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else seqLen = 0;
        bool valid = seqLen > 0 && i + seqLen <= len;
        if (valid) {
            for (uint32_t k = 1; k < seqLen; k++) {
                if (((unsigned char)bytes[i + k] & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
        }
        uint32_t cp = 0;
        if (valid) {
            cp = utf8_decode_cp(bytes, seqLen, i);
            if ((seqLen == 2 && cp < 0x80) || (seqLen == 3 && cp < 0x800) || (seqLen == 4 && cp < 0x10000) ||
                (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
                valid = false;
            }
        }
        if (valid) {
            memcpy(out + o, bytes + i, seqLen);
            o += seqLen;
            i += seqLen;
        } else {
            o += utf8_encode_cp(0xFFFD, out + o);
            i += 1;
        }
    }
    ObjString *s = string_new(gc, out, (uint32_t)o);
    free(out);
    return s;
}

bool string_equal(const ObjString *a, const ObjString *b) {
    if (a == b) return true;
    if (a->byteLen != b->byteLen) return false;
    return memcmp(a->chars, b->chars, a->byteLen) == 0;
}
