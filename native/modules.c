#include "modules.h"

#include <stdlib.h>
#include <string.h>

#include "blob.h"
#include "clock.h"
#include "computer.h"
#include "filez.h"
#include "gc.h"
#include "groupchat.h"
#include "internet.h"
#include "interns.h"
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
    {"blob", blob_build},
    {"yapper", yapper_build},
    {"stash", stash_build},
    {"groupchat", groupchat_build},
    {"rizz", rizz_build},
    {"filez", filez_build},
    {"clock", clock_build},
    {"sus", sus_build},
    {"computer", computer_build},
    {"internet", internet_build},
    {"interns", interns_build},
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
    size_t len = strlen(path);
    char *copy = (char *)malloc(len + 1);
    memcpy(copy, path, len + 1);
    /* Split on '/' by hand. This used to be strtok, whose cursor is one
       hidden variable shared by every thread in the process: with several
       `interns` importing modules at the same moment, one thread's
       strtok(NULL, ...) carried on through another thread's string, and a
       module that was in the bundle came back "isn't in this bundle". */
    char *p = copy;
    while (*p != '\0' && count < 64) {
        char *seg = p;
        while (*p != '\0' && *p != '/') p++;
        if (*p == '/') *p++ = '\0';
        if (seg[0] == '\0' || strcmp(seg, ".") == 0) {
            /* dropped */
        } else if (strcmp(seg, "..") == 0) {
            /* A leading ".." can't be resolved away -- keep it, so the
               lookup fails loudly instead of silently landing somewhere
               else. */
            if (count > 0 && strcmp(segments[count - 1], "..") != 0) count--;
            else segments[count++] = seg;
        } else {
            segments[count++] = seg;
        }
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

    /* A module reached again while it is still running is a cycle. The cache
       above only holds modules that have *finished*, so without this check
       the loader re-enters vm_run_module forever and overflows the C stack --
       a segfault where funnylang/modules.py raises a diagnostic. The smallest
       case is a file importing itself. */
    int start = vm_loading_index_of(vm, target);
    if (start >= 0) {
        char chain[1024];
        size_t pos = 0;
        for (int i = start; i <= vm_loading_count(vm); i++) {
            /* One past the end repeats the module that closes the loop, the
               way `self.loading[idx:] + [key]` does. */
            const char *name = i < vm_loading_count(vm) ? vm_loading_at(vm, i) : target;
            const char *slash = strrchr(name, '/');
            const char *base = slash != NULL ? slash + 1 : name;
            int written = snprintf(chain + pos, sizeof chain - pos, "%s%s",
                                   i > start ? " \xe2\x86\x92 " : "", base);
            if (written < 0 || (size_t)written >= sizeof chain - pos) break;
            pos += (size_t)written;
        }
        vm_throw_native(vm, "ImportSkillIssue", "circular import: %s", chain);
        return GHOST_VAL;
    }

    gc_push_temp(&vm->gc, key);
    vm_loading_push(vm, target);
    Value module = vm_run_module(vm, unit, target);
    vm_loading_pop(vm);
    if (!vm->hadError) groupchat_set(&vm->gc, cache, key, module);
    gc_pop_temp(&vm->gc);
    return module;
}
