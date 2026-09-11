/* native/blob.h -- RUNTIME_PLAN.md R1: bytes that are bytes.
 *
 * A `yapstring` is a sequence of codepoints. `string_new` counts codepoints
 * over whatever bytes it is handed, so a PNG read into one has a
 * `how_thicc()` that means nothing, and it cannot be built from FunnyLang at
 * all -- `chr_of(200)` is two UTF-8 bytes, not the byte 200. Servers,
 * encryption and JSON all need a sequence that is exactly bytes.
 *
 * A `blob` is immutable, like a yapstring and for the same reason: it is
 * handed around, used as a groupchat key, and copied to interns, and none of
 * that is safe if somebody can change it underneath. Building one up is
 * `stash` of `blob` then `blob.join`, which is the same advice as for
 * strings.
 *
 * It is deliberately NOT a string with different accessors. `"a" + b` is a
 * TypeVibeMismatch rather than a guess about which encoding was meant, and
 * `b.to_yap()` is the explicit, lossy (U+FFFD) way across.
 */
#ifndef FUNNY_BLOB_H
#define FUNNY_BLOB_H

#include <stdbool.h>
#include <stdint.h>

#include "object.h"
#include "value.h"
#include "vm.h"

typedef struct {
    Obj obj;
    uint8_t *bytes; /* owned; NUL-terminated as a courtesy to C callers, but
                       `byteLen` is the length -- a blob may hold NUL. */
    uint32_t byteLen;
} ObjBlob;

struct GC;
struct VM;

/* Copies `len` bytes into a new, GC-tracked blob. `bytes` may be NULL when
   `len` is 0. */
ObjBlob *blob_new(struct GC *gc, const uint8_t *bytes, uint32_t len);

/* Byte-for-byte equality. Two blobs are equal when their contents are, the
   same rule strings follow -- never pointer identity. */
bool blob_equal(const ObjBlob *a, const ObjBlob *b);

/* The instance-method table, bound by GET_PROP/INVOKE for a bare blob
   receiver (`b.to_hex()`). Arities exclude the receiver. */
NativeMethodFn blob_find_method(const char *name, int *outMinArity, int *outMaxArity);

/* `gimme blob`: the constructors, plus every instance method again as a free
   function taking the blob as its first argument (stash.h's convention). */
Value blob_build(struct VM *vm);

/* Hex and base64, shared with the goldens' expectations and -- from R3 --
   with `vault`, which speaks in these two encodings. `outLen` excludes the
   NUL; the result is malloc'd and the caller frees it. */
char *blob_hex_encode(const uint8_t *bytes, uint32_t len);
char *blob_base64_encode(const uint8_t *bytes, uint32_t len, uint32_t *outLen);
/* NULL when the text is not valid hex/base64 (odd length, a character
   outside the alphabet, bad padding). Never partially decodes. */
uint8_t *blob_hex_decode(const char *text, uint32_t len, uint32_t *outLen);
uint8_t *blob_base64_decode(const char *text, uint32_t len, uint32_t *outLen);

#endif /* FUNNY_BLOB_H */
