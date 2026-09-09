/* native/string.h -- a minimal `yapstring` object, built ahead of its own
 * N4 milestone because N2 already needs *something* to hold a string
 * constant (`yap "hello"`) and a global's name. This is deliberately not
 * yet what PLAN.md §3.9's yapstring needs in full: no codepoint indexing,
 * no interning, no Unicode-aware methods (`SCREAM()`, `chars()`, ...) --
 * those, and this file's real home, are N4's job (string.c: "UTF-8
 * yapstring, interning, codepoint indexing" per NATIVE_PLAN.md §4). What's
 * here is exactly what N2's opcode subset (CONST, YAP, GET/SET/DEF_GLOBAL)
 * needs: an immutable byte buffer, GC-tracked, printable, and comparable
 * for use as a global's key.
 */
#ifndef FUNNY_STRING_H
#define FUNNY_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "object.h"

typedef struct {
    Obj obj;
    char *chars;   /* NUL-terminated, owned, UTF-8 bytes */
    uint32_t byteLen; /* excludes the NUL terminator */
} ObjString;

struct GC;

/* Copies `len` bytes from `chars` into a new, GC-tracked ObjString (always
   copies -- no ownership-transfer variant yet, since nothing needs one at
   N2's scope). */
ObjString *string_new(struct GC *gc, const char *chars, uint32_t len);

bool string_equal(const ObjString *a, const ObjString *b);

#endif /* FUNNY_STRING_H */
