#include "internet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "blob.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "otw.h"
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
    /* "blob" is the body as it arrived; "body" is the same bytes decoded as
       UTF-8. A response that is a PNG has a useless "body" and a usable
       "blob" (RUNTIME_PLAN.md R1). */
    Value bodyBlob = OBJ_VAL(blob_new(&vm->gc, (const uint8_t *)resp.body, (uint32_t)resp.bodyLen));
    gc_push_temp(&vm->gc, bodyBlob);
    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "status", 6)), INT_VAL(resp.status));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "body", 4)), bodyStr);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "blob", 4)), bodyBlob);
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "headers", 7)), OBJ_VAL(respHeaders));
    gc_pop_temp(&vm->gc);
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

/* -- the other direction: listening ---------------------------------------
 *
 * Everything above dials out. These five let a FunnyLang program *be* the
 * thing on the other end of somebody else's request.
 *
 * A listener and a connection are both `numba` handles, not heap objects --
 * the same reasoning `sus`'s REPL sessions and `interns`'s workers already
 * record: a handle that is a plain integer stays out of the collector, and it
 * can cross to an `interns` worker (which an object could not), so a
 * connection can be handed to another OS thread to answer.
 *
 * `FUNNY_NO_NET=1` does not block these. That flag exists so a test runner
 * can stop a program *reaching out* to the network; a server binding its own
 * loopback port is not that, and blocking it would make this whole feature
 * untestable in exactly the environment that most needs testing.
 */

static int64_t handle_arg(VM *vm, Value v, const char *fnName) {
    if (!IS_INT(v)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a socket handle, not a %s.", fnName, vm_type_name(v));
        return -1;
    }
    return AS_INT(v);
}

static int int_opt(Value v, int fallback) {
    if (IS_INT(v)) return (int)AS_INT(v);
    if (IS_FLOAT(v)) return (int)AS_FLOAT(v);
    return fallback;
}

/* internet.open_shop(port, host?) -- bind and listen. */
static Value m_open_shop(VM *vm, Value *a, int argc) {
    if (!IS_INT(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'open_shop' needs a port number, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    const char *host = "";
    if (argc > 1 && !IS_GHOST(a[1])) {
        host = string_arg(vm, a[1], "open_shop");
        if (host == NULL) return GHOST_VAL;
    }
    char errbuf[256];
    int64_t listener = platform_tcp_listen(host, (int)AS_INT(a[0]), 64, errbuf, sizeof errbuf);
    if (listener == PLATFORM_SOCKET_NONE) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(listener);
}

/* internet.open_secure_shop(port, host?, {"pfx": path, "password": ...}) --
   open_shop, plus a TLS identity from a PKCS#12 file. Every connection from
   the listener it returns carries a TLS session that has not handshaken yet:
   drive `internet.handshake(conn)` to done before reading, exactly the way a
   read is driven -- `hold_up` and try again -- so nothing blocks. */
static Value m_open_secure_shop(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_INT(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'open_secure_shop' needs a port number, not a %s.",
                        vm_type_name(a[0]));
        return GHOST_VAL;
    }
    const char *host = "";
    if (!IS_GHOST(a[1])) {
        host = string_arg(vm, a[1], "open_secure_shop");
        if (host == NULL) return GHOST_VAL;
    }
    if (!(IS_OBJ(a[2]) && AS_OBJ(a[2])->type == OBJ_GROUPCHAT)) {
        vm_throw_native(vm, "TypeVibeMismatch",
                        "'open_secure_shop' needs its options as a groupchat: {\"pfx\": path, \"password\": ...}.");
        return GHOST_VAL;
    }
    ObjGroupChat *opts = (ObjGroupChat *)AS_OBJ(a[2]);
    Value pfxV = opt_get(&vm->gc, opts, "pfx", GHOST_VAL);
    if (!IS_STRING(pfxV)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'open_secure_shop' needs \"pfx\": the path to a PKCS#12 file.");
        return GHOST_VAL;
    }
    Value pwV = opt_get(&vm->gc, opts, "password", GHOST_VAL);
    if (!IS_GHOST(pwV) && !IS_STRING(pwV)) {
        vm_throw_native(vm, "TypeVibeMismatch", "'open_secure_shop' needs \"password\" as a yapstring.");
        return GHOST_VAL;
    }
    char errbuf[512];
    int64_t listener = platform_tls_listen(host, (int)AS_INT(a[0]), 128, AS_STRING(pfxV)->chars,
                                           IS_STRING(pwV) ? AS_STRING(pwV)->chars : "", errbuf, sizeof errbuf);
    if (listener == PLATFORM_SOCKET_NONE) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    return INT_VAL(listener);
}

/* internet.next_customer(listener, timeout_ms?) -- accept, or `ghost` if
   nobody turned up in time. A timeout is deliberately not an error: a server
   loop wants to check its own shutdown flag between callers, and making that
   cost a `sketchy` block would be a tax on the normal case. */
static Value m_next_customer(VM *vm, Value *a, int argc) {
    int64_t listener = handle_arg(vm, a[0], "next_customer");
    if (vm->hadError) return GHOST_VAL;
    int timeoutMs = argc > 1 ? int_opt(a[1], -1) : -1;

    char peer[128];
    int64_t conn = platform_tcp_accept(listener, timeoutMs, peer, sizeof peer);
    if (conn == PLATFORM_SOCKET_TIMEOUT) return GHOST_VAL;
    if (conn == PLATFORM_SOCKET_ERROR) {
        vm_throw_native(vm, "SkillIssue", "that listener is closed, or the accept failed.");
        return GHOST_VAL;
    }

    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "conn", 4)), INT_VAL(conn));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "peer", 4)),
                  OBJ_VAL(string_new(&vm->gc, peer, (uint32_t)strlen(peer))));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "secure", 6)), BOOL_VAL(platform_socket_is_tls(conn)));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

/* internet.hear_them_out(conn, max_bytes?, timeout_ms?, {"raw": fax}?) -- one
   read. `""` means the other end closed; `ghost` means it said nothing in
   time. With {"raw": fax} the bytes come back as a `blob` instead of being
   decoded as UTF-8 (RUNTIME_PLAN.md R1) -- which is what a request body, an
   image, or anything else that isn't text needs; an empty blob is then the
   closed-connection answer. */
static Value m_hear_them_out(VM *vm, Value *a, int argc) {
    int64_t conn = handle_arg(vm, a[0], "hear_them_out");
    if (vm->hadError) return GHOST_VAL;
    int maxBytes = argc > 1 ? int_opt(a[1], 65536) : 65536;
    if (maxBytes < 1) maxBytes = 1;
    if (maxBytes > 1048576) maxBytes = 1048576;
    int timeoutMs = argc > 2 ? int_opt(a[2], 15000) : 15000;
    bool raw = false;
    if (argc > 3 && !IS_GHOST(a[3])) {
        if (!(IS_OBJ(a[3]) && AS_OBJ(a[3])->type == OBJ_GROUPCHAT)) {
            vm_throw_native(vm, "TypeVibeMismatch", "'hear_them_out' options need to be a groupchat.");
            return GHOST_VAL;
        }
        raw = value_is_truthy(opt_get(&vm->gc, (ObjGroupChat *)AS_OBJ(a[3]), "raw", BOOL_VAL(false)));
    }

    char *buf = (char *)malloc((size_t)maxBytes);
    int64_t n = platform_socket_recv(conn, buf, (size_t)maxBytes, timeoutMs);
    if (n == PLATFORM_SOCKET_TIMEOUT) {
        free(buf);
        return GHOST_VAL;
    }
    if (n == PLATFORM_SOCKET_ERROR) {
        free(buf);
        vm_throw_native(vm, "SkillIssue", "that connection broke while we were listening.");
        return GHOST_VAL;
    }
    Value out = raw ? OBJ_VAL(blob_new(&vm->gc, (const uint8_t *)buf, (uint32_t)n))
                    : OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)n));
    free(buf);
    return out;
}

/* internet.holler_back(conn, text_or_blob) -- write all of it, or raise. */
static Value m_holler_back(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t conn = handle_arg(vm, a[0], "holler_back");
    if (vm->hadError) return GHOST_VAL;
    const char *bytes;
    uint32_t len;
    if (IS_STRING(a[1])) {
        /* byteLen, not the codepoint count: what goes on the wire is bytes,
           and an HTTP Content-Length that counted characters would be wrong
           for every response with a non-ASCII byte in it. */
        bytes = AS_STRING(a[1])->chars;
        len = AS_STRING(a[1])->byteLen;
    } else if (IS_BLOB(a[1])) {
        bytes = (const char *)AS_BLOB(a[1])->bytes;
        len = AS_BLOB(a[1])->byteLen;
    } else {
        vm_throw_native(vm, "TypeVibeMismatch", "'holler_back' needs a yapstring or a blob to send, not a %s.",
                        vm_type_name(a[1]));
        return GHOST_VAL;
    }
    if (!platform_socket_send(conn, bytes, len)) {
        vm_throw_native(vm, "SkillIssue", "that connection broke while we were talking.");
        return GHOST_VAL;
    }
    return INT_VAL((int64_t)len);
}

/* internet.kick_out(conn) / internet.close_shop(listener) -- the same call,
   under two names, because closing a connection and closing the door the
   connections arrive through are different acts and a program reads better
   when it says which one it meant. */
static Value m_kick_out(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t conn = handle_arg(vm, a[0], "kick_out");
    if (vm->hadError) return GHOST_VAL;
    platform_socket_close(conn);
    return GHOST_VAL;
}

/* internet.slide_into(host, port, timeout_ms?) -- a raw TCP connection, with
   none of the HTTP above it. The other end of `open_shop`, and what lets a
   test be both halves of a conversation.
 *
 * FUNNY_NO_NET stops this, with one exception that is not a loophole:
 * loopback. That flag exists so a test runner does not *reach the network*,
 * and a connection to 127.0.0.1 sends no packet anywhere. Refusing it would
 * make a server impossible to test in exactly the environment that most needs
 * its tests to run. */
static bool is_loopback(const char *host) {
    return strcmp(host, "127.0.0.1") == 0 || strcmp(host, "localhost") == 0 || strcmp(host, "::1") == 0 ||
           strncmp(host, "127.", 4) == 0;
}

static Value m_slide_into(VM *vm, Value *a, int argc) {
    const char *host = string_arg(vm, a[0], "slide_into");
    if (host == NULL) return GHOST_VAL;
    if (!IS_INT(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'slide_into' needs a port number, not a %s.", vm_type_name(a[1]));
        return GHOST_VAL;
    }
    if (platform_net_disabled() && !is_loopback(host)) {
        vm_throw_native(vm, "SkillIssue", "%s", NET_SAID_NO);
        return GHOST_VAL;
    }
    /* The third argument is a timeout in milliseconds, as it always was, or
       a groupchat: {"timeout_ms", "tls", "server_name", "ca"}. With "tls" the
       connection comes back handshaken and verified -- chain and name --
       against the system store, or against exactly the one CA in "ca". */
    int timeoutMs = DEFAULT_TIMEOUT_MS;
    bool tls = false;
    const char *serverName = NULL;
    const char *caPath = NULL;
    if (argc > 2 && IS_OBJ(a[2]) && AS_OBJ(a[2])->type == OBJ_GROUPCHAT) {
        ObjGroupChat *o = (ObjGroupChat *)AS_OBJ(a[2]);
        timeoutMs = int_opt(opt_get(&vm->gc, o, "timeout_ms", GHOST_VAL), DEFAULT_TIMEOUT_MS);
        tls = value_is_truthy(opt_get(&vm->gc, o, "tls", BOOL_VAL(false)));
        Value sn = opt_get(&vm->gc, o, "server_name", GHOST_VAL);
        if (IS_STRING(sn)) serverName = AS_STRING(sn)->chars;
        Value ca = opt_get(&vm->gc, o, "ca", GHOST_VAL);
        if (IS_STRING(ca)) caPath = AS_STRING(ca)->chars;
    } else if (argc > 2) {
        timeoutMs = int_opt(a[2], DEFAULT_TIMEOUT_MS);
    }
    if (tls) {
        char errbuf[512];
        int64_t secure = platform_tls_connect(host, (int)AS_INT(a[1]), timeoutMs, serverName, caPath, errbuf,
                                              sizeof errbuf);
        if (secure == PLATFORM_SOCKET_NONE) {
            vm_throw_native(vm, "SkillIssue", "%s", errbuf);
            return GHOST_VAL;
        }
        return INT_VAL(secure);
    }
    int64_t conn = platform_tcp_connect(host, (int)AS_INT(a[1]), timeoutMs);
    if (conn == PLATFORM_SOCKET_NONE) {
        vm_throw_native(vm, "SkillIssue", "couldn't get through to %s:%lld.", host, (long long)AS_INT(a[1]));
        return GHOST_VAL;
    }
    return INT_VAL(conn);
}

/* internet.hold_up(handle, timeout_ms?) -- an `otw` that settles `fax` when
   that socket has something to read (for a listener: somebody waiting to be
   accepted), or `cap` if the timeout runs out first.
 *
 * This is the difference between a server that answers one caller at a time
 * and one that answers several. A blocking read stops the whole thread,
 * including every other task; awaiting this stops only the task that asked,
 * and the event loop polls every socket anybody is waiting on in a single
 * call. Shared state stays safe because it is still one thread -- there is
 * nothing to lock.
 *
 *     bruh (fax) {
 *         sus (await_fr internet.hold_up(listener, 1000)) {
 *             serve(internet.next_customer(listener, 0))   // a task
 *         }
 *     }
 */
static Value m_hold_up(VM *vm, Value *a, int argc) {
    int64_t sock = handle_arg(vm, a[0], "hold_up");
    if (vm->hadError) return GHOST_VAL;
    double deadline = 0.0;
    if (argc > 1 && !IS_GHOST(a[1])) {
        int ms = int_opt(a[1], -1);
        if (ms >= 0) deadline = platform_monotonic_seconds() + (double)ms / 1000.0;
    }
    return OBJ_VAL(otw_for_socket(&vm->gc, sock, deadline));
}

/* internet.handshake(conn) -- one step of a server-side TLS handshake.
   `fax` when it is done (and at once for a plain connection), `cap` when it
   needs to hear from the client first: await `hold_up(conn)` and call again.
   A failure -- a client offering only TLS 1.0, or plain HTTP sent to the TLS
   port -- is a SkillIssue naming what went wrong. */
static Value m_handshake(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t conn = handle_arg(vm, a[0], "handshake");
    if (vm->hadError) return GHOST_VAL;
    char errbuf[512];
    int r = platform_tls_handshake(conn, errbuf, sizeof errbuf);
    if (r < 0) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf[0] != '\0' ? errbuf : "the TLS handshake failed.");
        return GHOST_VAL;
    }
    return BOOL_VAL(r == 1);
}

/* internet.tls_info(conn) -- {"version", "cipher"} once the handshake is
   done, `ghost` for a plain connection. */
static Value m_tls_info(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t conn = handle_arg(vm, a[0], "tls_info");
    if (vm->hadError) return GHOST_VAL;
    char version[64];
    char cipher[128];
    if (!platform_tls_info(conn, version, sizeof version, cipher, sizeof cipher)) return GHOST_VAL;
    ObjGroupChat *out = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(out));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "version", 7)),
                  OBJ_VAL(string_new(&vm->gc, version, (uint32_t)strlen(version))));
    groupchat_set(&vm->gc, out, OBJ_VAL(string_new(&vm->gc, "cipher", 6)),
                  OBJ_VAL(string_new(&vm->gc, cipher, (uint32_t)strlen(cipher))));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(out);
}

/* internet.shop_port(listener) -- which port it actually got. Only
   interesting after `open_shop(0)`, which is how you bind without gambling on
   a fixed number being free. */
static Value m_shop_port(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t listener = handle_arg(vm, a[0], "shop_port");
    if (vm->hadError) return GHOST_VAL;
    int port = platform_socket_port(listener);
    if (port < 0) {
        vm_throw_native(vm, "SkillIssue", "that listener isn't listening.");
        return GHOST_VAL;
    }
    return INT_VAL(port);
}

static Value m_close_shop(VM *vm, Value *a, int argc) {
    (void)argc;
    int64_t listener = handle_arg(vm, a[0], "close_shop");
    if (vm->hadError) return GHOST_VAL;
    platform_socket_close(listener);
    return GHOST_VAL;
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
    /* The listening half. */
    {"open_shop", m_open_shop, 1, 2},
    {"open_secure_shop", m_open_secure_shop, 3, 3},
    {"handshake", m_handshake, 1, 1},
    {"tls_info", m_tls_info, 1, 1},
    {"slide_into", m_slide_into, 2, 3},
    {"next_customer", m_next_customer, 1, 2},
    {"hold_up", m_hold_up, 1, 2},
    {"hear_them_out", m_hear_them_out, 1, 4},
    {"holler_back", m_holler_back, 2, 2},
    {"shop_port", m_shop_port, 1, 1},
    {"kick_out", m_kick_out, 1, 1},
    {"close_shop", m_close_shop, 1, 1},
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
