/* native/chunk.h -- NATIVE_PLAN.md N2 task 1: reads .funnyc (PLAN.md §5.2)
 * into an in-memory CompiledUnit, matching funnylang/serializer.py's
 * load_funnyc() field for field. .funnypak (§5.3, a linked bundle of
 * modules) is N5's problem -- IMPORT/EXPORT aren't in N2's opcode subset,
 * so there is never more than one compiled unit in scope yet.
 */
#ifndef FUNNY_CHUNK_H
#define FUNNY_CHUNK_H

#include <stdbool.h>
#include <stdint.h>

#include "value.h"

/* PLAN.md §5.2 constant-pool tags. */
typedef enum {
    CTAG_GHOST = 0,
    CTAG_BOOL = 1,
    CTAG_INT = 2,
    CTAG_FLOAT = 3,
    CTAG_STRING = 4,
    CTAG_PROTO_REF = 5,
} ConstTag;

/* A materialized constant-pool entry: string/int constants are already
   turned into real heap Values at load time (once, in chunk_load_funnyc),
   so CONST at run time is just an array index -- no per-execution
   allocation for a literal appearing in a loop body. */
typedef struct {
    ConstTag tag;
    Value value;      /* meaningful for CTAG_GHOST/BOOL/INT/FLOAT/STRING */
    uint32_t protoRef; /* meaningful for CTAG_PROTO_REF only */
} ConstEntry;

typedef struct {
    uint32_t codeOffset;
    uint32_t line;
    uint32_t col;
} LineEntry;

typedef struct {
    char *name; /* NUL-terminated, owned */
    uint8_t arity;
    uint8_t defaultCount;
    bool isVariadic;
    uint8_t upvalueCount;
    uint16_t maxStack;
    uint8_t localCount;
    uint8_t *code;
    uint32_t codeLen;
    LineEntry *lines;
    uint32_t lineCount;
} FunctionProto;

typedef struct {
    char *sourceName; /* NUL-terminated, owned */
    ConstEntry *consts;
    uint32_t constCount;
    FunctionProto *protos;
    uint32_t protoCount;
    uint32_t entryProto;
} CompiledUnit;

/* On success, returns a heap-allocated CompiledUnit (free with
   chunk_free_unit) and sets *err to NULL. On failure (bad magic, wrong
   version, or truncated/corrupt data), returns NULL and sets *err to a
   malloc'd, human-readable, caller-owned message (matching
   BytecodeVersionMismatch's messages in funnylang/serializer.py, so the
   two runtimes report the same thing for the same bad file). A GC instance
   is needed because loading a CTAG_STRING constant allocates a real
   ObjString, tracked immediately so it can never be swept out from under
   the unit that owns it. */
struct GC;
CompiledUnit *chunk_load_funnyc(const uint8_t *data, size_t len, struct GC *gc, char **err);
void chunk_free_unit(CompiledUnit *unit);

/* PLAN.md §5.3's linked bundle: several compiled modules plus the name of
   the one to run. This is what lets the native VM import a `gimme
   "other.funny"` at all -- resolving one from source would need a
   compiler, and the C runtime doesn't have one, so a bundle of
   already-compiled modules is the only self-contained way in. */
typedef struct {
    char *name;         /* logical module name, owned */
    CompiledUnit *unit; /* owned */
} PakModule;

typedef struct {
    PakModule *modules;
    uint32_t moduleCount;
    char *entryName; /* owned */
} CompiledPak;

/* Same error contract as chunk_load_funnyc. */
CompiledPak *chunk_load_funnypak(const uint8_t *data, size_t len, struct GC *gc, char **err);
void chunk_free_pak(CompiledPak *pak);
/* NULL when the bundle has no module under that exact name. */
CompiledUnit *chunk_pak_find(const CompiledPak *pak, const char *name);
/* True when `data` starts with the .funnypak magic -- lets a caller take
   one look at a file and pick a loader without guessing from the
   extension. */
bool chunk_is_funnypak(const uint8_t *data, size_t len);

/* Looks up a proto's line/col for a given code offset -- the last LineEntry
   at or before it, matching FunctionProto.line_for_offset in chunk.py. */
void chunk_line_for_offset(const FunctionProto *proto, uint32_t offset, uint32_t *lineOut, uint32_t *colOut);

#endif /* FUNNY_CHUNK_H */
