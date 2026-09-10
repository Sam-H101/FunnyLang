/* native/groupchat.h -- NATIVE_PLAN.md N4 task 4: `groupchat`, a map with
 * reference semantics. AGENT CHOICE (logged in NATIVE_PLAN.md Sec9, same
 * reasoning already used for the VM's own globals table in N2): a plain
 * linear-scan, insertion-ordered array of key/value pairs, not table.c's
 * real open-addressing hash table -- correctness and insertion-order
 * preservation (funnylang/stdlib/groupchat.py's `keys`/`values`/`pairs`
 * all rely on Python dict's insertion order) come first, raw lookup
 * performance later if a milestone ever needs it.
 *
 * Method bodies port funnylang/stdlib/groupchat.py's own `METHODS` dict
 * (11 entries) close to mechanically -- args[0] is always the receiver,
 * same convention as stash.h/ObjBoundNative. `invert`/`from_pairs` are
 * stdlib-module-only (N5's job), not instance methods, so they don't
 * live here.
 */
#ifndef FUNNY_GROUPCHAT_H
#define FUNNY_GROUPCHAT_H

#include "object.h"
#include "value.h"
#include "vm.h"

typedef struct {
    Value key;
    Value value;
} GroupChatEntry;

typedef struct {
    Obj obj;
    GroupChatEntry *entries;
    int count;
    int capacity;
    /* Open-addressed hash index over the *string* keys in `entries`, holding
       positions into it; -1 is an empty slot. NULL until the groupchat is
       big enough to be worth one (see GROUPCHAT_INDEX_MIN in groupchat.c) --
       a linear scan wins for the handful of entries most groupchats hold,
       and costs no allocation.

       `entries` stays the ordered array everything else reads, so insertion
       order, iteration, keys(), display and serialisation are all unchanged.
       This only answers "where is this key". */
    int32_t *index;
    int indexCapacity;
} ObjGroupChat;

struct GC;

/* Copies `count` entries into a new, GC-tracked ObjGroupChat (NULL/0 for
   empty). Later duplicate keys in `entries` win, matching dict-literal
   semantics -- callers building from BUILD_GROUPCHAT's flat stack pairs
   already produce entries in the order they should apply. */
ObjGroupChat *groupchat_new(struct GC *gc, const GroupChatEntry *entries, int count);

/* NULL if `key` isn't present (value_equal_narrow, not pointer identity --
   matches Python's dict `==`-based key lookup for the value tags this
   runtime supports at N4's scope: ghost/bool/numba/yapstring).

   O(1) for a string key once the index exists, O(n) otherwise -- see the
   note on `index` above for why numeric keys are deliberately not indexed. */
GroupChatEntry *groupchat_find(ObjGroupChat *g, Value key);
void groupchat_set(struct GC *gc, ObjGroupChat *g, Value key, Value value);
bool groupchat_remove(ObjGroupChat *g, Value key);

NativeMethodFn groupchat_find_method(const char *name, int *outMinArity, int *outMaxArity);

/* `gimme groupchat`'s own Module: all 11 instance methods, plus the 2
   free-function-only extras (invert, from_pairs) -- N5 task 2. */
Value groupchat_build(VM *vm);

#endif /* FUNNY_GROUPCHAT_H */
