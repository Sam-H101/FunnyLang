#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"

ObjError *error_new(GC *gc, const char *flavor, const char *message, uint32_t line, uint32_t col,
                     const char *file, Value payload, char **trace, int traceCount) {
    ObjError *e = (ObjError *)malloc(sizeof(ObjError));
    e->obj.type = OBJ_ERROR;
    e->obj.marked = false;
    e->obj.size = 0;
    e->obj.next = NULL;
    e->flavor = string_new(gc, flavor, (uint32_t)strlen(flavor));
    e->message = string_new(gc, message, (uint32_t)strlen(message));
    e->line = line;
    e->col = col;
    e->file = string_new(gc, file ? file : "", (uint32_t)strlen(file ? file : ""));
    e->payload = payload;
    e->rawTraceCount = traceCount;
    if (traceCount > 0) {
        e->rawTrace = (char **)malloc((size_t)traceCount * sizeof(char *));
        for (int i = 0; i < traceCount; i++) {
            size_t n = strlen(trace[i]) + 1;
            e->rawTrace[i] = (char *)malloc(n);
            memcpy(e->rawTrace[i], trace[i], n);
        }
    } else {
        e->rawTrace = NULL;
    }
    gc_track(gc, (Obj *)e, sizeof(ObjError));
    return e;
}

bool error_get_field(GC *gc, const ObjError *err, const char *name, Value *out) {
    if (strcmp(name, "flavor") == 0) {
        *out = OBJ_VAL(err->flavor);
        return true;
    }
    if (strcmp(name, "message") == 0) {
        *out = OBJ_VAL(err->message);
        return true;
    }
    if (strcmp(name, "line") == 0) {
        *out = INT_VAL((int64_t)err->line);
        return true;
    }
    if (strcmp(name, "col") == 0) {
        *out = INT_VAL((int64_t)err->col);
        return true;
    }
    if (strcmp(name, "file") == 0) {
        *out = OBJ_VAL(err->file);
        return true;
    }
    if (strcmp(name, "payload") == 0) {
        *out = err->payload;
        return true;
    }
    if (strcmp(name, "trace") == 0) {
        /* PLAN.md §3.9 wants a stash of strings; Stash doesn't exist until
           N4 (see error.h's own note). A single placeholder string beats
           silently returning ghost for something that isn't actually
           absent -- at least "how many frames deep" is visible. */
        char buf[32];
        snprintf(buf, sizeof buf, "<%d frame(s), see N4>", err->rawTraceCount);
        ObjString *s = string_new(gc, buf, (uint32_t)strlen(buf));
        *out = OBJ_VAL(s);
        return true;
    }
    return false;
}
