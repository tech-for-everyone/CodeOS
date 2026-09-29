#include <stdio.h>
#include "jengine/jengine.h"

static int failures = 0;
static int checks = 0;

static int check(const char *source, int type, long long value) {
    jengine_value result = jengine_eval(source);
    checks++;
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
    check("42x", JENGINE_ERROR, 0);                 /* undeclared ident glued to a number */
    check("1)", JENGINE_ERROR, 0);                  /* ')' is not an expression */
    /* A stray '}' ends the statement loop but leaves input unconsumed, so it is
     * the ONLY input that reaches the trailing-garbage check in jengine_eval.
     * `42x` and `1)` are rejected earlier, by the identifier and number paths. */
    check("1 }", JENGINE_ERROR, 0);
    check("let x = 1 }", JENGINE_ERROR, 0);
    check("}", JENGINE_ERROR, 0);

    /* ── Variables ─────────────────────────────────────────────────── */
    check("let x = 5; x + 3", JENGINE_NUMBER, 8);
    check("let x = 5", JENGINE_NUMBER, 0);          /* a declaration has no value */
    check("let x = 5; x", JENGINE_NUMBER, 5);
    check("let a = 2; let b = 3; a * b", JENGINE_NUMBER, 6);
    check("let x = 5; x = 10; x", JENGINE_NUMBER, 10);        /* assignment */
    check("let x = 1; let x = 2; x", JENGINE_NUMBER, 2);     /* redecl: last wins */
    check("const C = 3; C + 1", JENGINE_NUMBER, 4);
    check("let x = 2; x = x + 3; x * x", JENGINE_NUMBER, 25);
    check("{ let a = 1; a + 1 }", JENGINE_NUMBER, 2);        /* block scope */
    check("let x = 3; { let x = 9; x = 9; } x", JENGINE_NUMBER, 3); /* no leak */
    check("let x = 4; { let y = 5; y } x", JENGINE_NUMBER, 4);        /* outer x */

    check("const C = 3; C = 4", JENGINE_ERROR, 0);   /* const is enforced */
    check("x = 1", JENGINE_ERROR, 0);                /* assign to undeclared */
    check("let x =", JENGINE_ERROR, 0);               /* truncated declaration */
    check("let = 5", JENGINE_ERROR, 0);               /* missing name */

    /* ── Functions ──────────────────────────────────────────────────── */
    check("function add(a, b) { return a + b; } add(2, 3)", JENGINE_NUMBER, 5);
    check("function sq(x) { return x * x; } sq(7)", JENGINE_NUMBER, 49);
    check("function neg(x) { return -x; } neg(5)", JENGINE_NUMBER, -5);
    check("function id(x) { return x; } id(42)", JENGINE_NUMBER, 42);
    check("function two() { return 2; } two()", JENGINE_NUMBER, 2);  /* no params */
    check("function inc(x) { return x + 1; } inc(inc(inc(1)))", JENGINE_NUMBER, 4);
    check("function a(x) { return x + 1; } function b(x) { return a(x) * 2; } b(3)", JENGINE_NUMBER, 8);
    check("function dbl(x) { return x * 2; } dbl(dbl(5))", JENGINE_NUMBER, 20);

    /* A function body sees globals, but not its caller's locals. */
    check("let g = 7; function get() { return g; } get()", JENGINE_NUMBER, 7);
    check("let g = 7; function f() { let g = 1; return g; } f() + g", JENGINE_NUMBER, 8);

    check("function add(a, b) { return a + b; } add(1)", JENGINE_ERROR, 0);      /* arity */
    check("function add(a, b) { return a + b; } add(1, 2, 3)", JENGINE_ERROR, 0);
    check("nope(1)", JENGINE_ERROR, 0);                                           /* no fn */
    check("let x = 1; x(2)", JENGINE_ERROR, 0);                                   /* not callable */
    check("function f() { return; } f()", JENGINE_NUMBER, 0);      /* bare return */
    check("function f() { let a = 1; } f()", JENGINE_NUMBER, 0);    /* no return -> 0 */

    /* ── Evaluations are independent ────────────────────────────────── */
    /* Globals must not survive into the next jengine_eval: the calculator
     * calls init() once and evaluates every keystroke against one engine. */
    check("let leak = 99; leak", JENGINE_NUMBER, 99);
    check("leak", JENGINE_ERROR, 0);
    check("function fn1() { return 1; } fn1()", JENGINE_NUMBER, 1);
    check("fn1()", JENGINE_ERROR, 0);

    /* A suite that silently ran nothing would pass for the wrong reason, so
     * the count is reported and a suspiciously small run is itself a failure. */
    if (checks == 0) {
        fprintf(stderr, "Jengine: no checks ran\n");
        return 1;
    }
    if (checks < 70) {
        fprintf(stderr, "Jengine: only %d checks ran, expected >= 70 "
                        "(tests were lost, not relaxed)\n", checks);
        return 1;
    }
    if (failures) {
        fprintf(stderr, "Jengine: %d of %d test(s) failed\n", failures, checks);
        return 1;
    }
    printf("Jengine: all %d tests passed\n", checks);
    return 0;
}