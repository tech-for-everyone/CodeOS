/* Jengine -- CodeOS's minimal script evaluator.
 *
 * Compiled into BOTH the host test harness and the kernel
 * (kernel/Makefile SRC_C += ../src/jengine.c).  The kernel toolchain is
 * -ffreestanding -nostdlib -nodefaultlibs and has no libc headers at all,
 * so this file must not include <stdlib.h>, <string.h> or <ctype.h> and
 * must not call malloc/calloc/free/strdup/strcmp/strlen/isalpha.  Every
 * helper it needs is defined locally below.  Verified with:
 *   cc -std=c11 -Iinclude src/jengine.c tests/jengine_test.c   (host)
 *   x86_64-elf-gcc -ffreestanding -nostdlib -Iinclude -c ...   (kernel)
 *
 * Supported grammar:
 *   expressions   1 + 2 * 3, (a - b) / c, !x, -x, a < b, a && b, a || b
 *   declarations  let x = 1;   const LIMIT = 10;   (also without the ';')
 *   assignment    x = 42;
 *   functions     function add(a, b) { return a + b; }   add(1, 2);
 *   blocks        { let a = 1; }
 *   returns       return expr;
 *
 * Values are int64_t only.  There is no allocator here, so string/object
 * values are deliberately not produced; jengine_value keeps its str_val
 * field for API compatibility but eval never fills it.
 */

#include <stddef.h>
#include <stdint.h>
#include "jengine/jengine.h"

/* ── Limits.  Fixed, because there is no allocator: a function that recurses
 *    past JENGINE_MAX_DEPTH returns 0 rather than smashing the stack. ── */
#define JENGINE_NAME_LEN   32
#define JENGINE_MAX_VARS   64
#define JENGINE_MAX_FUNCS  16
#define JENGINE_MAX_PARAMS 8
#define JENGINE_MAX_DEPTH  16

/* ── Freestanding helpers.  No libc in the kernel build. ── */
static int je_is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static int je_is_digit(char c) { return c >= '0' && c <= '9'; }
static int je_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
static int je_is_alnum(char c) { return je_is_alpha(c) || je_is_digit(c); }

static int je_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static size_t je_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Copy a NUL-terminated identifier into a fixed-size slot, refusing to
 * overflow it.  Returns 0 on overflow so the caller can raise an error
 * instead of silently truncating two distinct names into one. */
static int je_copy_name(char *dst, size_t cap, const char *src, size_t len) {
    if (len == 0 || len >= cap) return 0;
    size_t i;
    for (i = 0; i < len; i++) dst[i] = src[i];
    dst[len] = '\0';
    return 1;
}

/* ── Runtime state ── */
typedef struct {
    char name[JENGINE_NAME_LEN];
    int64_t value;
    int readonly;                 /* declared with const */
} jengine_var;

typedef struct {
    jengine_var vars[JENGINE_MAX_VARS];
    int count;
} jengine_scope;

typedef struct {
    char name[JENGINE_NAME_LEN];
    char params[JENGINE_MAX_PARAMS][JENGINE_NAME_LEN];
    int nparams;
    const char *body;             /* points into the source being evaluated */
} jengine_func;

static jengine_func g_funcs[JENGINE_MAX_FUNCS];
static int g_nfuncs;

static jengine_scope g_scopes[JENGINE_MAX_DEPTH];
static int g_depth;               /* g_scopes[0] is the global scope */

typedef struct {
    const char *p;
    int error;
    int returning;                /* a `return` was executed */
    int64_t ret;
} parser_t;

/* ── Scope helpers.  Innermost scope is g_scopes[g_depth-1]. ── */
static void scope_push(void) {
    if (g_depth < JENGINE_MAX_DEPTH) {
        g_scopes[g_depth].count = 0;
        g_depth++;
    }
    /* Past the limit the scope is reused: bounded recursion, not a crash. */
}

static void scope_pop(void) {
    if (g_depth > 0) g_depth--;
}

static jengine_var *scope_find(const char *name) {
    int d;
    for (d = g_depth - 1; d >= 0; d--) {
        int i;
        for (i = 0; i < g_scopes[d].count; i++) {
            if (je_streq(g_scopes[d].vars[i].name, name)) return &g_scopes[d].vars[i];
        }
    }
    return NULL;
}

static jengine_var *scope_define(parser_t *ps, const char *name, int64_t value, int readonly) {
    jengine_scope *s;
    if (g_depth <= 0) { ps->error = 1; return NULL; }
    s = &g_scopes[g_depth - 1];
    size_t len = je_strlen(name);
    if (len == 0 || len >= JENGINE_NAME_LEN) { ps->error = 1; return NULL; }

    /* Redeclaring a name in the SAME scope reuses the existing slot, so the
     * later binding wins.  Appending instead would leave scope_find (which
     * scans forward) returning the stale first binding, making
     * `let x = 1; let x = 2; x` evaluate to 1. */
    int i;
    for (i = 0; i < s->count; i++) {
        if (je_streq(s->vars[i].name, name)) {
            s->vars[i].value = value;
            s->vars[i].readonly = readonly;
            return &s->vars[i];
        }
    }

    if (s->count >= JENGINE_MAX_VARS) { ps->error = 1; return NULL; }
    jengine_var *v = &s->vars[s->count++];
    je_copy_name(v->name, sizeof(v->name), name, len);
    v->value = value;
    v->readonly = readonly;
    return v;
}

/* ── Lexer helpers ── */
static void skip_space(parser_t *ps) {
    while (je_is_space(*ps->p)) ps->p++;
}

static int match(parser_t *ps, const char *text) {
    const char *p = ps->p;
    while (*text && *p == *text) { p++; text++; }
    if (*text) return 0;
    ps->p = p;
    return 1;
}

/* Consume an identifier into a fixed slot.  Returns 0 (and raises an error)
 * if the input does not start with one or it would not fit. */
static int take_identifier(parser_t *ps, char *dst, size_t cap) {
    skip_space(ps);
    const char *start = ps->p;
    if (!je_is_alpha(*ps->p)) { ps->error = 1; return 0; }
    while (je_is_alnum(*ps->p)) ps->p++;
    if (!je_copy_name(dst, cap, start, (size_t)(ps->p - start))) {
        ps->error = 1;
        return 0;
    }
    return 1;
}

static int match_keyword(parser_t *ps, const char *kw) {
    size_t n = je_strlen(kw);
    size_t i;
    skip_space(ps);
    for (i = 0; i < n; i++) {
        if (ps->p[i] != kw[i]) return 0;
    }
    if (je_is_alnum(ps->p[n]) || ps->p[n] == '_' || ps->p[n] == '$') return 0;
    ps->p += n;
    return 1;
}

/* ── Forward declarations ── */
static int64_t parse_expr(parser_t *ps);
static int64_t exec_statements(parser_t *ps);

/* ── Expression parser (precedence climbing) ── */
static int64_t parse_number(parser_t *ps) {
    int64_t value = 0;
    int digits = 0;
    while (je_is_digit(*ps->p)) {
        int digit = *ps->p++ - '0';
        if (value > (INT64_MAX - digit) / 10) ps->error = 1;
        value = value * 10 + digit;
        digits = 1;
    }
    if (!digits) ps->error = 1;
    return value;
}

static int64_t parse_or(parser_t *ps);

static int64_t parse_primary(parser_t *ps) {
    skip_space(ps);
    if (*ps->p == '(') {
        ps->p++;
        int64_t value = parse_or(ps);
        skip_space(ps);
        if (*ps->p != ')') { ps->error = 1; return 0; }
        ps->p++;
        return value;
    }
    if (je_is_alpha(*ps->p)) {
        char name[JENGINE_NAME_LEN];
        const char *save = ps->p;
        if (!take_identifier(ps, name, sizeof(name))) { ps->p = save; ps->error = 1; return 0; }
        skip_space(ps);
        if (*ps->p == '(') {
            /* Call: evaluate the function with the argument expressions. */
            int i, found = -1;
            for (i = 0; i < g_nfuncs; i++) if (je_streq(g_funcs[i].name, name)) found = i;
            if (found < 0) { ps->error = 1; return 0; }
            ps->p++;                            /* consume '(' */
            int64_t args[JENGINE_MAX_PARAMS];
            int argc = 0;
            skip_space(ps);
            if (*ps->p != ')') {
                for (;;) {
                    if (argc >= JENGINE_MAX_PARAMS) { ps->error = 1; return 0; }
                    args[argc++] = parse_expr(ps);
                    if (ps->error || ps->returning) return 0;
                    skip_space(ps);
                    if (*ps->p == ',') { ps->p++; continue; }
                    break;
                }
            }
            if (*ps->p != ')') { ps->error = 1; return 0; }
            ps->p++;                            /* consume ')' */

            jengine_func *fn = &g_funcs[found];
            if (argc != fn->nparams) { ps->error = 1; return 0; }

            /* Bind the arguments, then evaluate the body in a fresh scope
             * whose parent is the global scope: Jengine has one flat
             * namespace, so a function body cannot see its caller's
             * locals. */
            scope_push();
            int d;
            for (d = 0; d < fn->nparams; d++) {
                if (!scope_define(ps, fn->params[d], args[d], 0)) { scope_pop(); return 0; }
            }
            parser_t sub;
            sub.p = fn->body;
            sub.error = 0;
            sub.returning = 0;
            sub.ret = 0;
            exec_statements(&sub);
            int64_t result = sub.returning ? sub.ret : 0;
            int failed = sub.error;
            scope_pop();
            if (failed) { ps->error = 1; return 0; }
            return result;
        }
        /* Bare identifier: a variable, or an unknown name (an error, so
         * `2 + nope` is reported rather than silently treated as 0). */
        jengine_var *v = scope_find(name);
        if (!v) { ps->error = 1; return 0; }
        return v->value;
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
    for (;;) {
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
    for (;;) {
        int64_t rhs;
        skip_space(ps);
        if (!match(ps, "||")) break;
        /* Same reasoning as parse_and: || must not short-circuit the parse. */
        rhs = parse_and(ps);
        value = (value || rhs) ? 1 : 0;
    }
    return value;
}

static int64_t parse_expr(parser_t *ps) {
    return parse_or(ps);
}

/* ── Statements ── */
static void exec_declaration(parser_t *ps, int readonly) {
    char name[JENGINE_NAME_LEN];
    if (!take_identifier(ps, name, sizeof(name))) return;
    int64_t value = 0;
    skip_space(ps);
    if (*ps->p == '=') {
        ps->p++;
        value = parse_expr(ps);
        if (ps->error || ps->returning) return;
        skip_space(ps);
    }
    if (*ps->p == ';') ps->p++;
    scope_define(ps, name, value, readonly);
}

static void exec_assignment(parser_t *ps) {
    char name[JENGINE_NAME_LEN];
    const char *save = ps->p;
    if (!take_identifier(ps, name, sizeof(name))) { ps->p = save; ps->error = 1; return; }
    skip_space(ps);
    if (*ps->p != '=') { ps->p = save; ps->error = 1; return; }
    ps->p++;
    int64_t value = parse_expr(ps);
    if (ps->error || ps->returning) return;
    jengine_var *v = scope_find(name);
    if (!v) { ps->error = 1; return; }
    if (v->readonly) { ps->error = 1; return; }   /* const is enforced */
    v->value = value;
}

static void exec_function_def(parser_t *ps) {
    char name[JENGINE_NAME_LEN];
    if (!take_identifier(ps, name, sizeof(name))) return;
    skip_space(ps);
    if (*ps->p != '(') { ps->error = 1; return; }
    ps->p++;

    jengine_func fn;
    fn.nparams = 0;
    skip_space(ps);
    if (*ps->p != ')') {
        for (;;) {
            if (fn.nparams >= JENGINE_MAX_PARAMS) { ps->error = 1; return; }
            if (!take_identifier(ps, fn.params[fn.nparams], JENGINE_NAME_LEN)) return;
            fn.nparams++;
            skip_space(ps);
            if (*ps->p == ',') { ps->p++; continue; }
            break;
        }
    }
    if (*ps->p != ')') { ps->error = 1; return; }
    ps->p++;
    skip_space(ps);
    if (*ps->p != '{') { ps->error = 1; return; }
    ps->p++;
    fn.body = ps->p;                    /* body is the text up to the matching '}' */

    /* Find the matching close brace so the definition ends exactly at the
     * body; braces inside a nested block are counted, not taken as the end. */
    int depth = 1;
    const char *q = ps->p;
    while (*q && depth) {
        if (*q == '{') depth++;
        else if (*q == '}') { depth--; if (!depth) break; }
        q++;
    }
    if (*q != '}') { ps->error = 1; return; }

    if (!je_copy_name(fn.name, sizeof(fn.name), name, je_strlen(name))) { ps->error = 1; return; }
    fn.body = ps->p;

    if (g_nfuncs >= JENGINE_MAX_FUNCS) { ps->error = 1; return; }
    g_funcs[g_nfuncs++] = fn;
    ps->p = q + 1;                      /* resume after the closing '}' */
}

/* Runs statements until input is exhausted or a `return` fires.  The value of
 * the last evaluated expression statement is left in *out, so a bare
 * expression like `1 + 2 * 3` still evaluates to 7. */
static int64_t exec_statements(parser_t *ps) {
    int64_t last = 0;
    for (;;) {
        skip_space(ps);
        if (!*ps->p) break;
        if (*ps->p == '}') break;            /* leave the block to the caller */
        if (*ps->p == ';') { ps->p++; continue; }

        if (match_keyword(ps, "let"))       { exec_declaration(ps, 0); }
        else if (match_keyword(ps, "const")) { exec_declaration(ps, 1); }
        else if (match_keyword(ps, "function")) { exec_function_def(ps); }
        else if (match_keyword(ps, "return")) {
            skip_space(ps);
            if (*ps->p != ';' && *ps->p != '}') ps->ret = parse_expr(ps);
            else ps->ret = 0;
            ps->returning = 1;
            break;
        }
        else if (*ps->p == '{') {
            ps->p++;
            scope_push();
            /* Keep the block's last expression value: `{ 1 + 1 }` must
             * evaluate to 2, not silently to 0. */
            int64_t inner = exec_statements(ps);
            scope_pop();
            if (ps->returning) { last = inner; break; }
            last = inner;
            skip_space(ps);
            if (*ps->p == '}') ps->p++;
            else ps->error = 1;
        }
        else {
            /* Either `name = expr;` or a bare expression statement. */
            const char *save = ps->p;
            char name[JENGINE_NAME_LEN];
            int is_assign = 0;
            skip_space(ps);
            const char *id_start = ps->p;
            if (je_is_alpha(*ps->p)) {
                if (take_identifier(ps, name, sizeof(name))) {
                    skip_space(ps);
                    if (*ps->p == '=' && ps->p[1] != '=') is_assign = 1;
                }
            }
            if (is_assign) {
                ps->p = save;
                exec_assignment(ps);
                last = 0;
            } else {
                ps->p = save;
                (void)id_start;
                last = parse_expr(ps);
            }
            if (ps->error || ps->returning) break;
            skip_space(ps);
            if (*ps->p == ';') ps->p++;
        }
        if (ps->error) break;
    }
    return last;
}

/* ── Public API ── */
static void interpreter_reset(void) {
    g_nfuncs = 0;
    /* g_scopes[0] IS the global scope, so depth starts at 1, not 0: a depth
     * of 0 would mean "no active scope" and every declaration would fail. */
    g_depth = 1;
    g_scopes[0].count = 0;
}

void jengine_init(void) {
    interpreter_reset();
}

void jengine_cleanup(void) {
    interpreter_reset();
}

jengine_value jengine_eval(const char *source) {
    jengine_value result = { JENGINE_UNDEFINED, 0, NULL };
    parser_t parser;

    /* Each evaluation starts from a clean interpreter.  All three callers
     * (the calculator, the panels API and litehtml <script> bodies) treat
     * jengine_eval as a one-shot over a single expression or program, and
     * none of them re-declares a variable in a later call expecting it to
     * still be bound.  Resetting here keeps that guarantee, makes eval
     * idempotent, and means a caller that never called jengine_init() still
     * gets well-defined behaviour. */
    interpreter_reset();

    if (!source) return result;

    parser.p = source;
    parser.error = 0;
    parser.returning = 0;
    parser.ret = 0;

    int64_t value = exec_statements(&parser);
    skip_space(&parser);

    /* Trailing input that the statement loop did not consume is a syntax
     * error: `42x` must not quietly evaluate to 42. */
    if (parser.error) {
        result.type = JENGINE_ERROR;
        result.num_val = 0;
    } else if (*parser.p != '\0') {
        result.type = JENGINE_ERROR;
        result.num_val = 0;
    } else {
        result.type = JENGINE_NUMBER;
        result.num_val = value;
    }
    return result;
}
