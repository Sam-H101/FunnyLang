/* native/error.h -- NATIVE_PLAN.md N3 task 4: error objects with PLAN.md
 * §3.9's field set. `flavor` is just a distinguishing string (matching
 * `FunnyError.flavor` in funnylang/errors.py being nothing more than a
 * class-level string itself) -- there is no C class hierarchy mirroring
 * Python's exception classes, since nothing here needs virtual dispatch,
 * just a name plus a message.
 *
 * `.trace` (a stash of call-site strings, per §3.9) is a known, deliberate
 * gap: Stash doesn't exist until N4, so `error_get_field` returns `ghost`
 * for "trace" for now rather than blocking N3 on a type it doesn't own.
 * The raw frames are still captured into `rawTrace` so N4 only has to wrap
 * them in a real Stash, not recompute them.
 */
#ifndef FUNNY_ERROR_H
#define FUNNY_ERROR_H

#include "object.h"
#include "string.h"
#include "value.h"

typedef struct {
    Obj obj;
    ObjString *flavor;
    ObjString *message;
    uint32_t line;
    uint32_t col;
    ObjString *file; /* may be an empty string, never NULL */
    Value payload;   /* GHOST if this wasn't a `chuck`ed non-error value */
    char **rawTrace;  /* malloc'd array of malloc'd "at name() file:line" strings */
    int rawTraceCount;
} ObjError;

struct GC;
struct VM;

/* Constructs a tracked ObjError. `trace`/`traceCount` are copied (the
   caller's own buffer, if any, remains its to free). */
ObjError *error_new(struct GC *gc, const char *flavor, const char *message, uint32_t line, uint32_t col,
                     const char *file, Value payload, char **trace, int traceCount);

/* PLAN.md §3.9's field set: "flavor" "message" "line" "col" "file" "trace"
   "payload". Returns true and sets *out on a known field name; false (no
   *out write) for anything else, so the caller can raise its own WhoDis. */
bool error_get_field(struct GC *gc, const ObjError *err, const char *name, Value *out);

#endif /* FUNNY_ERROR_H */
