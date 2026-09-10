#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"

/* PLAN.md §4.1's own roast table, ported verbatim from
   funnylang/errors.py's DEFAULT_ROASTS -- used whenever a throw site
   doesn't supply a more specific one. */
static const struct {
    const char *flavor;
    const char *roast;
} DEFAULT_ROASTS[] = {
    {"LexerSaidNah", "what even IS that character. i'm not doing this."},
    {"ParserHadAStroke", "i read this three times. it's still not code."},
    {"WhoDis", "who? never heard of them."},
    {"TypeVibeMismatch", "those two do NOT have the same energy."},
    {"MathAintMathin", "you divided by zero. the universe said no."},
    {"OutOfPocket", "that's straight up out of pocket."},
    {"KeyGhosted", "that key left the group chat."},
    {"GhostError", "you're talking to a ghost, king."},
    {"NotACallableRizz", "that thing has no call rizz whatsoever."},
    {"WrongNumberOfHomies", "wrong number of homies. awkward."},
    {"TooDeepBro", "you recursed way too deep. touch grass."},
    {"ImportSkillIssue", "can't find it. did you make it up?"},
    {"ImmutableVibes", "it's deadass. it doesn't change. like your ex's opinion of you."},
    {"SkillIssue", "skill issue."},
    {"ComputerExploded", "it's over. exit code 69."},
};
#define DEFAULT_ROAST_COUNT (int)(sizeof(DEFAULT_ROASTS) / sizeof(DEFAULT_ROASTS[0]))

static const char *default_roast_for(const char *flavor, const char *fallback) {
    for (int i = 0; i < DEFAULT_ROAST_COUNT; i++) {
        if (strcmp(DEFAULT_ROASTS[i].flavor, flavor) == 0) return DEFAULT_ROASTS[i].roast;
    }
    return fallback;
}

/* This table *is* PLAN.md §4.1's taxonomy, so it doubles as the guest list
   for `oops(flavor, ...)`: a FunnyLang program may raise any of these and
   nothing else. Keeping the set closed means an error flavor always means
   the same thing, rather than being whatever string a library invented. */
bool error_is_known_flavor(const char *flavor) {
    for (int i = 0; i < DEFAULT_ROAST_COUNT; i++) {
        if (strcmp(DEFAULT_ROASTS[i].flavor, flavor) == 0) return true;
    }
    return false;
}

ObjError *error_new(GC *gc, const char *flavor, const char *message, const char *roast, const char *hint,
                     uint32_t line, uint32_t col, const char *file, Value payload, char **trace, int traceCount) {
    ObjError *e = (ObjError *)malloc(sizeof(ObjError));
    e->obj.type = OBJ_ERROR;
    e->obj.marked = false;
    e->obj.size = 0;
    e->obj.next = NULL;
    e->flavor = string_new(gc, flavor, (uint32_t)strlen(flavor));
    e->message = string_new(gc, message, (uint32_t)strlen(message));
    const char *roastText = roast ? roast : default_roast_for(flavor, message);
    e->roast = string_new(gc, roastText, (uint32_t)strlen(roastText));
    e->hint = hint ? string_new(gc, hint, (uint32_t)strlen(hint)) : NULL;
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
