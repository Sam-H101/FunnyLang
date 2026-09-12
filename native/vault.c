/* native/vault.c -- see vault.h. */
#include "vault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blob.h"
#include "da_string.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "platform.h"
#include "value.h"
#include "vm.h"

/* OWASP's 2023 floor for PBKDF2-HMAC-SHA256. Stored alongside every hash, so
   raising it later leaves every existing password still checkable. */
#define DEFAULT_ITERATIONS 600000
#define SALT_BYTES 16
#define HASH_BYTES 32
#define KEY_BYTES 32
#define NONCE_BYTES 12
#define TAG_BYTES 16

/* -- arguments ------------------------------------------------------------ */

/* A yapstring or a blob, as bytes. Both are byte sequences at this level, and
   refusing one of them would mean every caller converting by hand. */
static bool bytes_of(VM *vm, Value v, const char *fnName, const unsigned char **out, size_t *len) {
    if (IS_STRING(v)) {
        *out = (const unsigned char *)AS_STRING(v)->chars;
        *len = AS_STRING(v)->byteLen;
        return true;
    }
    if (IS_BLOB(v)) {
        *out = AS_BLOB(v)->bytes;
        *len = AS_BLOB(v)->byteLen;
        return true;
    }
    vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a yapstring or a blob, not a %s.", fnName, vm_type_name(v));
    return false;
}

/* A key is 32 bytes and nothing else. A short key is the kind of mistake that
   silently weakens everything built on it, so it is an error rather than
   something quietly padded. */
static bool key_of(VM *vm, Value v, const char *fnName, const unsigned char **out) {
    size_t len = 0;
    if (!bytes_of(vm, v, fnName, out, &len)) return false;
    if (len != KEY_BYTES) {
        vm_throw_native(vm, "TypeVibeMismatch", "'%s' needs a %d-byte key (vault.new_key() makes one), not %d bytes.",
                        fnName, KEY_BYTES, (int)len);
        return false;
    }
    return true;
}

static int iterations_arg(VM *vm, Value *a, int argc, int index) {
    if (argc <= index || IS_GHOST(a[index])) return DEFAULT_ITERATIONS;
    if (!IS_INT(a[index]) || AS_INT(a[index]) < 1) {
        vm_throw_native(vm, "TypeVibeMismatch", "iterations has to be a numba of at least 1.");
        return -1;
    }
    return (int)AS_INT(a[index]);
}

/* -- constant time --------------------------------------------------------- */

/* Every byte is looked at every time. Returning early on the first difference
   is the classic way to leak a secret one byte at a time: the time a compare
   takes tells an attacker how much of their guess was right. */
static bool same_bytes_constant_time(const unsigned char *a, size_t aLen, const unsigned char *b, size_t bLen) {
    if (aLen != bLen) return false;
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < aLen; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

/* -- passwords ------------------------------------------------------------- */

static Value m_hash_password(VM *vm, Value *a, int argc) {
    const unsigned char *password;
    size_t passwordLen;
    if (!bytes_of(vm, a[0], "hash_password", &password, &passwordLen)) return GHOST_VAL;
    int iterations = iterations_arg(vm, a, argc, 1);
    if (iterations < 0) return GHOST_VAL;

    unsigned char salt[SALT_BYTES];
    if (!platform_random_bytes(salt, sizeof salt)) {
        vm_throw_native(vm, "SkillIssue", "couldn't get any randomness from the OS for a salt.");
        return GHOST_VAL;
    }
    unsigned char hash[HASH_BYTES];
    char errbuf[256];
    if (!platform_pbkdf2_sha256(password, passwordLen, salt, sizeof salt, iterations, hash, sizeof hash, errbuf,
                                sizeof errbuf)) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }

    uint32_t saltB64Len = 0, hashB64Len = 0;
    char *saltB64 = blob_base64_encode(salt, sizeof salt, &saltB64Len);
    char *hashB64 = blob_base64_encode(hash, sizeof hash, &hashB64Len);
    char out[256];
    int n = snprintf(out, sizeof out, "pbkdf2-sha256$%d$%s$%s", iterations, saltB64, hashB64);
    free(saltB64);
    free(hashB64);
    return OBJ_VAL(string_new(&vm->gc, out, (uint32_t)n));
}

/* A login must not leak *why* it failed, so every malformed-stored-string
   case answers `cap` exactly like a wrong password does. */
static Value m_check_password(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *password;
    size_t passwordLen;
    if (!bytes_of(vm, a[0], "check_password", &password, &passwordLen)) return GHOST_VAL;
    if (!IS_STRING(a[1])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'check_password' needs the stored yapstring, not a %s.",
                        vm_type_name(a[1]));
        return GHOST_VAL;
    }
    const char *stored = AS_STRING(a[1])->chars;

    /* "pbkdf2-sha256$<iters>$<salt>$<hash>", parsed by hand: strtok's cursor
       is shared by every thread in the process. */
    if (strncmp(stored, "pbkdf2-sha256$", 14) != 0) return BOOL_VAL(false);
    const char *p = stored + 14;
    char *end = NULL;
    long iterations = strtol(p, &end, 10);
    if (end == p || *end != '$' || iterations < 1 || iterations > 100000000L) return BOOL_VAL(false);
    const char *saltStart = end + 1;
    const char *saltEnd = strchr(saltStart, '$');
    if (saltEnd == NULL) return BOOL_VAL(false);
    const char *hashStart = saltEnd + 1;
    if (strchr(hashStart, '$') != NULL) return BOOL_VAL(false);

    uint32_t saltLen = 0, wantLen = 0;
    unsigned char *salt = blob_base64_decode(saltStart, (uint32_t)(saltEnd - saltStart), &saltLen);
    if (salt == NULL) return BOOL_VAL(false);
    unsigned char *want = blob_base64_decode(hashStart, (uint32_t)strlen(hashStart), &wantLen);
    if (want == NULL || wantLen == 0 || wantLen > 64) {
        free(salt);
        free(want);
        return BOOL_VAL(false);
    }

    unsigned char got[64];
    char errbuf[256];
    bool ok = platform_pbkdf2_sha256(password, passwordLen, salt, saltLen, (int)iterations, got, wantLen, errbuf,
                                     sizeof errbuf);
    bool same = ok && same_bytes_constant_time(got, wantLen, want, wantLen);
    free(salt);
    free(want);
    if (!ok) {
        /* A machine with no crypto library cannot check a password, and
           answering `cap` would read as "wrong password". */
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    return BOOL_VAL(same);
}

static Value m_derive_key(VM *vm, Value *a, int argc) {
    const unsigned char *password, *salt;
    size_t passwordLen, saltLen;
    if (!bytes_of(vm, a[0], "derive_key", &password, &passwordLen)) return GHOST_VAL;
    if (!bytes_of(vm, a[1], "derive_key", &salt, &saltLen)) return GHOST_VAL;
    int iterations = iterations_arg(vm, a, argc, 2);
    if (iterations < 0) return GHOST_VAL;

    unsigned char key[KEY_BYTES];
    char errbuf[256];
    if (!platform_pbkdf2_sha256(password, passwordLen, salt, saltLen, iterations, key, sizeof key, errbuf,
                                sizeof errbuf)) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    return OBJ_VAL(blob_new(&vm->gc, key, KEY_BYTES));
}

static Value m_new_key(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    unsigned char key[KEY_BYTES];
    if (!platform_random_bytes(key, sizeof key)) {
        vm_throw_native(vm, "SkillIssue", "couldn't get any randomness from the OS for a key.");
        return GHOST_VAL;
    }
    return OBJ_VAL(blob_new(&vm->gc, key, KEY_BYTES));
}

/* -- sealing --------------------------------------------------------------- */

static Value m_seal(VM *vm, Value *a, int argc) {
    const unsigned char *plain;
    size_t plainLen;
    if (!bytes_of(vm, a[0], "seal", &plain, &plainLen)) return GHOST_VAL;
    /* Which of the two it was is recorded in the version tag, so `unseal`
       hands back the same type that went in. */
    bool wasText = IS_STRING(a[0]);
    const unsigned char *key;
    if (!key_of(vm, a[1], "seal", &key)) return GHOST_VAL;
    const unsigned char *aad = NULL;
    size_t aadLen = 0;
    if (argc > 2 && !IS_GHOST(a[2])) {
        if (!bytes_of(vm, a[2], "seal", &aad, &aadLen)) return GHOST_VAL;
    }

    unsigned char nonce[NONCE_BYTES];
    if (!platform_random_bytes(nonce, sizeof nonce)) {
        vm_throw_native(vm, "SkillIssue", "couldn't get any randomness from the OS for a nonce.");
        return GHOST_VAL;
    }

    /* Ciphertext and tag travel as one base64 field: they are always used
       together, and splitting them would invite somebody to keep one. */
    unsigned char *body = (unsigned char *)malloc(plainLen + TAG_BYTES + 1);
    char errbuf[256];
    if (!platform_aes_gcm_seal(key, nonce, aad, aadLen, plain, plainLen, body, body + plainLen, errbuf,
                               sizeof errbuf)) {
        free(body);
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }

    uint32_t nonceB64Len = 0, bodyB64Len = 0;
    char *nonceB64 = blob_base64_encode(nonce, sizeof nonce, &nonceB64Len);
    char *bodyB64 = blob_base64_encode(body, (uint32_t)(plainLen + TAG_BYTES), &bodyB64Len);
    free(body);

    size_t outLen = 8 + nonceB64Len + bodyB64Len;
    char *out = (char *)malloc(outLen);
    int n = snprintf(out, outLen, "%s$%s$%s", wasText ? "v1" : "v1b", nonceB64, bodyB64);
    free(nonceB64);
    free(bodyB64);
    Value result = OBJ_VAL(string_new(&vm->gc, out, (uint32_t)n));
    free(out);
    return result;
}

static Value m_unseal(VM *vm, Value *a, int argc) {
    if (!IS_STRING(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'unseal' needs the sealed yapstring, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    const unsigned char *key;
    if (!key_of(vm, a[1], "unseal", &key)) return GHOST_VAL;
    const unsigned char *aad = NULL;
    size_t aadLen = 0;
    if (argc > 2 && !IS_GHOST(a[2])) {
        if (!bytes_of(vm, a[2], "unseal", &aad, &aadLen)) return GHOST_VAL;
    }

    /* One message for every way this can fail, on purpose: which part was
       wrong is exactly what an attacker probing a decryption oracle wants
       told. */
    const char *WONT_OPEN = "that sealed value won't open.";
    const char *sealed = AS_STRING(a[0])->chars;
    bool wasText;
    const char *p;
    if (strncmp(sealed, "v1$", 3) == 0) {
        wasText = true;
        p = sealed + 3;
    } else if (strncmp(sealed, "v1b$", 4) == 0) {
        wasText = false;
        p = sealed + 4;
    } else {
        vm_throw_native(vm, "SkillIssue", "%s", WONT_OPEN);
        return GHOST_VAL;
    }

    const char *nonceEnd = strchr(p, '$');
    if (nonceEnd == NULL || strchr(nonceEnd + 1, '$') != NULL) {
        vm_throw_native(vm, "SkillIssue", "%s", WONT_OPEN);
        return GHOST_VAL;
    }
    uint32_t nonceLen = 0, bodyLen = 0;
    unsigned char *nonce = blob_base64_decode(p, (uint32_t)(nonceEnd - p), &nonceLen);
    unsigned char *body = blob_base64_decode(nonceEnd + 1, (uint32_t)strlen(nonceEnd + 1), &bodyLen);
    if (nonce == NULL || body == NULL || nonceLen != NONCE_BYTES || bodyLen < TAG_BYTES) {
        free(nonce);
        free(body);
        vm_throw_native(vm, "SkillIssue", "%s", WONT_OPEN);
        return GHOST_VAL;
    }

    size_t plainLen = bodyLen - TAG_BYTES;
    unsigned char *plain = (unsigned char *)malloc(plainLen + 1);
    char errbuf[256];
    bool ok = platform_aes_gcm_open(key, nonce, aad, aadLen, body, plainLen, body + plainLen, plain, errbuf,
                                    sizeof errbuf);
    free(nonce);
    free(body);
    if (!ok) {
        free(plain);
        /* errbuf may name a missing library, which is worth saying; every
           other failure is the one sentence above. */
        vm_throw_native(vm, "SkillIssue", "%s", errbuf[0] != '\0' ? errbuf : WONT_OPEN);
        return GHOST_VAL;
    }
    Value result = wasText ? OBJ_VAL(string_new(&vm->gc, (const char *)plain, (uint32_t)plainLen))
                           : OBJ_VAL(blob_new(&vm->gc, plain, (uint32_t)plainLen));
    free(plain);
    return result;
}

/* -- digests --------------------------------------------------------------- */

static Value m_sha256(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *data;
    size_t len;
    if (!bytes_of(vm, a[0], "sha256", &data, &len)) return GHOST_VAL;
    unsigned char digest[32];
    char errbuf[256];
    if (!platform_sha256(data, len, digest, errbuf, sizeof errbuf)) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    char *hex = blob_hex_encode(digest, sizeof digest);
    Value out = OBJ_VAL(string_new(&vm->gc, hex, 64));
    free(hex);
    return out;
}

/* SHA-1 is here for exactly one reason, and the documentation says so: RFC
   6455's WebSocket handshake computes Sec-WebSocket-Accept as
   base64(sha1(key + GUID)), and that is not negotiable by either end. It is
   not offered as a general-purpose digest -- SHA-1 is broken against
   collisions, `sha256` is one line away, and a program reaching for this
   without a protocol demanding it has picked the wrong one. */
static Value m_sha1(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *data;
    size_t len;
    if (!bytes_of(vm, a[0], "sha1", &data, &len)) return GHOST_VAL;
    unsigned char digest[20];
    char errbuf[256];
    if (!platform_sha1(data, len, digest, errbuf, sizeof errbuf)) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    char *hex = blob_hex_encode(digest, sizeof digest);
    Value out = OBJ_VAL(string_new(&vm->gc, hex, 40));
    free(hex);
    return out;
}

static Value m_hmac_sha256(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *key, *data;
    size_t keyLen, len;
    if (!bytes_of(vm, a[0], "hmac_sha256", &key, &keyLen)) return GHOST_VAL;
    if (!bytes_of(vm, a[1], "hmac_sha256", &data, &len)) return GHOST_VAL;
    unsigned char digest[32];
    char errbuf[256];
    if (!platform_hmac_sha256(key, keyLen, data, len, digest, errbuf, sizeof errbuf)) {
        vm_throw_native(vm, "SkillIssue", "%s", errbuf);
        return GHOST_VAL;
    }
    char *hex = blob_hex_encode(digest, sizeof digest);
    Value out = OBJ_VAL(string_new(&vm->gc, hex, 64));
    free(hex);
    return out;
}

static Value m_same_secret(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *x, *y;
    size_t xLen, yLen;
    if (!bytes_of(vm, a[0], "same_secret", &x, &xLen)) return GHOST_VAL;
    if (!bytes_of(vm, a[1], "same_secret", &y, &yLen)) return GHOST_VAL;
    return BOOL_VAL(same_bytes_constant_time(x, xLen, y, yLen));
}

/* -- base64 ---------------------------------------------------------------- */

static Value m_base64_encode(VM *vm, Value *a, int argc) {
    (void)argc;
    const unsigned char *data;
    size_t len;
    if (!bytes_of(vm, a[0], "base64_encode", &data, &len)) return GHOST_VAL;
    uint32_t outLen = 0;
    char *text = blob_base64_encode(data, (uint32_t)len, &outLen);
    Value out = OBJ_VAL(string_new(&vm->gc, text, outLen));
    free(text);
    return out;
}

/* `ghost` rather than an error on malformed input: decoding something that
   arrived from outside is a question, not an assertion. */
static Value m_base64_decode(VM *vm, Value *a, int argc) {
    (void)argc;
    if (!IS_STRING(a[0])) {
        vm_throw_native(vm, "TypeVibeMismatch", "'base64_decode' needs a yapstring, not a %s.", vm_type_name(a[0]));
        return GHOST_VAL;
    }
    ObjString *s = AS_STRING(a[0]);
    uint32_t len = 0;
    unsigned char *bytes = blob_base64_decode(s->chars, s->byteLen, &len);
    if (bytes == NULL) return GHOST_VAL;
    Value out = OBJ_VAL(blob_new(&vm->gc, bytes, len));
    free(bytes);
    return out;
}

/* -- the module ------------------------------------------------------------ */

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} VaultEntry;

static const VaultEntry VAULT_FUNCTIONS[] = {
    {"hash_password", m_hash_password, 1, 2},
    {"check_password", m_check_password, 2, 2},
    {"new_key", m_new_key, 0, 0},
    {"derive_key", m_derive_key, 2, 3},
    {"seal", m_seal, 2, 3},
    {"unseal", m_unseal, 2, 3},
    {"sha256", m_sha256, 1, 1},
    {"sha1", m_sha1, 1, 1},
    {"hmac_sha256", m_hmac_sha256, 2, 2},
    {"same_secret", m_same_secret, 2, 2},
    {"base64_encode", m_base64_encode, 1, 1},
    {"base64_decode", m_base64_decode, 1, 1},
};
#define VAULT_FUNCTIONS_COUNT (int)(sizeof(VAULT_FUNCTIONS) / sizeof(VAULT_FUNCTIONS[0]))

Value vault_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < VAULT_FUNCTIONS_COUNT; i++) {
        const VaultEntry *e = &VAULT_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "vault", 5);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
