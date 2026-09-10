#include "sus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "error.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "squad.h"
#include "stash.h"
#include "string.h"
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
