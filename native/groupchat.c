#include "groupchat.h"

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "gc.h"
#include "stash.h"

#define INITIAL_GROUPCHAT_CAPACITY 4

ObjGroupChat *groupchat_new(GC *gc, const GroupChatEntry *entries, int count) {
    ObjGroupChat *g = (ObjGroupChat *)malloc(sizeof(ObjGroupChat));
    g->obj.type = OBJ_GROUPCHAT;
    g->obj.marked = false;
    g->obj.size = 0;
    g->obj.next = NULL;
    g->count = 0;
    g->capacity = count > 0 ? count : INITIAL_GROUPCHAT_CAPACITY;
    g->entries = (GroupChatEntry *)malloc((size_t)g->capacity * sizeof(GroupChatEntry));
    gc_track(gc, (Obj *)g, sizeof(ObjGroupChat));
    for (int i = 0; i < count; i++) groupchat_set(gc, g, entries[i].key, entries[i].value);
    return g;
}

GroupChatEntry *groupchat_find(ObjGroupChat *g, Value key) {
    for (int i = 0; i < g->count; i++) {
        if (value_equal_narrow(g->entries[i].key, key)) return &g->entries[i];
    }
    return NULL;
}

void groupchat_set(GC *gc, ObjGroupChat *g, Value key, Value value) {
    (void)gc;
    GroupChatEntry *existing = groupchat_find(g, key);
    if (existing != NULL) {
        existing->value = value;
        return;
    }
    if (g->count == g->capacity) {
        g->capacity = g->capacity < INITIAL_GROUPCHAT_CAPACITY ? INITIAL_GROUPCHAT_CAPACITY : g->capacity * 2;
        g->entries = (GroupChatEntry *)realloc(g->entries, (size_t)g->capacity * sizeof(GroupChatEntry));
    }
    g->entries[g->count].key = key;
    g->entries[g->count].value = value;
    g->count++;
}

bool groupchat_remove(ObjGroupChat *g, Value key) {
    for (int i = 0; i < g->count; i++) {
        if (value_equal_narrow(g->entries[i].key, key)) {
            memmove(&g->entries[i], &g->entries[i + 1], (size_t)(g->count - i - 1) * sizeof(GroupChatEntry));
            g->count--;
            return true;
        }
    }
    return false;
}

/* -- methods (funnylang/stdlib/groupchat.py, ported) -------------------- */

static ObjGroupChat *as_groupchat(VM *vm, Value v, const char *fnName, bool *ok) {
    if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_GROUPCHAT) {
        *ok = true;
        return (ObjGroupChat *)AS_OBJ(v);
    }
    *ok = false;
    vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a groupchat, not a %s.", fnName, vm_type_name(v));
    return NULL;
}

static Value m_how_thicc(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "how_thicc", &ok);
    if (!ok) return GHOST_VAL;
    return INT_VAL(g->count);
}

static Value m_keys(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "keys", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (int i = 0; i < g->count; i++) stash_push(&vm->gc, s, g->entries[i].key);
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(s);
}

static Value m_values(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "values", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (int i = 0; i < g->count; i++) stash_push(&vm->gc, s, g->entries[i].value);
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(s);
}

static Value m_pairs(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "pairs", &ok);
    if (!ok) return GHOST_VAL;
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (int i = 0; i < g->count; i++) {
        Value pair[2] = {g->entries[i].key, g->entries[i].value};
        ObjStash *p = stash_new(&vm->gc, pair, 2);
        stash_push(&vm->gc, s, OBJ_VAL(p));
    }
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(s);
}

static Value m_has(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "has", &ok);
    if (!ok) return GHOST_VAL;
    return BOOL_VAL(groupchat_find(g, a[1]) != NULL);
}

static Value m_get(VM *vm, Value *a, int argc) {
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "get", &ok);
    if (!ok) return GHOST_VAL;
    GroupChatEntry *e = groupchat_find(g, a[1]);
    if (e != NULL) return e->value;
    return argc > 2 ? a[2] : GHOST_VAL;
}

static Value m_set(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "set", &ok);
    if (!ok) return GHOST_VAL;
    groupchat_set(&vm->gc, g, a[1], a[2]);
    return a[0];
}

static Value m_remove(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "remove", &ok);
    if (!ok) return GHOST_VAL;
    if (!groupchat_remove(g, a[1])) {
        char *disp = vm_value_to_display(a[1]);
        vm_throw_native(vm, "KeyGhosted", "key '%s' not found.", disp);
        free(disp);
        return GHOST_VAL;
    }
    return a[0];
}

static Value m_merge(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "merge", &ok);
    if (!ok) return GHOST_VAL;
    ObjGroupChat *other = as_groupchat(vm, a[1], "merge", &ok);
    if (!ok) return GHOST_VAL;
    for (int i = 0; i < other->count; i++) groupchat_set(&vm->gc, g, other->entries[i].key, other->entries[i].value);
    return a[0];
}

static Value m_clone(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "clone", &ok);
    if (!ok) return GHOST_VAL;
    return OBJ_VAL(groupchat_new(&vm->gc, g->entries, g->count));
}

static Value m_clear(VM *vm, Value *a, int argc) {
    (void)argc;
    bool ok;
    ObjGroupChat *g = as_groupchat(vm, a[0], "clear", &ok);
    if (!ok) return GHOST_VAL;
    g->count = 0;
    return a[0];
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} GroupChatMethodEntry;

static const GroupChatMethodEntry METHOD_TABLE[] = {
    {"how_thicc", m_how_thicc, 0, 0},
    {"keys", m_keys, 0, 0},
    {"values", m_values, 0, 0},
    {"pairs", m_pairs, 0, 0},
    {"has", m_has, 1, 1},
    {"get", m_get, 1, 2},
    {"set", m_set, 2, 2},
    {"remove", m_remove, 1, 1},
    {"merge", m_merge, 1, 1},
    {"clone", m_clone, 0, 0},
    {"clear", m_clear, 0, 0},
};
#define METHOD_TABLE_COUNT (int)(sizeof(METHOD_TABLE) / sizeof(METHOD_TABLE[0]))

NativeMethodFn groupchat_find_method(const char *name, int *outMinArity, int *outMaxArity) {
    for (int i = 0; i < METHOD_TABLE_COUNT; i++) {
        if (strcmp(METHOD_TABLE[i].name, name) == 0) {
            *outMinArity = METHOD_TABLE[i].minArity;
            *outMaxArity = METHOD_TABLE[i].maxArity;
            return METHOD_TABLE[i].fn;
        }
    }
    return NULL;
}
