/* native/object.h -- NATIVE_PLAN.md N1 task 2: the common header every
 * heap-allocated value shares, so the GC (gc.c) can walk, mark, and sweep
 * them uniformly without knowing what any of them individually mean.
 *
 * Same layout clox/Crafting Interpreters uses, for the same reason: an
 * intrusive singly-linked list through `next` needs no separate bookkeeping
 * structure for sweeping, and `type` lets every other file downcast safely.
 */
#ifndef FUNNY_OBJECT_H
#define FUNNY_OBJECT_H

#include <stdbool.h>
#include <stddef.h>

/* Grows across N2-N4 as strings/stash/groupchat/closures/squads/pointa/etc.
 * are added. Only OBJ_BIGNUM exists as of N1 -- everything else so far is
 * either a Value tag directly (ghost/boolski/fixnum/float) or doesn't exist
 * in the C runtime yet. */
typedef enum {
    OBJ_BIGNUM,
    OBJ_STRING,
    OBJ_UPVALUE,
    OBJ_CLOSURE,
    OBJ_ERROR,
    OBJ_POINTA,
    OBJ_STASH,
    OBJ_GROUPCHAT,
    OBJ_BOUND_NATIVE,
    OBJ_ITERATOR,
    OBJ_SQUAD,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_NATIVE_FN,
    OBJ_COMBO,
    OBJ_MODULE,
    OBJ_OTW,
    OBJ_BLOB,
} ObjType;

/* The common header. Every concrete object type (e.g. ObjBignum) is a
 * struct whose *first field* is `Obj obj;` -- the standard C "inherit by
 * embedding, cast the pointer" trick, so `(Obj*)someBignum` and
 * `(ObjBignum*)someObj` are both always valid, provided `someObj->type`
 * was checked first.
 *
 * `size` is what gc_track() was told the object's footprint is (the fixed
 * struct size only -- a Bignum's separately-malloc'd, variable-length limb
 * array is not included, a known undercount that's fine for a heuristic
 * collection trigger). gc.c's sweep() needs it to keep
 * GC::bytesAllocated accurate as objects are freed, not just as they're
 * allocated. */
struct Obj {
    ObjType type;
    bool marked;    /* GC mark bit -- set during the mark phase, cleared
                       (and the object freed) if still unset at sweep. */
    size_t size;
    struct Obj *next; /* intrusive link in the GC's all-objects list */
};
typedef struct Obj Obj;

#endif /* FUNNY_OBJECT_H */
