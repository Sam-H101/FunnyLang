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
    /* N6: the two diagnostics-only fields. `roast` is the comedic
       explanation the funny-mode renderer prints instead of `message`
       (never NULL -- see error_new); `hint` is the optional one-line
       "skill issue fix:" suggestion, NULL when there isn't one. Both are
       deliberately absent from error_get_field: PLAN.md §3.9's
       language-visible field set is flavor/message/line/col/file/trace/
       payload, and N6 is not the milestone that changes it. */
    ObjString *roast;
    ObjString *hint;
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
   caller's own buffer, if any, remains its to free).
   `roast` NULL means "use the default for this flavor", falling back to
   `message` for a flavor with no entry -- exactly funnylang/errors.py's
   own `roast or DEFAULT_ROASTS.get(self.flavor, message)`. `hint` NULL
   means there is no fix suggestion for this error. */
ObjError *error_new(struct GC *gc, const char *flavor, const char *message, const char *roast, const char *hint,
                     uint32_t line, uint32_t col, const char *file, Value payload, char **trace, int traceCount);

/* PLAN.md §3.9's field set: "flavor" "message" "line" "col" "file" "trace"
   "payload". Returns true and sets *out on a known field name; false (no
   *out write) for anything else, so the caller can raise its own WhoDis. */
bool error_get_field(struct GC *gc, const ObjError *err, const char *name, Value *out);

/* Whether `flavor` is one of PLAN.md §4.1's error flavors. The taxonomy is
   closed, so this is what `oops(flavor, ...)` validates against. */
bool error_is_known_flavor(const char *flavor);

#endif /* FUNNY_ERROR_H */
