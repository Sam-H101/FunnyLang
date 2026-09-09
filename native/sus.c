#include "sus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "squad.h"
#include "string.h"
#include "vm.h"

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
