/* native/portable.c -- see portable.h. */
#include "portable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum.h"
#include "da_string.h"
#include "gc.h"
#include "groupchat.h"
#include "object.h"
#include "stash.h"
#include "vm.h"

/* Objects currently being copied, innermost last. A container that contains
   itself would otherwise recurse until the C stack gives out -- the same
   failure the display path solved with `[...]`, except there is no honest
   `[...]` to hand another heap. Depth is the nesting depth of the value, so
   a fixed array would be a guess; this grows. */
typedef struct {
    Obj **seen;
    int count;
    int capacity;
} CopyTrail;

static bool trail_contains(const CopyTrail *t, Obj *obj) {
    for (int i = 0; i < t->count; i++) {
        if (t->seen[i] == obj) return true;
    }
    return false;
}

static void trail_push(CopyTrail *t, Obj *obj) {
    if (t->count == t->capacity) {
        t->capacity = t->capacity == 0 ? 8 : t->capacity * 2;
        t->seen = (Obj **)realloc(t->seen, (size_t)t->capacity * sizeof(Obj *));
    }
    t->seen[t->count++] = obj;
}

static void trail_pop(CopyTrail *t) {
    if (t->count > 0) t->count--;
}

static PortableValue *pv_new(PortableKind kind) {
    PortableValue *pv = (PortableValue *)calloc(1, sizeof(PortableValue));
    pv->kind = kind;
    return pv;
}

static PortableValue *copy_value(Value v, CopyTrail *trail, char *errbuf, size_t errbuf_len);

static PortableValue *copy_stash(ObjStash *s, CopyTrail *trail, char *errbuf, size_t errbuf_len) {
    PortableValue *pv = pv_new(PV_STASH);
    pv->as.list.count = (uint32_t)s->count;
    pv->as.list.items = (PortableValue **)calloc(s->count > 0 ? (size_t)s->count : 1,
                                                 sizeof(PortableValue *));
    for (int i = 0; i < s->count; i++) {
        PortableValue *item = copy_value(s->items[i], trail, errbuf, errbuf_len);
        if (item == NULL) {
            portable_free(pv);
            return NULL;
        }
        pv->as.list.items[i] = item;
    }
    return pv;
}

static PortableValue *copy_groupchat(ObjGroupChat *g, CopyTrail *trail, char *errbuf, size_t errbuf_len) {
    PortableValue *pv = pv_new(PV_GROUPCHAT);
    pv->as.map.count = (uint32_t)g->count;
    size_t slots = g->count > 0 ? (size_t)g->count : 1;
    pv->as.map.keys = (PortableValue **)calloc(slots, sizeof(PortableValue *));
    pv->as.map.values = (PortableValue **)calloc(slots, sizeof(PortableValue *));
    for (int i = 0; i < g->count; i++) {
        PortableValue *k = copy_value(g->entries[i].key, trail, errbuf, errbuf_len);
        if (k == NULL) {
            portable_free(pv);
            return NULL;
        }
        pv->as.map.keys[i] = k;
        PortableValue *val = copy_value(g->entries[i].value, trail, errbuf, errbuf_len);
        if (val == NULL) {
            portable_free(pv);
            return NULL;
        }
        pv->as.map.values[i] = val;
    }
    return pv;
}

static PortableValue *copy_value(Value v, CopyTrail *trail, char *errbuf, size_t errbuf_len) {
    if (IS_GHOST(v)) return pv_new(PV_GHOST);
    if (IS_BOOL(v)) {
        PortableValue *pv = pv_new(PV_BOOL);
        pv->as.boolean = AS_BOOL(v);
        return pv;
    }
    if (IS_INT(v)) {
        PortableValue *pv = pv_new(PV_INT);
        pv->as.integer = AS_INT(v);
        return pv;
    }
    if (IS_FLOAT(v)) {
        PortableValue *pv = pv_new(PV_FLOAT);
        pv->as.number = AS_FLOAT(v);
        return pv;
    }
    if (IS_BIGNUM(v)) {
        PortableValue *pv = pv_new(PV_BIGNUM);
        pv->as.bignum = bignum_copy(AS_BIGNUM(v));
        return pv;
    }
    if (IS_STRING(v)) {
        ObjString *s = AS_STRING(v);
        PortableValue *pv = pv_new(PV_STRING);
        pv->as.string.len = s->byteLen;
        pv->as.string.bytes = (char *)malloc(s->byteLen > 0 ? s->byteLen : 1);
        memcpy(pv->as.string.bytes, s->chars, s->byteLen);
        return pv;
    }

    if (IS_OBJ(v)) {
        Obj *obj = AS_OBJ(v);
        if (obj->type == OBJ_STASH || obj->type == OBJ_GROUPCHAT) {
            if (trail_contains(trail, obj)) {
                snprintf(errbuf, errbuf_len,
                         "that %s contains itself, so it can't be handed to an intern.",
                         obj->type == OBJ_STASH ? "stash" : "groupchat");
                return NULL;
            }
            trail_push(trail, obj);
            PortableValue *pv = obj->type == OBJ_STASH
                                    ? copy_stash((ObjStash *)obj, trail, errbuf, errbuf_len)
                                    : copy_groupchat((ObjGroupChat *)obj, trail, errbuf, errbuf_len);
            trail_pop(trail);
            return pv;
        }
    }

    /* Everything else references a heap, and there is no second copy of that
       heap to reference. Name the type: someone who just handed a closure to
       a thread needs to know which of their arguments was the problem. */
    const char *name = vm_type_name(v);
    /* "an otw", "an error", "an iterator" -- the type names are the ones a
       user typed, so the sentence should read like one. */
    const char *article = strchr("aeiouAEIOU", name[0]) != NULL && name[0] != '\0' ? "an" : "a";
    snprintf(errbuf, errbuf_len,
             "%s %s can't be handed to an intern -- it belongs to this heap. "
             "ghost, boolski, numba, yapstring, and stash/groupchat of those can cross.",
             article, name);
    return NULL;
}

PortableValue *portable_from_value(Value v, char *errbuf, size_t errbuf_len) {
    CopyTrail trail = {NULL, 0, 0};
    PortableValue *pv = copy_value(v, &trail, errbuf, errbuf_len);
    free(trail.seen);
    return pv;
}

Value portable_to_value(VM *vm, const PortableValue *pv) {
    if (pv == NULL) return GHOST_VAL;
    switch (pv->kind) {
        case PV_GHOST: return GHOST_VAL;
        case PV_BOOL: return BOOL_VAL(pv->as.boolean);
        case PV_INT: return INT_VAL(pv->as.integer);
        case PV_FLOAT: return FLOAT_VAL(pv->as.number);
        case PV_BIGNUM: {
            /* A fresh copy, tracked by *this* heap's collector -- the one in
               the PortableValue stays owned by the PortableValue. */
            ObjBignum *n = bignum_copy(pv->as.bignum);
            gc_track(&vm->gc, (Obj *)n, sizeof(ObjBignum));
            return OBJ_VAL(n);
        }
        case PV_STRING:
            return OBJ_VAL(string_new(&vm->gc, pv->as.string.bytes, pv->as.string.len));
        case PV_STASH: {
            ObjStash *s = stash_new(&vm->gc, NULL, 0);
            gc_push_temp(&vm->gc, OBJ_VAL(s));
            for (uint32_t i = 0; i < pv->as.list.count; i++) {
                Value item = portable_to_value(vm, pv->as.list.items[i]);
                gc_push_temp(&vm->gc, item);
                stash_push(&vm->gc, s, item);
                gc_pop_temp(&vm->gc);
            }
            gc_pop_temp(&vm->gc);
            return OBJ_VAL(s);
        }
        case PV_GROUPCHAT: {
            ObjGroupChat *g = groupchat_new(&vm->gc, NULL, 0);
            gc_push_temp(&vm->gc, OBJ_VAL(g));
            for (uint32_t i = 0; i < pv->as.map.count; i++) {
                Value k = portable_to_value(vm, pv->as.map.keys[i]);
                gc_push_temp(&vm->gc, k);
                Value val = portable_to_value(vm, pv->as.map.values[i]);
                gc_push_temp(&vm->gc, val);
                groupchat_set(&vm->gc, g, k, val);
                gc_pop_temp(&vm->gc);
                gc_pop_temp(&vm->gc);
            }
            gc_pop_temp(&vm->gc);
            return OBJ_VAL(g);
        }
    }
    return GHOST_VAL;
}

void portable_free(PortableValue *pv) {
    if (pv == NULL) return;
    switch (pv->kind) {
        case PV_BIGNUM:
            bignum_free(pv->as.bignum);
            break;
        case PV_STRING:
            free(pv->as.string.bytes);
            break;
        case PV_STASH:
            for (uint32_t i = 0; i < pv->as.list.count; i++) portable_free(pv->as.list.items[i]);
            free(pv->as.list.items);
            break;
        case PV_GROUPCHAT:
            for (uint32_t i = 0; i < pv->as.map.count; i++) {
                portable_free(pv->as.map.keys[i]);
                portable_free(pv->as.map.values[i]);
            }
            free(pv->as.map.keys);
            free(pv->as.map.values);
            break;
        default:
            break;
    }
    free(pv);
}
