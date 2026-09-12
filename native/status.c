/* native/status.c -- see status.h. */
#include "status.h"

#include <stdarg.h>
#include <string.h>

#include "platform.h"
#include "vm.h"

/* More threads than any sane program starts, and small enough that the whole
   table is one page. A program that somehow has more gets the first 256
   lines, which is 256 more than it had before. */
#define STATUS_MAX_ROWS 256

typedef struct {
    bool used;
    VM *owner;
    StatusRow row;
} Slot;

static PlatformMutex g_lock;
static bool g_ready = false;
static Slot g_slots[STATUS_MAX_ROWS];
static int g_nextId = 0;

/* Created on the first registration, which happens in vm_init on the main
   thread before any worker exists -- the same argument interns.c's registry
   makes for its own lock. */
static void ensure_lock(void) {
    if (g_ready) return;
    platform_mutex_init(&g_lock);
    g_ready = true;
}

void status_register(VM *vm, const char *label) {
    ensure_lock();
    platform_mutex_lock(&g_lock);
    int at = -1;
    for (int i = 0; i < STATUS_MAX_ROWS; i++) {
        if (!g_slots[i].used) {
            at = i;
            break;
        }
    }
    if (at >= 0) {
        g_slots[at].used = true;
        g_slots[at].owner = vm;
        g_slots[at].row.id = ++g_nextId;
        g_slots[at].row.isWorker = vm->workerContext != NULL;
        g_slots[at].row.updatedAt = platform_monotonic_seconds();
        snprintf(g_slots[at].row.text, STATUS_TEXT_MAX, "%s: starting", label != NULL ? label : "vm");
    }
    vm->statusSlot = at;
    platform_mutex_unlock(&g_lock);
}

void status_leave(VM *vm) {
    if (!g_ready || vm->statusSlot < 0) return;
    platform_mutex_lock(&g_lock);
    if (vm->statusSlot >= 0 && vm->statusSlot < STATUS_MAX_ROWS) {
        g_slots[vm->statusSlot].used = false;
        g_slots[vm->statusSlot].owner = NULL;
        g_slots[vm->statusSlot].row.text[0] = '\0';
    }
    vm->statusSlot = -1;
    platform_mutex_unlock(&g_lock);
}

void status_set(VM *vm, const char *fmt, ...) {
    if (!g_ready || vm == NULL || vm->statusSlot < 0 || vm->statusSlot >= STATUS_MAX_ROWS) return;
    /* Formatted outside the lock: vsnprintf is the expensive half, and the
       buffer it writes into belongs to this thread's own row. */
    char text[STATUS_TEXT_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    platform_mutex_lock(&g_lock);
    Slot *s = &g_slots[vm->statusSlot];
    if (s->used && s->owner == vm) {
        memcpy(s->row.text, text, sizeof text);
        s->row.updatedAt = platform_monotonic_seconds();
        /* A VM becomes a worker's before it runs anything, so this is right
           by the time anybody reads it. */
        s->row.isWorker = vm->workerContext != NULL;
    }
    platform_mutex_unlock(&g_lock);
}

void status_dump(FILE *out) {
    if (!g_ready) {
        fputs("\n-- no threads registered --\n", out);
        return;
    }
    double now = platform_monotonic_seconds();
    platform_mutex_lock(&g_lock);
    fputs("\n-- what every thread is waiting on --\n", out);
    for (int i = 0; i < STATUS_MAX_ROWS; i++) {
        if (!g_slots[i].used) continue;
        fprintf(out, "  #%d %s%s (%.1fs ago)\n", g_slots[i].row.id, g_slots[i].row.isWorker ? "[intern] " : "",
                g_slots[i].row.text, now - g_slots[i].row.updatedAt);
    }
    fputs("--\n", out);
    platform_mutex_unlock(&g_lock);
}

/* Signal-handler safe: no lock (the thread this interrupted may be holding
   it), no stdio, no allocation -- just the bytes, through the one write the
   platform layer guarantees is safe here. */
void status_dump_raw(void) {
    if (!g_ready) return;
    platform_write_stderr_raw("\n-- what every thread is waiting on (may be torn) --\n");
    for (int i = 0; i < STATUS_MAX_ROWS; i++) {
        if (!g_slots[i].used) continue;
        char line[STATUS_TEXT_MAX + 32];
        /* snprintf is not on POSIX's async-signal-safe list; this builds the
           line by hand instead, which is. */
        size_t at = 0;
        line[at++] = ' ';
        line[at++] = ' ';
        line[at++] = '#';
        int id = g_slots[i].row.id;
        char digits[12];
        int n = 0;
        if (id <= 0) digits[n++] = '0';
        while (id > 0 && n < (int)sizeof digits) {
            digits[n++] = (char)('0' + (id % 10));
            id /= 10;
        }
        while (n > 0) line[at++] = digits[--n];
        line[at++] = ' ';
        for (size_t k = 0; k < STATUS_TEXT_MAX && g_slots[i].row.text[k] != '\0'; k++) {
            if (at + 2 >= sizeof line) break;
            line[at++] = g_slots[i].row.text[k];
        }
        line[at++] = '\n';
        line[at] = '\0';
        platform_write_stderr_raw(line);
    }
    platform_write_stderr_raw("--\n");
}

int status_snapshot(StatusRow *rows, int max) {
    if (!g_ready) return 0;
    int count = 0;
    platform_mutex_lock(&g_lock);
    for (int i = 0; i < STATUS_MAX_ROWS && count < max; i++) {
        if (!g_slots[i].used) continue;
        rows[count++] = g_slots[i].row;
    }
    platform_mutex_unlock(&g_lock);
    return count;
}

double status_quiet_seconds(void) {
    if (!g_ready) return 0.0;
    double newest = 0.0;
    bool any = false;
    platform_mutex_lock(&g_lock);
    for (int i = 0; i < STATUS_MAX_ROWS; i++) {
        if (!g_slots[i].used) continue;
        if (!any || g_slots[i].row.updatedAt > newest) {
            newest = g_slots[i].row.updatedAt;
            any = true;
        }
    }
    platform_mutex_unlock(&g_lock);
    if (!any) return 0.0;
    return platform_monotonic_seconds() - newest;
}

void status_shutdown(void) {
    if (!g_ready) return;
    platform_mutex_destroy(&g_lock);
    g_ready = false;
}
