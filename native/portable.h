/* native/portable.h -- ASYNC_PLAN.md A1: a value that can cross a heap.
 *
 * An `interns` worker runs in its own VM with its own collector, so nothing
 * on the parent's heap may be handed to it and nothing on its heap may come
 * back. ASYNC_PLAN.md §2.2 says why that is not negotiable: the GC is one
 * object per VM with a single intrusive object list, one gray stack and one
 * tempRoots stack, and `gc_push_temp` -- the rooting protocol every native
 * function follows -- is correct precisely because one thread uses it.
 *
 * So values are *copied* across, into this representation, which references
 * nothing owned by any collector. It is plain malloc'd memory: allocated on
 * one thread, read and freed on another, which malloc supports and a GC heap
 * does not.
 *
 * WHAT CAN CROSS, and why the rest cannot:
 *
 *   ghost, boolski, numba (fixnum, float and bignum), yapstring, blob   yes
 *   stash and groupchat of the above, recursively                 yes
 *
 *   bet / lowkey   a closure captures upvalues and a module's globals, which
 *                  live in the heap it was made on. Copying it would either
 *                  copy the whole world or silently share it.
 *   squad, an instance, module   same, through the class and its methods.
 *   pointa         addresses a cell in one specific heap. There is no
 *                  meaningful "the same cell, over there".
 *   error, iterator, otw         hold interpreter state, not data.
 *
 * A rejection is a TypeVibeMismatch naming the type, because "it did not
 * work" is not a useful thing to tell someone who just tried to hand a
 * closure to a thread.
 */
#ifndef FUNNY_PORTABLE_H
#define FUNNY_PORTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "value.h"
/* ObjBignum is a typedef'd anonymous struct, so it cannot be forward-
   declared the way a named one could -- the header has to come in whole,
   exactly as error.h notes about ObjError. */
#include "bignum.h"

struct VM;

typedef enum {
    PV_GHOST,
    PV_BOOL,
    PV_INT,
    PV_FLOAT,
    PV_BIGNUM,
    PV_STRING,
    PV_BLOB,
    PV_STASH,
    PV_GROUPCHAT,
} PortableKind;

typedef struct PortableValue {
    PortableKind kind;
    union {
        bool boolean;
        int64_t integer;
        double number;
        /* An ObjBignum copied out of the source heap and *not* gc_tracked.
           bignum_copy deep-copies the limbs, and an untracked ObjBignum is
           ordinary malloc'd memory -- builtins.c already relies on exactly
           that when it decides late whether a result fits a fixnum. */
        ObjBignum *bignum;
        struct {
            uint8_t *bytes; /* owned; a blob is bytes, so there is no encoding here */
            uint32_t len;
        } blob;
        struct {
            char *bytes; /* owned; NOT NUL-reliant -- a yapstring may hold NUL */
            uint32_t len;
        } string;
        struct {
            struct PortableValue **items;
            uint32_t count;
        } list;
        struct {
            struct PortableValue **keys;
            struct PortableValue **values;
            uint32_t count;
        } map;
    } as;
} PortableValue;

/* Copies `v` out of whatever heap it is on. NULL if it cannot cross, with a
   reason written into errbuf -- the caller raises it as a TypeVibeMismatch.
   A structure that contains itself is rejected rather than followed: the
   display path renders those as `[...]`, but there is no honest way to hand
   a cycle to another heap without either looping forever or silently
   flattening it. */
PortableValue *portable_from_value(Value v, char *errbuf, size_t errbuf_len);

/* Rebuilds the value on `vm`'s heap. Every object it makes belongs to that
   collector. The caller must root the result before allocating again. */
Value portable_to_value(struct VM *vm, const PortableValue *pv);

void portable_free(PortableValue *pv);

#endif /* FUNNY_PORTABLE_H */
