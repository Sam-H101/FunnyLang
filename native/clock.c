#include "clock.h"

#include <string.h>

#include "bignum.h"
#include "gc.h"
#include "groupchat.h"
#include "modules.h"
#include "platform.h"
#include "da_string.h"
#include "vm.h"

static Value m_now(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return FLOAT_VAL(platform_now_seconds());
}

static Value m_now_ms(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)a;
    (void)argc;
    return INT_VAL((int64_t)(platform_now_seconds() * 1000.0));
}

static Value m_touch_grass(VM *vm, Value *a, int argc) {
    double seconds = 0.0;
    if (argc >= 1) {
        if (!IS_NUM(a[0])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'touch_grass' needs a numba, not a %s.", vm_type_name(a[0]));
            return GHOST_VAL;
        }
        seconds = IS_FLOAT(a[0]) ? AS_FLOAT(a[0]) : (IS_INT(a[0]) ? (double)AS_INT(a[0]) : bignum_to_double(AS_BIGNUM(a[0])));
    }
    platform_sleep_seconds(seconds);
    return GHOST_VAL;
}

/* `bn->receiver` holds the FLOAT_VAL start time stopwatch() captured;
   call_bound_native always prepends it as args[0], the same convention
   an ordinary receiver-bound method uses -- repurposed here to give a
   plain native function its own per-instance captured state, since
   NativeMethodFn itself has no closure/userdata slot. */
static Value m_elapsed(VM *vm, Value *a, int argc) {
    (void)vm;
    (void)argc;
    double start = AS_FLOAT(a[0]);
    return FLOAT_VAL(platform_monotonic_seconds() - start);
}

static Value m_stopwatch(VM *vm, Value *a, int argc) {
    (void)a;
    (void)argc;
    double start = platform_monotonic_seconds();
    return OBJ_VAL(bound_native_new(&vm->gc, FLOAT_VAL(start), m_elapsed, "elapsed", 0, 0));
}

static Value m_date_yap(VM *vm, Value *a, int argc) {
    const char *fmt = "%Y-%m-%d %H:%M:%S";
    if (argc >= 1 && !IS_GHOST(a[0])) {
        if (!IS_STRING(a[0])) {
            vm_throw_native(vm, "TypeVibeMismatch", "'date_yap' needs a yapstring format, not a %s.",
                             vm_type_name(a[0]));
            return GHOST_VAL;
        }
        fmt = AS_STRING(a[0])->chars;
    }
    char buf[256];
    size_t n = platform_strftime_now(fmt, buf, sizeof(buf));
    return OBJ_VAL(string_new(&vm->gc, buf, (uint32_t)n));
}

typedef struct {
    const char *name;
    NativeMethodFn fn;
    int minArity;
    int maxArity;
} ClockEntry;

static const ClockEntry CLOCK_FUNCTIONS[] = {
    {"now", m_now, 0, 0},
    {"now_ms", m_now_ms, 0, 0},
    {"touch_grass", m_touch_grass, 0, 1},
    {"stopwatch", m_stopwatch, 0, 0},
    {"date_yap", m_date_yap, 0, 1},
};
#define CLOCK_FUNCTIONS_COUNT (int)(sizeof(CLOCK_FUNCTIONS) / sizeof(CLOCK_FUNCTIONS[0]))

Value clock_build(VM *vm) {
    ObjGroupChat *members = groupchat_new(&vm->gc, NULL, 0);
    gc_push_temp(&vm->gc, OBJ_VAL(members));
    for (int i = 0; i < CLOCK_FUNCTIONS_COUNT; i++) {
        const ClockEntry *e = &CLOCK_FUNCTIONS[i];
        ObjString *name = string_new(&vm->gc, e->name, (uint32_t)strlen(e->name));
        ObjNativeFn *fn = native_fn_new(&vm->gc, e->fn, name->chars, e->minArity, e->maxArity);
        groupchat_set(&vm->gc, members, OBJ_VAL(name), OBJ_VAL(fn));
    }
    ObjString *moduleName = string_new(&vm->gc, "clock", 5);
    ObjModule *mod = module_new(&vm->gc, moduleName, OBJ_VAL(members));
    gc_pop_temp(&vm->gc);
    return OBJ_VAL(mod);
}
