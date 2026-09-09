#include <assert.h>
#include <stdio.h>

#include "../value.h"
#include "../bignum.h"

static void test_basic_equality(void) {
    assert(value_equal_narrow(GHOST_VAL, GHOST_VAL));
    assert(!value_equal_narrow(GHOST_VAL, INT_VAL(0)));
    assert(value_equal_narrow(BOOL_VAL(true), BOOL_VAL(true)));
    assert(!value_equal_narrow(BOOL_VAL(true), BOOL_VAL(false)));
    assert(!value_equal_narrow(BOOL_VAL(true), INT_VAL(1))); /* bool never equals a numba */
    assert(value_equal_narrow(INT_VAL(5), INT_VAL(5)));
    assert(!value_equal_narrow(INT_VAL(5), INT_VAL(6)));
    assert(value_equal_narrow(INT_VAL(5), FLOAT_VAL(5.0))); /* cross-type numeric equality */
    assert(!value_equal_narrow(INT_VAL(5), FLOAT_VAL(5.5)));
}

static void test_bignum_equality(void) {
    ObjBignum *big = bignum_from_int64(1000000000000000000LL);
    ObjBignum *big2 = bignum_from_int64(1000000000000000000LL);
    Value a = OBJ_VAL(big);
    Value b = OBJ_VAL(big2);
    assert(value_equal_narrow(a, b)); /* distinct objects, same magnitude */
    assert(value_equal_narrow(a, INT_VAL(1000000000000000000LL)));
    assert(!value_equal_narrow(a, INT_VAL(1000000000000000001LL)));
    bignum_free(big);
    bignum_free(big2);
}

static void test_truthiness(void) {
    assert(!value_is_truthy(GHOST_VAL));
    assert(!value_is_truthy(BOOL_VAL(false)));
    assert(value_is_truthy(BOOL_VAL(true)));
    assert(!value_is_truthy(INT_VAL(0)));
    assert(value_is_truthy(INT_VAL(1)));
    assert(value_is_truthy(INT_VAL(-1)));
    assert(!value_is_truthy(FLOAT_VAL(0.0)));
    assert(value_is_truthy(FLOAT_VAL(0.5)));

    ObjBignum *zero = bignum_from_int64(0);
    ObjBignum *nonzero = bignum_from_int64(42);
    assert(!value_is_truthy(OBJ_VAL(zero)));
    assert(value_is_truthy(OBJ_VAL(nonzero)));
    bignum_free(zero);
    bignum_free(nonzero);
}

int main(void) {
    test_basic_equality();
    test_bignum_equality();
    test_truthiness();
    printf("test_value: all tests passed\n");
    return 0;
}
