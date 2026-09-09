/* native/string.h -- `yapstring`: immutable, UTF-8 (NATIVE_PLAN.md N4
 * task 1). Built incrementally -- N2 first needed just *something* to hold
 * a string constant (`yap "hello"`) and a global's name (a plain byte
 * buffer, no codepoint awareness), and every indexing/slicing/iteration
 * opcode that touches a yapstring was byte-indexed until this task landed.
 *
 * `codepointCount`/`isAscii` are computed once, at construction (an O(n)
 * UTF-8 decode pass, amortized over however many times the string is
 * later indexed/sliced/measured) -- `isAscii` is true exactly when every
 * byte is already its own codepoint, which is when `codepointCount ==
 * byteLen`; a caller doing bulk conversions doesn't need to check both.
 * `is_ascii` gives O(1) codepoint indexing (byte offset == codepoint
 * index); a non-ASCII string still needs an O(n) walk to translate a
 * codepoint index into a byte offset (utf8_byte_offset_of) -- exactly the
 * tradeoff NATIVE_PLAN.md's own task description asks for.
 *
 * AGENT CHOICE: no interning (NATIVE_PLAN.md §9) -- every string compares
 * by content already (string_equal/vm_value_equal), never by identity, so
 * interning would only be a memory/allocation optimization here, not a
 * correctness requirement; deferred until a milestone that actually needs
 * the memory savings.
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
    uint32_t codepointCount;
    bool isAscii;
} ObjString;

struct GC;

/* Copies `len` bytes from `chars` into a new, GC-tracked ObjString (always
   copies -- no ownership-transfer variant, since nothing needs one). */
ObjString *string_new(struct GC *gc, const char *chars, uint32_t len);

bool string_equal(const ObjString *a, const ObjString *b);

/* -- UTF-8 primitives, shared by string.c's own construction and vm.c's
   GET_INDEX/GET_SLICE/ITER_NEW/json-repr handling for yapstring. Every
   one of these trusts its input is well-formed UTF-8 (guaranteed by
   construction: source files are UTF-8, and every operation that builds
   a new ObjString -- concatenation, slicing, indexing -- only ever
   combines or cuts at codepoint boundaries, so malformed UTF-8 can never
   actually arise) but still never reads past `byteLen` even if it
   somehow did, so a stray/truncated leading byte degrades to a 1-byte
   "codepoint" rather than reading out of bounds. */

/* Byte length (1-4) of the UTF-8 sequence starting at chars[offset],
   clamped to byteLen. */
uint32_t utf8_seq_len(const char *chars, uint32_t byteLen, uint32_t offset);
/* Decodes the `seqLen`-byte UTF-8 sequence at chars[offset] into its
   scalar codepoint value. */
uint32_t utf8_decode_cp(const char *chars, uint32_t seqLen, uint32_t offset);
uint32_t utf8_codepoint_count(const char *chars, uint32_t byteLen);
/* Byte offset of the `codepointIdx`-th codepoint (0-based); byteLen if
   codepointIdx == the string's own codepoint count (one-past-the-end, for
   slice bounds). Caller ensures 0 <= codepointIdx <= codepoint count. */
uint32_t utf8_byte_offset_of(const char *chars, uint32_t byteLen, uint32_t codepointIdx);

#endif /* FUNNY_STRING_H */
