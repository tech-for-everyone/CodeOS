#include <stdio.h>
#include "jengine/jengine.h"

static int failures = 0;

static int check(const char *source, int type, long long value) {
    jengine_value result = jengine_eval(source);
    if (result.type != type || (type == JENGINE_NUMBER && result.num_val != value)) {
        fprintf(stderr, "Jengine failed: %s => type=%d value=%lld (expected type=%d value=%lld)\n",
                source, result.type, (long long)result.num_val, type, value);
        failures++;
        return 0;
    }
    return 1;
}

int main(void) {
    /* Arithmetic */
    check("42", JENGINE_NUMBER, 42);
    check("1 + 2 * 3", JENGINE_NUMBER, 7);          /* precedence: * before + */
    check("(10 - 4) / 2", JENGINE_NUMBER, 3);       /* parentheses */
    check("2 * 3 + 4 * 5", JENGINE_NUMBER, 26);     /* left-to-right per precedence */
    check("7 % 4", JENGINE_NUMBER, 3);              /* modulo */
    check("5 - 2 - 1", JENGINE_NUMBER, 2);          /* left associativity */
    check("10 / 4", JENGINE_NUMBER, 2);             /* integer division */

    /* Unary operators */
    check("-5", JENGINE_NUMBER, -5);
    check("+5", JENGINE_NUMBER, 5);
    check("-2 * -3", JENGINE_NUMBER, 6);
    check("!0", JENGINE_NUMBER, 1);
    check("!1", JENGINE_NUMBER, 0);
    check("-(3 + 4)", JENGINE_NUMBER, -7);

    /* Comparisons */
    check("2 < 3 && 4 != 5", JENGINE_NUMBER, 1);
    check("2 < 1", JENGINE_NUMBER, 0);
    check("1 == 1", JENGINE_NUMBER, 1);
    check("1 != 1", JENGINE_NUMBER, 0);
    check("3 >= 3", JENGINE_NUMBER, 1);
    check("3 <= 2", JENGINE_NUMBER, 0);

    /* Logical operators (regression: C short-circuit used to skip parsing
     * the right-hand side, leaving trailing tokens -> false JENGINE_ERROR) */
    check("0 && 1", JENGINE_NUMBER, 0);
    check("1 && 1 && 1", JENGINE_NUMBER, 1);
    check("3 || 0", JENGINE_NUMBER, 1);
    check("0 || 1", JENGINE_NUMBER, 1);
    check("2 && 3 || 0", JENGINE_NUMBER, 1);
    check("0 && 3 || 4", JENGINE_NUMBER, 1);
    check("5 == 5 && 1", JENGINE_NUMBER, 1);

    /* Whitespace is tolerated around tokens */
    check("  1 + 2  ", JENGINE_NUMBER, 3);
    check("\t3\n+\t4", JENGINE_NUMBER, 7);

    /* Errors */
    check("1 / 0", JENGINE_ERROR, 0);               /* division by zero */
    check("1 % 0", JENGINE_ERROR, 0);               /* modulo by zero */
    check("2 + nope", JENGINE_ERROR, 0);            /* not a number */
    check("2 +", JENGINE_ERROR, 0);                 /* dangling operator */
    check("(1 + 2", JENGINE_ERROR, 0);              /* unbalanced paren */
    check("42x", JENGINE_ERROR, 0);                 /* trailing garbage */

    if (failures) {
        fprintf(stderr, "Jengine: %d test(s) failed\n", failures);
        return 1;
    }
    printf("Jengine: all tests passed\n");
    return 0;
}