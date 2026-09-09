/* native/vm.h -- NATIVE_PLAN.md N2 tasks 2-4: the interpreter loop, for the
 * opcode subset that needs no call frames yet (no CALL/CLOSURE/RETURN-to-
 * a-caller -- those are N3). The VM runs exactly one FunctionProto (the
 * compiled unit's entry proto) top to bottom, with internal jumps; locals
 * are plain stack slots at a fixed base of 0.
 */
#ifndef FUNNY_VM_H
#define FUNNY_VM_H

#include <stdio.h>

#include "chunk.h"
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

typedef struct {
    GC gc;

    Value *stack;
    int stackCount;
    int stackCapacity;

    /* A plain linear-scan table, not table.c's real hash table (N4) --
       correctness first, and N2's test programs have a handful of globals
       at most. See NATIVE_PLAN.md §9's N2 entry. */
    GlobalEntry *globals;
    int globalCount;
    int globalCapacity;

    CompiledUnit *unit;   /* borrowed; caller keeps it alive */
    FunctionProto *proto; /* the entry proto being run */
    uint32_t ip;

    FILE *out; /* where YAP writes */

    bool hadError;
    char *errorFlavor;  /* e.g. "TypeVibeMismatch"; malloc'd, or NULL */
    char *errorMessage; /* malloc'd, or NULL */
} VM;

void vm_init(VM *vm);
void vm_destroy(VM *vm);

/* Runs unit->protos[unit->entryProto]. `unit` must outlive the call (its
   consts, in particular, are referenced directly, not copied). Returns
   VM_ERROR if a runtime error occurred; check vm->errorFlavor/errorMessage
   in that case (both owned by the VM, valid until vm_destroy). */
VmResult vm_run(VM *vm, CompiledUnit *unit, FILE *out);

#endif /* FUNNY_VM_H */
