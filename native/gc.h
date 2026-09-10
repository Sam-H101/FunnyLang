/* native/gc.h -- NATIVE_PLAN.md N1 task 3/4: mark-sweep GC, plus the
 * temp-root (shadow-stack) protocol native functions must use.
 *
 * No VM struct exists yet (that's N2), so there is nothing real to mark as
 * a "value stack" or "call frames" yet. This is deliberately built as a
 * standalone, independently testable heap manager with an *external roots*
 * callback: once N2's VM exists, it registers a callback here that marks
 * its own value stack/frames/globals/open upvalues/module table, and
 * everything below already works correctly with it. See
 * native/ARCHITECTURE.md's GC section for the full contract this file
 * exists to satisfy, including why a `pointa`'s strong reference to its
 * target must be traced like any other object field once N3/N4 add it.
 */
#ifndef FUNNY_GC_H
#define FUNNY_GC_H

#include <stddef.h>

#include "object.h"
#include "value.h"

typedef struct GC GC;

typedef void (*MarkRootsFn)(GC *gc, void *userdata);

struct GC {
    Obj *objects;
    size_t bytesAllocated;
    size_t nextGC; /* collect when bytesAllocated exceeds this */
    /* FUNNY_GC_STRESS: collect every `stressPeriod` *safepoints* rather than
       waiting for the byte threshold -- a safepoint being a call to
       gc_maybe_collect, which is every allocation site plus the top of the
       dispatch loop. 0 is off; 1 (the documented value) collects at every
       one of them.

       A larger period costs proportionally less and still finds a missing
       root: an unrooted object only has to survive `stressPeriod` safepoints
       to be collected out from under the C local holding it. That is what
       lets CI run the whole corpus under stress rather than a chosen few --
       at 1, compiling a single trivial golden with the self-hosted compiler
       takes 2.8 s against 0.005 s without. */
    unsigned stressPeriod;
    unsigned stressCounter;

    Obj **grayStack;
    int grayCount;
    int grayCapacity;

    /* PUSH_TEMP/POP_TEMP (native/ARCHITECTURE.md's #1 correctness hazard):
       every value a native (C stdlib) function needs to keep alive across
       an allocation it doesn't otherwise hold a root to goes here. */
    Value *tempRoots;
    int tempRootCount;
    int tempRootCapacity;

    /* Registered once a real VM exists (N2+); NULL is a legitimate value
       meaning "no other roots exist yet", not an error. */
    MarkRootsFn markExternalRoots;
    void *externalRootsUserdata;
};

void gc_init(GC *gc);
/* Frees every object the GC is tracking, unconditionally -- shutdown, or
   test teardown between independent test cases. Does not touch tempRoots/
   grayStack's own backing arrays' *contents* semantics, just objects. */
void gc_free_all(GC *gc);
void gc_destroy(GC *gc); /* gc_free_all, then releases the GC's own scratch arrays */

/* Every object-allocating constructor calls this immediately after
   allocating a new Obj, exactly once, linking it onto the GC's heap and
   accounting `size` bytes toward the growth threshold. Returns `obj`
   unchanged, so it composes at the call site: `return gc_track(gc,
   (Obj*)bignum_new(...), sizeof(ObjBignum));`. */
Obj *gc_track(GC *gc, Obj *obj, size_t size);

void gc_push_temp(GC *gc, Value v);
void gc_pop_temp(GC *gc);
int gc_temp_count(const GC *gc); /* for a debug-build balance assertion */

/* Marks `v`/`obj` and, for a composite object, pushes it onto the gray
   stack so gc_collect's mark phase visits its children too. Safe to call
   with a non-VAL_OBJ Value or a NULL/already-marked Obj (no-op). */
void gc_mark_value(GC *gc, Value v);
void gc_mark_object(GC *gc, Obj *obj);

/* Runs a full mark-sweep collection right now, unconditionally. */
void gc_collect(GC *gc);
/* What allocators call instead: collects when the stress period comes round,
   or when the heap has grown past nextGC since the last collection. */
void gc_maybe_collect(GC *gc);

#endif /* FUNNY_GC_H */
