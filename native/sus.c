#include "sus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "diag.h"
#include "runner.h"
#include "error.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "squad.h"
#include "stash.h"
#include "da_string.h"
#include "value.h"
#include "vm.h"

/* A malloc'd copy of a C string, so the child VM's heap can be torn down
   before the result groupchat is built in the caller's. */
static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    memcpy(out, s, n + 1);
    return out;
}

static Value m_type_of(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *name = vm_type_name(a[0]);
    return OBJ_VAL(string_new(&vm->gc, name, (uint32_t)strlen(name)));
}

static Value m_fields_of(VM *vm, Value *a, int argc) {
    (void)argc;
    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    if (IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_INSTANCE) {
        ObjInstance *inst = (ObjInstance *)AS_OBJ(a[0]);
        for (int i = 0; i < inst->fieldCount; i++) {
            groupchat_set(&vm->gc, out, OBJ_VAL(inst->fields[i].name), inst->fields[i].value);
        }
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_is_a(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)argc;
    if (!IS_STRING(a[1])) return BOOL_VAL(false);
    return BOOL_VAL(strcmp(vm_type_name(a[0]), AS_STRING(a[1])->chars) == 0);
}

static Value m_stack_trace(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    return vm_stack_trace_stash(vm);
}

static Value m_dump(VM *vm, Value *a, int argc) {
    (void)argc;
    char *r = vm_value_to_repr(vm, a[0]);
    fputs(r, vm->out);
    fputc('\n', vm->out);
    free(r);
    return a[0];
}

/* -- sus.run_bytecode ------------------------------------------------------
 *
 * NATIVE_PLAN.md N8 task 4. A self-hosted toolchain needs to be able to run
 * FunnyLang from FunnyLang: `funny test` has to observe another program's
 * stdout and find out which error flavor (if any) escaped it, and
 * `funny bootstrap --verify` has to run a compiler bundle three times. In
 * Python those are `VM(stdout=StringIO()); vm.interpret(unit)` -- there was
 * simply no equivalent a FunnyLang program could reach.
 *
 * A *fresh* VM with its own globals and its own GC heap, deliberately: this
 * is isolation, not `eval`. The child cannot see or disturb the caller's
 * state, and nothing from its heap escapes -- every string handed back is
 * copied into the caller's heap before the child is destroyed. The child
 * never calls back into the caller, so the caller's GC cannot run while the
 * child is alive.
 *
 * stdout is captured through `tmpfile()` rather than a memory stream:
 * `open_memstream` is POSIX-only and `vm_run` takes a plain `FILE *`, so a
 * temp file is the portable way to get one. Buffer size is not a concern
 * for a test runner, and the file is deleted on close by the C standard.
 * stderr is *not* redirected: an uncaught error is reported through the
 * returned `flavor`/`message` rather than by printing a diagnostic, so
 * there is nothing for the child to write there.
 */
static Value m_run_bytecode(VM *vm, Value *a, int argc) {
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'run_bytecode' needs a stash of bytes, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *blob = (ObjStash *)AS_OBJ(a[0]);
    size_t len = (size_t)blob->count;
    uint8_t *data = (uint8_t *)malloc(len > 0 ? len : 1);
    for (size_t i = 0; i < len; i++) {
        Value b = blob->items[i];
        if (!IS_INT(b) || AS_INT(b) < 0 || AS_INT(b) > 255) {
            free(data);
            vm_throw_native(vm, "TypeVibeMismatch", "'run_bytecode' needs a stash of ints 0-255.");
            return GHOST_VAL;
        }
        data[i] = (uint8_t)AS_INT(b);
    }

    /* Program args for the child's own the_args(). Read out of the caller's
       heap now, into plain C strings, so the child never holds a reference
       to anything the caller owns. */
    int childArgc = 0;
    char **childArgv = NULL;
    if (argc > 1 && IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH) {
        ObjStash *argStash = (ObjStash *)AS_OBJ(a[1]);
        childArgc = argStash->count;
        childArgv = (char **)malloc((size_t)(childArgc > 0 ? childArgc : 1) * sizeof(char *));
        for (int i = 0; i < childArgc; i++) {
            const char *s = IS_STRING(argStash->items[i]) ? AS_STRING(argStash->items[i])->chars : "";
            size_t n = strlen(s);
            childArgv[i] = (char *)malloc(n + 1);
            memcpy(childArgv[i], s, n + 1);
        }
    }

    VM child;
    vm_init(&child);
    builtins_install(&child);
    ObjStash *args = stash_new(&child.gc, NULL, 0);
    for (int i = 0; i < childArgc; i++) {
        stash_push(&child.gc, args, OBJ_VAL(string_new(&child.gc, childArgv[i], (uint32_t)strlen(childArgv[i]))));
    }
    child.programArgs = OBJ_VAL(args);

    char *loadErr = NULL;
    CompiledUnit *unit = NULL;
    CompiledPak *pak = NULL;
    if (chunk_is_funnypak(data, len)) {
        pak = chunk_load_funnypak(data, len, &child.gc, &loadErr);
    } else {
        unit = chunk_load_funnyc(data, len, &child.gc, &loadErr);
    }

    /* Everything the child produced, pulled into plain C memory before its
       heap goes away. */
    char *outText = NULL;
    size_t outLen = 0;
    char *flavor = NULL;
    char *message = NULL;
    int64_t exitCode = 0;

    if (!unit && !pak) {
        flavor = dup_cstr("BytecodeVersionMismatch");
        message = loadErr != NULL ? loadErr : dup_cstr("couldn't load that bytecode.");
        loadErr = NULL;
        exitCode = 1;
    } else {
        FILE *capture = tmpfile();
        VmResult result = pak ? vm_run_pak(&child, pak, capture ? capture : stdout)
                              : vm_run(&child, unit, capture ? capture : stdout);
        if (capture != NULL) {
            long end = ftell(capture);
            if (end > 0) {
                outLen = (size_t)end;
                outText = (char *)malloc(outLen + 1);
                rewind(capture);
                outLen = fread(outText, 1, outLen, capture);
                outText[outLen] = '\0';
            }
            fclose(capture);
        }
        if (result == VM_ERROR) {
            int64_t systemExitCode;
            if (vm_is_system_exit(child.uncaughtError, &systemExitCode)) {
                exitCode = systemExitCode; /* dip(n) is a clean exit, not a failure */
            } else {
                ObjError *e = (ObjError *)AS_OBJ(child.uncaughtError);
                flavor = dup_cstr(e->flavor->chars);
                message = dup_cstr(e->message->chars);
                exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
            }
        }
    }

    if (pak) chunk_free_pak(pak);
    else if (unit) chunk_free_unit(unit);
    vm_destroy(&child);
    free(data);
    for (int i = 0; i < childArgc; i++) free(childArgv[i]);
    free(childArgv);
    free(loadErr);

    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    Value outValue = OBJ_VAL(string_new(&vm->gc, outText != NULL ? outText : "", (uint32_t)outLen));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "out", 3)), outValue);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "flavor", 6)),
                  flavor != NULL ? OBJ_VAL(string_new(&vm->gc, flavor, (uint32_t)strlen(flavor))) : GHOST_VAL);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "message", 7)),
                  message != NULL ? OBJ_VAL(string_new(&vm->gc, message, (uint32_t)strlen(message))) : GHOST_VAL);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "code", 4)), INT_VAL(exitCode));
    gc_pop_temp(&vm->gc);

    free(outText);
    free(flavor);
    free(message);
    return OBJ_VAL(out);
}

/* -- REPL sessions ---------------------------------------------------------
 *
 * `sus.run_bytecode` is deliberately a *fresh* VM every time, which is what a
 * test runner wants and exactly what a REPL does not: `yo x = 1` on one line
 * has to still be there on the next. A session is that same isolated child
 * VM, kept alive between calls, with its top-level namespaces living on the
 * VM itself so nothing else has to root them.
 *
 * Two other differences from run_bytecode, both driven by what a REPL needs:
 * output is *not* captured (it goes straight to the caller's own stream, so a
 * long-running input streams and an `ask()` prompt appears before its input),
 * and the entry's return value comes back as `repr` -- the repr text of the
 * last expression's value, when the unit was compiled with repl_capture_last.
 *
 * `repr` is *text*, not the value. The child has its own collector, and
 * handing one of its objects to the caller would let the caller's GC free
 * something it does not own; only the rendering crosses over. A REPL only
 * ever prints it, so nothing is lost.
 *
 * Sessions are addressed by a small integer rather than a heap object, which
 * keeps them out of the collector entirely. The cost is that an unclosed
 * session lives until its owning VM is destroyed; `sus.close_session` exists
 * for a caller (the REPL's `.clear`) that wants one reclaimed sooner.
 */
/* Runs a compiled program the way the top level runs one: output goes
   straight to this process's stdout (not captured), an uncaught error is
   rendered as the full §4.2 diagnostic on stderr, and the exit code comes
   back -- 0, `dip(n)`'s own code, 69 for an uncaught ComputerExploded, else
   1. Exactly `funny_run_bytecode`, which is what `funny run` has always
   called; exposing it is what lets the CLI itself be FunnyLang.

   Distinct from `run_bytecode` (captures output, reports the flavor instead
   of rendering) and from `run_in` (a persistent session): this one is for a
   caller that *is* the command line. */
static Value m_run_program(VM *vm, Value *a, int argc) {
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'run_program' needs a stash of bytes, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *blob = (ObjStash *)AS_OBJ(a[0]);
    size_t len = (size_t)blob->count;
    uint8_t *data = (uint8_t *)malloc(len > 0 ? len : 1);
    for (size_t i = 0; i < len; i++) {
        Value b = blob->items[i];
        if (!IS_INT(b) || AS_INT(b) < 0 || AS_INT(b) > 255) {
            free(data);
            vm_throw_native(vm, "TypeVibeMismatch", "'run_program' needs a stash of ints 0-255.");
            return GHOST_VAL;
        }
        data[i] = (uint8_t)AS_INT(b);
    }

    int childArgc = 0;
    char **childArgv = NULL;
    if (argc > 1 && IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH) {
        ObjStash *argStash = (ObjStash *)AS_OBJ(a[1]);
        childArgc = argStash->count;
        childArgv = (char **)malloc((size_t)(childArgc > 0 ? childArgc : 1) * sizeof(char *));
        for (int i = 0; i < childArgc; i++) {
            const char *s = IS_STRING(argStash->items[i]) ? AS_STRING(argStash->items[i])->chars : "";
            size_t n = strlen(s);
            childArgv[i] = (char *)malloc(n + 1);
            memcpy(childArgv[i], s, n + 1);
        }
    }

    /* An error *label* turns the diagnostic into "couldn't compile X:" plus
       the message, which is what the compile phase wants and a plain run
       does not; the caller picks by passing one or not. */
    char *label = NULL;
    if (argc > 2 && IS_STRING(a[2])) {
        const char *s = AS_STRING(a[2])->chars;
        size_t n = strlen(s);
        label = (char *)malloc(n + 1);
        memcpy(label, s, n + 1);
    }

    double runMs = 0.0;
    RunnerOptions opts;
    opts.diag = diag_default_options();
    opts.errorLabel = label;
    fflush(vm->out); /* the child writes to the real stdout; keep the order right */
    int status = funny_run_bytecode(data, len, childArgv, childArgc, opts, &runMs);

    free(data);
    free(label);
    for (int i = 0; i < childArgc; i++) free(childArgv[i]);
    free(childArgv);

    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "code", 4)), INT_VAL(status));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "ms", 2)), FLOAT_VAL(runMs));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

static Value m_new_session(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    return INT_VAL(vm_session_open(vm));
}

static Value m_close_session(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_INT(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'close_session' needs a session id, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    vm_session_close(vm, (int)AS_INT(a[0]));
    return GHOST_VAL;
}

static Value m_run_in(VM *vm, Value *a, int argc) {
    if (!IS_INT(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'run_in' needs a session id, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    struct ReplSession *session = vm_session_get(vm, (int)AS_INT(a[0]));
    if (session == NULL) {
        vm_throw_native(vm, "OutOfPocket", "there's no open session %lld.", (long long)AS_INT(a[0]));
        return GHOST_VAL;
    }
    if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'run_in' needs a stash of bytes, not a %s.", vm_type_name(a[1]));
        return GHOST_VAL;
    }
    ObjStash *blob = (ObjStash *)AS_OBJ(a[1]);
    size_t len = (size_t)blob->count;
    uint8_t *data = (uint8_t *)malloc(len > 0 ? len : 1);
    for (size_t i = 0; i < len; i++) {
        Value b = blob->items[i];
        if (!IS_INT(b) || AS_INT(b) < 0 || AS_INT(b) > 255) {
            free(data);
            vm_throw_native(vm, "TypeVibeMismatch", "'run_in' needs a stash of ints 0-255.");
            return GHOST_VAL;
        }
        data[i] = (uint8_t)AS_INT(b);
    }

    VM *child = session->vm;
    if (argc > 2 && IS_OBJ(a[2]) && AS_OBJ(a[2])->type == OBJ_STASH) {
        ObjStash *argStash = (ObjStash *)AS_OBJ(a[2]);
        ObjStash *args = stash_new(&child->gc, NULL, 0);
        child->programArgs = OBJ_VAL(args);
        for (int i = 0; i < argStash->count; i++) {
            const char *s = IS_STRING(argStash->items[i]) ? AS_STRING(argStash->items[i])->chars : "";
            stash_push(&child->gc, args, OBJ_VAL(string_new(&child->gc, s, (uint32_t)strlen(s))));
        }
    }

    char *loadErr = NULL;
    CompiledUnit *unit = chunk_load_funnyc(data, len, &child->gc, &loadErr);
    free(data);

    char *flavor = NULL;
    char *message = NULL;
    char *repr = NULL;
    int64_t exitCode = 0;

    if (unit == NULL) {
        flavor = dup_cstr("BytecodeVersionMismatch");
        message = loadErr != NULL ? loadErr : dup_cstr("couldn't load that bytecode.");
        loadErr = NULL;
        exitCode = 1;
    } else {
        /* The child VM owns the unit from here: a `bet` defined by this
           input and called by a later one holds a pointer into it, and its
           constants were allocated on the child's heap, so the child is what
           must root them. */
        vm_adopt_unit(child, unit);

        Value result;
        VmResult status = vm_run_repl_unit(child, unit, vm->out, &result);
        if (status == VM_ERROR) {
            int64_t systemExitCode;
            if (vm_is_system_exit(child->uncaughtError, &systemExitCode)) {
                exitCode = systemExitCode;
            } else {
                ObjError *e = (ObjError *)AS_OBJ(child->uncaughtError);
                flavor = dup_cstr(e->flavor->chars);
                message = dup_cstr(e->message->chars);
                exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
            }
            child->uncaughtError = GHOST_VAL;
        } else {
            repr = vm_value_to_repr(child, result);
        }
    }
    free(loadErr);

    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "repr", 4)),
                  repr != NULL ? OBJ_VAL(string_new(&vm->gc, repr, (uint32_t)strlen(repr))) : GHOST_VAL);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "flavor", 6)),
                  flavor != NULL ? OBJ_VAL(string_new(&vm->gc, flavor, (uint32_t)strlen(flavor))) : GHOST_VAL);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "message", 7)),
                  message != NULL ? OBJ_VAL(string_new(&vm->gc, message, (uint32_t)strlen(message))) : GHOST_VAL);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "code", 4)), INT_VAL(exitCode));
    gc_pop_temp(&vm->gc);

    free(flavor);
    free(message);
    free(repr);
    return OBJ_VAL(out);
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} SusEntry;

static const SusEntry SUS_FUNCTIONS[] = {
    {"type_of", m_type_of, 1, 1},
    {"fields_of", m_fields_of, 1, 1},
    {"is_a", m_is_a, 2, 2},
    {"stack_trace", m_stack_trace, 0, 0},
    {"dump", m_dump, 1, 1},
    {"run_bytecode", m_run_bytecode, 1, 2},
    {"run_program", m_run_program, 1, 3},
    {"new_session", m_new_session, 0, 0},
    {"run_in", m_run_in, 2, 3},
    {"close_session", m_close_session, 1, 1},
};
#define SUS_FUNCTIONS_COUNT (int)(sizeof(SUS_FUNCTIONS) / sizeof(SUS_FUNCTIONS[0]))

Value sus_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < SUS_FUNCTIONS_COUNT; i++) {
        const SusEntry *e = &SUS_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "sus", 3);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
