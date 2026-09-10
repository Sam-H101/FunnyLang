/* native/main.c -- NATIVE_PLAN.md N7: the native CLI's entry point.
 *
 * Deliberately thin, and meant to get thinner. N8 moves argument parsing
 * and subcommand dispatch into `selfhost/cli.funny`, at which point this
 * file's job shrinks to "load the toolchain bundle, hand it argv" -- so
 * what lives here is only what has to be in C: the global flags that
 * configure the C-side diagnostic renderer, deciding which loader a file
 * needs, and driving the self-hosted compiler for a `.funny` source file.
 *
 * `funny <file> [args...]` still works exactly as it did before the
 * subcommand existed, since every native test invokes it that way.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "chunk.h"
#include "diag.h"
#include "error.h"
#include "gc.h"
#include "platform.h"
#include "rizz.h"
#include "stash.h"
#include "string.h"
#include "value.h"
#include "vm.h"

/* Matches funnylang/__init__.py -- the language and its bytecode format are
   the same ones, whichever runtime is executing them. */
#define FUNNY_VERSION "1.1.0"
#define FUNNY_BYTECODE_VERSION 2

static const char *VIBES_QUIPS[] = {
    "cooking...", "no cap, almost there...", "vibing through the bytecode...",
    "asking the compiler nicely...", "channeling big brain energy...",
    "it's giving compiler...", "manifesting correct syntax...",
    "lowkey grinding...", "bet.", "sending it...", "one sec, fr fr...",
    "doing the most (the necessary amount)...", "skill issue prevention in progress...",
    "yeeting bytes around...", "trust the process...",
};
#define VIBES_QUIP_COUNT (int)(sizeof(VIBES_QUIPS) / sizeof(VIBES_QUIPS[0]))

typedef struct {
    DiagOptions diag;
    bool showTime;
    bool vibes;
} CliOptions;

static void quip(const CliOptions *opts, const char *phase) {
    if (!opts->vibes) return;
    printf("[%s] %s\n", phase, VIBES_QUIPS[rizz_next_u64() % (uint64_t)VIBES_QUIP_COUNT]);
}

static uint8_t *read_file(const char *path, size_t *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)size + 1);
    size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) {
        free(data);
        return NULL;
    }
    *outLen = (size_t)size;
    return data;
}

static bool has_suffix(const char *s, const char *suffix) {
    size_t sl = strlen(s), fl = strlen(suffix);
    return sl >= fl && strcmp(s + sl - fl, suffix) == 0;
}

/* Loads and runs already-compiled bytes -- a .funnyc or a .funnypak, told
   apart by their own magic rather than by the file's name, so a bundle
   under any extension (or with no name at all, as in the yeet stub) still
   runs. Returns the process exit code. */
static int run_bytecode(uint8_t *data, size_t len, char **programArgs, int programArgc, const CliOptions *opts,
                         const char *errorLabel, double *runMsOut) {
    VM vm;
    vm_init(&vm);
    builtins_install(&vm);

    ObjStash *args = stash_new(&vm.gc, NULL, 0);
    for (int i = 0; i < programArgc; i++) {
        stash_push(&vm.gc, args, OBJ_VAL(string_new(&vm.gc, programArgs[i], (uint32_t)strlen(programArgs[i]))));
    }
    vm.programArgs = OBJ_VAL(args);

    char *err = NULL;
    CompiledUnit *unit = NULL;
    CompiledPak *pak = NULL;
    if (chunk_is_funnypak(data, len)) {
        pak = chunk_load_funnypak(data, len, &vm.gc, &err);
    } else {
        unit = chunk_load_funnyc(data, len, &vm.gc, &err);
    }
    if (!unit && !pak) {
        fprintf(stderr, "%s\n", err);
        free(err);
        vm_destroy(&vm);
        return 1;
    }

    double startRun = platform_monotonic_seconds();
    VmResult result = pak ? vm_run_pak(&vm, pak, stdout) : vm_run(&vm, unit, stdout);
    if (runMsOut) *runMsOut = (platform_monotonic_seconds() - startRun) * 1000.0;

    int exitCode = 0;
    if (result == VM_ERROR) {
        int64_t systemExitCode;
        if (vm_is_system_exit(vm.uncaughtError, &systemExitCode)) {
            /* dip(n): a clean process exit, not a crash -- no message,
               matching an uncaught Python SystemExit(n). */
            exitCode = (int)systemExitCode;
        } else {
            ObjError *e = (ObjError *)AS_OBJ(vm.uncaughtError);
            if (errorLabel != NULL) {
                /* An error raised *by the compiler while compiling* is not
                   the user's program failing, and rendering it as one buries
                   the actual problem under a stack trace through
                   funnyc.funny. The self-hosted compiler encodes the real
                   flavour and position into its message (it can only
                   `chuck` a plain value), so the message is the useful part.
                   Full §4.2 diagnostics for source errors need the compiler
                   itself to report them properly -- N8's job, not a thing
                   to fake from out here. */
                fprintf(stderr, "%s\n  %s\n", errorLabel, e->message->chars);
                vm_destroy(&vm);
                if (pak) chunk_free_pak(pak);
                else chunk_free_unit(unit);
                return 1;
            }
            diag_render_error(stderr, e, opts->diag);
            /* computer.explode()'s ComputerExploded is an ordinary
               catchable FunnyError everywhere else (sketchy/my_bad catches
               it like any other) -- exit 69 is purely a top-level
               uncaught-error mapping, matching funnylang/cli.py's own
               dedicated `except ComputerExploded` clause. */
            exitCode = strcmp(e->flavor->chars, "ComputerExploded") == 0 ? 69 : 1;
        }
    }
    if (pak) chunk_free_pak(pak);
    else chunk_free_unit(unit);
    vm_destroy(&vm);
    return exitCode;
}

/* Where the self-hosted compiler lives. Checked in the order that lets a
   built-from-source tree, an installed copy and a test harness each find
   it without the others having to care:
     1. FUNNY_TOOLCHAIN, for anyone who needs to be explicit;
     2. beside the binary, which is where an installed `funny` keeps it;
     3. the working directory, which is where a freshly built one is.
   N10 replaces all three by linking the bundle into the binary itself
   (§4's toolchain_blob.c), at which point this function goes away. */
static bool find_toolchain(char *out, size_t outLen) {
    const char *override = getenv("FUNNY_TOOLCHAIN");
    if (override && override[0] != '\0') {
        snprintf(out, outLen, "%s", override);
        return platform_path_exists(out);
    }
    char exe[4096];
    if (platform_executable_path(exe, sizeof(exe))) {
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            /* Built by hand rather than with snprintf: the directory and the
               destination are the same size, so -O2's format-truncation
               analysis can't prove the join fits and rejects it. */
            static const char suffix[] = "/bootstrap/funnyc.funnypak";
            size_t dirLen = strlen(exe);
            if (dirLen + sizeof(suffix) <= outLen) {
                memcpy(out, exe, dirLen);
                memcpy(out + dirLen, suffix, sizeof(suffix));
                if (platform_path_exists(out)) return true;
            }
        }
    }
    snprintf(out, outLen, "bootstrap/funnyc.funnypak");
    return platform_path_exists(out);
}

/* `funny run foo.funny`: compile with the self-hosted compiler running on
   this same VM, then run what it produced. The compiler is FunnyLang, so
   this is the C runtime driving FunnyLang to compile FunnyLang -- no
   Python anywhere in the path. */
static int run_source(const char *path, char **programArgs, int programArgc, const CliOptions *opts) {
    char toolchain[4096];
    if (!find_toolchain(toolchain, sizeof(toolchain))) {
        fprintf(stderr,
                 "can't find the compiler bundle (bootstrap/funnyc.funnypak).\n"
                 "set FUNNY_TOOLCHAIN, or run from a tree that has one.\n");
        return 1;
    }
    size_t toolchainLen;
    uint8_t *toolchainBytes = read_file(toolchain, &toolchainLen);
    if (!toolchainBytes) {
        fprintf(stderr, "couldn't read '%s'.\n", toolchain);
        return 1;
    }

    char out[4096];
    if (!platform_temp_file("funny", out, sizeof(out))) {
        fprintf(stderr, "couldn't make a temp file to compile into.\n");
        free(toolchainBytes);
        return 1;
    }

    quip(opts, "compiling");
    double startCompile = platform_monotonic_seconds();
    /* The compiler reads its two arguments (in-path, out-path) with
       the_args(), the same way it does when invoked by hand. */
    char *compilerArgs[2];
    compilerArgs[0] = (char *)path;
    compilerArgs[1] = out;
    char label[4200];
    snprintf(label, sizeof(label), "couldn't compile %s:", path);
    int compileStatus = run_bytecode(toolchainBytes, toolchainLen, compilerArgs, 2, opts, label, NULL);
    double compileMs = (platform_monotonic_seconds() - startCompile) * 1000.0;
    free(toolchainBytes);
    if (compileStatus != 0) {
        remove(out);
        return compileStatus;
    }
    quip(opts, "running");
    size_t programLen;
    uint8_t *programBytes = read_file(out, &programLen);
    remove(out);
    if (!programBytes) {
        fprintf(stderr, "the compiler didn't leave anything to run.\n");
        return 1;
    }
    double runMs = 0.0;
    int status = run_bytecode(programBytes, programLen, programArgs, programArgc, opts, NULL, &runMs);
    free(programBytes);
    /* One line, both halves, matching funnylang/cli.py's own --time output. */
    if (opts->showTime && status == 0) {
        printf("compiled in %.0fms, ran in %.0fms. blazingly fast (probably)\n", compileMs, runMs);
    }
    return status;
}

int main(int argc, char **argv) {
    platform_console_init(); /* UTF-8 + ANSI on Windows; a no-op elsewhere */

    CliOptions opts;
    opts.diag = diag_default_options();
    opts.showTime = false;
    opts.vibes = false;
    bool wantVersion = false;

    /* Global flags are stripped in place, so they work before or after the
       subcommand (N7 task 5) and never reach the program's own the_args(). */
    int argWrite = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serious") == 0) {
            opts.diag.serious = true;
        } else if (strcmp(argv[i], "--no-color") == 0 || strcmp(argv[i], "--no-colour") == 0) {
            opts.diag.color = false;
        } else if (strcmp(argv[i], "--time") == 0) {
            opts.showTime = true;
        } else if (strcmp(argv[i], "--vibes") == 0) {
            opts.vibes = true;
        } else if (strcmp(argv[i], "--version") == 0) {
            wantVersion = true;
        } else {
            argv[argWrite++] = argv[i];
        }
    }
    argc = argWrite;

    if (wantVersion) {
        printf("funny %s (bytecode v%d)\n", FUNNY_VERSION, FUNNY_BYTECODE_VERSION);
        return 0;
    }

    /* `run` is optional: `funny x.funnyc` and `funny run x.funnyc` are the
       same thing. The other subcommands (build/xray/fmt/test/vibe/yeet)
       are N8's, written in FunnyLang. */
    int fileIndex = 1;
    if (argc > 1 && strcmp(argv[1], "run") == 0) fileIndex = 2;

    if (fileIndex >= argc) {
        puts("FunnyLang (native) -- it compiles. somehow.");
        puts("usage: funny [--serious] [--no-color] [--time] [--vibes] [run] <file> [args...]");
        puts("  <file> may be .funny (compiled on the spot), .funnyc, or .funnypak.");
        return 0;
    }

    const char *path = argv[fileIndex];
    char **programArgs = argv + fileIndex + 1;
    int programArgc = argc - fileIndex - 1;

    /* Source is the one case decided by extension rather than magic:
       there is no magic number for "FunnyLang source", and guessing from
       the contents would be worse than reading the name. */
    if (has_suffix(path, ".funny")) return run_source(path, programArgs, programArgc, &opts);

    size_t len;
    uint8_t *data = read_file(path, &len);
    if (!data) {
        fprintf(stderr, "couldn't read '%s'.\n", path);
        return 1;
    }
    double runMs = 0.0;
    int status = run_bytecode(data, len, programArgs, programArgc, &opts, NULL, &runMs);
    free(data);
    if (opts.showTime && status == 0) printf("ran in %.0fms. blazingly fast (probably)\n", runMs);
    return status;
}
