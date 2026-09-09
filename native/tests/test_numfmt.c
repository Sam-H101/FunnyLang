/* native/tests/test_numfmt.c -- known-value checks for numfmt.c, each
 * cross-checked against CPython's repr() (see NATIVE_PLAN.md §9). Random
 * coverage against the real Python oracle is fuzz_numfmt_check.py's job.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../numfmt.h"

static void check(double v, const char *expected) {
    char *s = numfmt_repr(v);
    if (strcmp(s, expected) != 0) {
        fprintf(stderr, "numfmt_repr(%.20g) = %s, expected %s\n", v, s, expected);
        assert(0 && "repr mismatch");
    }
    free(s);
}

int main(void) {
    check(0.0, "0.0");
    check(-0.0, "-0.0");
    check(1.0, "1.0");
    check(-1.0, "-1.0");
    check(100.0, "100.0");
    check(0.1, "0.1");
    check(1.1, "1.1");
    check(3.14159265358979, "3.14159265358979");
    check(1e16, "1e+16");
    check(1e17, "1e+17");
    check(1e21, "1e+21");
    check(1.5e16, "1.5e+16");
    check(0.0001, "0.0001");
    check(0.00001, "1e-05");
    check(123456789012345.0, "123456789012345.0");
    check(1234567890123456.0, "1234567890123456.0");
    check(5e-324, "5e-324");
    check(1.7976931348623157e+308, "1.7976931348623157e+308");
    check(2.2250738585072014e-308, "2.2250738585072014e-308");
    check(1e300, "1e+300");
    check(1e-300, "1e-300");
    check(9999999999999998.0, "9999999999999998.0");
    check(1.0000000000000002e+16, "1.0000000000000002e+16");
    check(0.0 / -1.0, "-0.0"); /* IEEE -0.0 via negation */
    printf("test_numfmt: all tests passed\n");
    return 0;
}
