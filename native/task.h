/* native/task.h -- ASYNC_PLAN.md A3: more than one thing on the go at once.
 *
 * A task is an independent line of execution inside one VM, on one thread.
 * The entry program is task zero; A4's `async_ngl bet` makes more.
 *
 * WHY THIS IS POSSIBLE AT ALL: the VM keeps its own `stack` and `frames`
 * arrays rather than using the C stack, so a task's entire state is a slice
 * of two arrays plus a handful of scalars. Suspending it is saving those;
 * resuming it is putting them back. Nothing on the C stack has to survive.
 *
 * WHERE IT STOPS, and this is §3.1: `vm_call_value` re-enters `vm_execute`
 * on the C stack -- that is how a native function calls back into FunnyLang
 * (`stash.sort_by`'s comparator, `combo`, a squad's magic method). A task
 * cannot suspend across one of those, because that C frame cannot be saved.
 * A4 enforces it with `CantWaitRightNow` rather than pretending otherwise.
 *
 * HOW THE CONTEXT IS HELD, which is the design decision worth arguing with:
 * the running task's context lives in the **VM's own fields**, exactly where
 * it always did, and a Task's copies of those fields are stale while it runs.
 * The alternative -- the VM holding a `Task *current` and every access going
 * through it -- would have meant rewriting several hundred `vm->stack` and
 * `vm->frameCount` references in the dispatch loop, which is a large diff
 * through the most correctness-critical code in the project in exchange for
 * nothing a switch does not already give. A3's whole acceptance criterion is
 * that the existing corpus does not notice, and a diff that size is the
 * opposite of that.
 *
 * The one place it shows is `ObjUpvalue`, which holds an absolute index into
 * "the stack". A suspended task's open upvalues must index *its* stack, not
 * whichever task happens to be running -- so an upvalue remembers which task
 * it belongs to, and frames.c asks whether that task is the running one.
 * That is six lines, and it is the entire cost of the choice.
 */
#ifndef FUNNY_TASK_H
#define FUNNY_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "chunk.h"
#include "frames.h"
#include "value.h"

struct VM;
struct GC;
struct ObjOtw;

typedef enum {
    /* Its context is in the VM's fields right now. Exactly one task per VM
       is in this state whenever any FunnyLang code is running. */
    TASK_RUNNING,
    /* Suspended, and able to run: the loop may resume it whenever it likes. */
    TASK_READY,
    /* Suspended on an `otw` that has not settled. Becomes READY when it
       does. */
    TASK_WAITING,
    /* Ran to the end. Its arrays are freed; the Task survives only until the
       VM is torn down. */
    TASK_DONE,
} TaskState;

typedef struct Task {
    int id;
    TaskState state;

    /* -- saved context ----------------------------------------------------
       Stale while this task is the running one; the VM's fields are the
       truth then. The *arrays* are always this task's own, though, which is
       what lets a suspended task's upvalues still find their slots. */
    Value *stack;
    int stackCount;
    int stackCapacity;

    Frame *frames;
    int frameCount;
    int frameCapacity;

    ObjUpvalue **openUpvalues;
    int openUpvalueCount;
    int openUpvalueCapacity;

    int currentFrameIndex;
    uint32_t currentInstrStart;
    bool hadError;
    Value pendingError;

    /* The module a task was executing in. Swapped by vm_run_module for the
       duration of an import, so a task suspended inside one and resumed
       later has to put back the pair it had -- otherwise its error
       positions start naming whichever file the *scheduler* was in. */
    CompiledUnit *unit;
    const char *currentModuleName;

    /* -- scheduling (A4) --------------------------------------------------
       The `otw` this task settles when it finishes, and the one it is
       suspended on. Both are GC objects on this VM's heap, and both are
       rooted by task_mark -- an `otw` whose only reference is the task that
       will fulfil it is exactly the case that would otherwise be swept. */
    struct ObjOtw *result;
    struct ObjOtw *awaiting;

    /* How many `vm_execute` calls are running this task, nested on the C
       stack. One is the task's own; more means a native function called back
       into FunnyLang (`stash.sort_by`'s comparator, `combo`, a squad's magic
       method) and §3.1's boundary applies -- that C frame cannot be saved, so
       `await_fr` under it raises `CantWaitRightNow` instead of corrupting the
       frame stack. `nativeName` is which one, so the message can say. */
    int reentry;
    const char *nativeName;
} Task;

/* A fresh task with its own empty stack and frames. Not registered anywhere
   -- vm_task_spawn does that. */
Task *task_new(int id);
void task_free(Task *t);

/* Copies the VM's live context into `t` / out of `t`. Always paired: save the
   one going out, restore the one coming in. */
void task_save(struct VM *vm, Task *t);
void task_restore(struct VM *vm, Task *t);

/* Marks everything a *suspended* task keeps alive. The running task is
   already covered by mark_vm_roots walking the VM's own fields -- calling
   this on it would mark a stale snapshot, which is harmless but pointless.
   §3.2 calls missing this the single most likely serious bug in the plan,
   and it is right: what it looks like is an intermittent use-after-free in
   whichever task happens to resume next. */
void task_mark(struct GC *gc, Task *t);

#endif /* FUNNY_TASK_H */
