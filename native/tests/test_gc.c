/* native/tests/test_gc.c -- exercises gc.c's mark-sweep cycle directly,
 * since no real VM exists yet (N2+) to drive it through normal opcodes.
 * Uses temp roots as the only root source (no external-roots callback
 * needed for these cases) plus a synthetic external-roots callback to
 * prove that mechanism works too, ahead of N2 actually registering one.
 */
/* setenv/unsetenv (POSIX, used only by this *test*, to exercise gc_init's
   getenv("FUNNY_GC_STRESS") check) aren't in strict C11 -- opt into POSIX
   before any system header is included. gc.c itself only ever calls
   getenv, which is plain C11 and needs none of this. */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../gc.h"
#include "../bignum.h"

static Obj *track_bignum(GC *gc, int64_t v) {
    ObjBignum *n = bignum_from_int64(v);
    return gc_track(gc, (Obj *)n, sizeof(ObjBignum));
}

static int count_objects(const GC *gc) {
    int n = 0;
    for (Obj *o = gc->objects; o != NULL; o = o->next) n++;
    return n;
}

static void test_unreferenced_object_is_swept(void) {
    GC gc;
    gc_init(&gc);
    track_bignum(&gc, 42); /* nothing roots this */
    assert(count_objects(&gc) == 1);
    gc_collect(&gc);
    assert(count_objects(&gc) == 0);
    assert(gc.bytesAllocated == 0);
    gc_destroy(&gc);
}

static void test_temp_root_keeps_object_alive(void) {
    GC gc;
    gc_init(&gc);
    Obj *o = track_bignum(&gc, 7);
    gc_push_temp(&gc, OBJ_VAL(o));
    gc_collect(&gc);
    assert(count_objects(&gc) == 1); /* still rooted */
    gc_pop_temp(&gc);
    gc_collect(&gc);
    assert(count_objects(&gc) == 0); /* now unrooted, swept */
    gc_destroy(&gc);
}

static void test_temp_root_balance(void) {
    GC gc;
    gc_init(&gc);
    assert(gc_temp_count(&gc) == 0);
    gc_push_temp(&gc, INT_VAL(1));
    gc_push_temp(&gc, INT_VAL(2));
    assert(gc_temp_count(&gc) == 2);
    gc_pop_temp(&gc);
    assert(gc_temp_count(&gc) == 1);
    gc_pop_temp(&gc);
    assert(gc_temp_count(&gc) == 0);
    gc_destroy(&gc);
}

/* Simulates what N2's VM will do: register a callback marking its own
   roots (here, a plain array standing in for "the value stack"). */
typedef struct {
    Value *fakeStack;
    int count;
} FakeVmRoots;

static void mark_fake_vm_roots(GC *gc, void *userdata) {
    FakeVmRoots *roots = (FakeVmRoots *)userdata;
    for (int i = 0; i < roots->count; i++) {
        gc_mark_value(gc, roots->fakeStack[i]);
    }
}

static void test_external_roots_callback(void) {
    GC gc;
    gc_init(&gc);
    Obj *rooted = track_bignum(&gc, 100);
    Obj *unrooted = track_bignum(&gc, 200);
    (void)unrooted;

    Value fakeStack[1] = { OBJ_VAL(rooted) };
    FakeVmRoots roots = { fakeStack, 1 };
    gc.markExternalRoots = mark_fake_vm_roots;
    gc.externalRootsUserdata = &roots;

    assert(count_objects(&gc) == 2);
    gc_collect(&gc);
    assert(count_objects(&gc) == 1); /* only the externally-rooted one survives */
    gc_destroy(&gc);
}

static void test_gc_stress_mode_via_env(void) {
    setenv("FUNNY_GC_STRESS", "1", 1);
    GC gc;
    gc_init(&gc);
    assert(gc.stressMode);
    unsetenv("FUNNY_GC_STRESS");
    gc_destroy(&gc);
}

static void test_repeated_collections_dont_corrupt_state(void) {
    GC gc;
    gc_init(&gc);
    Obj *o = track_bignum(&gc, 999);
    gc_push_temp(&gc, OBJ_VAL(o));
    for (int i = 0; i < 50; i++) {
        track_bignum(&gc, i); /* garbage each round */
        gc_collect(&gc);
        assert(count_objects(&gc) == 1); /* only the rooted one ever survives */
    }
    gc_destroy(&gc);
}

int main(void) {
    test_unreferenced_object_is_swept();
    test_temp_root_keeps_object_alive();
    test_temp_root_balance();
    test_external_roots_callback();
    test_gc_stress_mode_via_env();
    test_repeated_collections_dont_corrupt_state();
    printf("test_gc: all tests passed\n");
    return 0;
}
