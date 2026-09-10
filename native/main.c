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
#include "runner.h"
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

/* Compiles `path` into `outPath` by running the self-hosted compiler on
   this same VM. Returns 0 on success. The compiler is itself FunnyLang, so
   this is the C runtime driving FunnyLang to compile FunnyLang -- no
   Python anywhere in the path. */
static int compile_source(const char *path, const char *outPath, const CliOptions *opts, double *compileMsOut) {
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

    quip(opts, "compiling");
    double start = platform_monotonic_seconds();
    /* The compiler reads its two arguments (in-path, out-path) with
       the_args(), the same way it does when invoked by hand. */
    char *compilerArgs[2];
    compilerArgs[0] = (char *)path;
    compilerArgs[1] = (char *)outPath;
    char label[4200];
    snprintf(label, sizeof(label), "couldn't compile %s:", path);
    RunnerOptions compileOpts = {opts->diag, label};
    int status = funny_run_bytecode(toolchainBytes, toolchainLen, compilerArgs, 2, compileOpts, NULL);
    if (compileMsOut) *compileMsOut = (platform_monotonic_seconds() - start) * 1000.0;
    free(toolchainBytes);
    return status;
}

/* `funny run foo.funny`: compile to a temporary, then run it. */
static int run_source(const char *path, char **programArgs, int programArgc, const CliOptions *opts) {
    char out[4096];
    if (!platform_temp_file("funny", out, sizeof(out))) {
        fprintf(stderr, "couldn't make a temp file to compile into.\n");
        return 1;
    }
    double compileMs = 0.0;
    int compileStatus = compile_source(path, out, opts, &compileMs);
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
    RunnerOptions runOpts = {opts->diag, NULL};
    int status = funny_run_bytecode(programBytes, programLen, programArgs, programArgc, runOpts, &runMs);
    free(programBytes);
    /* One line, both halves, matching funnylang/cli.py's own --time output. */
    if (opts->showTime && status == 0) {
        printf("compiled in %.0fms, ran in %.0fms. blazingly fast (probably)\n", compileMs, runMs);
    }
    return status;
}

/* The runtime stub `funny yeet` appends a program to, found the same way
   the compiler bundle is (beside the binary, then the working directory). */
static bool find_stub(char *out, size_t outLen) {
    const char *stubName =
#ifdef _WIN32
        "funnyrt.exe";
#else
        "funnyrt";
#endif
    const char *override = getenv("FUNNY_STUB");
    if (override && override[0] != '\0') {
        snprintf(out, outLen, "%s", override);
        return platform_path_exists(out);
    }
    char exe[4096];
    if (platform_executable_path(exe, sizeof(exe))) {
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            size_t dirLen = strlen(exe);
            size_t nameLen = strlen(stubName);
            if (dirLen + nameLen + 2 <= outLen) {
                memcpy(out, exe, dirLen);
                out[dirLen] = '/';
                memcpy(out + dirLen + 1, stubName, nameLen + 1);
                if (platform_path_exists(out)) return true;
            }
        }
    }
    snprintf(out, outLen, "%s", stubName);
    return platform_path_exists(out);
}

/* `funny yeet foo.funny -o out`: PLAN.md §5.4. Copy the stub, append the
   compiled program and a 17-byte trailer, mark it executable. No
   PyInstaller, no per-program freeze, no 8 MB. */
static int yeet(const char *path, const char *outPath, const CliOptions *opts) {
    char stub[4096];
    if (!find_stub(stub, sizeof(stub))) {
        fprintf(stderr, "can't find the runtime stub (funnyrt). set FUNNY_STUB, or build it.\n");
        return 1;
    }

    char compiled[4096];
    if (!platform_temp_file("funnyyeet", compiled, sizeof(compiled))) {
        fprintf(stderr, "couldn't make a temp file to compile into.\n");
        return 1;
    }
    int compileStatus = compile_source(path, compiled, opts, NULL);
    if (compileStatus != 0) {
        remove(compiled);
        return compileStatus;
    }

    size_t payloadLen = 0, stubLen = 0;
    uint8_t *payload = read_file(compiled, &payloadLen);
    remove(compiled);
    uint8_t *stubBytes = read_file(stub, &stubLen);
    if (!payload || !stubBytes) {
        fprintf(stderr, "couldn't read the stub or the compiled program.\n");
        free(payload);
        free(stubBytes);
        return 1;
    }

    quip(opts, "yeeting");
    /* The M13 packager fix, kept: create -o's parent directory rather than
       failing on a path whose directory doesn't exist yet. */
    char parent[4096];
    snprintf(parent, sizeof(parent), "%s", outPath);
    /* Either separator: a Windows user types -o out\prog, and looking only
       for '/' silently skipped the mkdir and failed on the write instead. */
    char *slash = strrchr(parent, '/');
    char *backslash = strrchr(parent, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash != NULL && slash != parent) {
        *slash = '\0';
        char errbuf[256];
        platform_mkdir_p(parent, errbuf, sizeof(errbuf));
    }

    char errbuf[256];
    bool ok = platform_write_file(outPath, stubBytes, stubLen, errbuf, sizeof(errbuf));
    if (ok) ok = platform_append_file(outPath, payload, payloadLen, errbuf, sizeof(errbuf));
    if (ok) {
        /* [ magic (9) ][ payload length, u64 big-endian (8) ] -- the same
           17-byte trailer funnylang/packager.py writes, unchanged. */
        unsigned char trailer[17];
        memcpy(trailer, "FUNNYYEET", 9);
        for (int i = 0; i < 8; i++) trailer[9 + i] = (unsigned char)((uint64_t)payloadLen >> (8 * (7 - i)));
        ok = platform_append_file(outPath, trailer, sizeof(trailer), errbuf, sizeof(errbuf));
    }
    size_t total = stubLen + payloadLen + 17;
    free(payload);
    free(stubBytes);
    if (!ok) {
        fprintf(stderr, "couldn't write '%s': %s.\n", outPath, errbuf);
        return 1;
    }
    platform_make_executable(outPath);

    printf("yeeted %.1f MB of pure comedy into %s. it runs anywhere. no python. no cap.\n",
            (double)total / (1024.0 * 1024.0), outPath);
    return 0;
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

    /* `yeet` needs its own handling (it takes -o); `run` is optional, so
       `funny x.funnyc` and `funny run x.funnyc` are the same thing. The
       remaining subcommands (build/xray/fmt/test/vibe) are N8's, written
       in FunnyLang. */
    if (argc > 2 && strcmp(argv[1], "yeet") == 0) {
        const char *source = argv[2];
        const char *out = NULL;
        for (int i = 3; i < argc - 1; i++) {
            if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) out = argv[i + 1];
        }
        char defaultOut[4096];
        if (out == NULL) {
            /* Same default as funnylang/cli.py: the source path with its
               extension dropped (plus .exe on Windows). */
            snprintf(defaultOut, sizeof(defaultOut), "%s", source);
            char *dot = strrchr(defaultOut, '.');
            if (dot != NULL) *dot = '\0';
#ifdef _WIN32
            size_t n = strlen(defaultOut);
            if (n + 5 < sizeof(defaultOut)) memcpy(defaultOut + n, ".exe", 5);
#endif
            out = defaultOut;
        }
        return yeet(source, out, &opts);
    }

    int fileIndex = 1;
    if (argc > 1 && strcmp(argv[1], "run") == 0) fileIndex = 2;

    if (fileIndex >= argc) {
        puts("FunnyLang (native) -- it compiles. somehow.");
        puts("usage: funny [--serious] [--no-color] [--time] [--vibes] [run] <file> [args...]");
        puts("       funny yeet <file.funny> [-o <out>]");
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
    RunnerOptions runOpts = {opts.diag, NULL};
    int status = funny_run_bytecode(data, len, programArgs, programArgc, runOpts, &runMs);
    free(data);
    if (opts.showTime && status == 0) printf("ran in %.0fms. blazingly fast (probably)\n", runMs);
    return status;
}
