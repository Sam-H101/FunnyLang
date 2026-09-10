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
#include "da_string.h"
#include "value.h"

typedef struct {
    ObjString *name;
    Value value;
} GlobalEntry;

typedef enum {
    VM_OK,
    VM_ERROR,
} VmResult;

/* A method bound to a receiver (PLAN.md §3.9's per-type instance methods --
 * stash/groupchat now, numba/yapstring/pointa's method forms later). Mirrors
 * funnylang/vm.py's `_bind_native_method`, which wraps a plain Python
 * function into a NativeFn closing over the receiver; `args[0]` is always
 * that receiver here too, exactly like the Python method functions
 * (funnylang/stdlib/stash.py's own `_yeet_in(vm, a)` etc. read `a[0]` for
 * it), so porting one is close to mechanical.
 */
typedef Value (*NativeMethodFn)(struct VM *vm, Value *argsIncludingReceiver, int argcIncludingReceiver);

typedef struct {
    Obj obj;
    Value receiver;
    NativeMethodFn fn;
    const char *name; /* a static string literal; never owned/freed */
    int minArity;      /* excluding the receiver */
    int maxArity;
} ObjBoundNative;

struct GC;
ObjBoundNative *bound_native_new(struct GC *gc, Value receiver, NativeMethodFn fn, const char *name, int minArity, int maxArity);

/* A builtin/stdlib-module function with no implicit receiver (unlike
 * ObjBoundNative) -- mirrors funnylang/values.py's own NativeFn exactly:
 * `fn(vm, args) -> value`, `args` being only the call's own arguments.
 * Used for the always-in-scope globals (native/builtins.c) and, from N5's
 * later sub-phases, real stdlib module members (mafs.sqrt, yapper.upper,
 * ...).
 */
typedef struct {
    Obj obj;
    NativeMethodFn fn; /* same C signature as a bound method's; the
                           difference is purely convention -- nothing here
                           treats args[0] as a receiver. */
    const char *name;
    int minArity;
    int maxArity;
} ObjNativeFn;

ObjNativeFn *native_fn_new(struct GC *gc, NativeMethodFn fn, const char *name, int minArity, int maxArity);

/* PLAN.md M11's recursion cap -- ported unchanged (funnylang/vm.py's
   MAX_FRAMES). */
#define VM_MAX_FRAMES 10000

struct VM {
    GC gc;

    /* ASYNC_PLAN.md A3: these six, plus the open-upvalue array below and the
       position/error scalars further down, are the *running task's* context.
       Every other task's copy of them is saved in its own Task (task.h), and
       switching is a pair of struct copies. task.h argues why the running
       one stays here rather than behind a pointer. */
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

    /* Module globals no longer live directly on the VM (N5 task 3): each
       closure has its own `moduleGlobals` GroupChat (frames.h), and
       GET/SET/DEF_GLOBAL and PTR_GLOBAL all read/write the *current
       frame's* one -- see vm.c's own note where globals_find/globals_set
       used to be. */

    /* The always-in-scope builtins (native/builtins.c), a *separate*
       namespace from `globals` -- GET_GLOBAL checks `globals` first, then
       falls back to this, exactly matching funnylang/vm.py's own
       `_read_global` (module-globals, then vm.builtins). Once real module
       namespacing (N5 task 3) exists, every module's own globals dict
       falls back to this same table, unchanged. */
    GlobalEntry *builtins;
    int builtinCount;
    int builtinCapacity;

    /* the_args() wraps this in a Stash; GHOST_VAL (not an empty Stash)
       until the embedder (main.c) sets it, matching
       funnylang/stdlib/builtins.py's own `getattr(vm, "program_args",
       [])` default. */
    Value programArgs;

    /* The module currently executing, borrowed -- swapped by vm_run_module
       for the duration of an import and restored afterwards, so error
       positions and stack traces name the file the code actually came
       from. Directly analogous to funnylang/vm.py's own `self.source`
       swap; note it is *not* where constants come from (those hang off
       each closure's own unit, since a call can cross module boundaries
       mid-execution). */
    CompiledUnit *unit;

    /* PLAN.md §5.3's linked bundle, when one is being run: the modules a
       quoted `gimme "other.funny"` can resolve against, plus the
       already-run ones. Both NULL/ghost for a plain .funnyc run, which is
       what makes a file import fail there with WhoDis instead of
       half-working. */
    CompiledPak *pak; /* borrowed; the embedder keeps it alive */
    Value pakModuleCache; /* canonical name -> Module, so each runs once */
    /* The *logical bundle name* of the module currently executing, which a
       relative `gimme "../x.funny"` resolves against. Deliberately not the
       unit's own sourceName: the bundler's keys are what imports are
       written against, and funnylang/modules.py's loader resolves against
       exactly this (its CanonicalSource(target)), not the original file
       path a module happened to be compiled from. */
    const char *currentModuleName;

    FILE *out; /* where YAP writes */
    /* Where `yell` writes, and where an uncaught error's diagnostic is
       rendered. stderr for a top-level program, and a capture for a
       child VM: `sus.run_bytecode` is meant to isolate the program it
       runs, and a child's diagnostics landing in the parent's stderr is
       the same leak stdout used to have. Never NULL after vm_init. */
    FILE *err;
    /* Bundle modules currently being executed, innermost last. A module is
       only added to `pakModuleCache` once it has *finished*, so without this
       a cycle re-enters vm_run_module forever and overflows the C stack --
       which is a segfault, not a diagnostic. funnylang/modules.py has always
       kept the same stack, and reports the cycle from it. Owned strings. */
    char **loadingModules;
    int loadingCount;
    int loadingCapacity;

    /* Every task in this VM, task zero (the entry program) first. The one
       whose context is in the fields above is `currentTask`; the rest have
       theirs saved. The collector walks all of them -- §3.2 names missing
       that as the single most likely serious bug in the whole plan. */
    struct Task **tasks;
    int taskCount;
    int taskCapacity;
    struct Task *currentTask;
    int nextTaskId;

    /* The `interns` worker this VM is running as, or NULL for a VM that is
       not one (the main program, a `sus` child, a REPL session). It is here
       rather than in thread-local storage because the VM already *is* the
       per-execution context, and a `_Thread_local` would be a second one --
       with the added problem that MSVC's support for the C11 spelling is not
       something to depend on. `interns.assignment()` and `interns.deliver()`
       read it to find which worker they belong to. */
    void *workerContext;

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

    /* A REPL session's persistent top-level namespaces (see
       vm_run_repl_unit). GHOST for any VM that is not one; rooted by the
       GC so they survive between inputs, when no frame refers to them. */
    Value sessionGlobals;
    Value sessionExports;

    /* sus.new_session()'s bookkeeping, owned by this VM so that
       vm_destroy closes any session a program left open. Sessions are
       addressed by a 1-based index rather than a heap object: that keeps
       them out of the collector entirely, at the cost of needing
       sus.close_session() to reclaim one early. */
    struct ReplSession *sessions;
    int sessionCount;
    int sessionCapacity;

    /* CompiledUnits this VM owns outright, rather than borrowing from an
       embedder: every REPL input ever run in a session. A `bet` defined by
       one input and called by a later one holds a pointer into its unit, so
       they have to live as long as the VM -- and their constants have to be
       rooted by *this* VM's collector, since that is the heap the strings
       in them were allocated on. */
    CompiledUnit **ownedUnits;
    int ownedUnitCount;
    int ownedUnitCapacity;
};

/* A persistent child VM. The units it has run live on the VM itself (see
   ownedUnits) rather than here, so the collector that allocated their
   constants is the one that roots and frees them. */
struct ReplSession {
    VM *vm;
    bool open;
};

/* Hands `unit` to `vm` to own and root for the rest of its life. */
void vm_adopt_unit(VM *vm, CompiledUnit *unit);

void vm_init(VM *vm);
void vm_destroy(VM *vm);

/* -- tasks (ASYNC_PLAN.md A3) ---------------------------------------------
 *
 * A new task, registered with this VM and READY. It has its own empty stack
 * and frames; putting something on them is the caller's job. */
struct Task *vm_task_spawn(VM *vm);

/* Makes `to` the running task: saves the current one's context into its Task
   and restores `to`'s. A no-op if `to` is already running. The task going out
   keeps whatever state it had set for itself (WAITING, DONE, ...) and is
   marked READY only if it was still RUNNING. */
void vm_task_switch(VM *vm, struct Task *to);

/* Frees a finished task's arrays early rather than at vm_destroy. Safe only
   once nothing can resume it: its state must be TASK_DONE and it must not be
   the running task. */
void vm_task_retire(VM *vm, struct Task *t);

/* Runs unit->protos[unit->entryProto]. `unit` must outlive the call (its
   consts, in particular, are referenced directly, not copied). Returns
   VM_ERROR if the program raised past its outermost frame; see
   vm->uncaughtError. */
VmResult vm_run(VM *vm, CompiledUnit *unit, FILE *out);

/* Runs a bundle: sets up `pak`, then runs its entry module. Everything the
   entry imports resolves against the bundle rather than the filesystem, so
   a .funnypak is self-contained by construction. */
VmResult vm_run_pak(VM *vm, CompiledPak *pak, FILE *out);

/* The import-loading stack, for modules.c's cycle check. Not part of the
   language surface: these exist because do_import lives in another
   translation unit. */
void vm_loading_push(VM *vm, const char *name);
void vm_loading_pop(VM *vm);
int vm_loading_index_of(const VM *vm, const char *name);
const char *vm_loading_at(const VM *vm, int i);
int vm_loading_count(const VM *vm);

/* Runs `unit`'s entry as the top level of a *persistent* session: the
   globals and exports live on the VM rather than being created fresh, which
   is what "the REPL's globals survive between inputs" means. `*valueOut`
   receives the entry's own return value -- the last expression's value when
   the unit was compiled with repl_capture_last, `ghost` otherwise. The unit
   must outlive the session, not just the call. */
VmResult vm_run_repl_unit(VM *vm, CompiledUnit *unit, FILE *out, Value *valueOut);

/* Opens a session and returns its 1-based id, or closes one. Reopening a
   closed id never happens: ids are handed out monotonically. */
int vm_session_open(VM *vm);
struct ReplSession *vm_session_get(VM *vm, int id);
void vm_session_close(VM *vm, int id);

/* Runs one module's top-level code once, in its own fresh namespace, and
   returns a Module of whatever it `flex`ed -- funnylang/vm.py's own
   run_module. On error returns GHOST_VAL with vm->hadError set, same
   convention as every other VM-internal helper that can raise. */
Value vm_run_module(VM *vm, CompiledUnit *unit, const char *moduleName);

/* Calls any callable Value (an ObjClosure or an ObjBoundNative) with
   `argc` args and returns its result -- for native methods that need to
   call back into FunnyLang code (stash's sort/glow_up/vibe_check/squish/
   any/all all take a callback). Mirrors funnylang/vm.py's own
   `call_value`, which native functions there call for exactly the same
   reason. On error, returns GHOST_VAL and leaves vm->hadError set for the
   caller (the dispatch loop, or another nested vm_call_value) to notice
   and propagate -- never raises or exits on its own. */
Value vm_call_value(VM *vm, Value callee, Value *args, int argc);

/* Exposed so native method bodies (stash.c, groupchat.c, ...) can raise
   FunnyErrors and format values the exact same way the dispatch loop's
   own arithmetic helpers do, without duplicating vm.c's private
   type_name_of/value_to_display/vm_throw_fmt logic. vm_throw_native sets
   vm->hadError the same way vm_throw_fmt does; callers must still return
   promptly afterward (typically GHOST_VAL) so the dispatch loop notices
   on its next iteration -- it does not unwind or exit on its own.
   vm_value_to_display's result is malloc'd; the caller frees it. Both take
   `vm` (not just `Value`) because an Instance's display routes through its
   squad's `to_yap` magic method, if it has one -- a real call back into
   FunnyLang code, exactly like the callback-taking stash methods. */
void vm_throw_native(VM *vm, const char *flavor, const char *fmt, ...);

/* Raises an ObjError that already exists instead of building one. An error
   with no position yet is stamped with the current site, so a worker's error
   re-raised by `interns.wait_up` points at the line that waited. */
void vm_rethrow(VM *vm, Value errValue);
/* Same, but with PLAN.md §4.1's site-specific roast -- the comedic line
   funny-mode diagnostics print instead of the message. Only for the stdlib
   throw sites whose Python counterpart passes an explicit `roast=`; the
   rest keep calling vm_throw_native and get the flavor's default. */
void vm_throw_native_roast(VM *vm, const char *flavor, const char *roast, const char *fmt, ...);
const char *vm_type_name(Value v);
char *vm_value_to_display(VM *vm, Value v);
/* Same, but reports the display's true byte length. A FunnyLang string can
   contain NUL bytes, so any caller that copies the display into a new
   string or writes it to a stream must use this -- strlen() would cut the
   value short at the first NUL. */
char *vm_value_to_display_len(VM *vm, Value v, size_t *lenOut);
/* Like vm_value_to_display, but a string is quoted (json-style) --
   funnylang/values.py's own `to_repr`, for the one builtin (sheesh) that
   calls it directly rather than through a stash/groupchat's own nested
   repr. */
char *vm_value_to_repr(VM *vm, Value v);

/* A stash of "at foo()  path:line" strings for the VM's current call
   stack, innermost frame first -- funnylang/vm.py's own `_build_trace`,
   wrapped as a Value for `sus.stack_trace()` (the only stdlib caller;
   the dispatch loop's own error-unwinding path uses vm.c's private
   build_trace directly and has no need for a Stash). */
Value vm_stack_trace_stash(VM *vm);

/* dip()'s "terminate the whole process now, no `my_bad`/`regardless`
   gets a chance to run at any nesting level" -- deliberately not a normal
   FunnyError: matches Python's SystemExit not being a FunnyError
   subclass, so `except FunnyError` (and therefore _unwind) never even
   sees it there either. vm_unwind_to_handler refuses to search for a
   handler at all once it recognizes this, at every level, the same way
   Python's own exception propagation skips every `except FunnyError`
   clause on its way out. */
void vm_request_exit(VM *vm, int64_t code);
/* True (with *outCode set) if `errValue` is exactly that sentinel --
   for vm_run/main.c to notice an uncaught one and exit(code) cleanly
   instead of reporting it as a crash. */
bool vm_is_system_exit(Value errValue, int64_t *outCode);

/* Registers a name in vm->builtins (native/builtins.c's own setup code;
   never called by opcode dispatch -- SET_GLOBAL/DEF_GLOBAL only ever
   write to vm->globals, matching funnylang/vm.py's own SET_GLOBAL, which
   always targets module_globals and never vm.builtins). Overwrites an
   existing entry for `name` if one exists. */
void vm_define_builtin(VM *vm, ObjString *name, Value value);

/* Structural equality (funnylang/values.py's `funny_eq`): unlike
   value_equal_narrow (value.h), Stash/GroupChat compare by contents here,
   recursively, matching OP_EQ/OP_NEQ and stash's contains/index_of. Takes
   `vm` for the same reason as vm_value_to_display: comparing two Instances
   calls their squad's `same_energy` magic method, if it has one. */
bool vm_value_equal(VM *vm, Value a, Value b);

/* mafs.pow needs int**int arbitrary-precision behavior identical to the
   `^`/POW opcode's own -- exposes vm_pow (otherwise private to vm.c)
   rather than reimplementing bignum exponentiation a second time. */
Value vm_numeric_pow(VM *vm, Value a, Value b);
/* stash.sum_up needs the exact same numeric +/promotion semantics as the
   `+` opcode -- exposes vm_add rather than reimplementing int64-overflow-
   to-bignum promotion a second time. Only ever called with confirmed-
   numeric operands, so vm_add's own string/stash/pointer branches never
   trigger. */
Value vm_numeric_add(VM *vm, Value a, Value b);

#endif /* FUNNY_VM_H */
