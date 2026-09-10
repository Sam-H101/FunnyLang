/* native/main.c -- NATIVE_PLAN.md N8 task 6: the loader.
 *
 * Everything a user types is parsed and dispatched by selfhost/cli.funny,
 * which is FunnyLang. This file does the three things that cannot be done
 * from inside the language before the language is running:
 *
 *   1. set up the console (UTF-8 + ANSI on Windows),
 *   2. read the two diagnostic flags, because they configure the *runtime's*
 *      own renderer and have to apply before any program runs,
 *   3. find the CLI bundle and hand it argv.
 *
 * `--version` is answered here as well, deliberately: it is the one thing
 * that must work even when the CLI bundle is missing, which is exactly the
 * situation someone reporting a broken install is in. N10 embeds the bundle
 * in the binary and step 3 becomes a pointer into a byte array.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "platform.h"
#include "runner.h"

/* Kept in step with selfhost/cli.funny's own VERSION/BYTECODE_VERSION by
   `tests/native/test_native_cli.py`, which reads both and compares them --
   two copies of a version string is exactly the kind of thing that drifts
   silently. */
#define FUNNY_VERSION "2.0.0"
#define FUNNY_BYTECODE_VERSION 2

static uint8_t *read_file(const char *path, size_t *outLen) {
    unsigned char *data;
    size_t len;
    char errbuf[256];
    if (!platform_read_file(path, &data, &len, errbuf, sizeof(errbuf))) return NULL;
    *outLen = len;
    return (uint8_t *)data;
}

/* The CLI bundle, looked for in the same three places its FunnyLang half
   looks for the compiler bundle: an explicit override, next to the running
   binary, then the working directory. */
static bool find_cli(char *out, size_t outLen) {
    const char *override = getenv("FUNNY_CLI");
    if (override && override[0] != '\0') {
        snprintf(out, outLen, "%s", override);
        return platform_path_exists(out);
    }
    char exe[4096];
    if (platform_executable_path(exe, sizeof(exe))) {
        char *slash = strrchr(exe, '/');
        char *backslash = strrchr(exe, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
        if (slash) {
            *slash = '\0';
            /* Built by hand rather than with snprintf: the directory and the
               destination are the same size, so -O2's format-truncation
               analysis can't prove the join fits and rejects it. */
            static const char suffix[] = "/bootstrap/cli.funnypak";
            size_t dirLen = strlen(exe);
            if (dirLen + sizeof(suffix) <= outLen) {
                memcpy(out, exe, dirLen);
                memcpy(out + dirLen, suffix, sizeof(suffix));
                if (platform_path_exists(out)) return true;
            }
        }
    }
    snprintf(out, outLen, "bootstrap/cli.funnypak");
    return platform_path_exists(out);
}

int main(int argc, char **argv) {
    platform_console_init(); /* UTF-8 + ANSI on Windows; a no-op elsewhere */

    /* The two flags that configure the runtime's own diagnostic renderer are
       read here and installed process-wide, so every nested run -- the
       compiler, `sus.run_program`, a REPL input -- renders the same way.
       They are *not* removed from argv: cli.funny strips them again on its
       side, and leaving them lets `funny --serious run x.funny` look the
       same to both halves. */
    DiagOptions diag = diag_default_options();
    bool wantVersion = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serious") == 0) {
            diag.serious = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--no-colour") == 0) {
            diag.color = false;
        } else if (strcmp(argv[i], "--version") == 0) {
            wantVersion = true;
        }
    }
    diag_set_default_options(diag);

    if (wantVersion) {
        printf("funny %s (bytecode v%d)\n", FUNNY_VERSION, FUNNY_BYTECODE_VERSION);
        return 0;
    }

    char cliPath[4096];
    if (!find_cli(cliPath, sizeof(cliPath))) {
        fprintf(stderr,
                "can't find the CLI bundle (bootstrap/cli.funnypak).\n"
                "set FUNNY_CLI, or run from a tree that has one.\n");
        return 1;
    }
    size_t cliLen;
    uint8_t *cliBytes = read_file(cliPath, &cliLen);
    if (!cliBytes) {
        fprintf(stderr, "couldn't read '%s'.\n", cliPath);
        return 1;
    }

    RunnerOptions opts = {diag, NULL};
    int status = funny_run_bytecode(cliBytes, cliLen, argv + 1, argc - 1, opts, NULL);
    free(cliBytes);
    return status;
}
