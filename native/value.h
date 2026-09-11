/* native/value.h -- NATIVE_PLAN.md N1 task 1: the tagged-union Value every
 * opcode pushes/pops. AGENT CHOICE (NATIVE_PLAN.md §9): a tagged union, not
 * NaN-boxing -- correctness first, and PLAN.md's arbitrary-precision numba
 * already puts most "interesting" numbers on the heap as a Bignum, which
 * blunts NaN-boxing's usual win anyway.
 *
 * Five tags, matching PLAN.md §3.9's ghost/boolski/numba/... split at the
 * point where it actually matters for representation: `numba` itself is
 * two tags here (a fixnum int64_t and a float double) plus the Obj-backed
 * Bignum for anything a fixnum can't hold -- funnylang/values.py gets this
 * for free from Python's own int; the C runtime has to build it by hand.
 */
#ifndef FUNNY_VALUE_H
#define FUNNY_VALUE_H

#include <stdbool.h>
#include <stdint.h>

#include "object.h"

typedef enum {
    VAL_GHOST,
    VAL_BOOL,
    VAL_INT,   /* fixnum: int64_t. Promotes to VAL_OBJ/OBJ_BIGNUM on overflow. */
    VAL_FLOAT,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double number;
        Obj *obj;
    } as;
} Value;

#define IS_GHOST(v) ((v).type == VAL_GHOST)
#define IS_BOOL(v)  ((v).type == VAL_BOOL)
#define IS_INT(v)   ((v).type == VAL_INT)
#define IS_FLOAT(v) ((v).type == VAL_FLOAT)
#define IS_OBJ(v)   ((v).type == VAL_OBJ)
/* "numba" per PLAN.md §3.9: a fixnum, a float, or a heap bignum -- NOT a
   boolski, even though booleans are represented as small integers in many
   languages (PLAN.md §16 calls this out explicitly for the Python VM, and
   the C runtime must match: `fax + 1` is a TypeVibeMismatch, not 2). */
#define IS_NUM(v)   (IS_INT(v) || IS_FLOAT(v) || IS_BIGNUM(v))
#define IS_BIGNUM(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BIGNUM)
#define IS_STRING(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STRING)
/* A `blob` (RUNTIME_PLAN.md R1): bytes, not codepoints. The cast expands
   only where native/blob.h is already included, the same arrangement
   AS_STRING has with da_string.h. */
#define IS_BLOB(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BLOB)

#define AS_BOOL(v)   ((v).as.boolean)
#define AS_INT(v)    ((v).as.integer)
#define AS_FLOAT(v)  ((v).as.number)
#define AS_OBJ(v)    ((v).as.obj)
#define AS_BIGNUM(v) ((ObjBignum *)AS_OBJ(v))
#define AS_STRING(v) ((ObjString *)AS_OBJ(v))
#define AS_BLOB(v)   ((ObjBlob *)AS_OBJ(v))

#define GHOST_VAL      ((Value){VAL_GHOST, {.integer = 0}})
#define BOOL_VAL(b)    ((Value){VAL_BOOL, {.boolean = (b)}})
#define INT_VAL(i)     ((Value){VAL_INT, {.integer = (i)}})
#define FLOAT_VAL(f)   ((Value){VAL_FLOAT, {.number = (f)}})
#define OBJ_VAL(o)     ((Value){VAL_OBJ, {.obj = (Obj *)(o)}})

/* Place identity for VAL_OBJ (same pointer), value identity for everything
   else -- NOT the full funny_eq (values.py) cross-type numeric/structural
   equality, which needs Stash/GroupChat/Instance and belongs to whichever
   later milestone's file defines those (N4). This is the narrow piece N1
   can define correctly on its own: two fixnums, two floats, two bools, or
   ghost==ghost. */
bool value_equal_narrow(Value a, Value b);

/* ghost, cap, boolski 0, numba 0 (any representation), and an empty
   string/stash/groupchat are falsy; everything else, truthy -- matches
   funnylang/values.py's is_truthy exactly for every tag this file knows
   about. (String/stash/groupchat emptiness is N4's job to wire in; this
   returns true for any VAL_OBJ that isn't a zero-valued Bignum, which is
   already correct for Bignum specifically.) */
bool value_is_truthy(Value v);

#endif /* FUNNY_VALUE_H */
