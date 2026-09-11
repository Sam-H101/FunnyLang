#include "yapper.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "stash.h"
#include "da_string.h"
#include "unicode_tbl.h"

static char *dup_cstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    memcpy(r, s, n);
    return r;
}

static bool check_str(VM *vm, Value v, const char *fnName) {
    if (IS_STRING(v)) return true;
    vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a yapstring, not a %s.", fnName, vm_type_name(v));
    return false;
}

static bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* -- split / join ------------------------------------------------------- */

static Value m_split(VM *vm, Value *a, int argc) {
    if (!check_str(vm, a[0], "split")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    bool hasSep = argc > 1 && !IS_GHOST(a[1]);
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    if (!hasSep) {
        uint32_t i = 0;
        while (i < s->byteLen) {
            while (i < s->byteLen && is_ascii_space(s->chars[i])) i++;
            if (i >= s->byteLen) break;
            uint32_t start = i;
            while (i < s->byteLen && !is_ascii_space(s->chars[i])) i++;
            stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + start, i - start)));
        }
    } else {
        if (!check_str(vm, a[1], "split")) {
            gc_pop_temp(&vm->gc);
            return GHOST_VAL;
        }
        ObjString *sep = AS_STRING(a[1]);
        if (sep->byteLen == 0) {
            vm_throw_native(vm, "TypeVibeMismatch", "empty separator.");
            gc_pop_temp(&vm->gc);
            return GHOST_VAL;
        }
        uint32_t start = 0, i = 0;
        while (i + sep->byteLen <= s->byteLen) {
            if (memcmp(s->chars + i, sep->chars, sep->byteLen) == 0) {
                stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + start, i - start)));
                i += sep->byteLen;
                start = i;
            } else {
                i++;
            }
        }
        stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + start, s->byteLen - start)));
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

/* funnylang/stdlib/yapper.py's own `_join` uses Python's raw `str(x)` on
   each item, not to_display -- observably different for a bare boolski
   ("True"/"False", not "fax"/"cap") and, empirically verified against
   the real Python VM, for a nested Stash/GroupChat/Instance (Python's own
   debug `repr()`, e.g. "Stash([1, 2])"/"GroupChat({'a': 1})", not
   FunnyLang's own display format at all -- almost certainly an oversight
   in the reference, not a specified behavior). Replicated exactly for
   ghost/boolski/numba/yapstring (cheap, and plausibly relied upon); NOT
   replicated for anything needing Python's own container repr, which
   this uses to_display for instead -- disproportionate effort for what
   looks like a reference bug nothing should depend on. Logged rather than
   silently matched or silently diverged. */
static char *join_item_str(VM *vm, Value v, size_t *lenOut) {
    if (IS_BOOL(v)) {
        *lenOut = AS_BOOL(v) ? 4 : 5;
        return dup_cstr(AS_BOOL(v) ? "True" : "False");
    }
    return vm_value_to_display_len(vm, v, lenOut);
}

static Value m_join(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "join")) return GHOST_VAL;
    if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'join' needs a stash of yapstrings.");
        return GHOST_VAL;
    }
    ObjString *sep = AS_STRING(a[0]);
    ObjStash *items = (ObjStash *)AS_OBJ(a[1]);
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (int i = 0; i < items->count; i++) {
        size_t pieceLen;
        char *piece = join_item_str(vm, items->items[i], &pieceLen);
        size_t addLen = pieceLen + (i > 0 ? sep->byteLen : 0);
        if (len + addLen > cap) {
            cap = (len + addLen) * 2 + 16;
            buf = (char *)realloc(buf, cap);
        }
        if (i > 0) {
            memcpy(buf + len, sep->chars, sep->byteLen);
            len += sep->byteLen;
        }
        memcpy(buf + len, piece, pieceLen);
        len += pieceLen;
        free(piece);
    }
    ObjString *r = string_new(&vm->gc, buf ? buf : "", (uint32_t)len);
    free(buf);
    return OBJ_VAL(r);
}

/* -- case conversion, over the generated Unicode tables ------------------ */

/* Code point by code point, not byte by byte: `toupper` on a UTF-8 lead byte
   is meaningless, and mapping each byte of `é` independently corrupts it.
   The simple (one-to-one) case mapping can change a character's encoded
   length -- U+00FF ÿ uppercases to U+0178 Ÿ, two bytes to two, but
   U+0131 ı lowercases within the BMP and U+A64B ꙋ does not -- so the output
   is built into a grown buffer rather than assumed to be the same size.

   Four bytes of headroom per code point is the most UTF-8 ever needs, so one
   allocation of 4 x codepointCount can never overflow and there is no
   reallocation path to get wrong. */
static Value case_map(VM *vm, Value strVal, const char *fnName, bool toUpper) {
    if (!check_str(vm, strVal, fnName)) return GHOST_VAL;
    ObjString *s = AS_STRING(strVal);
    size_t cap = (size_t)s->codepointCount * 4 + 1;
    char *buf = (char *)malloc(cap);
    size_t out = 0;
    uint32_t bi = 0;
    while (bi < s->byteLen) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        uint32_t cp = utf8_decode_cp(s->chars, seqLen, bi);
        uint32_t mapped = toUpper ? unicode_to_upper(cp) : unicode_to_lower(cp);
        out += utf8_encode_cp(mapped, buf + out);
        bi += seqLen;
    }
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)out);
    free(buf);
    return OBJ_VAL(r);
}

static Value m_scream(VM *vm, Value *a, int argc) {
    (void)argc;
    return case_map(vm, a[0], "SCREAM", true);
}

static Value m_whisper(VM *vm, Value *a, int argc) {
    (void)argc;
    return case_map(vm, a[0], "whisper", false);
}

/* -- trim ----------------------------------------------------------------- */

static Value trim_impl(VM *vm, Value strVal, const char *fnName, bool left, bool right) {
    if (!check_str(vm, strVal, fnName)) return GHOST_VAL;
    ObjString *s = AS_STRING(strVal);
    uint32_t start = 0, end = s->byteLen;
    if (left) {
        while (start < end && is_ascii_space(s->chars[start])) start++;
    }
    if (right) {
        while (end > start && is_ascii_space(s->chars[end - 1])) end--;
    }
    return OBJ_VAL(string_new(&vm->gc, s->chars + start, end - start));
}

static Value m_trim(VM *vm, Value *a, int argc) {
    (void)argc;
    return trim_impl(vm, a[0], "trim", true, true);
}

static Value m_ltrim(VM *vm, Value *a, int argc) {
    (void)argc;
    return trim_impl(vm, a[0], "ltrim", true, false);
}

static Value m_rtrim(VM *vm, Value *a, int argc) {
    (void)argc;
    return trim_impl(vm, a[0], "rtrim", false, true);
}

/* -- replace / contains / starts_with / ends_with / index_of ------------- */

static Value m_replace(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "replace") || !check_str(vm, a[1], "replace") || !check_str(vm, a[2], "replace")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjString *old = AS_STRING(a[1]);
    ObjString *newS = AS_STRING(a[2]);
    char *buf = NULL;
    size_t len = 0, cap = 0;
    uint32_t i = 0;
    while (i < s->byteLen) {
        if (old->byteLen > 0 && i + old->byteLen <= s->byteLen && memcmp(s->chars + i, old->chars, old->byteLen) == 0) {
            if (len + newS->byteLen > cap) {
                cap = (len + newS->byteLen) * 2 + 16;
                buf = (char *)realloc(buf, cap);
            }
            memcpy(buf + len, newS->chars, newS->byteLen);
            len += newS->byteLen;
            i += old->byteLen;
        } else {
            if (len + 1 > cap) {
                cap = (len + 1) * 2 + 16;
                buf = (char *)realloc(buf, cap);
            }
            buf[len++] = s->chars[i];
            i++;
        }
    }
    ObjString *r = string_new(&vm->gc, buf ? buf : "", (uint32_t)len);
    free(buf);
    return OBJ_VAL(r);
}

static Value m_contains(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "contains") || !check_str(vm, a[1], "contains")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjString *needle = AS_STRING(a[1]);
    if (needle->byteLen == 0) return BOOL_VAL(true);
    if (needle->byteLen > s->byteLen) return BOOL_VAL(false);
    for (uint32_t i = 0; i + needle->byteLen <= s->byteLen; i++) {
        if (memcmp(s->chars + i, needle->chars, needle->byteLen) == 0) return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value m_starts_with(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "starts_with") || !check_str(vm, a[1], "starts_with")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjString *prefix = AS_STRING(a[1]);
    if (prefix->byteLen > s->byteLen) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(s->chars, prefix->chars, prefix->byteLen) == 0);
}

static Value m_ends_with(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "ends_with") || !check_str(vm, a[1], "ends_with")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjString *suffix = AS_STRING(a[1]);
    if (suffix->byteLen > s->byteLen) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(s->chars + (s->byteLen - suffix->byteLen), suffix->chars, suffix->byteLen) == 0);
}

static Value m_index_of(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "index_of") || !check_str(vm, a[1], "index_of")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjString *needle = AS_STRING(a[1]);
    if (needle->byteLen == 0) return INT_VAL(0);
    if (needle->byteLen <= s->byteLen) {
        for (uint32_t i = 0; i + needle->byteLen <= s->byteLen; i++) {
            if (memcmp(s->chars + i, needle->chars, needle->byteLen) == 0) return INT_VAL((int64_t)i);
        }
    }
    return INT_VAL(-1);
}

/* -- slice / reverse / repeat / pad -------------------------------------- */

static Value m_slice(VM *vm, Value *a, int argc) {
    if (!check_str(vm, a[0], "slice")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    int64_t n = s->codepointCount;
    int64_t start = (argc > 1 && !IS_GHOST(a[1])) ? AS_INT(a[1]) : 0;
    int64_t stop = (argc > 2 && !IS_GHOST(a[2])) ? AS_INT(a[2]) : n;
    if (start < 0) start += n;
    if (stop < 0) stop += n;
    if (start < 0) start = 0;
    if (stop > n) stop = n;
    if (start > stop) start = stop;
    uint32_t byteStart = s->isAscii ? (uint32_t)start : utf8_byte_offset_of(s->chars, s->byteLen, (uint32_t)start);
    uint32_t byteStop = s->isAscii ? (uint32_t)stop : utf8_byte_offset_of(s->chars, s->byteLen, (uint32_t)stop);
    return OBJ_VAL(string_new(&vm->gc, s->chars + byteStart, byteStop - byteStart));
}

static Value m_reverse(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "reverse")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    if (s->isAscii) {
        char *buf = (char *)malloc(s->byteLen > 0 ? s->byteLen : 1);
        for (uint32_t i = 0; i < s->byteLen; i++) buf[i] = s->chars[s->byteLen - 1 - i];
        ObjString *r = string_new(&vm->gc, buf, s->byteLen);
        free(buf);
        return OBJ_VAL(r);
    }
    uint32_t n = s->codepointCount;
    uint32_t *offsets = (uint32_t *)malloc((size_t)(n + 1) * sizeof(uint32_t));
    uint32_t bi = 0;
    for (uint32_t ci = 0; ci < n; ci++) {
        offsets[ci] = bi;
        bi += utf8_seq_len(s->chars, s->byteLen, bi);
    }
    offsets[n] = s->byteLen;
    char *buf = (char *)malloc(s->byteLen > 0 ? s->byteLen : 1);
    size_t o = 0;
    for (int64_t ci = (int64_t)n - 1; ci >= 0; ci--) {
        uint32_t seqLen = offsets[ci + 1] - offsets[ci];
        memcpy(buf + o, s->chars + offsets[ci], seqLen);
        o += seqLen;
    }
    free(offsets);
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)o);
    free(buf);
    return OBJ_VAL(r);
}

static Value m_repeat(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "repeat")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    int64_t n = AS_INT(a[1]);
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

static Value pad_impl(VM *vm, Value *a, int argc, const char *fnName, bool padLeft) {
    if (!check_str(vm, a[0], fnName)) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    int64_t width = AS_INT(a[1]);
    char padChar = ' ';
    if (argc > 2 && IS_STRING(a[2]) && AS_STRING(a[2])->byteLen > 0) padChar = AS_STRING(a[2])->chars[0];
    int64_t n = s->codepointCount;
    if (width <= n) return OBJ_VAL(string_new(&vm->gc, s->chars, s->byteLen));
    int64_t padCount = width - n;
    char *buf = (char *)malloc((size_t)(s->byteLen + padCount));
    size_t o = 0;
    if (padLeft) {
        for (int64_t i = 0; i < padCount; i++) buf[o++] = padChar;
        memcpy(buf + o, s->chars, s->byteLen);
        o += s->byteLen;
    } else {
        memcpy(buf + o, s->chars, s->byteLen);
        o += s->byteLen;
        for (int64_t i = 0; i < padCount; i++) buf[o++] = padChar;
    }
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)o);
    free(buf);
    return OBJ_VAL(r);
}

static Value m_pad_left(VM *vm, Value *a, int argc) {
    return pad_impl(vm, a, argc, "pad_left", true);
}

static Value m_pad_right(VM *vm, Value *a, int argc) {
    return pad_impl(vm, a, argc, "pad_right", false);
}

/* -- codepoint-level: chars / ord_of / chr_of / at / code_at ------------- */

static Value m_chars(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "chars")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    uint32_t bi = 0;
    for (uint32_t ci = 0; ci < s->codepointCount; ci++) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + bi, seqLen)));
        bi += seqLen;
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_ord_of(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "ord_of")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    if (s->codepointCount != 1) {
        vm_throw_native(vm, "TypeVibeMismatch", "'ord_of' needs a single-character yapstring.");
        return GHOST_VAL;
    }
    uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, 0);
    return INT_VAL((int64_t)utf8_decode_cp(s->chars, seqLen, 0));
}

static Value m_chr_of(VM *vm, Value *a, int argc) {
    (void)argc;
    uint32_t cp = (uint32_t)AS_INT(a[0]);
    char buf[4];
    uint32_t len = utf8_encode_cp(cp, buf);
    return OBJ_VAL(string_new(&vm->gc, buf, len));
}

static bool at_impl(VM *vm, Value strVal, int64_t i, uint32_t *outByteStart, uint32_t *outSeqLen) {
    ObjString *s = AS_STRING(strVal);
    int64_t n = s->codepointCount;
    int64_t idx = i < 0 ? i + n : i;
    if (idx < 0 || idx >= n) {
        vm_throw_native(vm, "OutOfPocket", "index %lld on a yapstring of length %lld.", (long long)i, (long long)n);
        return false;
    }
    *outByteStart = s->isAscii ? (uint32_t)idx : utf8_byte_offset_of(s->chars, s->byteLen, (uint32_t)idx);
    *outSeqLen = utf8_seq_len(s->chars, s->byteLen, *outByteStart);
    return true;
}

static Value m_at(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "at")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    uint32_t byteStart, seqLen;
    if (!at_impl(vm, a[0], AS_INT(a[1]), &byteStart, &seqLen)) return GHOST_VAL;
    return OBJ_VAL(string_new(&vm->gc, s->chars + byteStart, seqLen));
}

static Value m_code_at(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "code_at")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    uint32_t byteStart, seqLen;
    if (!at_impl(vm, a[0], AS_INT(a[1]), &byteStart, &seqLen)) return GHOST_VAL;
    return INT_VAL((int64_t)utf8_decode_cp(s->chars, seqLen, byteStart));
}

/* -- format (AGENT CHOICE: positional-only, see yapper.h) ---------------- */

static Value m_format(VM *vm, Value *a, int argc) {
    if (!check_str(vm, a[0], "format")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int autoIndex = 0;
    uint32_t i = 0;
    while (i < s->byteLen) {
        char c = s->chars[i];
        if (c == '{') {
            if (i + 1 < s->byteLen && s->chars[i + 1] == '{') {
                c = '{';
                i += 2;
            } else {
                uint32_t j = i + 1;
                while (j < s->byteLen && s->chars[j] != '}') j++;
                if (j >= s->byteLen) {
                    vm_throw_native(vm, "TypeVibeMismatch", "'format' couldn't fill in every placeholder: unclosed '{'.");
                    free(buf);
                    return GHOST_VAL;
                }
                int argIndex;
                if (j == i + 1) {
                    argIndex = autoIndex++;
                } else {
                    argIndex = 0;
                    bool allDigits = true;
                    for (uint32_t k = i + 1; k < j; k++) {
                        if (!isdigit((unsigned char)s->chars[k])) {
                            allDigits = false;
                            break;
                        }
                        argIndex = argIndex * 10 + (s->chars[k] - '0');
                    }
                    if (!allDigits) {
                        vm_throw_native(vm, "TypeVibeMismatch",
                                        "'format' couldn't fill in every placeholder: named fields/format specs aren't supported natively yet.");
                        free(buf);
                        return GHOST_VAL;
                    }
                }
                if (argIndex + 1 >= argc) {
                    vm_throw_native(vm, "TypeVibeMismatch", "'format' couldn't fill in every placeholder: not enough args.");
                    free(buf);
                    return GHOST_VAL;
                }
                size_t pieceLen;
                char *piece = vm_value_to_display_len(vm, a[argIndex + 1], &pieceLen);
                if (len + pieceLen > cap) {
                    cap = (len + pieceLen) * 2 + 16;
                    buf = (char *)realloc(buf, cap);
                }
                memcpy(buf + len, piece, pieceLen);
                len += pieceLen;
                free(piece);
                i = j + 1;
                continue;
            }
        } else if (c == '}' && i + 1 < s->byteLen && s->chars[i + 1] == '}') {
            i += 2;
        } else {
            i += 1;
        }
        if (len + 1 > cap) {
            cap = (len + 1) * 2 + 16;
            buf = (char *)realloc(buf, cap);
        }
        buf[len++] = c;
    }
    ObjString *r = string_new(&vm->gc, buf ? buf : "", (uint32_t)len);
    free(buf);
    return OBJ_VAL(r);
}

/* -- is_numba / is_letter / is_alnum ------------------------------------- */

static Value m_is_numba(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "is_numba")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    char *endPtr;
    double d;
    (void)d;
    uint32_t start = 0, end = s->byteLen;
    while (start < end && is_ascii_space(s->chars[start])) start++;
    while (end > start && is_ascii_space(s->chars[end - 1])) end--;
    if (start >= end) return BOOL_VAL(false);
    char *tmp = (char *)malloc((size_t)(end - start) + 1);
    memcpy(tmp, s->chars + start, end - start);
    tmp[end - start] = '\0';
    d = strtod(tmp, &endPtr);
    bool ok = endPtr == tmp + (end - start) && endPtr != tmp;
    free(tmp);
    return BOOL_VAL(ok);
}

/* Per code point, against the generated category tables -- `isalpha` over
   bytes said `cap` for every non-ASCII letter, which is where `yo 変数 = 1`
   failed to lex: the lexer's identifier rule goes through here. */
static Value m_is_letter(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "is_letter")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    if (s->byteLen == 0) return BOOL_VAL(false);
    for (uint32_t bi = 0; bi < s->byteLen;) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        if (!unicode_is_letter(utf8_decode_cp(s->chars, seqLen, bi))) return BOOL_VAL(false);
        bi += seqLen;
    }
    return BOOL_VAL(true);
}

static Value m_is_alnum(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "is_alnum")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    if (s->byteLen == 0) return BOOL_VAL(false);
    for (uint32_t bi = 0; bi < s->byteLen;) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        if (!unicode_is_alnum(utf8_decode_cp(s->chars, seqLen, bi))) return BOOL_VAL(false);
        bi += seqLen;
    }
    return BOOL_VAL(true);
}

/* -- lines / words -------------------------------------------------------- */

static Value m_lines(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "lines")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    uint32_t start = 0, i = 0;
    while (i < s->byteLen) {
        char c = s->chars[i];
        if (c == '\n' || c == '\r') {
            stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + start, i - start)));
            if (c == '\r' && i + 1 < s->byteLen && s->chars[i + 1] == '\n') i += 2;
            else i += 1;
            start = i;
        } else {
            i++;
        }
    }
    if (start < s->byteLen) stash_push(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, s->chars + start, s->byteLen - start)));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_words(VM *vm, Value *a, int argc) {
    return m_split(vm, a, argc); /* str.split() with no args == splitting on whitespace, same as _words */
}

/* -- title_case / sarcasm_case ------------------------------------------- */

static Value m_title_case(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "title_case")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    size_t cap = (size_t)s->codepointCount * 4 + 1;
    char *buf = (char *)malloc(cap);
    size_t out = 0;
    bool prevWasAlpha = false;
    uint32_t bi = 0;
    while (bi < s->byteLen) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        uint32_t cp = utf8_decode_cp(s->chars, seqLen, bi);
        if (unicode_is_letter(cp)) {
            out += utf8_encode_cp(prevWasAlpha ? unicode_to_lower(cp) : unicode_to_upper(cp), buf + out);
            prevWasAlpha = true;
        } else {
            out += utf8_encode_cp(cp, buf + out);
            prevWasAlpha = false;
        }
        bi += seqLen;
    }
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)out);
    free(buf);
    return OBJ_VAL(r);
}

/* Alternating case by *code point* index, which it already was -- the change
   here is that a multi-byte code point now gets cased instead of copied
   through untouched. */
static Value m_sarcasm_case(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "sarcasm_case")) return GHOST_VAL;
    ObjString *s = AS_STRING(a[0]);
    size_t cap = (size_t)s->codepointCount * 4 + 1;
    char *buf = (char *)malloc(cap);
    size_t out = 0;
    uint32_t bi = 0;
    int64_t cpIndex = 0;
    while (bi < s->byteLen) {
        uint32_t seqLen = utf8_seq_len(s->chars, s->byteLen, bi);
        uint32_t cp = utf8_decode_cp(s->chars, seqLen, bi);
        out += utf8_encode_cp((cpIndex % 2) ? unicode_to_upper(cp) : unicode_to_lower(cp), buf + out);
        bi += seqLen;
        cpIndex++;
    }
    ObjString *r = string_new(&vm->gc, buf, (uint32_t)out);
    free(buf);
    return OBJ_VAL(r);
}

/* -- how_thicc / to_numba (method-only; how_thicc/to_numba aren't module
   members -- builtins.py's own `how_thicc(x)`/`to_numba(x)` cover those
   for strings already) --------------------------------------------------- */

static Value m_how_thicc(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "how_thicc")) return GHOST_VAL;
    return INT_VAL(AS_STRING(a[0])->codepointCount);
}

/* The length in *bytes*, where how_thicc is the length in codepoints.
   Almost nothing wants this -- and then one thing does and nothing else will
   do: an HTTP `Content-Length` counts bytes, so a response with a single
   non-ASCII character in it would be sent short and the other end would sit
   waiting for the rest. Anything speaking a wire protocol has the same
   problem. */
static Value m_byte_len(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!check_str(vm, a[0], "byte_len")) return GHOST_VAL;
    return INT_VAL(AS_STRING(a[0])->byteLen);
}

static Value m_to_numba(VM *vm, Value *a, int argc) {
    (void)argc;
    return to_numba_value(vm, a[0]);
}

/* -- registration --------------------------------------------------------- */

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity; /* including the string itself */
    int maxArity;
} YapperModuleEntry;

static const YapperModuleEntry MODULE_FUNCTIONS[] = {
    {"split", m_split, 1, 2},
    {"join", m_join, 2, 2},
    {"byte_len", m_byte_len, 1, 1},
    {"SCREAM", m_scream, 1, 1},
    {"whisper", m_whisper, 1, 1},
    {"trim", m_trim, 1, 1},
    {"ltrim", m_ltrim, 1, 1},
    {"rtrim", m_rtrim, 1, 1},
    {"replace", m_replace, 3, 3},
    {"contains", m_contains, 2, 2},
    {"starts_with", m_starts_with, 2, 2},
    {"ends_with", m_ends_with, 2, 2},
    {"index_of", m_index_of, 2, 2},
    {"slice", m_slice, 1, 3},
    {"reverse", m_reverse, 1, 1},
    {"repeat", m_repeat, 2, 2},
    {"pad_left", m_pad_left, 2, 3},
    {"pad_right", m_pad_right, 2, 3},
    {"chars", m_chars, 1, 1},
    {"ord_of", m_ord_of, 1, 1},
    {"chr_of", m_chr_of, 1, 1},
    {"format", m_format, 1, 255},
    {"is_numba", m_is_numba, 1, 1},
    {"is_letter", m_is_letter, 1, 1},
    {"is_alnum", m_is_alnum, 1, 1},
    {"lines", m_lines, 1, 1},
    {"words", m_words, 1, 1},
    {"title_case", m_title_case, 1, 1},
    {"sarcasm_case", m_sarcasm_case, 1, 1},
};
#define MODULE_FUNCTIONS_COUNT (int)(sizeof(MODULE_FUNCTIONS) / sizeof(MODULE_FUNCTIONS[0]))

/* Arity here excludes the receiver -- the *extra* args beyond it, same
   convention as stash.h/groupchat.h's own method tables. how_thicc/
   to_numba/at/code_at aren't module functions at all (see their own
   definitions above), only instance methods, matching
   funnylang/stdlib/yapper.py's own YAPSTRING_METHODS exactly. */
static const YapperModuleEntry YAPSTRING_METHOD_TABLE[] = {
    {"how_thicc", m_how_thicc, 0, 0},
    {"byte_len", m_byte_len, 0, 0},
    {"SCREAM", m_scream, 0, 0},
    {"whisper", m_whisper, 0, 0},
    {"trim", m_trim, 0, 0},
    {"split", m_split, 0, 1},
    {"contains", m_contains, 1, 1},
    {"starts_with", m_starts_with, 1, 1},
    {"ends_with", m_ends_with, 1, 1},
    {"replace", m_replace, 2, 2},
    {"index_of", m_index_of, 1, 1},
    {"slice", m_slice, 0, 2},
    {"reverse", m_reverse, 0, 0},
    {"to_numba", m_to_numba, 0, 0},
    {"chars", m_chars, 0, 0},
    {"at", m_at, 1, 1},
    {"code_at", m_code_at, 1, 1},
    {"repeat", m_repeat, 1, 1},
    {"pad_left", m_pad_left, 1, 2},
    {"pad_right", m_pad_right, 1, 2},
};
#define YAPSTRING_METHOD_TABLE_COUNT (int)(sizeof(YAPSTRING_METHOD_TABLE) / sizeof(YAPSTRING_METHOD_TABLE[0]))

NativeMethodFn yapstring_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < YAPSTRING_METHOD_TABLE_COUNT; i++) {
        if (strcmp(YAPSTRING_METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = YAPSTRING_METHOD_TABLE[i].minArity;
            *outMaxArity = YAPSTRING_METHOD_TABLE[i].maxArity;
            return YAPSTRING_METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}

Value yapper_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < MODULE_FUNCTIONS_COUNT; i++) {
        const YapperModuleEntry *e = &MODULE_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "yapper", 6);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
