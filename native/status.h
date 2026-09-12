/* native/status.h -- RUNTIME_PLAN.md R9: what every thread is waiting on.
 *
 * A hung program is the failure mode threads add, and it is the one a stack
 * trace cannot help with: nothing crashed, so there is nothing to print. What
 * you want to know is the same thing every time -- which threads exist, and
 * what each of them is blocked on -- and the only moment you can ask is after
 * it has already happened.
 *
 * So every thread keeps one line of plain C text, updated at each point where
 * it is about to wait for something:
 *
 *     #1 loop: 3 tasks; waiting on socket 7 (2.1s left)
 *     #2 joining intern #4 (worker.funny)
 *     #3 in native: filez.slurp
 *
 * The table is process-wide and mutex-guarded. A thread takes a row when its
 * VM is created and gives it back when the VM is destroyed, so the rows are
 * exactly the live VMs -- which, since one VM belongs to one thread, is
 * exactly the threads.
 *
 * Deliberately plain C: `char[160]` per row, no allocation, no `Value`. A
 * diagnostic that needs the collector, or the lock the program is stuck on,
 * is a diagnostic you cannot get when you need it.
 */
#ifndef FUNNY_STATUS_H
#define FUNNY_STATUS_H

#include <stdbool.h>
#include <stdio.h>

struct VM;

#define STATUS_TEXT_MAX 160

typedef struct {
    int id;                     /* small and stable, for reading a dump */
    char text[STATUS_TEXT_MAX]; /* what this thread is doing or waiting on */
    double updatedAt;           /* platform_monotonic_seconds at the last set */
    bool isWorker;              /* an `interns` worker rather than a main VM */
} StatusRow;

/* Takes a row for `vm`'s thread. Called from vm_init, so every VM has one.
   `label` is what the thread is for ("program", "intern", "compiler"). */
void status_register(struct VM *vm, const char *label);

/* Gives the row back. Called from vm_destroy. */
void status_leave(struct VM *vm);

/* Replaces this thread's line. Called at each wait point, so it is on the
   path of every loop turn that blocks: one snprintf into a fixed buffer, no
   allocation, and the lock is held only for the copy. */
void status_set(struct VM *vm, const char *fmt, ...);

/* The whole table, one line per thread, to `out`. Takes the lock. */
void status_dump(FILE *out);

/* The same, without taking the lock and without stdio: for a signal handler,
   where taking a lock can deadlock against the thread the signal interrupted.
   The output says it may be torn, because it may be. A torn diagnostic beats
   no diagnostic. */
void status_dump_raw(void);

/* Copies up to `max` rows out for `sus.threads()`. Returns how many. */
int status_snapshot(StatusRow *rows, int max);

/* How long since *any* thread last updated its line. The watchdog behind
   `--dump-on-stall N` compares this against N. Zero when nothing is
   registered. */
double status_quiet_seconds(void);

/* Frees the table's lock. Called once at exit, after every VM is gone. */
void status_shutdown(void);

#endif /* FUNNY_STATUS_H */
