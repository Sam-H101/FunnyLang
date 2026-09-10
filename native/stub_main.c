/* native/stub_main.c -- PLAN.md §5.4's runtime stub, and NATIVE_PLAN.md
 * N7 task 2. Built as its own binary (`funnyrt`); `funny yeet` copies it,
 * appends a program, and the result is a standalone executable.
 *
 * The trailer format is unchanged from the PyInstaller-based stub this
 * replaces -- only the language is different:
 *
 *     [ stub binary ][ payload ][ "FUNNYYEET" (9) ][ payload length (u64 BE, 8) ]
 *
 * so the last 17 bytes are the trailer, magic first. (PLAN.md §5.4's
 * diagram shows the length before the magic; the format the packager
 * actually writes, and that funnylang/stub_main.py reads, is the order
 * above. Following the code, not the diagram.)
 *
 * This binary deliberately contains no compiler: a shipped executable
 * only ever runs already-compiled bytecode. That is also why the yeeted
 * result is a couple of hundred kilobytes rather than PyInstaller's eight
 * megabytes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interns.h"
#include "platform.h"
#include "runner.h"

#define TRAILER_MAGIC "FUNNYYEET"
#define TRAILER_MAGIC_LEN 9
#define TRAILER_LEN 17 /* magic + u64 big-endian payload length */

/* Reads the payload appended to this very executable. NULL when there
   isn't one, which is the "naked stub" case -- someone ran `funnyrt`
   itself instead of something yeeted from it. */
static unsigned char *read_own_payload(size_t *outLen) {
    char self[4096];
    if (!platform_executable_path(self, sizeof(self))) return NULL;

    FILE *f = fopen(self, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long total = ftell(f);
    if (total < TRAILER_LEN) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, total - TRAILER_LEN, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char trailer[TRAILER_LEN];
    if (fread(trailer, 1, TRAILER_LEN, f) != TRAILER_LEN) {
        fclose(f);
        return NULL;
    }
    if (memcmp(trailer, TRAILER_MAGIC, TRAILER_MAGIC_LEN) != 0) {
        fclose(f);
        return NULL;
    }
    uint64_t payloadLen = 0;
    for (int i = 0; i < 8; i++) payloadLen = (payloadLen << 8) | trailer[TRAILER_MAGIC_LEN + i];
    if (payloadLen == 0 || (uint64_t)total < (uint64_t)TRAILER_LEN + payloadLen) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, total - TRAILER_LEN - (long)payloadLen, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *payload = (unsigned char *)malloc((size_t)payloadLen);
    if (!payload || fread(payload, 1, (size_t)payloadLen, f) != payloadLen) {
        free(payload);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *outLen = (size_t)payloadLen;
    return payload;
}

int main(int argc, char **argv) {
    platform_console_init();

    RunnerOptions opts;
    opts.diag = diag_default_options();
    opts.errorLabel = NULL;
    opts.out = NULL; /* stdout/stderr: a yeeted program is the top level */
    opts.err = NULL;

    /* The diagnostic flags still apply to a shipped program's own errors;
       everything else on the command line belongs to the program. */
    int argWrite = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serious") == 0) {
            opts.diag.serious = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--no-colour") == 0) {
            opts.diag.color = false;
        } else {
            argv[argWrite++] = argv[i];
        }
    }
    argc = argWrite;

    size_t len = 0;
    unsigned char *payload = read_own_payload(&len);
    if (!payload) {
        fprintf(stderr, "this stub is naked. it has no program. sad.\n");
        return 2;
    }

    int status = funny_run_bytecode(payload, len, argv + 1, argc - 1, opts, NULL);
    free(payload);
    /* Interns are joined as each VM is destroyed; this is the last sweep --
       the compiled-worker cache, and the registry itself. Nothing is killed:
       ASYNC_PLAN.md §3.4 -- a thread stopped mid-allocation leaves a heap
       nothing can safely free, so `funny` waits for its workers. */
    interns_shutdown();
    return status;
}
