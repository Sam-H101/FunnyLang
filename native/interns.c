/* native/interns.c -- see interns.h. */
#include "interns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "diag.h"
#include "error.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "object.h"
#include "platform.h"
#include "portable.h"
#include "runner.h"
#include "stash.h"
#include "da_string.h"
#include "sus.h"
#include "value.h"
#include "vm.h"

static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    memcpy(out, s, n + 1);
    return out;
}

/* -- one intern ------------------------------------------------------------
 *
 * Everything an Intern owns is plain malloc'd memory. Nothing in here points
 * into any GC heap -- not the parent's, not the worker's -- which is what
 * makes it safe for the struct to be written on one thread and read on
 * another, and safe for a worker to outlive the run that hired it.
 */
typedef struct Intern {
    /* Unique among this owner's interns, not among the process's -- see
       intern_at. */
    int id;
    struct VM *owner;

    PlatformThread thread;
    bool spawned;
    bool joined;
    bool retired; /* waited on; the slot survives only to say so */

    char *label; /* the path it was hired from, for messages */

    /* Owned by the parent until the thread starts, then by the worker. The
       thread start is the handoff, and platform_thread_join is the handback;
       nothing else touches these while the worker runs. */
    uint8_t *code;
    size_t codeLen;
    PortableValue *assignment;
    PortableValue *result;
    char *flavor; /* NULL unless an error escaped the worker */
    char *message;
    char *outText;
    size_t outLen;
    char *errText;
    size_t errLen;
    int64_t exitCode;
} Intern;

/* The registry. Global rather than per-VM because a worker may hire its own
   interns, and its VM is a different one; handles stay unique across the
   process so a stray one is reported as not-yours rather than silently
   naming somebody else's worker. */
static PlatformMutex g_lock;
static bool g_ready = false;
static Intern **g_interns = NULL;
static int g_internCount = 0;
static int g_internCapacity = 0;

/* Compiled worker bundles, keyed by the path they were hired from. The §1
   example hires the same script twice, and compiling it twice means running
   the whole self-hosted toolchain twice for a byte-identical answer. Nothing
   invalidates this: a source file changing underneath a running process is
   not a case the rest of the toolchain handles either. */
typedef struct {
    char *path;
    uint8_t *code;
    size_t len;
} CompiledWorker;

static CompiledWorker *g_cache = NULL;
static int g_cacheCount = 0;
static int g_cacheCapacity = 0;

/* Called from interns_build, i.e. from `gimme interns`, which the hiring VM
   must have done before any worker thread exists -- there is no other way to
   reach `hire`. So this one-time init is genuinely single-threaded, even
   though everything it guards is not. */
static void registry_init(void) {
    if (g_ready) return;
    platform_mutex_init(&g_lock);
    g_ready = true;
}

static void intern_release(Intern *in) {
    free(in->label);
    free(in->code);
    portable_free(in->assignment);
    portable_free(in->result);
    free(in->flavor);
    free(in->message);
    free(in->outText);
    free(in->errText);
    in->label = NULL;
    in->code = NULL;
    in->assignment = NULL;
    in->result = NULL;
    in->flavor = NULL;
    in->message = NULL;
    in->outText = NULL;
    in->errText = NULL;
}

/* -- the worker thread ----------------------------------------------------- */

/* Drains a capture stream into malloc'd memory and closes it. The stream is
   a tmpfile(), so closing it is also what deletes it. */
static char *drain(FILE *f, size_t *outLen) {
    *outLen = 0;
    if (f == NULL) return NULL;
    fflush(f);
    long end = ftell(f);
    if (end <= 0) {
        fclose(f);
        return NULL;
    }
    size_t n = (size_t)end;
    char *text = (char *)malloc(n + 1);
    rewind(f);
    n = fread(text, 1, n, f);
    text[n] = '\0';
    fclose(f);
    *outLen = n;
    return text;
}

static void intern_body(void *userdata) {
    Intern *in = (Intern *)userdata;

    VM child;
    vm_init(&child);
    builtins_install(&child);
    /* How `interns.assignment()` and `interns.deliver()` find their way back
       here from inside the worker's own code. */
    child.workerContext = in;

    /* The worker's `yap` and `yell` are held rather than printed, and
       replayed by `wait_up` on the waiting thread. Two workers writing to a
       shared stream would interleave by luck, and §5 rules out a golden that
       depends on luck; replaying at the join point makes the output appear in
       the order the program waited, which is a fixed order. */
    FILE *outCap = tmpfile();
    FILE *errCap = tmpfile();
    if (errCap != NULL) child.err = errCap;

    char *loadErr = NULL;
    CompiledUnit *unit = NULL;
    CompiledPak *pak = NULL;
    if (chunk_is_funnypak(in->code, in->codeLen)) {
        pak = chunk_load_funnypak(in->code, in->codeLen, &child.gc, &loadErr);
    } else {
        unit = chunk_load_funnyc(in->code, in->codeLen, &child.gc, &loadErr);
    }

    if (!unit && !pak) {
        in->flavor = dup_cstr("BytecodeVersionMismatch");
        in->message = loadErr != NULL ? loadErr : dup_cstr("couldn't load that bytecode.");
        loadErr = NULL;
        in->exitCode = 1;
    } else {
        VmResult result = pak ? vm_run_pak(&child, pak, outCap != NULL ? outCap : stdout)
                              : vm_run(&child, unit, outCap != NULL ? outCap : stdout);
        if (result == VM_ERROR) {
            int64_t systemExitCode;
            if (vm_is_system_exit(child.uncaughtError, &systemExitCode)) {
                in->exitCode = systemExitCode; /* dip(n) is a clean finish */
            } else {
                ObjError *e = (ObjError *)AS_OBJ(child.uncaughtError);
                in->flavor = dup_cstr(e->flavor->chars);
                in->message = dup_cstr(e->message->chars);
                in->exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
            }
        }
    }

    in->outText = drain(outCap, &in->outLen);
    child.err = stderr;
    in->errText = drain(errCap, &in->errLen);

    if (pak) chunk_free_pak(pak);
    else if (unit) chunk_free_unit(unit);
    vm_destroy(&child);
    free(loadErr);
}

/* -- getting the worker's bytecode ----------------------------------------- */

static const uint8_t *cache_get(const char *path, size_t *outLen) {
    for (int i = 0; i < g_cacheCount; i++) {
        if (strcmp(g_cache[i].path, path) == 0) {
            *outLen = g_cache[i].len;
            return g_cache[i].code;
        }
    }
    return NULL;
}

static void cache_put(const char *path, const uint8_t *code, size_t len) {
    if (g_cacheCount == g_cacheCapacity) {
        g_cacheCapacity = g_cacheCapacity == 0 ? 4 : g_cacheCapacity * 2;
        g_cache = (CompiledWorker *)realloc(g_cache, (size_t)g_cacheCapacity * sizeof(CompiledWorker));
    }
    uint8_t *copy = (uint8_t *)malloc(len > 0 ? len : 1);
    memcpy(copy, code, len);
    g_cache[g_cacheCount].path = dup_cstr(path);
    g_cache[g_cacheCount].code = copy;
    g_cache[g_cacheCount].len = len;
    g_cacheCount++;
}

static bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + (n - m), suffix) == 0;
}

/* Compiles `path` by running the embedded toolchain -- the same bytes
   `funny build` is -- in a child VM, exactly as `sus.run_program` does. There
   is no C compiler for FunnyLang to call instead: the compiler is written in
   FunnyLang, which is the point of the self-hosting milestone, and running it
   is how anything compiles anything here.

   Both of its streams are captured. On success the toolchain prints
   "built /tmp/... (N bytes, 1 module)." and nobody hired an intern to read
   that; on failure `cmd_build` has already written "Flavor: message" to
   stderr, which is the useful half of the diagnostic and becomes the message
   of the error raised at the `hire` site. */
static bool compile_worker(const char *path, uint8_t **outCode, size_t *outLen, char **errText) {
    size_t tcLen = 0;
    const uint8_t *tc = sus_get_toolchain(&tcLen);
    if (tc == NULL) {
        *errText = dup_cstr("there's no compiler in this build, so an intern can only be hired from "
                            "already-compiled bytecode (.funnyc or .funnypak).");
        return false;
    }

    char tmp[1024];
    if (!platform_temp_file("funnyintern", tmp, sizeof tmp)) {
        *errText = dup_cstr("couldn't make a temp file to compile the intern's work into.");
        return false;
    }

    char build[] = "build";
    char dashO[] = "-o";
    char *argv[4];
    argv[0] = build;
    argv[1] = (char *)path;
    argv[2] = dashO;
    argv[3] = tmp;

    FILE *outCap = tmpfile();
    FILE *errCap = tmpfile();
    RunnerOptions opts;
    opts.diag = diag_default_options();
    opts.diag.color = false;
    opts.errorLabel = NULL;
    opts.out = outCap != NULL ? outCap : stdout;
    opts.err = errCap != NULL ? errCap : stderr;
    int status = funny_run_bytecode(tc, tcLen, argv, 4, opts, NULL);

    size_t junkLen = 0;
    free(drain(outCap, &junkLen));
    size_t diagLen = 0;
    char *diagText = drain(errCap, &diagLen);

    bool ok = false;
    if (status != 0) {
        if (diagText != NULL && diagLen > 0) {
            /* One trailing newline, because this becomes the tail of a
               sentence in the parent's own diagnostic. */
            while (diagLen > 0 && (diagText[diagLen - 1] == '\n' || diagText[diagLen - 1] == '\r')) {
                diagText[--diagLen] = '\0';
            }
            *errText = diagText;
            diagText = NULL;
        } else {
            *errText = dup_cstr("that file wouldn't compile.");
        }
    } else {
        char errbuf[512];
        unsigned char *data = NULL;
        size_t len = 0;
        if (platform_read_file(tmp, &data, &len, errbuf, sizeof errbuf)) {
            *outCode = data;
            *outLen = len;
            ok = true;
        } else {
            *errText = dup_cstr(errbuf);
        }
    }

    free(diagText);
    char rmbuf[512];
    platform_remove_path(tmp, rmbuf, sizeof rmbuf);
    return ok;
}

/* Bytes for a worker hired from `path`, compiling it first if it is source.
   The cache is consulted and filled here, so an already-compiled bundle
   passed by path is *not* cached -- reading a file is cheap, and caching it
   would be the one case where a stale copy is plausible. */
static bool worker_code(const char *path, uint8_t **outCode, size_t *outLen, char **errText) {
    if (ends_with(path, ".funnyc") || ends_with(path, ".funnypak")) {
        char errbuf[512];
        unsigned char *data = NULL;
        size_t len = 0;
        if (!platform_read_file(path, &data, &len, errbuf, sizeof errbuf)) {
            *errText = dup_cstr(errbuf);
            return false;
        }
        *outCode = data;
        *outLen = len;
        return true;
    }

    platform_mutex_lock(&g_lock);
    size_t cachedLen = 0;
    const uint8_t *cached = cache_get(path, &cachedLen);
    if (cached != NULL) {
        *outCode = (uint8_t *)malloc(cachedLen > 0 ? cachedLen : 1);
        memcpy(*outCode, cached, cachedLen);
        *outLen = cachedLen;
        platform_mutex_unlock(&g_lock);
        return true;
    }
    platform_mutex_unlock(&g_lock);

    if (!compile_worker(path, outCode, outLen, errText)) return false;

    platform_mutex_lock(&g_lock);
    if (cache_get(path, &cachedLen) == NULL) cache_put(path, *outCode, *outLen);
    platform_mutex_unlock(&g_lock);
    return true;
}

/* -- the module ------------------------------------------------------------ */

/* Handles are numbered per hiring VM, not per process, and looked up the
   same way. Two reasons, and the second is the one that matters:

   A numba is one of the things that *can* cross to a worker, so an intern can
   be handed a colleague's ticket. With process-wide numbering that ticket
   would name a thread on another VM, and two threads joining the same thread
   is undefined behaviour rather than a race anything would catch. Numbering
   per VM makes a stolen handle mean "my own intern #2", which either exists
   -- and is safely this thread's to join -- or does not. The check is
   structural instead of being a rule to remember.

   And it makes #1 mean #1: a golden that prints a handle would otherwise
   depend on how many interns every *other* golden in the same `funny test`
   process had hired first.

   Both callers hold g_lock. */
static Intern *intern_at(VM *vm, int id) {
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->owner == vm && in->id == id) return in;
    }
    return NULL;
}

static int next_id_for(VM *vm) {
    int highest = 0;
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->owner == vm && in->id > highest) highest = in->id;
    }
    return highest + 1;
}

static Value m_hire(VM *vm, Value *a, int argc) {
    if (!IS_STRING(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'hire' needs the path to a .funny file, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    char *path = dup_cstr(AS_STRING(a[0])->chars);

    char reason[512];
    reason[0] = '\0';
    PortableValue *assignment = portable_from_value(argc > 1 ? a[1] : GHOST_VAL, reason, sizeof reason);
    if (assignment == NULL) {
        free(path);
        vm_throw_native(vm, "TypeVibeMismatch", "%s", reason);
        return GHOST_VAL;
    }

    uint8_t *code = NULL;
    size_t codeLen = 0;
    char *errText = NULL;
    if (!worker_code(path, &code, &codeLen, &errText)) {
        portable_free(assignment);
        vm_throw_native(vm, "ImportSkillIssue", "can't hire from '%s': %s", path,
                        errText != NULL ? errText : "unknown problem.");
        free(errText);
        free(path);
        return GHOST_VAL;
    }

    Intern *in = (Intern *)calloc(1, sizeof(Intern));
    in->owner = vm;
    in->label = path;
    in->code = code;
    in->codeLen = codeLen;
    in->assignment = assignment;

    /* Registered and started under one hold of the lock, so `spawned` is
       never observed out of step with the thread that it describes -- a
       worker may be hiring on another thread at the same moment, and
       `spawned` is exactly the field that says whether a PlatformThread is
       safe to join. The Intern is fully filled in before the thread exists,
       so the worker never sees a half-built one; and the child cannot
       deadlock on the lock we are holding, because nothing it does before
       running the program touches the registry. */
    char errbuf[256];
    platform_mutex_lock(&g_lock);
    if (g_internCount == g_internCapacity) {
        g_internCapacity = g_internCapacity == 0 ? 8 : g_internCapacity * 2;
        g_interns = (Intern **)realloc(g_interns, (size_t)g_internCapacity * sizeof(Intern *));
    }
    g_interns[g_internCount++] = in;
    in->id = next_id_for(vm);
    bool started = platform_thread_start(&in->thread, intern_body, in, errbuf, sizeof errbuf);
    in->spawned = started;
    if (!started) {
        in->retired = true;
        intern_release(in);
    }
    int id = in->id;
    platform_mutex_unlock(&g_lock);

    if (!started) {
        vm_throw_native(vm, "ComputerExploded", "couldn't start a thread for that intern: %s", errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(id);
}

/* Joins, replays what the worker printed, and hands back what it delivered --
   or re-raises what killed it, with its own original flavor (§2.4: wrapping a
   worker's TypeVibeMismatch in a concurrency-specific flavor would tell you
   less than the flavor already did). */
static Value wait_for(VM *vm, int id, bool *ok) {
    *ok = false;
    platform_mutex_lock(&g_lock);
    Intern *in = intern_at(vm, id);
    platform_mutex_unlock(&g_lock);

    /* The Intern outlives the lock safely: entries are only ever removed by
       the owning VM's own teardown, which is this thread. */
    if (in == NULL) {
        vm_throw_native(vm, "OutOfPocket", "you've got no intern #%d.", id);
        return GHOST_VAL;
    }
    if (in->retired) {
        vm_throw_native(vm, "OutOfPocket", "you already waited on intern #%d.", id);
        return GHOST_VAL;
    }

    if (in->spawned && !in->joined) {
        platform_thread_join(&in->thread);
        in->joined = true;
    }

    if (in->outText != NULL) fwrite(in->outText, 1, in->outLen, vm->out);
    if (in->errText != NULL) fwrite(in->errText, 1, in->errLen, vm->err);

    if (in->flavor != NULL) {
        /* Copied out before the release, and passed through "%s" -- a
           worker's message is user text and may well contain a percent. */
        char *flavor = in->flavor;
        char *message = in->message;
        in->flavor = NULL;
        in->message = NULL;
        in->retired = true;
        intern_release(in);
        vm_throw_native(vm, flavor, "%s", message != NULL ? message : "it fell over.");
        free(flavor);
        free(message);
        return GHOST_VAL;
    }

    Value out = portable_to_value(vm, in->result);
    in->retired = true;
    intern_release(in);
    *ok = true;
    return out;
}

static Value m_wait_up(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_INT(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'wait_up' needs an intern, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    bool ok = false;
    return wait_for(vm, (int)AS_INT(a[0]), &ok);
}

/* Waits on all of them, in the order given, and hands back a stash of what
   each delivered. In order rather than in finishing order, because §5 rules
   out a golden whose output depends on which thread got there first -- and
   because a caller that wanted finishing order would have to be told which
   result was whose anyway. */
static Value m_everybody(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'everybody' needs a stash of interns, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjStash *handles = (ObjStash *)AS_OBJ(a[0]);
    int count = handles->count;
    int *ids = (int *)malloc((size_t)(count > 0 ? count : 1) * sizeof(int));
    for (int i = 0; i < count; i++) {
        if (!IS_INT(handles->items[i])) {
            free(ids);
            vm_throw_native(vm, "TypeVibeMismatch", "'everybody' needs a stash of interns; #%d is a %s.", i,
                            vm_type_name(handles->items[i]));
            return GHOST_VAL;
        }
        ids[i] = (int)AS_INT(handles->items[i]);
    }

    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    for (int i = 0; i < count; i++) {
        bool ok = false;
        Value v = wait_for(vm, ids[i], &ok);
        if (!ok) {
            gc_pop_temp(&vm->gc);
            free(ids);
            return GHOST_VAL; /* the error is already pending */
        }
        gc_push_temp(&vm->gc, v);
        stash_push(&vm->gc, out, v);
        gc_pop_temp(&vm->gc);
    }
    gc_pop_temp(&vm->gc);
    free(ids);
    return OBJ_VAL(out);
}

/* How many interns are worth hiring: the machine's logical CPU count, the
   same number `computer.cpus()` reports. Not a limit -- nothing stops you
   hiring more -- just the honest answer to "how much of this actually runs
   at the same time". */
static Value m_headcount(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    int n = platform_cpu_count();
    return INT_VAL(n > 0 ? n : 1);
}

static Intern *this_worker(VM *vm, const char *who) {
    Intern *in = (Intern *)vm->workerContext;
    if (in == NULL) {
        vm_throw_native(vm, "OutOfPocket", "'%s' only means something inside an intern's own script.", who);
    }
    return in;
}

static Value m_assignment(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    Intern *in = this_worker(vm, "assignment");
    if (in == NULL) return GHOST_VAL;
    return portable_to_value(vm, in->assignment);
}

static Value m_deliver(VM *vm, Value *a, int argc) {
    (void)argc;
    Intern *in = this_worker(vm, "deliver");
    if (in == NULL) return GHOST_VAL;

    char reason[512];
    reason[0] = '\0';
    PortableValue *pv = portable_from_value(a[0], reason, sizeof reason);
    if (pv == NULL) {
        vm_throw_native(vm, "TypeVibeMismatch", "%s", reason);
        return GHOST_VAL;
    }
    /* Last delivery wins, rather than being an error: a worker that computes
       a result twice is doing something odd, but nothing about it is
       ambiguous. */
    portable_free(in->result);
    in->result = pv;
    return a[0];
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} InternEntry;

static const InternEntry INTERN_FUNCTIONS[] = {
    {"hire", m_hire, 1, 2},
    {"wait_up", m_wait_up, 1, 1},
    {"everybody", m_everybody, 1, 1},
    {"headcount", m_headcount, 0, 0},
    {"assignment", m_assignment, 0, 0},
    {"deliver", m_deliver, 1, 1},
};
#define INTERN_FUNCTIONS_COUNT (int)(sizeof(INTERN_FUNCTIONS) / sizeof(INTERN_FUNCTIONS[0]))

Value interns_build(VM *vm) {
    registry_init();
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < INTERN_FUNCTIONS_COUNT; i++) {
        const InternEntry *e = &INTERN_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "interns", 7);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}

void interns_join_owned_by(VM *vm) {
    if (!g_ready) return;

    /* Only `vm`'s own thread reaches here, and a handle belongs to the VM it
       was issued to, so nothing else can be joining these. The lock is for
       the vector -- a worker on another thread may be hiring into it -- and
       is released across the join itself, because a join is unbounded and a
       worker that needed the lock to finish would never get it. */
    for (;;) {
        platform_mutex_lock(&g_lock);
        Intern *pending = NULL;
        for (int i = 0; i < g_internCount; i++) {
            Intern *in = g_interns[i];
            if (in != NULL && in->owner == vm && in->spawned && !in->joined) {
                pending = in;
                break;
            }
        }
        platform_mutex_unlock(&g_lock);
        if (pending == NULL) break;

        platform_thread_join(&pending->thread);
        pending->joined = true;
    }

    /* And then forget them. Handles are numbered per VM, so this VM's are of
       no use to anybody once it is gone -- and without the removal the
       registry would grow for the life of the process, which for
       `funny test` means every worker every golden ever hired. */
    platform_mutex_lock(&g_lock);
    int kept = 0;
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->owner == vm) {
            intern_release(in);
            free(in);
            continue;
        }
        g_interns[kept++] = in;
    }
    g_internCount = kept;
    platform_mutex_unlock(&g_lock);
}

void interns_shutdown(void) {
    if (!g_ready) return;
    /* Every VM joins its own interns as it is destroyed, and every VM in this
       process is destroyed -- the runner's, sus.run_bytecode's child, a REPL
       session's, a worker's own. So by the time the entry point gets here,
       transitively, no thread is left running and this is single-threaded
       again. The sweep below is the belt to that argument's braces: it costs
       one pass and it is the difference between a missed join being a hang
       and a missed join being a use-after-free. */
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->spawned && !in->joined) {
            platform_thread_join(&in->thread);
            in->joined = true;
        }
    }

    platform_mutex_lock(&g_lock);
    for (int i = 0; i < g_internCount; i++) {
        if (g_interns[i] == NULL) continue;
        intern_release(g_interns[i]);
        free(g_interns[i]);
    }
    free(g_interns);
    g_interns = NULL;
    g_internCount = 0;
    g_internCapacity = 0;

    for (int i = 0; i < g_cacheCount; i++) {
        free(g_cache[i].path);
        free(g_cache[i].code);
    }
    free(g_cache);
    g_cache = NULL;
    g_cacheCount = 0;
    g_cacheCapacity = 0;
    platform_mutex_unlock(&g_lock);

    platform_mutex_destroy(&g_lock);
    g_ready = false;
}
