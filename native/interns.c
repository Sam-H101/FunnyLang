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
#include "loop.h"
#include "groupchat.h"
#include "modules.h"
#include "object.h"
#include "otw.h"
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
/* -- mailboxes -------------------------------------------------------------
 *
 * RUNTIME_PLAN.md R2. One per VM: a worker's belongs to its `Intern` rather
 * than to its VM, so a parent can post to it before the thread has started
 * and after the worker's VM is gone; a top-level VM makes its own on first
 * use. Plain malloc'd memory with its own mutex, holding PortableValues --
 * nothing in here belongs to any collector, which is what makes it safe for
 * one thread to fill and another to drain.
 *
 * Lock order, everywhere: g_lock first, then a mailbox's own lock. A sender
 * holds g_lock across the whole post, which is what keeps the target's
 * mailbox from being freed underneath it; a receiver drains its own inbox
 * with only the mailbox lock, so the two never meet head-on.
 */

/* A producer a million messages ahead of its consumer is a bug, and the
   alternative to saying so is the process quietly running out of memory. */
#define MAILBOX_CAP 1000000

typedef struct {
    PortableValue *value;
    /* The sender's id within the *receiving* VM, or 0 for "boss" -- so the
       "from" a receiver sees is something it can `dm` straight back. */
    int fromId;
} Dm;

typedef struct Mailbox {
    PlatformMutex lock;
    Dm *items;
    int count;
    int capacity;
    int head; /* ring start */
} Mailbox;

static Mailbox *mailbox_new(void) {
    Mailbox *m = (Mailbox *)calloc(1, sizeof(Mailbox));
    platform_mutex_init(&m->lock);
    return m;
}

static bool mailbox_push(Mailbox *m, PortableValue *pv, int fromId) {
    platform_mutex_lock(&m->lock);
    if (m->count >= MAILBOX_CAP) {
        platform_mutex_unlock(&m->lock);
        return false;
    }
    if (m->count == m->capacity) {
        int wanted = m->capacity == 0 ? 8 : m->capacity * 2;
        Dm *grown = (Dm *)malloc((size_t)wanted * sizeof(Dm));
        /* Copied out in order rather than memcpy'd: the ring wraps, and the
           new array starts at 0 again. */
        for (int i = 0; i < m->count; i++) grown[i] = m->items[(m->head + i) % m->capacity];
        free(m->items);
        m->items = grown;
        m->capacity = wanted;
        m->head = 0;
    }
    m->items[(m->head + m->count) % m->capacity].value = pv;
    m->items[(m->head + m->count) % m->capacity].fromId = fromId;
    m->count++;
    platform_mutex_unlock(&m->lock);
    return true;
}

static bool mailbox_pop(Mailbox *m, Dm *out) {
    platform_mutex_lock(&m->lock);
    if (m->count == 0) {
        platform_mutex_unlock(&m->lock);
        return false;
    }
    *out = m->items[m->head];
    m->head = (m->head + 1) % m->capacity;
    m->count--;
    platform_mutex_unlock(&m->lock);
    return true;
}

static int mailbox_count(Mailbox *m) {
    if (m == NULL) return 0;
    platform_mutex_lock(&m->lock);
    int n = m->count;
    platform_mutex_unlock(&m->lock);
    return n;
}

static void mailbox_free(Mailbox *m) {
    if (m == NULL) return;
    platform_mutex_lock(&m->lock);
    for (int i = 0; i < m->count; i++) portable_free(m->items[(m->head + i) % m->capacity].value);
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->capacity = 0;
    m->head = 0;
    platform_mutex_unlock(&m->lock);
    platform_mutex_destroy(&m->lock);
    free(m);
}

typedef struct Intern {
    /* Unique among this owner's interns, not among the process's -- see
       intern_at. */
    int id;
    struct VM *owner;

    PlatformThread thread;
    bool spawned;
    bool joined;
    bool retired; /* collected; the slot survives only to say so */

    /* Set by the worker as the very last thing it does, under g_lock, with a
       broadcast on g_wake. It is what lets the owning thread ask "is this one
       finished?" without committing to a join that might never return --
       which is the whole difference between A1's blocking `wait_up` and the
       event loop A5 builds on top of it. `joined` is not a substitute: it
       says the *waiter* has collected, not that the worker has finished. */
    bool done;

    char *label; /* the path it was hired from, for messages */

    /* This worker's inbox. On the Intern rather than on its VM because the
       parent may post before the thread starts and after the VM is gone. */
    Mailbox *mailbox;

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
/* Broadcast whenever a worker finishes. A5's loop sleeps on this rather than
   polling, and a timed wait on it is how A6 combines "wake when a worker is
   done" with "wake when the next timer is due" in one call. */
static PlatformCond g_wake;
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
    platform_cond_init(&g_wake);
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
    mailbox_free(in->mailbox);
    in->mailbox = NULL;
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
    /* Messages posted to this worker land on its Intern, so its VM points at
       that mailbox rather than owning one of its own. */
    child.inbox = in->mailbox;
    /* the_script() inside a worker is where it was hired from, made absolute
       so the worker can find its own neighbours whatever the working
       directory is. A path that will not resolve stays as it was given. */
    {
        char absPath[4096];
        char absErr[256];
        const char *src = platform_abs_path(in->label, absPath, sizeof absPath, absErr, sizeof absErr) ? absPath
                                                                                                        : in->label;
        child.scriptPath = dup_cstr(src);
    }

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

    /* Last, and only now: everything this Intern carries is written above,
       and `done` is the flag that says it is safe to read. The lock is not
       held anywhere across a join, so announcing here cannot deadlock with a
       thread waiting for this one. */
    platform_mutex_lock(&g_lock);
    in->done = true;
    platform_cond_broadcast(&g_wake);
    platform_mutex_unlock(&g_lock);
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
    opts.scriptPath = NULL;
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
    in->mailbox = mailbox_new();

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
    /* A2: what the caller gets is an `otw`, not the id. The id never leaves
       this file now, which retires A1's stolen-ticket problem outright -- an
       `otw` is not one of the things that can cross to a worker, so an intern
       cannot be handed one at all. */
    return OBJ_VAL(otw_for_intern(&vm->gc, id));
}

/* -- settling an otw ------------------------------------------------------
 *
 * The worker never touches the `otw`. It fills in its own Intern -- plain
 * malloc'd memory, no collector involved -- and says `done` under g_lock; the
 * owning VM's thread does everything below, on its own heap. otw.h explains
 * why that is not the cross-thread mutation §2.4 expected, and why it is a
 * better answer than the one that was expected.
 */

/* Is there a worker thread behind this `otw` at all? The loop asks before
   deciding that a waiting task is stuck forever. */
bool interns_has_worker(VM *vm, ObjOtw *p) {
    if (!g_ready || p->internId == 0) return false;
    platform_mutex_lock(&g_lock);
    bool found = intern_at(vm, p->internId) != NULL;
    platform_mutex_unlock(&g_lock);
    return found;
}

/* True if the worker behind `p` has finished, without waiting for it. False
   for an `otw` with no worker, which A2 never produces and A5's loop will. */
bool interns_ready(VM *vm, ObjOtw *p) {
    if (!g_ready || p->internId == 0) return false;
    platform_mutex_lock(&g_lock);
    Intern *in = intern_at(vm, p->internId);
    bool ready = in != NULL && in->done;
    platform_mutex_unlock(&g_lock);
    return ready;
}

/* Blocks until some worker owned by `vm` finishes, or `timeoutMs` elapses
   (negative means no timeout). True if one has finished -- including one that
   finished before this was called. This is what the event loop sleeps on
   instead of spinning, and a *timed* wait is how A6 combines "wake when a
   worker is done" with "wake when the next timer is due" in one call. */
bool interns_wait_any(VM *vm, int timeoutMs) {
    if (!g_ready) return false;
    platform_mutex_lock(&g_lock);
    bool found = false;
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->owner == vm && in->done && !in->retired) {
            found = true;
            break;
        }
    }
    if (!found) {
        /* One wait, not a loop: a spurious wake-up returns false and the
           caller goes round its own loop, which has other things to check
           anyway. Sleeping again here would mean re-deciding the timeout. */
        platform_cond_wait_ms(&g_wake, &g_lock, timeoutMs < 0 ? 60000 : timeoutMs);
        for (int i = 0; i < g_internCount; i++) {
            Intern *in = g_interns[i];
            if (in != NULL && in->owner == vm && in->done && !in->retired) {
                found = true;
                break;
            }
        }
    }
    platform_mutex_unlock(&g_lock);
    return found;
}

/* Joins the worker behind `p` and settles it -- fulfilled with what the
   worker delivered, or rejected with the error that killed it, carrying its
   own original flavor (§2.4: wrapping a worker's TypeVibeMismatch in a
   concurrency-specific flavor would tell you less than the flavor already
   did). Whatever the worker printed is replayed first.
 *
 * Idempotent: an already-settled `otw` is left exactly as it is. */
void interns_collect(VM *vm, ObjOtw *p) {
    if (p->state != OTW_PENDING) return;
    if (!g_ready || p->internId == 0) {
        gc_push_temp(&vm->gc, OBJ_VAL(p));
        ObjError *e = error_new(&vm->gc, "LeftOnRead", "nobody is working on that otw, so it is never going to settle.",
                                NULL, NULL, 0, 0, NULL, GHOST_VAL, NULL, 0);
        otw_reject(p, OBJ_VAL(e));
        gc_pop_temp(&vm->gc);
        return;
    }

    platform_mutex_lock(&g_lock);
    Intern *in = intern_at(vm, p->internId);
    platform_mutex_unlock(&g_lock);

    /* The Intern outlives the lock safely: entries are only ever removed by
       the owning VM's own teardown, which is this thread. */
    if (in == NULL || in->retired) {
        /* Unreachable while an `otw` is the only thing holding an id and it
           settles on the first collect. Left as a real answer anyway, since
           "the intern vanished" must never be silence. */
        gc_push_temp(&vm->gc, OBJ_VAL(p));
        ObjError *e = error_new(&vm->gc, "LeftOnRead", "that intern is already gone.", NULL, NULL, 0, 0, NULL,
                                GHOST_VAL, NULL, 0);
        otw_reject(p, OBJ_VAL(e));
        gc_pop_temp(&vm->gc);
        return;
    }

    if (in->spawned && !in->joined) {
        platform_thread_join(&in->thread);
        in->joined = true;
    }

    if (in->outText != NULL) fwrite(in->outText, 1, in->outLen, vm->out);
    if (in->errText != NULL) fwrite(in->errText, 1, in->errLen, vm->err);

    gc_push_temp(&vm->gc, OBJ_VAL(p)); /* everything below allocates */
    if (in->flavor != NULL) {
        ObjError *e = error_new(&vm->gc, in->flavor, in->message != NULL ? in->message : "it fell over.", NULL, NULL, 0,
                                0, NULL, GHOST_VAL, NULL, 0);
        otw_reject(p, OBJ_VAL(e));
    } else {
        otw_fulfill(p, portable_to_value(vm, in->result));
    }
    gc_pop_temp(&vm->gc);

    platform_mutex_lock(&g_lock);
    in->retired = true;
    intern_release(in);
    platform_mutex_unlock(&g_lock);
}

/* `wait_up(x)` -- block until `x` has an answer, and be that answer.
 *
 * A value that is not an `otw` is simply itself: there is nothing to wait
 * for, and answering "that's not an intern" would make every helper that
 * might or might not be asynchronous need to know which it was. A4's
 * `await_fr` takes the same position for the same reason. */
static Value settle_and_read(VM *vm, Value v) {
    if (!(IS_OBJ(v) && AS_OBJ(v)->type == OBJ_OTW)) return v;
    ObjOtw *p = (ObjOtw *)AS_OBJ(v);
    p->awaited = true;
    loop_settle_blocking(vm, p);
    if (p->state == OTW_REJECTED) {
        vm_rethrow(vm, p->error);
        return GHOST_VAL;
    }
    return p->value;
}

static Value m_wait_up(VM *vm, Value *a, int argc) {
    (void)argc;
    return settle_and_read(vm, a[0]);
}

/* Waits on all of them, in the order given, and hands back a stash of what
   each delivered. In order rather than in finishing order, because §5 rules
   out a golden whose output depends on which thread got there first -- and
   because a caller that wanted finishing order would have to be told which
   result was whose anyway. All of them are already *running*; the order here
   is only the order they are collected in. */
static Value m_everybody(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!(IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'everybody' needs a stash of interns, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }

    ObjStash *out = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    /* Re-read the source stash each time round: settling runs FunnyLang's
       collector, and `a[0]` is a rooted Value, but the ObjStash pointer read
       once before the loop would be no less valid -- it is re-read because
       the count is what matters and a worker's own `yap` may not change it,
       but nothing here should depend on that being true. */
    for (int i = 0; i < ((ObjStash *)AS_OBJ(a[0]))->count; i++) {
        Value v = settle_and_read(vm, ((ObjStash *)AS_OBJ(a[0]))->items[i]);
        if (vm->hadError) {
            gc_pop_temp(&vm->gc);
            return GHOST_VAL;
        }
        gc_push_temp(&vm->gc, v);
        stash_push(&vm->gc, out, v);
        gc_pop_temp(&vm->gc);
    }
    gc_pop_temp(&vm->gc);
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

/* -- DMs (RUNTIME_PLAN.md R2) ---------------------------------------------
 *
 * A star, not a mesh: a VM can post to an intern it hired, and a worker can
 * post to "boss". Worker-to-worker would mean one VM naming another VM's
 * thread, which is exactly the handle-forging problem per-VM numbering was
 * built to retire -- and a forwarding worker (which is three lines) is a
 * clearer way to say it anyway.
 */

/* This VM's inbox, made on first use. Callers hold g_lock: another thread
   may be posting to this same VM at this same moment. */
static Mailbox *inbox_locked(VM *vm) {
    if (vm->inbox == NULL) vm->inbox = mailbox_new();
    return (Mailbox *)vm->inbox;
}

static Value m_dm(VM *vm, Value *a, int argc) {
    (void)argc;

    bool toBoss = IS_STRING(a[0]) && strcmp(AS_STRING(a[0])->chars, "boss") == 0;
    int id = 0;
    bool finished = false;
    if (!toBoss) {
        if (IS_OBJ(a[0]) && AS_OBJ(a[0])->type == OBJ_OTW) {
            ObjOtw *p = (ObjOtw *)AS_OBJ(a[0]);
            id = p->internId;
            /* A settled handle has already given its id up -- the intern
               behind it is finished, which is a different thing from the
               handle being wrong. */
            if (id == 0 && p->state != OTW_PENDING) finished = true;
        } else if (IS_INT(a[0])) {
            /* The `from` of a message that arrived here. Safe to accept as an
               address precisely because ids are numbered per VM: a stolen one
               can only ever name one of the thief's own interns. */
            id = (int)AS_INT(a[0]);
        }
        if (finished) {
            vm_throw_native(vm, "LeftOnRead", "that intern has finished -- it is never going to read that.");
            return GHOST_VAL;
        }
        if (id <= 0) {
            vm_throw_native(vm, "OutOfPocket", "'dm' needs one of your own interns or \"boss\", not a %s.",
                            vm_type_name(a[0]));
            return GHOST_VAL;
        }
    }

    char reason[512];
    reason[0] = '\0';
    PortableValue *pv = portable_from_value(a[1], reason, sizeof reason);
    if (pv == NULL) {
        vm_throw_native(vm, "TypeVibeMismatch", "%s", reason);
        return GHOST_VAL;
    }

    /* Everything below happens under g_lock, including the push itself: it is
       what stops the target's mailbox being freed by a collect on another
       thread between finding it and writing to it. */
    const char *flavor = NULL;
    const char *problem = NULL;
    platform_mutex_lock(&g_lock);
    if (toBoss) {
        Intern *me = (Intern *)vm->workerContext;
        if (me == NULL) {
            flavor = "OutOfPocket";
            problem = "\"boss\" only means something inside an intern's own script.";
        } else if (!mailbox_push(inbox_locked(me->owner), pv, me->id)) {
            flavor = "OutOfPocket";
            problem = "that inbox is a million messages deep. whoever is reading it has given up.";
        }
    } else {
        Intern *in = intern_at(vm, id);
        if (in == NULL) {
            flavor = "OutOfPocket";
            problem = "that isn't one of your interns.";
        } else if (in->done || in->retired) {
            flavor = "LeftOnRead";
            problem = "that intern has finished -- it is never going to read that.";
        } else if (!mailbox_push(in->mailbox, pv, 0)) {
            flavor = "OutOfPocket";
            problem = "that inbox is a million messages deep. whoever is reading it has given up.";
        }
    }
    if (flavor == NULL) platform_cond_broadcast(&g_wake);
    platform_mutex_unlock(&g_lock);

    if (flavor != NULL) {
        portable_free(pv);
        vm_throw_native(vm, flavor, "%s", problem);
        return GHOST_VAL;
    }
    return GHOST_VAL;
}

bool interns_settle_mailbox(VM *vm, ObjOtw *p) {
    Dm msg;
    if (vm->inbox == NULL || !mailbox_pop((Mailbox *)vm->inbox, &msg)) return false;

    gc_push_temp(&vm->gc, OBJ_VAL(p));
    ObjGroupChat *g = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(g));
    Value from = msg.fromId == 0 ? OBJ_VAL(string_new(&vm->gc, "boss", 4)) : INT_VAL(msg.fromId);
    gc_push_temp(&vm->gc, from);
    groupchat_set(&vm->gc, g, OBJ_VAL(string_new(&vm->gc, "from", 4)), from);
    gc_pop_temp(&vm->gc);
    Value body = portable_to_value(vm, msg.value);
    gc_push_temp(&vm->gc, body);
    groupchat_set(&vm->gc, g, OBJ_VAL(string_new(&vm->gc, "msg", 3)), body);
    gc_pop_temp(&vm->gc);
    otw_fulfill(p, OBJ_VAL(g));
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);

    portable_free(msg.value);
    return true;
}

static Value m_check_dms(VM *vm, Value *a, int argc) {
    double deadline = 0.0;
    if (argc > 0 && !IS_GHOST(a[0])) {
        double ms;
        if (IS_INT(a[0])) ms = (double)AS_INT(a[0]);
        else if (IS_FLOAT(a[0])) ms = AS_FLOAT(a[0]);
        else {
            vm_throw_native(vm, "TypeVibeMismatch", "'check_dms' needs a numba of milliseconds, not a %s.",
                            vm_type_name(a[0]));
            return GHOST_VAL;
        }
        if (ms < 0.0) ms = 0.0;
        deadline = platform_monotonic_seconds() + ms / 1000.0;
    }
    ObjOtw *p = otw_for_mailbox(&vm->gc, deadline);
    /* Already something waiting? Then it is already settled, and nothing
       needs to go round the loop for it. */
    interns_settle_mailbox(vm, p);
    return OBJ_VAL(p);
}

static Value m_dms_waiting(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    return INT_VAL(mailbox_count((Mailbox *)vm->inbox));
}

bool interns_mail_possible(VM *vm) {
    /* A worker's parent is alive for as long as the worker is -- a VM joins
       its interns before it dies -- so a message can always still come. */
    if (vm->workerContext != NULL) return true;
    if (!g_ready) return false;
    platform_mutex_lock(&g_lock);
    bool any = false;
    for (int i = 0; i < g_internCount; i++) {
        Intern *in = g_interns[i];
        if (in != NULL && in->owner == vm && !in->retired && !in->done) {
            any = true;
            break;
        }
    }
    platform_mutex_unlock(&g_lock);
    return any;
}

void interns_wait_for_mail(VM *vm, int timeoutMs) {
    if (!g_ready) return;
    /* The count is checked under g_lock, and a sender posts under g_lock and
       broadcasts before releasing it -- so a message that arrives between the
       check and the wait cannot be missed. */
    platform_mutex_lock(&g_lock);
    if (mailbox_count((Mailbox *)vm->inbox) == 0) {
        platform_cond_wait_ms(&g_wake, &g_lock, timeoutMs < 0 ? 60000 : timeoutMs);
    }
    platform_mutex_unlock(&g_lock);
}

void interns_vm_teardown(VM *vm) {
    if (vm->inbox == NULL) return;
    /* A worker's inbox belongs to its Intern, which outlives its VM. */
    if (vm->workerContext == NULL) mailbox_free((Mailbox *)vm->inbox);
    vm->inbox = NULL;
}

static const InternEntry INTERN_FUNCTIONS[] = {
    {"hire", m_hire, 1, 2},
    {"wait_up", m_wait_up, 1, 1},
    {"everybody", m_everybody, 1, 1},
    {"headcount", m_headcount, 0, 0},
    {"assignment", m_assignment, 0, 0},
    {"deliver", m_deliver, 1, 1},
    {"dm", m_dm, 2, 2},
    {"check_dms", m_check_dms, 0, 1},
    {"dms_waiting", m_dms_waiting, 0, 0},
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

    platform_cond_destroy(&g_wake);
    platform_mutex_destroy(&g_lock);
    g_ready = false;
}
