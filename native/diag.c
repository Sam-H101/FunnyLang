#include "diag.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "platform.h"

#define ANSI_RED "\x1b[31m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_RESET "\x1b[0m"

/* funnylang/errors.py's own _COMPILE_TIME_FLAVORS: these never get a
   stack trace printed, because there was no stack yet when they happened. */
static bool is_compile_time_flavor(const char *flavor) {
    return strcmp(flavor, "LexerSaidNah") == 0 || strcmp(flavor, "ParserHadAStroke") == 0 ||
           strcmp(flavor, "WhoDis") == 0 || strcmp(flavor, "ImmutableVibes") == 0;
}

/* FUNNY_SERIOUS plus stdout's own tty-ness -- what the options are when
   nobody has said otherwise. */
static DiagOptions derive_default_options(void) {
    DiagOptions opts;
    const char *serious = getenv("FUNNY_SERIOUS");
    opts.serious = serious != NULL && strcmp(serious, "1") == 0;
    opts.color = platform_stdout_is_tty();
    opts.sourceRoot = NULL;
    return opts;
}

/* The CLI parses --serious/--no-color once and installs the result here, so
   a nested run (sus.run_program, the compiler, a REPL input) renders the
   same way instead of each re-deriving from the environment.

   Process-global on purpose, and safe because of when it is written: once,
   by main.c, before the first VM exists and so before any `interns` thread
   can. Every later access is a read. Nothing else may call the setter
   (RUNTIME_PLAN.md R0). */
static bool g_hasOverride = false;
static DiagOptions g_override;

void diag_set_default_options(DiagOptions opts) {
    /* Start-up only, and exactly once: main.c, before any VM and so before any
       thread. A second call from anywhere else would be a write racing every
       other thread's reads (RUNTIME_PLAN.md R0). */
    assert(!g_hasOverride && "diag options are installed once, at start-up");
    g_override = opts;
    g_hasOverride = true;
}

DiagOptions diag_default_options(void) {
    return g_hasOverride ? g_override : derive_default_options();
}

/* One source file, split into lines the way funnylang/source.py does:
   on '\n', keeping a final empty line when the text ends with one (so a
   one-line file that ends in a newline reports two lines, and the
   snippet's "line + 1" context row is that empty line, exactly as the
   Python renderer prints it). */
typedef struct {
    char *text;
    char **lines;
    int count;
} SourceLines;

/* `root` + separator + `rel`, malloc'd. No normalization: this only ever
   joins a caller-supplied directory to a bundle key, and both are already
   the shape the loader produced. NULL on an empty root, so the caller falls
   back to leaving the path alone. */
static char *path_under_root(const char *root, const char *rel) {
    size_t rootLen = strlen(root);
    if (rootLen == 0) return NULL;
    bool needSep = !platform_is_path_sep(root[rootLen - 1]);
    size_t relLen = strlen(rel);
    char *out = (char *)malloc(rootLen + (needSep ? 1 : 0) + relLen + 1);
    if (out == NULL) return NULL;
    memcpy(out, root, rootLen);
    size_t at = rootLen;
    if (needSep) out[at++] = platform_path_sep();
    memcpy(out + at, rel, relLen + 1);
    return out;
}

static bool source_lines_load(const char *path, SourceLines *out) {
    out->text = NULL;
    out->lines = NULL;
    out->count = 0;
    if (!path || path[0] == '\0') return false;
    unsigned char *data = NULL;
    size_t len = 0;
    char errbuf[256];
    if (!platform_read_file(path, &data, &len, errbuf, sizeof(errbuf))) return false;

    out->text = (char *)data;
    int capacity = 16;
    out->lines = (char **)malloc((size_t)capacity * sizeof(char *));
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i != len && out->text[i] != '\n') continue;
        if (out->count == capacity) {
            capacity *= 2;
            out->lines = (char **)realloc(out->lines, (size_t)capacity * sizeof(char *));
        }
        size_t end = i;
        /* line_text() strips a trailing \r as well, so a CRLF file renders
           the same as an LF one rather than smearing the caret line. */
        if (end > start && out->text[end - 1] == '\r') end--;
        out->text[end] = '\0';
        out->lines[out->count++] = out->text + start;
        start = i + 1;
    }
    return true;
}

static void source_lines_free(SourceLines *src) {
    free(src->lines);
    free(src->text);
    src->lines = NULL;
    src->text = NULL;
    src->count = 0;
}

static void put_colored(FILE *out, const char *text, const char *code, bool color) {
    if (color) {
        fputs(code, out);
        fputs(text, out);
        fputs(ANSI_RESET, out);
    } else {
        fputs(text, out);
    }
}

static void render_snippet(FILE *out, const SourceLines *src, uint32_t line, uint32_t col, bool color) {
    if (src->count == 0) return;
    int total = src->count;
    int target = (int)line;
    int startLine = target - 2 < 1 ? 1 : target - 2;
    int endLine = target + 1 > total ? total : target + 1;
    char widthBuf[16];
    int width = snprintf(widthBuf, sizeof(widthBuf), "%d", endLine);
    for (int n = startLine; n <= endLine; n++) {
        const char *text = (n >= 1 && n <= total) ? src->lines[n - 1] : "";
        fprintf(out, "     %*d \xe2\x94\x82 %s\n", width, n, text);
        if (n != target) continue;
        fprintf(out, "     %*s \xe2\x94\x82 ", width, "");
        for (uint32_t i = 1; i < col; i++) fputc(' ', out);
        put_colored(out, "^ this right here", ANSI_RED, color);
        fputc('\n', out);
    }
}

/* Prints `body` indented two spaces per line. Python renders
   `body.splitlines() or [""]`, so an empty body still produces one
   (two-space) line rather than nothing at all. */
static void render_body(FILE *out, const char *body) {
    if (body[0] == '\0') {
        fputs("  \n", out);
        return;
    }
    const char *p = body;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        fprintf(out, "  %.*s\n", (int)len, p);
        if (!nl) break;
        p = nl + 1;
    }
}

void diag_render_error(FILE *out, const ObjError *err, DiagOptions opts) {
    const char *flavor = err->flavor->chars;
    /* line 0 is this runtime's "no span" -- nothing located it, so there's
       nowhere to point a caret at (funnylang's `err.span is None`). */
    bool hasSpan = err->line > 0;

    put_colored(out, opts.serious ? "FUNNYLANG ERROR" : "\xf0\x9f\x92\x80\xf0\x9f\x92\x80\xf0\x9f\x92\x80 FUNNYLANG MOMENT \xf0\x9f\x92\x80\xf0\x9f\x92\x80\xf0\x9f\x92\x80",
                 ANSI_BOLD, opts.color);
    fputs("\n\n", out);

    fputs("  ", out);
    if (opts.color) fputs(ANSI_RED ANSI_BOLD, out);
    fputs(flavor, out);
    if (opts.color) fputs(ANSI_RESET, out);
    if (hasSpan) {
        const char *path = err->file->byteLen > 0 ? err->file->chars : "<unknown>";
        fprintf(out, "  \xe2\x94\x80\xe2\x94\x80  %s:%u:%u", path, err->line, err->col);
    }
    fputc('\n', out);

    /* Try the path as given first: an absolute path, or a relative one that
       already resolves from here, is what `funny run` produces and must keep
       working even when a sourceRoot is set. Only fall back to rooting it. */
    SourceLines src;
    bool haveSource = hasSpan && source_lines_load(err->file->chars, &src);
    if (hasSpan && !haveSource && opts.sourceRoot != NULL && err->file->byteLen > 0) {
        char *rooted = path_under_root(opts.sourceRoot, err->file->chars);
        if (rooted != NULL) {
            haveSource = source_lines_load(rooted, &src);
            free(rooted);
        }
    }
    if (haveSource) {
        fputc('\n', out);
        render_snippet(out, &src, err->line, err->col, opts.color);
        source_lines_free(&src);
    }

    fputc('\n', out);
    render_body(out, opts.serious ? err->message->chars : err->roast->chars);

    if (err->hint != NULL) {
        fputc('\n', out);
        fprintf(out, "  %s  %s\n", opts.serious ? "fix:" : "\xf0\x9f\x92\xa1 skill issue fix:", err->hint->chars);
    }

    if (err->rawTraceCount > 0 && !is_compile_time_flavor(flavor)) {
        fputc('\n', out);
        fputs(opts.serious ? "  stack trace:\n" : "  \xf0\x9f\xa5\x9e stack of shame:\n", out);
        for (int i = 0; i < err->rawTraceCount; i++) fprintf(out, "       %s\n", err->rawTrace[i]);
    }
}
