/* native/iterator.h -- NATIVE_PLAN.md N3 task 5 (ITER_NEW/ITER_NEXT), never
 * actually implemented when N3 landed (only the opcode numbers exist in
 * opcodes.h) -- discovered and filled in while starting N4, since
 * `grind x in <stash-or-groupchat>` needs it to be testable at all; see
 * NATIVE_PLAN.md §9's N4 entry.
 *
 * Mirrors funnylang/vm.py's `Iterator(gen)`, except the "generator" here is
 * a snapshot array materialized once, up front, exactly like Python's own
 * `iter(list(iterable.items))`/`iter(str)` do -- a stash mutated mid-loop
 * is iterated as it was at ITER_NEW time, not live.
 */
#ifndef FUNNY_ITERATOR_H
#define FUNNY_ITERATOR_H

#include "object.h"
#include "value.h"

typedef struct {
    Obj obj;
    Value *items;
    int count;
    int pos; /* next unread index; pos == count means exhausted */
} ObjIterator;

struct GC;

ObjIterator *iterator_new(struct GC *gc, const Value *items, int count);

#endif /* FUNNY_ITERATOR_H */
