/* cora — a small Python-inspired language that compiles to C.
 *
 * Example cora program (hello.co):
 *
 *     print("Hello World")
 *
 * The compiler translates indentation-based cora into ordinary C, then a C
 * compiler produces the final machine code.  Nothing outside this package is
 * required to build it; the generated C only uses <stdio.h>.
 *
 * Supported syntax:
 *   # comments
 *   name = expression
 *   print(expr, expr, ...)          (prints a newline, like Python)
 *   if cond: / elif cond: / else:
 *   while cond:
 *   def name(p1, p2): ... return expression
 *   function calls, + - * / %, == != < <= > >=, and/or/not, True/False
 *
 * Strings may use "..." or '...'.  Identifiers, integers and strings are
 * long/const-char* in the generated C.  Source files are capped at 64 KiB,
 * matching the kernel's FS_CONTENT_MAX.
 *
 * Usage: cora INPUT.co [OUTPUT.c]
 *        with no OUTPUT.c, INPUT.c is written next to INPUT.co.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_SRC     65536
#define MAX_LINES   512
#define MAX_LINE    256
#define MAX_NAME    64
#define MAX_VARS    128
#define MAX_ARGS    16

typedef struct {
    int lineno;
    int indent;
    char text[MAX_LINE];
} line_t;

static line_t src[MAX_LINES];
static int nsrc;

static char generated[MAX_SRC];
static size_t glen;
static int failed;

static char vars[MAX_VARS][MAX_NAME];
static int nvars;

/* ---------- small utilities ---------- */

static void emitf(const char *fmt, ...) {
    if (glen >= sizeof(generated)) { failed = 1; return; }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(generated + glen, sizeof(generated) - glen, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(generated) - glen) {
        failed = 1;
        glen = 0;
        return;
    }
    glen += (size_t)n;
}

static void perr(int line, const char *msg) {
    printf("cora: line %d: %s\n", line, msg);
    failed = 1;
}

static int starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static char *skip_ws(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static void strip_comment(char *s) {
    char quote = 0;
    for (size_t i = 0; s[i]; i++) {
        if (quote) {
            if (s[i] == '\\' && s[i + 1]) { i++; continue; }
            if (s[i] == quote) quote = 0;
        } else if (s[i] == '"' || s[i] == '\'') {
            quote = s[i];
        } else if (s[i] == '#') {
            s[i] = 0;
            return;
        }
    }
}

static int is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int is_identifier(const char *s, const char *end) {
    if (end <= s || !isalpha((unsigned char)*s)) return 0;
    for (const char *p = s + 1; p < end; p++)
        if (!is_name_char(*p)) return 0;
    return 1;
}

/* ---------- source loading ---------- */

static int load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("cora: cannot open %s\n", path);
        return -1;
    }
    char buf[MAX_LINE];
    int lineno = 0;
    while (fgets(buf, sizeof(buf), f)) {
        lineno++;
        size_t len = strlen(buf);
        while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = 0;

        int indent = 0;
        while (buf[indent] == ' ') indent++;
        if (buf[indent] == '\t') {
            printf("cora: line %d: tabs are not indentation; use spaces\n", lineno);
            fclose(f);
            return -1;
        }
        char *text = buf + indent;
        strip_comment(text);
        text = skip_ws(text);
        rtrim(text);
        if (!*text) continue;

        if (nsrc >= MAX_LINES || strlen(text) >= MAX_LINE) {
            printf("cora: source is too large for this build\n");
            fclose(f);
            return -1;
        }
        src[nsrc].lineno = lineno;
        src[nsrc].indent = indent;
        memcpy(src[nsrc].text, text, strlen(text) + 1);
        nsrc++;
    }
    fclose(f);
    return 0;
}

/* ---------- expressions ---------- */

/* Translate Python-flavoured operators and quotes into C. */
static void cexpr(const char *s, char *dst, size_t cap) {
    size_t d = 0;
    size_t i = 0;
    while (s[i] && d + 8 < cap) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            char quote = c;
            dst[d++] = '"';
            i++;
            while (s[i] && s[i] != quote && d + 4 < cap) {
                if (s[i] == '\\' && s[i + 1]) {
                    dst[d++] = s[i++];
                    dst[d++] = s[i++];
                } else if (s[i] == '"' && quote == '\'') {
                    dst[d++] = '\\';
                    dst[d++] = '"';
                    i++;
                } else {
                    dst[d++] = s[i++];
                }
            }
            if (s[i] == quote) i++;
            dst[d++] = '"';
        } else if (isalpha((unsigned char)c) || c == '_') {
            size_t b = i;
            while (is_name_char(s[i])) i++;
            size_t n = i - b;
            char word[MAX_NAME];
            if (n >= sizeof(word)) n = sizeof(word) - 1;
            memcpy(word, s + b, n);
            word[n] = 0;
            if (strcmp(word, "and") == 0) { dst[d++] = '&'; dst[d++] = '&'; }
            else if (strcmp(word, "or") == 0) { dst[d++] = '|'; dst[d++] = '|'; }
            else if (strcmp(word, "not") == 0) { dst[d++] = '!'; }
            else if (strcmp(word, "True") == 0) { dst[d++] = '1'; }
            else if (strcmp(word, "False") == 0) { dst[d++] = '0'; }
            else if (strcmp(word, "None") == 0) { dst[d++] = '0'; }
            else { memcpy(dst + d, word, strlen(word)); d += strlen(word); }
        } else {
            dst[d++] = c;
            i++;
        }
    }
    dst[d] = 0;
}

static int is_string_literal(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '"' && *s != '\'') return 0;
    char quote = *s++;
    while (*s) {
        if (*s == '\\' && s[1]) { s += 2; continue; }
        if (*s == quote) { s++; break; }
        s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    return *s == 0;
}

/* Split a comma-separated argument list, ignoring commas inside strings or
 * nested parentheses. */
static int split_args(const char *s, char out[MAX_ARGS][MAX_LINE]) {
    int n = 0;
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n >= MAX_ARGS) return -1;

        size_t k = 0;
        char quote = 0;
        int depth = 0;
        while (*p) {
            char c = *p;
            if (quote) {
                if (c == '\\' && p[1]) {
                    if (k + 1 < MAX_LINE) out[n][k++] = c;
                    p++;
                    if (k + 1 < MAX_LINE) out[n][k++] = *p;
                    p++;
                    continue;
                }
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                if (depth == 0) break;
                depth--;
            } else if (c == ',' && depth == 0) {
                break;
            }
            if (k + 1 < MAX_LINE) out[n][k++] = c;
            p++;
        }
        out[n][k] = 0;
        rtrim(out[n]);
        n++;
        if (*p == ',') p++;
        else break;
    }
    return n;
}

static void emit_print(int lineno, const char *inner) {
    char args[MAX_ARGS][MAX_LINE];
    int n = split_args(inner, args);
    if (n < 0) { perr(lineno, "too many print arguments"); return; }
    if (n == 0) { emitf("printf(\"\\n\");\n"); return; }

    char fmt[MAX_LINE * 2];
    char vals[MAX_ARGS * MAX_LINE * 2];
    size_t fl = 0, vl = 0;
    for (int i = 0; i < n; i++) {
        if (i) fmt[fl++] = ' ';                    /* print()'s separator */
        vals[vl++] = ',';
        vals[vl++] = ' ';
        if (is_string_literal(args[i])) {
            fmt[fl++] = '%';
            fmt[fl++] = 's';
            char converted[MAX_LINE];
            cexpr(args[i], converted, sizeof(converted));
            size_t k = 0;
            while (converted[k] && vl + 1 < sizeof(vals)) vals[vl++] = converted[k++];
        } else {
            fmt[fl++] = '%';
            fmt[fl++] = 'l';
            fmt[fl++] = 'd';
            char converted[MAX_LINE];
            cexpr(args[i], converted, sizeof(converted));
            const char *pre = "(long)(";
            const char *post = ")";
            while (*pre && vl + 1 < sizeof(vals)) vals[vl++] = *pre++;
            size_t k = 0;
            while (converted[k] && vl + 1 < sizeof(vals)) vals[vl++] = converted[k++];
            while (*post && vl + 1 < sizeof(vals)) vals[vl++] = *post++;
        }
    }
    fmt[fl++] = '\\';
    fmt[fl++] = 'n';
    fmt[fl] = 0;
    vals[vl] = 0;
    emitf("printf(\"%s\"%s);\n", fmt, vals);
}

static void emit_call(int lineno, const char *text) {
    const char *paren = strchr(text, '(');
    if (!paren || text[strlen(text) - 1] != ')') {
        perr(lineno, "malformed call");
        return;
    }
    if (!is_identifier(text, paren)) {
        perr(lineno, "bad function name");
        return;
    }
    size_t nlen = (size_t)(paren - text);
    char name[MAX_NAME];
    if (nlen >= sizeof(name)) { perr(lineno, "function name too long"); return; }
    memcpy(name, text, nlen);
    name[nlen] = 0;

    size_t ilen = strlen(paren) - 2;
    char inner[MAX_LINE];
    if (ilen >= sizeof(inner)) { perr(lineno, "call is too long"); return; }
    memcpy(inner, paren + 1, ilen);
    inner[ilen] = 0;

    char args[MAX_ARGS][MAX_LINE];
    int n = split_args(inner, args);
    if (n < 0) { perr(lineno, "too many call arguments"); return; }
    emitf("%s(", name);
    for (int i = 0; i < n; i++) {
        char converted[MAX_LINE];
        cexpr(args[i], converted, sizeof(converted));
        emitf("%s(long)(%s)", i ? ", " : "", converted);
    }
    emitf(");\n");
}

/* Recognise `name = expr`, but not `==`, `<=`, `>=`, `!=` or compound
 * assignments.  RHS and LHS are returned in caller-owned buffers. */
static int assignment(const char *text, char *name, char *expr) {
    const char *p = text;
    char quote = 0;
    int depth = 0;
    while (*p) {
        char c = *p;
        if (quote) {
            if (c == '\\' && p[1]) { p += 2; continue; }
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (c == '=' && depth == 0) {
            if (p[1] == '=') { p += 2; continue; }
            if (p > text && strchr("!<>+-*/%", p[-1])) { p++; continue; }
            const char *a = text, *b = p;
            while (a < b && (*a == ' ' || *a == '\t')) a++;
            while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
            if (!is_identifier(a, b) || (size_t)(b - a) >= MAX_NAME) return 0;
            memcpy(name, a, (size_t)(b - a));
            name[b - a] = 0;
            const char *r = skip_ws((char *)(p + 1));
            size_t k = 0;
            while (r[k] && k + 1 < MAX_LINE) { expr[k] = r[k]; k++; }
            expr[k] = 0;
            return 1;
        }
        p++;
    }
    return 0;
}

/* ---------- variable declarations ---------- */

static void add_var(const char *name) {
    for (int i = 0; i < nvars; i++)
        if (strcmp(vars[i], name) == 0) return;
    if (nvars < MAX_VARS) {
        memcpy(vars[nvars], name, strlen(name) + 1);
        nvars++;
    }
}

static void collect_line(int idx) {
    char name[MAX_NAME], expr[MAX_LINE];
    if (assignment(src[idx].text, name, expr)) add_var(name);
}

static void collect_range(int start, int end) {
    for (int i = start; i < end; i++) collect_line(i);
}

/* Top-level lines belong to main(), but function bodies do not. */
static void collect_main(void) {
    for (int i = 0; i < nsrc; i++) {
        if (src[i].indent == 0 && starts(src[i].text, "def ")) {
            while (i + 1 < nsrc && src[i + 1].indent > 0) i++;
            continue;
        }
        collect_line(i);
    }
}

static void emit_decls(void) {
    for (int i = 0; i < nvars; i++)
        emitf("    long %s = 0;\n", vars[i]);
}

/* ---------- statements and blocks ---------- */

static int header_expr(const char *text, size_t kwlen, char *out, size_t cap) {
    const char *p = text + kwlen;
    while (*p == ' ' || *p == '\t') p++;
    const char *e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    if (e <= p || e[-1] != ':') return 0;
    e--;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    size_t n = (size_t)(e - p);
    if (n == 0 || n >= cap) return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static int child_indent(int next, int parent, int lineno) {
    if (next >= nsrc || src[next].indent <= parent) {
        perr(lineno, "expected an indented block");
        return parent + 4;
    }
    return src[next].indent;
}

static int parse_block(int i, int indent);

static int parse_if(int i, int indent) {
    char cond[MAX_LINE];
    if (!header_expr(src[i].text, 3, cond, sizeof(cond))) {
        perr(src[i].lineno, "malformed if statement");
        return i + 1;
    }
    cexpr(cond, cond, sizeof(cond));
    emitf("if (%s) {\n", cond);

    int child = child_indent(i + 1, indent, src[i].lineno);
    i = parse_block(i + 1, child);
    emitf("}\n");

    while (i < nsrc && src[i].indent == indent &&
           (starts(src[i].text, "elif ") || strcmp(src[i].text, "else:") == 0)) {
        if (starts(src[i].text, "elif ")) {
            if (!header_expr(src[i].text, 5, cond, sizeof(cond))) {
                perr(src[i].lineno, "malformed elif statement");
                return i + 1;
            }
            cexpr(cond, cond, sizeof(cond));
            emitf("else if (%s) {\n", cond);
        } else {
            emitf("else {\n");
        }
        child = child_indent(i + 1, indent, src[i].lineno);
        i = parse_block(i + 1, child);
        emitf("}\n");
    }
    return i;
}

static int parse_block(int i, int indent) {
    while (i < nsrc && src[i].indent == indent) {
        char *text = src[i].text;
        int lineno = src[i].lineno;

        /* Functions are emitted separately at file scope. */
        if (starts(text, "def ")) {
            i++;
            while (i < nsrc && src[i].indent > indent) i++;
            continue;
        }
        if (starts(text, "if ")) {
            if (text[strlen(text) - 1] != ':') {
                perr(lineno, "malformed if statement");
                i++;
                continue;
            }
            i = parse_if(i, indent);
            continue;
        }
        if (starts(text, "elif ") || strcmp(text, "else:") == 0) {
            perr(lineno, "elif/else without a matching if");
            i++;
            continue;
        }
        if (starts(text, "while ")) {
            if (text[strlen(text) - 1] != ':') {
                perr(lineno, "malformed while statement");
                i++;
                continue;
            }
            char cond[MAX_LINE];
            if (!header_expr(text, 6, cond, sizeof(cond))) {
                perr(lineno, "malformed while statement");
                i++;
                continue;
            }
            cexpr(cond, cond, sizeof(cond));
            emitf("while (%s) {\n", cond);
            int child = child_indent(i + 1, indent, lineno);
            i = parse_block(i + 1, child);
            emitf("}\n");
            continue;
        }
        if (starts(text, "return")) {
            char expr[MAX_LINE] = "";
            const char *p = skip_ws(text + 6);
            if (*p) {
                cexpr(p, expr, sizeof(expr));
                emitf("return (long)(%s);\n", expr);
            } else {
                emitf("return 0;\n");
            }
            i++;
            continue;
        }
        if (starts(text, "print") && text[5] == '(' && text[strlen(text) - 1] == ')') {
            size_t inner = strlen(text) - 7;
            char argbuf[MAX_LINE];
            if (inner >= sizeof(argbuf)) { perr(lineno, "print is too long"); i++; continue; }
            memcpy(argbuf, text + 6, inner);
            argbuf[inner] = 0;
            emit_print(lineno, argbuf);
            i++;
            continue;
        }

        char name[MAX_NAME], expr[MAX_LINE];
        if (assignment(text, name, expr)) {
            cexpr(expr, expr, sizeof(expr));
            emitf("%s = (long)(%s);\n", name, expr);
            i++;
            continue;
        }
        if (strchr(text, '(') && text[strlen(text) - 1] == ')') {
            emit_call(lineno, text);
            i++;
            continue;
        }
        perr(lineno, "unrecognised statement");
        i++;
    }
    if (i < nsrc && src[i].indent > indent) {
        /* A deeper line that no header claimed is malformed input. Consume
         * the whole run so callers always make progress. */
        perr(src[i].lineno, "unexpected indentation");
        while (i < nsrc && src[i].indent > indent) i++;
    }
    return i;
}

/* ---------- functions ---------- */

static void signature(const char *text, char *name, char *params) {
    const char *p = text + 4;                  /* skip "def " */
    while (*p == ' ' || *p == '(') p++;
    const char *b = p;
    while (*p && *p != '(' && *p != ':') p++;
    size_t n = (size_t)(p - b);
    while (n && (b[n - 1] == ' ' || b[n - 1] == '\t')) n--;
    if (n >= sizeof(name)) n = sizeof(name) - 1;
    memcpy(name, b, n);
    name[n] = 0;

    params[0] = 0;
    if (*p == '(') {
        p++;
        b = p;
        while (*p && *p != ')') p++;
        n = (size_t)(p - b);
        if (n >= MAX_LINE) n = MAX_LINE - 1;
        memcpy(params, b, n);
        params[n] = 0;
    }
}

static void emit_params(const char *params) {
    char args[MAX_ARGS][MAX_LINE];
    int n = split_args(params, args);
    if (n < 0) n = 0;
    if (n == 0) { emitf("void"); return; }
    for (int i = 0; i < n; i++) {
        char name[MAX_NAME];
        size_t k = 0;
        const char *p = skip_ws(args[i]);
        while (p[k] && k + 1 < sizeof(name) && is_name_char(p[k])) { name[k] = p[k]; k++; }
        name[k] = 0;
        emitf("%slong %s", i ? ", " : "", name);
    }
}

static void emit_prototype(int idx) {
    char name[MAX_NAME], params[MAX_LINE];
    signature(src[idx].text, name, params);
    emitf("static long %s(", name);
    emit_params(params);
    emitf(");\n");
}

static void emit_function(int idx) {
    char name[MAX_NAME], params[MAX_LINE];
    signature(src[idx].text, name, params);
    emitf("static long %s(", name);
    emit_params(params);
    emitf(") {\n");

    int start = idx + 1;
    if (start >= nsrc || src[start].indent <= src[idx].indent) {
        perr(src[idx].lineno, "function has no body");
        emitf("    return 0;\n}\n\n");
        return;
    }
    int end = start;
    while (end < nsrc && src[end].indent > src[idx].indent) end++;

    nvars = 0;
    collect_range(start, end);
    emit_decls();
    parse_block(start, src[start].indent);
    emitf("    return 0;\n}\n\n");
}

static void compile(void) {
    emitf("#include <stdio.h>\n\n/* Generated by cora.  Do not edit. */\n\n");

    for (int i = 0; i < nsrc; i++)
        if (src[i].indent == 0 && starts(src[i].text, "def "))
            emit_prototype(i);
    emitf("\n");

    emitf("int main(void) {\n");
    nvars = 0;
    collect_main();
    emit_decls();
    for (int i = 0; i < nsrc;) {
        if (src[i].indent == 0 && starts(src[i].text, "def ")) {
            i++;
            while (i < nsrc && src[i].indent > 0) i++;
            continue;
        }
        int next = parse_block(i, 0);
        if (next == i) {              /* never spin on malformed input */
            perr(src[i].lineno, "unexpected indentation");
            i++;
        } else {
            i = next;
        }
    }
    emitf("    return 0;\n}\n\n");

    for (int i = 0; i < nsrc; i++)
        if (src[i].indent == 0 && starts(src[i].text, "def "))
            emit_function(i);
}

static const char *default_output(const char *input, char *buf, size_t cap) {
    size_t n = strlen(input);
    const char *dot = strrchr(input, '.');
    if (dot && dot > input && strcmp(dot, ".co") == 0) n = (size_t)(dot - input);
    if (n + 3 >= cap) n = cap - 4;
    memcpy(buf, input, n);
    memcpy(buf + n, ".c", 3);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        puts("usage: cora INPUT.co [OUTPUT.c]");
        return 2;
    }
    if (load(argv[1])) return 1;
    compile();
    if (failed) return 1;

    char derived[MAX_LINE];
    const char *outpath = (argc == 3) ? argv[2]
                                      : default_output(argv[1], derived, sizeof(derived));
    FILE *f = fopen(outpath, "wb");
    if (!f) {
        printf("cora: cannot write %s\n", outpath);
        return 1;
    }
    if (glen && fwrite(generated, 1, glen, f) != glen) {
        fclose(f);
        puts("cora: write failed");
        return 1;
    }
    fclose(f);
    return 0;
}
