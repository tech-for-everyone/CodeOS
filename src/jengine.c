#include <stddef.h>
#include <stdint.h>
#include "jengine/jengine.h"

typedef struct {
    const char *p;
    int error;
} parser_t;

static void skip_space(parser_t *ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n') ps->p++;
}

static int match(parser_t *ps, const char *text) {
    const char *p = ps->p;
    while (*text && *p == *text) { p++; text++; }
    if (*text) return 0;
    ps->p = p;
    return 1;
}

static int64_t parse_or(parser_t *ps);

static int64_t parse_number(parser_t *ps) {
    int64_t value = 0;
    int digits = 0;
    while (*ps->p >= '0' && *ps->p <= '9') {
        int digit = *ps->p++ - '0';
        if (value > (INT64_MAX - digit) / 10) ps->error = 1;
        value = value * 10 + digit;
        digits = 1;
    }
    if (!digits) ps->error = 1;
    return value;
}

static int64_t parse_primary(parser_t *ps) {
    int64_t value;
    skip_space(ps);
    if (*ps->p == '(') {
        ps->p++;
        value = parse_or(ps);
        skip_space(ps);
        if (*ps->p != ')') ps->error = 1;
        else ps->p++;
        return value;
    }
    return parse_number(ps);
}

static int64_t parse_unary(parser_t *ps) {
    skip_space(ps);
    if (*ps->p == '+') { ps->p++; return parse_unary(ps); }
    if (*ps->p == '-') { ps->p++; return -parse_unary(ps); }
    if (*ps->p == '!') { ps->p++; return !parse_unary(ps); }
    return parse_primary(ps);
}

static int64_t parse_multiply(parser_t *ps) {
    int64_t value = parse_unary(ps);
    for (;;) {
        skip_space(ps);
        if (*ps->p == '*') {
            ps->p++; value *= parse_unary(ps);
        } else if (*ps->p == '/') {
            int64_t rhs;
            ps->p++; rhs = parse_unary(ps);
            if (rhs == 0) ps->error = 1;
            else value /= rhs;
        } else if (*ps->p == '%') {
            int64_t rhs;
            ps->p++; rhs = parse_unary(ps);
            if (rhs == 0) ps->error = 1;
            else value %= rhs;
        } else break;
    }
    return value;
}

static int64_t parse_add(parser_t *ps) {
    int64_t value = parse_multiply(ps);
    for (;;) {
        skip_space(ps);
        if (*ps->p == '+') { ps->p++; value += parse_multiply(ps); }
        else if (*ps->p == '-') { ps->p++; value -= parse_multiply(ps); }
        else break;
    }
    return value;
}

static int64_t parse_compare(parser_t *ps) {
    int64_t left = parse_add(ps);
    for (;;) {
        int64_t right;
        skip_space(ps);
        if (match(ps, "==")) { right = parse_add(ps); left = (left == right); }
        else if (match(ps, "!=")) { right = parse_add(ps); left = (left != right); }
        else if (match(ps, "<=")) { right = parse_add(ps); left = (left <= right); }
        else if (match(ps, ">=")) { right = parse_add(ps); left = (left >= right); }
        else if (*ps->p == '<') { ps->p++; right = parse_add(ps); left = (left < right); }
        else if (*ps->p == '>') { ps->p++; right = parse_add(ps); left = (left > right); }
        else break;
    }
    return left;
}

static int64_t parse_and(parser_t *ps) {
    int64_t value = parse_compare(ps);
    while (1) {
        int64_t rhs;
        skip_space(ps);
        if (!match(ps, "&&")) break;
        /* Parse the right operand unconditionally: C's && short-circuits, so
         * computing `value && parse_compare(ps)` directly would skip parsing
         * the right side (and with it the forward progress of ps->p). */
        rhs = parse_compare(ps);
        value = (value && rhs) ? 1 : 0;
    }
    return value;
}

static int64_t parse_or(parser_t *ps) {
    int64_t value = parse_and(ps);
    while (1) {
        int64_t rhs;
        skip_space(ps);
        if (!match(ps, "||")) break;
        /* Same reasoning as parse_and: || must not short-circuit the parse. */
        rhs = parse_and(ps);
        value = (value || rhs) ? 1 : 0;
    }
    return value;
}

jengine_value jengine_eval(const char *source) {
    jengine_value result = { JENGINE_UNDEFINED, 0, NULL };
    parser_t parser;
    if (!source) return result;
    parser.p = source;
    parser.error = 0;
    result.num_val = parse_or(&parser);
    skip_space(&parser);
    if (*parser.p != '\0') parser.error = 1;
    if (parser.error) {
        result.type = JENGINE_ERROR;
        result.num_val = 0;
    } else {
        result.type = JENGINE_NUMBER;
    }
    return result;
}

void jengine_init(void) { }
void jengine_cleanup(void) { }
