#include "modules.h"

#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "computer.h"
#include "filez.h"
#include "gc.h"
#include "groupchat.h"
#include "internet.h"
#include "mafs.h"
#include "rizz.h"
#include "stash.h"
#include "sus.h"
#include "vm.h"
#include "yapper.h"

ObjModule *module_new(GC *gc, ObjString *name, Value members) {
    ObjModule *m = (ObjModule *)malloc(sizeof(ObjModule));
    m->obj.type = OBJ_MODULE;
    m->obj.marked = false;
    m->obj.size = 0;
    m->obj.next = NULL;
    m->name = name;
    m->members = members;
    gc_track(gc, (Obj *)m, sizeof(ObjModule));
    return m;
}

/* The stdlib registry (funnylang/stdlib/__init__.py's own STDLIB_NAMES/
   _build): one entry per module `gimme modulename` can resolve. All 12
   modules are now ported (N5 task 2 complete). */
typedef Value (*StdlibBuilderFn)(VM *vm);

typedef struct {
    const char *name;
    StdlibBuilderFn build;
} StdlibEntry;

static const StdlibEntry STDLIB_REGISTRY[] = {
    {"mafs", mafs_build},
    {"yapper", yapper_build},
    {"stash", stash_build},
    {"groupchat", groupchat_build},
    {"rizz", rizz_build},
    {"filez", filez_build},
    {"clock", clock_build},
    {"sus", sus_build},
    {"computer", computer_build},
    {"internet", internet_build},
};
#define STDLIB_REGISTRY_COUNT (int)(sizeof(STDLIB_REGISTRY) / sizeof(STDLIB_REGISTRY[0]))

/* posixpath.normpath over a '/'-separated logical name, in place: collapse
   ".", resolve ".." against what came before, and drop empty segments.
   Bundle names are always relative and always use forward slashes (the
   linker writes them that way), so there is no drive letter or leading-
   slash case to handle here. */
static void normalize_module_path(char *path) {
    char *segments[64];
    int count = 0;
    char *save = path;
    char *copy = (char *)malloc(strlen(path) + 1);
    memcpy(copy, path, strlen(path) + 1);
    char *tok = strtok(copy, "/");
    while (tok && count < 64) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            /* dropped */
        } else if (strcmp(tok, "..") == 0) {
            /* A leading ".." can't be resolved away -- keep it, so the
               lookup fails loudly instead of silently landing somewhere
               else. */
            if (count > 0 && strcmp(segments[count - 1], "..") != 0) count--;
            else segments[count++] = tok;
        } else {
            segments[count++] = tok;
        }
        tok = strtok(NULL, "/");
    }
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) save[pos++] = '/';
        size_t n = strlen(segments[i]);
        memcpy(save + pos, segments[i], n);
        pos += n;
    }
    save[pos] = '\0';
    free(copy);
}

/* Resolves a quoted import against the module doing the importing, exactly
   as funnylang/modules.py's own pak loader does: join it onto the current
   module's directory, then normalize. */
static void resolve_against_current(const VM *vm, const char *path, char *out, size_t outLen) {
    const char *current = vm->currentModuleName ? vm->currentModuleName : "";
    const char *slash = strrchr(current, '/');
    if (slash != NULL) {
        size_t dirLen = (size_t)(slash - current);
        snprintf(out, outLen, "%.*s/%s", (int)dirLen, current, path);
    } else {
        snprintf(out, outLen, "%s", path);
    }
    normalize_module_path(out);
}

Value do_import(VM *vm, const char *path, int mode) {
    if (mode == 2) {
        for (int i = 0; i < STDLIB_REGISTRY_COUNT; i++) {
            if (strcmp(STDLIB_REGISTRY[i].name, path) == 0) return STDLIB_REGISTRY[i].build(vm);
        }
        vm_throw_native(vm, "ImportSkillIssue", "no stdlib module named '%s'.", path);
        return GHOST_VAL;
    }

    /* Modes 0 and 1 (`gimme "x.funny"` and `gimme { a } from "x.funny"`)
       resolve identically -- mode 1's caller just does a GET_PROP per name
       afterwards, so both want the whole Module here. */
    if (vm->pak == NULL) {
        /* No bundle: there is nothing to resolve against, and compiling
           the file from source would need a compiler this runtime doesn't
           have. Same WhoDis funnylang/vm.py raises when no module_loader
           is configured, rather than half-working. */
        vm_throw_native(vm, "WhoDis", "can't find '%s'. modules aren't wired up yet.", path);
        return GHOST_VAL;
    }

    char target[1024];
    resolve_against_current(vm, path, target, sizeof(target));

    /* Each module runs once, however many times it's imported -- the cache
       is what makes a diamond import share one instance rather than
       re-running side effects. */
    Value key = OBJ_VAL(string_new(&vm->gc, target, (uint32_t)strlen(target)));
    ObjGroupChat *cache = (ObjGroupChat *)AS_OBJ(vm->pakModuleCache);
    GroupChatEntry *cached = groupchat_find(cache, key);
    if (cached != NULL) return cached->value;

    CompiledUnit *unit = chunk_pak_find(vm->pak, target);
    if (unit == NULL) {
        vm_throw_native(vm, "ImportSkillIssue", "'%s' isn't in this bundle.", path);
        return GHOST_VAL;
    }
    gc_push_temp(&vm->gc, key);
    Value module = vm_run_module(vm, unit, target);
    if (!vm->hadError) groupchat_set(&vm->gc, cache, key, module);
    gc_pop_temp(&vm->gc);
    return module;
}
