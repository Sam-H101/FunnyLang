#include "internet.h"

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

#define DEFAULT_TIMEOUT_MS 10000
/* funnylang/stdlib/internet.py's own NET_SAID_NO -- every failure mode
   (timeout, DNS failure, connection refused, malformed response, a
   not-yet-supported https:// scheme, ...) collapses to this exact same
   generic SkillIssue, matching Python's own single `except (URLError,
   OSError, ValueError): raise _no_net_error()` catch-all around every
   possible urllib/socket failure. */
static const char *NET_SAID_NO = "the internet said no. \xF0\x9F\x9A\xAB";

static const char *string_arg(VM *vm, Value v, const char *fnName) {
    if (!IS_STRING(v)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a yapstring, not a %s.", fnName, vm_type_name(v));
        return NULL;
    }
    return AS_STRING(v)->chars;
}

/* Every network failure collapses to Python's own generic NET_SAID_NO --
   except the one platform.c is allowed to name: a machine with no TLS
   library at all, which NATIVE_PLAN.md §3.1 requires be reported as "a
   clean SkillIssue naming the missing library" rather than hidden behind
   the generic text. Nothing differential-tests that branch (urllib always
   has ssl, so Python can't produce it). */
static void throw_net_error(VM *vm, const PlatformHttpResponse *resp) {
    const char *text = resp->failReason[0] ? resp->failReason : NET_SAID_NO;
    /* internet.py uses the same line for message and roast, so funny mode
       says "the internet said no" rather than the generic "skill issue." */
    vm_throw_native_roast(vm, "SkillIssue", text, "%s", text);
}

static double value_as_double(Value v) {
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    if (IS_INT(v)) return (double)AS_INT(v);
    if (IS_BIGNUM(v)) return bignum_to_double(AS_BIGNUM(v));
    return 0.0;
}

static Value opt_get(GC *gc, ObjGroupChat *opts, const char *key, Value fallback) {
    Value k = OBJ_VAL(string_new(gc, key, (uint32_t)strlen(key)));
    GroupChatEntry *e = groupchat_find(opts, k);
    return e ? e->value : fallback;
}

static Value m_go_brrrr(VM *vm, Value *a, int argc) {
    if (platform_net_disabled()) {
        vm_throw_native_roast(vm, "SkillIssue", NET_SAID_NO, "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    const char *url = string_arg(vm, a[0], "go_brrrr");
    if (!url) return GHOST_VAL;

    ObjGroupChat *opts = NULL;
    if (argc > 1 && !IS_GHOST(a[1])) {
        if (!(IS_OBJ(a[1]) && AS_OBJ(a[1])->type == OBJ_GROUPCHAT)) {
            vm_throw_native(vm, "TypeVibeMismatch", "'go_brrrr' options need to be a groupchat.");
            return GHOST_VAL;
        }
        opts = (ObjGroupChat *)AS_OBJ(a[1]);
    }
    Value methodV = opts ? opt_get(&vm->gc, opts, "method", GHOST_VAL) : GHOST_VAL;
    const char *method = "GET";
    if (!IS_GHOST(methodV)) {
        if (!IS_STRING(methodV)) {
            vm_throw_native(vm, "TypeVibeMismatch", "'go_brrrr' options need to be a groupchat.");
            return GHOST_VAL;
        }
        method = AS_STRING(methodV)->chars;
    }
    Value bodyV = opts ? opt_get(&vm->gc, opts, "body", GHOST_VAL) : GHOST_VAL;
    const char *bodyBytes = NULL;
    size_t bodyLen = 0;
    if (IS_STRING(bodyV)) {
        bodyBytes = AS_STRING(bodyV)->chars;
        bodyLen = AS_STRING(bodyV)->byteLen;
    }
    Value headersV = opts ? opt_get(&vm->gc, opts, "headers", GHOST_VAL) : GHOST_VAL;
    PlatformHttpHeader reqHeaders[64];
    int reqHeaderCount = 0;
    if (IS_OBJ(headersV) && AS_OBJ(headersV)->type == OBJ_GROUPCHAT) {
        ObjGroupChat *hg = (ObjGroupChat *)AS_OBJ(headersV);
        for (int i = 0; i < hg->count && reqHeaderCount < 64; i++) {
            if (IS_STRING(hg->entries[i].key) && IS_STRING(hg->entries[i].value)) {
                reqHeaders[reqHeaderCount].name = AS_STRING(hg->entries[i].key)->chars;
                reqHeaders[reqHeaderCount].value = AS_STRING(hg->entries[i].value)->chars;
                reqHeaderCount++;
            }
        }
    }
    Value timeoutV = opts ? opt_get(&vm->gc, opts, "timeout", GHOST_VAL) : GHOST_VAL;
    int timeoutMs = IS_GHOST(timeoutV) ? DEFAULT_TIMEOUT_MS : (int)(value_as_double(timeoutV) * 1000.0);

    PlatformHttpResponse resp =
        platform_http_request(method, url, reqHeaders, reqHeaderCount, bodyBytes, bodyLen, timeoutMs);
    if (!resp.ok) {
        throw_net_error(vm, &resp);
        return GHOST_VAL;
    }

    ObjGroupChat *respHeaders = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(respHeaders));
    for (int i = 0; i < resp.headerCount; i++) {
        Value k = OBJ_VAL(string_new(&vm->gc, resp.headers[i].name, (uint32_t)strlen(resp.headers[i].name)));
        Value v = OBJ_VAL(string_new(&vm->gc, resp.headers[i].value, (uint32_t)strlen(resp.headers[i].value)));
        groupchat_set(&vm->gc, respHeaders, k, v);
    }
    Value bodyStr = OBJ_VAL(string_new_utf8_lossy(&vm->gc, resp.body, (uint32_t)resp.bodyLen));
    gc_push_temp(&vm->gc, bodyStr);
    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "status", 6)), INT_VAL(resp.status));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "body", 4)), bodyStr);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "headers", 7)), OBJ_VAL(respHeaders));
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    gc_pop_temp(&vm->gc);
    platform_http_response_free(&resp);
    return OBJ_VAL(out);
}

static Value m_is_it_up(VM *vm, Value *a, int argc) {
    (void)argc;
    if (platform_net_disabled()) return BOOL_VAL(false);
    const char *url = string_arg(vm, a[0], "is_it_up");
    if (!url) return GHOST_VAL;
    PlatformHttpResponse resp = platform_http_request("GET", url, NULL, 0, NULL, 0, DEFAULT_TIMEOUT_MS);
    bool ok = resp.ok;
    platform_http_response_free(&resp);
    return BOOL_VAL(ok);
}

static Value m_download(VM *vm, Value *a, int argc) {
    (void)argc;
    if (platform_net_disabled()) {
        vm_throw_native_roast(vm, "SkillIssue", NET_SAID_NO, "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    const char *url = string_arg(vm, a[0], "download");
    if (!url) return GHOST_VAL;
    const char *path = string_arg(vm, a[1], "download");
    if (!path) return GHOST_VAL;
    PlatformHttpResponse resp = platform_http_request("GET", url, NULL, 0, NULL, 0, DEFAULT_TIMEOUT_MS);
    if (!resp.ok) {
        throw_net_error(vm, &resp);
        return GHOST_VAL;
    }
    char errbuf[256];
    bool wrote = platform_write_file(path, (const unsigned char *)resp.body, resp.bodyLen, errbuf, sizeof(errbuf));
    size_t len = resp.bodyLen;
    platform_http_response_free(&resp);
    if (!wrote) {
        vm_throw_native(vm, "SkillIssue", "'download' on '%s' failed: %s.", path, errbuf);
        return GHOST_VAL;
    }
    return INT_VAL((int64_t)len);
}

static Value m_speed_test(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    if (platform_net_disabled()) {
        vm_throw_native_roast(vm, "SkillIssue", NET_SAID_NO, "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    double start = platform_monotonic_seconds();
    PlatformHttpResponse resp =
        platform_http_request("GET", "https://example.com/", NULL, 0, NULL, 0, DEFAULT_TIMEOUT_MS);
    if (!resp.ok) {
        throw_net_error(vm, &resp);
        return GHOST_VAL;
    }
    double elapsed = platform_monotonic_seconds() - start;
    if (elapsed < 1e-6) elapsed = 1e-6;
    double mbps = ((double)resp.bodyLen * 8.0 / 1000000.0) / elapsed;
    platform_http_response_free(&resp);
    char buf[128];
    snprintf(buf, sizeof(buf), "your internet: %.2f mbps. mid.", mbps);
    return OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)strlen(buf)));
}

static Value m_ping(VM *vm, Value *a, int argc) {
    (void)argc;
    if (platform_net_disabled()) {
        vm_throw_native_roast(vm, "SkillIssue", NET_SAID_NO, "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    const char *host = string_arg(vm, a[0], "ping");
    if (!host) return GHOST_VAL;
    double ms;
    if (!platform_tcp_ping(host, 80, DEFAULT_TIMEOUT_MS, &ms)) {
        vm_throw_native_roast(vm, "SkillIssue", NET_SAID_NO, "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    return FLOAT_VAL(ms);
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} InternetEntry;

static const InternetEntry INTERNET_FUNCTIONS[] = {
    {"go_brrrr", m_go_brrrr, 1, 2}, {"is_it_up", m_is_it_up, 1, 1},   {"download", m_download, 2, 2},
    {"speed_test", m_speed_test, 0, 0}, {"ping", m_ping, 1, 1},
};
#define INTERNET_FUNCTIONS_COUNT (int)(sizeof(INTERNET_FUNCTIONS) / sizeof(INTERNET_FUNCTIONS[0]))

Value internet_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < INTERNET_FUNCTIONS_COUNT; i++) {
        const InternetEntry *e = &INTERNET_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "internet", 8);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
