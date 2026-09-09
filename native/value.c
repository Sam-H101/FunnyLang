#include "value.h"

#include "bignum.h"
#include "string.h"

bool value_equal_narrow(Value a, Value b) {
    if (IS_GHOST(a) || IS_GHOST(b)) return IS_GHOST(a) && IS_GHOST(b);
    if (IS_BOOL(a) || IS_BOOL(b)) return IS_BOOL(a) && IS_BOOL(b) && AS_BOOL(a) == AS_BOOL(b);

    if (IS_NUM(a) && IS_NUM(b)) {
        if (!IS_BIGNUM(a) && !IS_BIGNUM(b)) {
            if (IS_INT(a) && IS_INT(b)) return AS_INT(a) == AS_INT(b);
            double da = IS_FLOAT(a) ? AS_FLOAT(a) : (double)AS_INT(a);
            double db = IS_FLOAT(b) ? AS_FLOAT(b) : (double)AS_INT(b);
            return da == db;
        }
        if (IS_FLOAT(a) || IS_FLOAT(b)) {
            /* A bignum-vs-float comparison here goes through `double`,
               which can lose precision for a bignum too large to be exact
               as a double -- a known, deliberate simplification (see
               bignum.h's own note on this same tradeoff for
               bignum_to_double), acceptable since exact bignum<->float
               comparison needs comparing against a float's exact rational
               value, real work with no bearing on N1's own acceptance
               criteria (bignum arithmetic and repr() formatting). */
            double da = IS_FLOAT(a) ? AS_FLOAT(a) : (IS_INT(a) ? (double)AS_INT(a) : bignum_to_double(AS_BIGNUM(a)));
            double db = IS_FLOAT(b) ? AS_FLOAT(b) : (IS_INT(b) ? (double)AS_INT(b) : bignum_to_double(AS_BIGNUM(b)));
            return da == db;
        }
        /* int/bignum mix, no float involved: exact comparison, no precision loss */
        ObjBignum *ba = IS_BIGNUM(a) ? AS_BIGNUM(a) : bignum_from_int64(AS_INT(a));
        ObjBignum *bb = IS_BIGNUM(b) ? AS_BIGNUM(b) : bignum_from_int64(AS_INT(b));
        bool eq = bignum_compare(ba, bb) == 0;
        if (!IS_BIGNUM(a)) bignum_free(ba);
        if (!IS_BIGNUM(b)) bignum_free(bb);
        return eq;
    }

    if (IS_STRING(a) && IS_STRING(b)) return string_equal(AS_STRING(a), AS_STRING(b));
    if (IS_OBJ(a) && IS_OBJ(b)) return AS_OBJ(a) == AS_OBJ(b);
    return false;
}

bool value_is_truthy(Value v) {
    if (IS_GHOST(v)) return false;
    if (IS_BOOL(v)) return AS_BOOL(v);
    if (IS_INT(v)) return AS_INT(v) != 0;
    if (IS_FLOAT(v)) return AS_FLOAT(v) != 0.0;
    if (IS_BIGNUM(v)) return !bignum_is_zero(AS_BIGNUM(v));
    if (IS_STRING(v)) return AS_STRING(v)->byteLen > 0;
    return true; /* every other Obj kind (N4+) is truthy unless empty -- N4's job */
}
