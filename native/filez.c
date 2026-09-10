#include "filez.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "platform.h"
#include "stash.h"
#include "da_string.h"
#include "vm.h"

static const char *path_str(VM *vm, Value v, const char *fn_name) {
    if (!IS_STRING(v)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a yapstring path, not a %s.", fn_name,
                         vm_type_name(v));
        return NULL;
    }
    return AS_STRING(v)->chars;
}

static void io_fail(VM *vm, const char *fn_name, const char *path, const char *errbuf) {
    /* funnylang/stdlib/filez.py's _io_wrap pairs the strerror message with
       its own roast, so funny mode blames the filesystem by name instead
       of falling back to the flavor's generic "skill issue." */
    char roast[512];
    snprintf(roast, sizeof(roast), "couldn't %s `%s`. the filesystem said no.", fn_name, path);
    vm_throw_native_roast(vm, "SkillIssue", roast, "'%s' on '%s' failed: %s.", fn_name, path, errbuf);
}

/* -- pure path-string helpers (funnylang/stdlib/filez.py never touches
   the filesystem for these -- pathlib's join/parent/name/suffix are all
   string manipulation) --------------------------------------------- */

typedef struct {
    bool absolute;
    char drive[8]; /* "C:" on Windows, empty everywhere else */
    char **parts;
    int count;
} ParsedPath;

/* Splits on whatever this OS treats as a separator, and keeps any drive
   prefix aside. filez.py uses pathlib, which is WindowsPath on Windows --
   so a POSIX-only parse would disagree with the reference about every path
   a Windows program builds. */
static ParsedPath parse_path(const char *path) {
    ParsedPath p;
    p.drive[0] = '\0';
    size_t driveLen = platform_drive_prefix_len(path);
    if (driveLen > 0 && driveLen < sizeof(p.drive)) {
        memcpy(p.drive, path, driveLen);
        p.drive[driveLen] = '\0';
    }
    const char *rest = path + driveLen;
    p.absolute = rest[0] != '\0' && platform_is_path_sep(rest[0]);
    p.parts = (char **)malloc(sizeof(char *) * 256);
    p.count = 0;
    size_t restLen = strlen(rest);
    char *copy = (char *)malloc(restLen + 1);
    memcpy(copy, rest, restLen + 1);
    size_t start = 0;
    for (size_t i = 0; i <= restLen; i++) {
        if (i == restLen || platform_is_path_sep(copy[i])) {
            copy[i] = '\0';
            const char *tok = copy + start;
            if (tok[0] != '\0' && strcmp(tok, ".") != 0 && p.count < 256) {
                size_t len = strlen(tok);
                char *dup = (char *)malloc(len + 1);
                memcpy(dup, tok, len + 1);
                p.parts[p.count++] = dup;
            }
            start = i + 1;
        }
    }
    free(copy);
    return p;
}

static void free_parsed(ParsedPath *p) {
    for (int i = 0; i < p->count; i++) free(p->parts[i]);
    free(p->parts);
}

static Value m_join_path(VM *vm, Value *a, int argc) {
    for (int i = 0; i < argc; i++) {
        if (!IS_STRING(a[i])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'join_path' needs a yapstring path, not a %s.",
                             vm_type_name(a[i]));
            return GHOST_VAL;
        }
    }
    size_t cap = 256, len = 0;
    char *buf = (char *)malloc(cap);
    buf[0] = '\0';
    const char sep = platform_path_sep();
    for (int i = 0; i < argc; i++) {
        ObjString *part = AS_STRING(a[i]);
        if (part->byteLen == 0) continue;
        bool rooted = platform_is_path_sep(part->chars[0]) || platform_drive_prefix_len(part->chars) > 0;
        if (i == 0 || rooted) {
            /* A later part that starts a root of its own -- a leading
               separator, or a drive prefix on Windows -- overrides
               everything before it, matching pathlib's Path.__truediv__. */
            if (part->byteLen + 1 > cap) {
                cap = part->byteLen + 1;
                buf = (char *)realloc(buf, cap);
            }
            memcpy(buf, part->chars, part->byteLen);
            len = part->byteLen;
            buf[len] = '\0';
            continue;
        }
        bool needSep = len > 0 && !platform_is_path_sep(buf[len - 1]);
        size_t needed = len + (needSep ? 1 : 0) + part->byteLen + 1;
        if (needed > cap) {
            cap = needed;
            buf = (char *)realloc(buf, cap);
        }
        if (needSep) buf[len++] = sep;
        memcpy(buf + len, part->chars, part->byteLen);
        len += part->byteLen;
        buf[len] = '\0';
    }
    /* pathlib does not concatenate: it parses each part into components and
       re-renders them with the OS separator, so "a/" + "b" is "a\b" on
       Windows and repeated or "." components collapse. Re-rendering the
       joined string through the same parser gets all of that for free. */
    ParsedPath parsed = parse_path(buf);
    free(buf);
    char out[4096];
    size_t pos = strlen(parsed.drive);
    memcpy(out, parsed.drive, pos);
    if (parsed.absolute) out[pos++] = sep;
    for (int i = 0; i < parsed.count; i++) {
        if (i > 0) out[pos++] = sep;
        size_t l = strlen(parsed.parts[i]);
        if (pos + l < sizeof(out)) {
            memcpy(out + pos, parsed.parts[i], l);
            pos += l;
        }
    }
    if (pos == 0) out[pos++] = '.'; /* str(Path("")) is "." */
    out[pos] = '\0';
    free_parsed(&parsed);
    return OBJ_VAL(string_new(&vm->gc, out, (uint32_t)pos));
}

static Value m_dir_of(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "dir_of");
    if (!path) return GHOST_VAL;
    ParsedPath p = parse_path(path);
    const char sep = platform_path_sep();
    char buf[4096];
    size_t driveLen = strlen(p.drive);
    memcpy(buf, p.drive, driveLen);
    size_t pos = driveLen;
    if (p.count <= 1) {
        /* pathlib: the parent of a bare name is ".", of a root is the root
           itself, and a drive keeps its drive. */
        if (p.absolute) {
            buf[pos++] = sep;
        } else if (driveLen == 0) {
            buf[pos++] = '.';
        }
    } else {
        if (p.absolute) buf[pos++] = sep;
        for (int i = 0; i < p.count - 1; i++) {
            if (i > 0) buf[pos++] = sep;
            size_t l = strlen(p.parts[i]);
            if (pos + l < sizeof(buf)) {
                memcpy(buf + pos, p.parts[i], l);
                pos += l;
            }
        }
    }
    buf[pos] = '\0';
    free_parsed(&p);
    return OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)strlen(buf)));
}

static Value m_base_of(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "base_of");
    if (!path) return GHOST_VAL;
    ParsedPath p = parse_path(path);
    const char *name = p.count > 0 ? p.parts[p.count - 1] : "";
    Value result = OBJ_VAL(string_new(&vm->gc, name, (uint32_t)strlen(name)));
    free_parsed(&p);
    return result;
}

static Value m_ext_of(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "ext_of");
    if (!path) return GHOST_VAL;
    ParsedPath p = parse_path(path);
    const char *name = p.count > 0 ? p.parts[p.count - 1] : "";
    size_t nameLen = strlen(name);
    const char *suffix = "";
    /* pathlib's Path.suffix: the last dot, provided it's neither the
       first nor the last character of the name. */
    for (size_t i = nameLen; i-- > 0;) {
        if (name[i] == '.') {
            if (i > 0 && i < nameLen - 1) suffix = name + i;
            break;
        }
    }
    Value result = OBJ_VAL(string_new(&vm->gc, suffix, (uint32_t)strlen(suffix)));
    free_parsed(&p);
    return result;
}

/* -- real filesystem access, all via platform.h --------------------- */

static Value m_slurp(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "slurp");
    if (!path) return GHOST_VAL;
    unsigned char *data;
    size_t len;
    char errbuf[256];
    if (!platform_read_file(path, &data, &len, errbuf, sizeof(errbuf))) {
        io_fail(vm, "slurp", path, errbuf);
        return GHOST_VAL;
    }
    /* Text mode, matching funnylang/stdlib/filez.py's Path.read_text():
       universal newlines, so every "\r\n" and every lone "\r" arrives as
       "\n" and a CRLF file reads identically on every platform. Done here
       rather than in platform.c because this is text-format semantics, not
       an OS difference -- a CRLF file is a CRLF file on Linux too.
       `filez.read_bytes` is the way to get the bytes as they are. */
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\r') {
            data[out++] = '\n';
            if (i + 1 < len && data[i + 1] == '\n') i++;
        } else {
            data[out++] = data[i];
        }
    }
    len = out;
    Value result = OBJ_VAL(string_new(&vm->gc, (const char *)data, (uint32_t)len));
    free(data);
    return result;
}

static Value m_yeet_out(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "yeet_out");
    if (!path) return GHOST_VAL;
    if (!IS_STRING(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'yeet_out' needs a yapstring, not a %s.", vm_type_name(a[1]));
        return GHOST_VAL;
    }
    ObjString *text = AS_STRING(a[1]);
    char errbuf[256];
    if (!platform_write_file(path, (const unsigned char *)text->chars, text->byteLen, errbuf, sizeof(errbuf))) {
        io_fail(vm, "yeet_out", path, errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(text->codepointCount);
}

static Value m_append_to(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "append_to");
    if (!path) return GHOST_VAL;
    if (!IS_STRING(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'append_to' needs a yapstring, not a %s.", vm_type_name(a[1]));
        return GHOST_VAL;
    }
    ObjString *text = AS_STRING(a[1]);
    char errbuf[256];
    if (!platform_append_file(path, (const unsigned char *)text->chars, text->byteLen, errbuf, sizeof(errbuf))) {
        io_fail(vm, "append_to", path, errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(text->codepointCount);
}

static Value m_exists(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "exists");
    if (!path) return GHOST_VAL;
    return BOOL_VAL(platform_path_exists(path));
}

/* The raw-bytes counterpart to `append_to`. `write_bytes` already existed;
   without this, a program could create a binary but never add to one --
   which is exactly what `funny yeet` does to the runtime stub. */
static Value m_append_bytes(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "append_bytes");
    if (!path) return GHOST_VAL;
    if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'append_bytes' needs a stash of ints 0-255, not a %s.",
                        vm_type_name(a[1]));
        return GHOST_VAL;
    }
    ObjStash *s = (ObjStash *)AS_OBJ(a[1]);
    unsigned char *buf = (unsigned char *)malloc(s->count > 0 ? (size_t)s->count : 1);
    for (int i = 0; i < s->count; i++) {
        Value b = s->items[i];
        if (!IS_INT(b) || AS_INT(b) < 0 || AS_INT(b) > 255) {
            free(buf);
            vm_throw_native(vm, "TypeVibeMismatch", "'append_bytes' needs a stash of ints 0-255.");
            return GHOST_VAL;
        }
        buf[i] = (unsigned char)AS_INT(b);
    }
    char errbuf[256];
    bool ok = platform_append_file(path, buf, (size_t)s->count, errbuf, sizeof(errbuf));
    free(buf);
    if (!ok) {
        io_fail(vm, "append_bytes", path, errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(s->count);
}

/* Marks `path` runnable: the executable bits on POSIX, a no-op on Windows,
   where being executable is a matter of the extension. A program that
   writes a program has to be able to do this. */
static Value m_make_executable(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "make_executable");
    if (!path) return GHOST_VAL;
    platform_make_executable(path);
    return GHOST_VAL;
}

/* A path in the OS temp directory that nothing else is using. The file is
   the caller's to remove -- `obliterate` when done. */
static Value m_temp_file(VM *vm, Value *a, int argc) {
    const char *prefix = "funny";
    if (argc > 0 && !IS_GHOST(a[0])) {
        if (!IS_STRING(a[0])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'temp_file' needs a yapstring prefix, not a %s.",
                            vm_type_name(a[0]));
            return GHOST_VAL;
        }
        prefix = AS_STRING(a[0])->chars;
    }
    char buf[4096];
    if (!platform_temp_file(prefix, buf, sizeof(buf))) {
        vm_throw_native_roast(vm, "SkillIssue", "couldn't make a temp file. the filesystem said no.",
                              "'temp_file' couldn't create a temporary file.");
        return GHOST_VAL;
    }
    return OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)strlen(buf)));
}

static Value m_is_dir(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "is_dir");
    if (!path) return GHOST_VAL;
    return BOOL_VAL(platform_path_is_dir(path));
}

static Value m_is_file(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "is_file");
    if (!path) return GHOST_VAL;
    return BOOL_VAL(platform_path_is_file(path));
}

static Value m_obliterate(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "obliterate");
    if (!path) return GHOST_VAL;
    char errbuf[256];
    if (!platform_remove_path(path, errbuf, sizeof(errbuf))) {
        io_fail(vm, "obliterate", path, errbuf);
        return GHOST_VAL;
    }
    return BOOL_VAL(true);
}

static Value m_list_dir(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "list_dir");
    if (!path) return GHOST_VAL;
    char **names;
    size_t count;
    char errbuf[256];
    if (!platform_list_dir(path, &names, &count, errbuf, sizeof(errbuf))) {
        io_fail(vm, "list_dir", path, errbuf);
        return GHOST_VAL;
    }
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (size_t i = 0; i < count; i++) {
        stash_push(&vm->gc, s, OBJ_VAL(string_new(&vm->gc, names[i], (uint32_t)strlen(names[i]))));
    }
    gc_pop_temp(&vm->gc);
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    return OBJ_VAL(s);
}

static Value m_mkdir(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "mkdir");
    if (!path) return GHOST_VAL;
    char errbuf[256];
    if (!platform_mkdir_p(path, errbuf, sizeof(errbuf))) {
        io_fail(vm, "mkdir", path, errbuf);
        return GHOST_VAL;
    }
    return BOOL_VAL(true);
}

static Value m_read_bytes(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "read_bytes");
    if (!path) return GHOST_VAL;
    unsigned char *data;
    size_t len;
    char errbuf[256];
    if (!platform_read_file(path, &data, &len, errbuf, sizeof(errbuf))) {
        io_fail(vm, "read_bytes", path, errbuf);
        return GHOST_VAL;
    }
    ObjStash *s = stash_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(s));
    for (size_t i = 0; i < len; i++) stash_push(&vm->gc, s, INT_VAL(data[i]));
    gc_pop_temp(&vm->gc);
    free(data);
    return OBJ_VAL(s);
}

/* int(x) & 0xFF, matching write_bytes.py's `bytes(int(x) & 0xFF for x in
   data.items)` for the ordinary numba cases; a bignum too large for
   int64_t is a narrower, safety-improving TypeVibeMismatch here instead
   of chasing Python's arbitrary-precision `& 0xFF` for a case no
   realistic byte-stash will ever hit (see rizz.roll's own precedent,
   NATIVE_PLAN.md §9). */
static bool value_to_byte(Value v, unsigned char *out) {
    int64_t n;
    if (IS_BOOL(v)) {
        n = AS_BOOL(v) ? 1 : 0;
    } else if (IS_INT(v)) {
        n = AS_INT(v);
    } else if (IS_FLOAT(v)) {
        n = (int64_t)AS_FLOAT(v);
    } else if (IS_BIGNUM(v)) {
        if (!bignum_to_int64(AS_BIGNUM(v), &n)) return false;
    } else {
        return false;
    }
    *out = (unsigned char)(n & 0xFF);
    return true;
}

static Value m_write_bytes(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "write_bytes");
    if (!path) return GHOST_VAL;
    if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_STASH)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'write_bytes' needs a stash of numbas.");
        return GHOST_VAL;
    }
    ObjStash *s = (ObjStash *)AS_OBJ(a[1]);
    unsigned char *payload = (unsigned char *)malloc(s->count > 0 ? (size_t)s->count : 1);
    for (int i = 0; i < s->count; i++) {
        if (!value_to_byte(s->items[i], &payload[i])) {
            free(payload);
            vm_throw_native(vm, "TypeVibeMismatch", "'write_bytes' needs a stash of numbas.");
            return GHOST_VAL;
        }
    }
    char errbuf[256];
    if (!platform_write_file(path, payload, (size_t)s->count, errbuf, sizeof(errbuf))) {
        free(payload);
        io_fail(vm, "write_bytes", path, errbuf);
        return GHOST_VAL;
    }
    free(payload);
    return INT_VAL(s->count);
}

static Value m_abs_path(VM *vm, Value *a, int argc) {
    (void)argc;
    const char *path = path_str(vm, a[0], "abs_path");
    if (!path) return GHOST_VAL;
    char out[4096];
    char errbuf[256];
    if (!platform_abs_path(path, out, sizeof(out), errbuf, sizeof(errbuf))) {
        io_fail(vm, "abs_path", path, errbuf);
        return GHOST_VAL;
    }
    return OBJ_VAL(string_new(&vm->gc, out, (uint32_t)strlen(out)));
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} FilezEntry;

static const FilezEntry FILEZ_FUNCTIONS[] = {
    {"slurp", m_slurp, 1, 1},
    {"yeet_out", m_yeet_out, 2, 2},
    {"append_to", m_append_to, 2, 2},
    {"exists", m_exists, 1, 1},
    {"is_dir", m_is_dir, 1, 1},
    {"is_file", m_is_file, 1, 1},
    {"obliterate", m_obliterate, 1, 1},
    {"list_dir", m_list_dir, 1, 1},
    {"mkdir", m_mkdir, 1, 1},
    {"read_bytes", m_read_bytes, 1, 1},
    {"write_bytes", m_write_bytes, 2, 2},
    {"append_bytes", m_append_bytes, 2, 2},
    {"make_executable", m_make_executable, 1, 1},
    {"temp_file", m_temp_file, 0, 1},
    {"abs_path", m_abs_path, 1, 1},
    {"join_path", m_join_path, 1, 255},
    {"dir_of", m_dir_of, 1, 1},
    {"base_of", m_base_of, 1, 1},
    {"ext_of", m_ext_of, 1, 1},
};
#define FILEZ_FUNCTIONS_COUNT (int)(sizeof(FILEZ_FUNCTIONS) / sizeof(FILEZ_FUNCTIONS[0]))

Value filez_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < FILEZ_FUNCTIONS_COUNT; i++) {
        const FilezEntry *e = &FILEZ_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "filez", 5);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
