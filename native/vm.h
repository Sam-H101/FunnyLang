/* native/vm.h -- NATIVE_PLAN.md N2's opcode subset (no call frames) plus
 * N3's frames/closures/upvalues/try-catch/pointers. The VM now runs a real
 * call stack: `vm->frames[vm->frameCount-1]` is always "the current
 * frame", re-fetched at the top of every dispatch iteration (mirroring
 * funnylang/vm.py's own `while True: frame = self.frames[-1]; ...` loop
 * structure exactly, since a CALL/RETURN can change which frame is
 * current between iterations).
 */
#ifndef FUNNY_VM_H
#define FUNNY_VM_H

#include <stdio.h>

#include "chunk.h"
#include "frames.h"
#include "gc.h"
#include "string.h"
#include "value.h"

typedef struct {
    ObjString *name;
    Value value;
} GlobalEntry;

typedef enum {
    VM_OK,
    VM_ERROR,
} VmResult;

/* PLAN.md M11's recursion cap -- ported unchanged (funnylang/vm.py's
   MAX_FRAMES). */
#define VM_MAX_FRAMES 10000

struct VM {
    GC gc;

    Value *stack;
    int stackCount;
    int stackCapacity;

    Frame *frames;
    int frameCount;
    int frameCapacity;

    /* Open upvalues: a flat array, linear-searched -- funnylang/vm.py's
       own `open_upvalues` list is exactly this, ported structurally
       rather than adopting clox's sorted-linked-list optimization (not
       needed for correctness, and correctness is what N1-N3 optimize
       for). */
    ObjUpvalue **openUpvalues;
    int openUpvalueCount;
    int openUpvalueCapacity;

    /* A plain linear-scan table, not table.c's real hash table (N4) --
       see NATIVE_PLAN.md §9's N2 entry. */
    GlobalEntry *globals;
    int globalCount;
    int globalCapacity;

    CompiledUnit *unit; /* borrowed; caller keeps it alive */

    FILE *out; /* where YAP writes */

    /* Where the dispatch loop currently is, refreshed at the top of every
       iteration -- vm_throw() (callable from deep inside an arithmetic
       helper, not just the dispatch switch itself) needs this to build a
       correct line/col and call trace without every helper function
       threading frame/ip through its own parameters. */
    int currentFrameIndex;
    uint32_t currentInstrStart;

    /* Error signaling: vm_throw() builds a complete ObjError immediately
       (using the position above) and sets these; the top of the dispatch
       loop notices `hadError` and searches for a handler via
       vm_unwind_to_handler before giving up. This mirrors Python's
       `except FunnyError as err: ... if self._unwind(err, base): continue;
       raise` wrapping, just spelled with a flag instead of a native
       exception mechanism. `pendingError` is always an OBJ_VAL(ObjError*)
       once `hadError` is set -- CHUCK re-throwing an already-caught error
       assigns the *original* ObjError here unchanged (preserving its own
       flavor/site), rather than constructing a new one at the chuck site,
       matching funnylang/vm.py's `_make_thrown`. */
    bool hadError;
    Value pendingError;

    /* Set once vm_run returns VM_ERROR: the uncaught ObjError (as a
       Value), for the caller (main.c today, a real CLI's diagnostic
       renderer from N6 on) to report. */
    Value uncaughtError;
};

void vm_init(VM *vm);
void vm_destroy(VM *vm);

/* Runs unit->protos[unit->entryProto]. `unit` must outlive the call (its
   consts, in particular, are referenced directly, not copied). Returns
   VM_ERROR if the program raised past its outermost frame; see
   vm->uncaughtError. */
VmResult vm_run(VM *vm, CompiledUnit *unit, FILE *out);

#endif /* FUNNY_VM_H */
