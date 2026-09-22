#include "script.h"
#include "kprintf.h"
#include "string.h"
#include "pmm.h"
#include "mm.h"
#include "shell.h"
#include "fs.h"
#include "stdlib.h"

/* Simple script_atoll implementation for freestanding environment */
static int64_t script_atoll(const char *s) {
    int64_t result = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

/* ---- lexer ---- */

typedef enum {
    TOK_NUMBER, TOK_STRING, TOK_IDENT,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ,
    TOK_ASSIGN,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_SEMICOLON, TOK_COMMA, TOK_DOT, TOK_COLON,
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_PRINT, TOK_LET, TOK_CONST,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_THIS,
    TOK_FUNCTION, TOK_RETURN, TOK_TRY, TOK_CATCH, TOK_THROW,
    TOK_ARROW, TOK_BREAK, TOK_CONTINUE,
    TOK_EOF, TOK_ERROR
} tok_type_t;

typedef struct {
    tok_type_t type;
    int64_t num_val;
    char *str_val;
} token_t;

typedef struct {
    const char *start;
    const char *pos;
    token_t cur;
    char error[128];
} lexer_t;

static const char *kw[] = {
    "if", "else", "while", "for", "print", "let", "const", "and", "or", "not",
    "true", "false", "null", "this", "function", "return", "try", "catch", "throw",
    "break", "continue",
    0
};

static tok_type_t kw_type[] = {
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_PRINT, TOK_LET, TOK_CONST,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_TRUE, TOK_FALSE, TOK_NULL, TOK_THIS, TOK_FUNCTION, TOK_RETURN,
    TOK_TRY, TOK_CATCH, TOK_THROW, TOK_BREAK, TOK_CONTINUE
};

static int kw_lookup(const char *s, int len) {
    for (int i = 0; kw[i]; i++) {
        const char *k = kw[i];
        int j;
        for (j = 0; j < len && k[j] && s[j] == k[j]; j++);
        if (j == len && k[j] == 0) return i;
    }
    return -1;
}

static char lex_peek(lexer_t *lx) { return *lx->pos; }

static void lex_skip_ws(lexer_t *lx) {
    while (*lx->pos == ' ' || *lx->pos == '\t' || *lx->pos == '\r') lx->pos++;
}

static int lex_next_token(lexer_t *lx) {
    lex_skip_ws(lx);
    lx->cur.str_val = 0;
    lx->cur.num_val = 0;

    char c = lex_peek(lx);
    if (c == 0) { lx->cur.type = TOK_EOF; return 0; }
    if (c == '\n') { lx->pos++; lx->cur.type = TOK_SEMICOLON; return 0; }

    if (c == '#') {
        while (*lx->pos && *lx->pos != '\n') lx->pos++;
        if (*lx->pos == '\n') lx->pos++;
        return lex_next_token(lx);
    }

    if (c >= '0' && c <= '9') {
        int64_t v = 0;
        while (*lx->pos >= '0' && *lx->pos <= '9') {
            v = v * 10 + (*lx->pos - '0');
            lx->pos++;
        }
        lx->cur.type = TOK_NUMBER;
        lx->cur.num_val = v;
        return 0;
    }

    if (c == '"') {
        lx->pos++;
        const char *start = lx->pos;
        int len = 0;
        while (*lx->pos && *lx->pos != '"') {
            if (*lx->pos == '\\' && lx->pos[1]) { lx->pos += 2; len++; continue; }
            lx->pos++; len++;
        }
        if (*lx->pos != '"') { lx->cur.type = TOK_ERROR; return -1; }
        lx->pos++;
        lx->cur.type = TOK_STRING;
        char *s = (char*)malloc(len + 1);
        if (!s) return -1;
        int len2 = 0;
        const char *p = start;
        while (p < lx->pos - 1) {
            if (*p == '\\' && p + 1 < lx->pos - 1) {
                p++;
                switch (*p) {
                case 'n': s[len2++] = '\n'; break;
                case 't': s[len2++] = '\t'; break;
                case 'r': s[len2++] = '\r'; break;
                case '\\': s[len2++] = '\\'; break;
                case '"': s[len2++] = '"'; break;
                default: s[len2++] = *p; break;
                }
                p++;
            } else {
                s[len2++] = *p++;
            }
        }
        s[len2] = 0;
        lx->cur.str_val = s;
        return 0;
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        const char *start = lx->pos;
        int len = 0;
        while ((*lx->pos >= 'a' && *lx->pos <= 'z') ||
               (*lx->pos >= 'A' && *lx->pos <= 'Z') ||
               (*lx->pos >= '0' && *lx->pos <= '9') ||
               *lx->pos == '_') { lx->pos++; len++; }
        int ki = kw_lookup(start, len);
        if (ki >= 0) {
            lx->cur.type = kw_type[ki];
            return 0;
        }
        lx->cur.type = TOK_IDENT;
        char *s = (char*)malloc(len + 1);
        if (!s) return -1;
        for (int i = 0; i < len; i++) s[i] = start[i];
        s[len] = 0;
        lx->cur.str_val = s;
        return 0;
    }

    lx->pos++;
    switch (c) {
    case '=':
        if (lex_peek(lx) == '=') { lx->pos++; lx->cur.type = TOK_EQ; }
        else if (lex_peek(lx) == '>') { lx->pos++; lx->cur.type = TOK_ARROW; }
        else lx->cur.type = TOK_ASSIGN;
        return 0;
    case '!':
        if (lex_peek(lx) == '=') { lx->pos++; lx->cur.type = TOK_NEQ; }
        else { lx->cur.type = TOK_ERROR; lx->error[0] = 0; }
        return 0;
    case '<':
        if (lex_peek(lx) == '=') { lx->pos++; lx->cur.type = TOK_LEQ; }
        else lx->cur.type = TOK_LT;
        return 0;
    case '>':
        if (lex_peek(lx) == '=') { lx->pos++; lx->cur.type = TOK_GEQ; }
        else lx->cur.type = TOK_GT;
        return 0;
    case '+': lx->cur.type = TOK_PLUS; return 0;
    case '-': lx->cur.type = TOK_MINUS; return 0;
    case '*': lx->cur.type = TOK_STAR; return 0;
    case '/': lx->cur.type = TOK_SLASH; return 0;
    case '%': lx->cur.type = TOK_PERCENT; return 0;
    case '(': lx->cur.type = TOK_LPAREN; return 0;
    case ')': lx->cur.type = TOK_RPAREN; return 0;
    case '{': lx->cur.type = TOK_LBRACE; return 0;
    case '}': lx->cur.type = TOK_RBRACE; return 0;
    case '[': lx->cur.type = TOK_LBRACKET; return 0;
    case ']': lx->cur.type = TOK_RBRACKET; return 0;
    case ';': lx->cur.type = TOK_SEMICOLON; return 0;
    case ',': lx->cur.type = TOK_COMMA; return 0;
    case '.': lx->cur.type = TOK_DOT; return 0;
    case ':': lx->cur.type = TOK_COLON; return 0;
    case '`': {
        /* Template literal - parse until closing backtick */
        lx->pos++;
        const char *start = lx->pos;
        int len = 0;
        while (*lx->pos && *lx->pos != '`') { lx->pos++; len++; }
        if (*lx->pos != '`') { lx->cur.type = TOK_ERROR; return -1; }
        lx->pos++;
        lx->cur.type = TOK_STRING;  /* reuse string type for template literals */
        char *s = (char*)malloc(len + 1);
        if (!s) return -1;
        for (int i = 0; i < len; i++) s[i] = start[i];
        s[len] = 0;
        lx->cur.str_val = s;
        return 0;
    }
    default:
        /* Check for arrow => */
        if (c == '=' && lex_peek(lx) == '>') {
            lx->pos += 2;
            lx->cur.type = TOK_ARROW;
            return 0;
        }
        lx->cur.type = TOK_ERROR;
        return -1;
    }
    return 0;
}

/* ---- parser ---- */

typedef enum {
    NODE_NUMBER = 0, NODE_BINOP = 1, NODE_IF = 2, NODE_WHILE = 3, NODE_UNARY = 4,
    NODE_ASSIGN = 5, NODE_PRINT = 6, NODE_VAR = 7, NODE_BLOCK = 8, NODE_CALL = 9,
    NODE_STRING = 10, NODE_PROGRAM = 11, NODE_STMT = 12, NODE_OBJECT = 13,
    NODE_PROP = 14, NODE_ARRAY = 15, NODE_ARROW_FUNC = 16, NODE_PARAM = 17,
    NODE_TRUE = 18, NODE_FALSE = 19, NODE_NULL = 20, NODE_THIS = 21,
    NODE_PROP_ACCESS = 22, NODE_INDEX_ACCESS = 23, NODE_METHOD_CALL = 24,
    NODE_CONST_ASSIGN = 25, NODE_THROW = 26, NODE_TRY = 27, NODE_FUNC_DECL = 28,
    NODE_FOR = 29, NODE_RETURN = 30, NODE_BREAK = 31, NODE_CONTINUE = 32
} node_type_t;

typedef struct node {
    int type;
    struct node *left, *right, *extra;
    int64_t num_val;
    char *str_val;
} node_t;

static node_t *new_node(int type) {
    node_t *n = (node_t*)calloc(1, sizeof(node_t));
    if (!n) return 0;
    n->type = type;
    return n;
}

static void free_lex_token(lexer_t *lx) {
    if (lx->cur.str_val) {
        free(lx->cur.str_val);
        lx->cur.str_val = 0;
    }
}

static void free_node(node_t *n) {
    if (!n) return;
    free_node(n->left);
    free_node(n->right);
    free_node(n->extra);
    if (n->str_val) free(n->str_val);
    free(n);
}

/* Deep copy of an AST subtree so function bodies survive the program tree
 * being freed after each script_eval. */
static node_t *node_dup(node_t *n) {
    if (!n) return 0;
    node_t *c = new_node(n->type);
    if (!c) return 0;
    c->num_val = n->num_val;
    if (n->str_val) c->str_val = strdup(n->str_val);
    c->left = node_dup(n->left);
    c->right = node_dup(n->right);
    c->extra = node_dup(n->extra);
    return c;
}

static node_t *parse_stmt(lexer_t *lx);
static node_t *parse_expr(lexer_t *lx);
static node_t *parse_or(lexer_t *lx);
static node_t *parse_and(lexer_t *lx);
static node_t *parse_cmp(lexer_t *lx);
static node_t *parse_add(lexer_t *lx);
static node_t *parse_mul(lexer_t *lx);
static node_t *parse_unary(lexer_t *lx);
static node_t *parse_primary(lexer_t *lx);
static node_t *parse_postfix(lexer_t *lx);
static node_t *parse_object(lexer_t *lx);
static node_t *parse_array(lexer_t *lx);
static node_t *parse_block(lexer_t *lx);
static node_t *parse_substmt(lexer_t *lx);

static int lex_accept(lexer_t *lx, tok_type_t t) {
    if (lx->cur.type == t) { lex_next_token(lx); return 1; }
    return 0;
}

/* Body of if/while/for: either a {..} block or a single statement. */
static node_t *parse_substmt(lexer_t *lx) {
    if (lx->cur.type == TOK_LBRACE) return parse_block(lx);
    node_t *s = parse_stmt(lx);
    if (!s) return 0;
    node_t *b = new_node(NODE_BLOCK);
    b->left = s;
    return b;
}

static int lex_expect(lexer_t *lx, tok_type_t t) {
    if (lx->cur.type == t) { lex_next_token(lx); return 1; }
    return 0;
}

static node_t *parse_block(lexer_t *lx) {
    if (!lex_expect(lx, TOK_LBRACE)) return 0;
    node_t *block = new_node(NODE_BLOCK);
    node_t **tail = &block->left;
    while (lx->cur.type != TOK_RBRACE && lx->cur.type != TOK_EOF) {
        *tail = parse_stmt(lx);
        if (!*tail) { free_node(block); return 0; }
        tail = &(*tail)->extra;
        lex_accept(lx, TOK_SEMICOLON);
    }
    if (!lex_expect(lx, TOK_RBRACE)) { free_node(block); return 0; }
    return block;
}

static node_t *parse_stmt(lexer_t *lx) {
    if (lx->cur.type == TOK_LET || lx->cur.type == TOK_CONST) {
        bool is_const = (lx->cur.type == TOK_CONST);
        lex_next_token(lx);
        if (lx->cur.type != TOK_IDENT) { free_lex_token(lx); return 0; }
        node_t *n = new_node(is_const ? NODE_CONST_ASSIGN : NODE_ASSIGN);
        n->str_val = lx->cur.str_val; lx->cur.str_val = 0;
        lex_next_token(lx);
        if (!lex_expect(lx, TOK_ASSIGN)) { free_node(n); return 0; }
        n->left = parse_expr(lx);
        return n;
    }
    if (lx->cur.type == TOK_FUNCTION) {
        lex_next_token(lx);
        if (lx->cur.type != TOK_IDENT) { free_lex_token(lx); return 0; }
        char *fname = lx->cur.str_val; lx->cur.str_val = 0;
        lex_next_token(lx);
        if (!lex_expect(lx, TOK_LPAREN)) { free(fname); return 0; }
        node_t *fn = new_node(NODE_FUNC_DECL);
        fn->str_val = fname;
        node_t **param_tail = &fn->left;
        while (lx->cur.type != TOK_RPAREN && lx->cur.type != TOK_EOF) {
            if (lx->cur.type == TOK_IDENT) {
                char *pname = lx->cur.str_val; lx->cur.str_val = 0;
                node_t *p = new_node(NODE_PARAM);
                p->str_val = pname;
                *param_tail = p;
                param_tail = &p->extra;
            }
            lex_next_token(lx);
            if (!lex_accept(lx, TOK_COMMA)) break;
        }
        lex_expect(lx, TOK_RPAREN);
        fn->right = parse_block(lx);
        return fn;
    }
    if (lx->cur.type == TOK_PRINT) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_PRINT);
        n->left = parse_expr(lx);
        return n;
    }
    if (lx->cur.type == TOK_RETURN) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_RETURN);
        if (lx->cur.type == TOK_SEMICOLON || lx->cur.type == TOK_RBRACE ||
            lx->cur.type == TOK_EOF)
            n->left = 0;  /* bare `return;` */
        else
            n->left = parse_expr(lx);
        return n;
    }
    if (lx->cur.type == TOK_BREAK) {
        lex_next_token(lx);
        return new_node(NODE_BREAK);
    }
    if (lx->cur.type == TOK_CONTINUE) {
        lex_next_token(lx);
        return new_node(NODE_CONTINUE);
    }
    if (lx->cur.type == TOK_IF) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_IF);
        n->left = parse_expr(lx);
        n->right = parse_substmt(lx);
        n->extra = 0;
        if (lx->cur.type == TOK_ELSE) {
            lex_next_token(lx);
            n->extra = parse_substmt(lx);
        }
        /* wrap in a stmt node so n->extra (else branch) doesn't
           collide with program-level statement chaining */
        node_t *w = new_node(NODE_STMT);
        w->left = n;
        return w;
    }
    if (lx->cur.type == TOK_FOR) {
        lex_next_token(lx);
        if (!lex_expect(lx, TOK_LPAREN)) return 0;
        node_t *n = new_node(NODE_FOR);
        /* init — allow `let i = ...` / `const i = ...` as well as `i = ...` */
        if (lx->cur.type == TOK_SEMICOLON) {
            n->left = 0;
        } else if (lx->cur.type == TOK_LET || lx->cur.type == TOK_CONST) {
            bool is_const = (lx->cur.type == TOK_CONST);
            lex_next_token(lx);
            if (lx->cur.type != TOK_IDENT) return 0;
            node_t *init = new_node(is_const ? NODE_CONST_ASSIGN : NODE_ASSIGN);
            init->str_val = lx->cur.str_val; lx->cur.str_val = 0;
            lex_next_token(lx);
            if (!lex_expect(lx, TOK_ASSIGN)) { free_node(init); return 0; }
            init->left = parse_expr(lx);
            n->left = init;
        } else {
            n->left = parse_expr(lx);
        }
        lex_accept(lx, TOK_SEMICOLON);
        /* condition */
        if (lx->cur.type == TOK_SEMICOLON) {
            n->extra = 0;  /* no condition = infinite loop */
        } else {
            n->extra = parse_expr(lx);
        }
        lex_accept(lx, TOK_SEMICOLON);
        /* update - stored in cond->extra to save a field */
        if (lx->cur.type == TOK_RPAREN) {
            if (n->extra) n->extra->extra = 0;
        } else {
            node_t *upd = parse_expr(lx);
            if (n->extra) n->extra->extra = upd;
        }
        lex_expect(lx, TOK_RPAREN);
        /* body */
        n->right = parse_substmt(lx);
        /* wrap in a stmt node so n->extra (condition) doesn't collide
           with program/block-level statement chaining */
        node_t *w = new_node(NODE_STMT);
        w->left = n;
        return w;
    }
    if (lx->cur.type == TOK_WHILE) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_WHILE);
        n->left = parse_expr(lx);
        n->right = parse_substmt(lx);
        return n;
    }
    if (lx->cur.type == TOK_TRY) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_TRY);
        n->left = parse_block(lx);  /* try block */
        n->right = 0;
        n->str_val = 0;
        if (lx->cur.type == TOK_CATCH) {
            lex_next_token(lx);
            lex_expect(lx, TOK_LPAREN);
            if (lx->cur.type == TOK_IDENT) {
                n->str_val = lx->cur.str_val; lx->cur.str_val = 0;
                lex_next_token(lx);
            }
            lex_expect(lx, TOK_RPAREN);
            n->right = parse_block(lx); /* catch block */
        }
        node_t *w = new_node(NODE_STMT);
        w->left = n;
        return w;
    }
    if (lx->cur.type == TOK_THROW) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_THROW);
        n->left = parse_expr(lx);
        return n;
    }
    if (lx->cur.type == TOK_LBRACE) {
        return parse_block(lx);
    }
    if (lx->cur.type == TOK_SEMICOLON) {
        lex_next_token(lx);
        return parse_stmt(lx);
    }
    if (lx->cur.type == TOK_EOF || lx->cur.type == TOK_RBRACE) return 0;
    return parse_expr(lx);
}

static node_t *parse_expr(lexer_t *lx) { return parse_or(lx); }

static node_t *parse_or(lexer_t *lx) {
    node_t *n = parse_and(lx);
    while (lx->cur.type == TOK_OR) {
        lex_next_token(lx);
        node_t *bin = new_node(NODE_BINOP);
        bin->num_val = TOK_OR;
        bin->left = n;
        bin->right = parse_and(lx);
        n = bin;
    }
    return n;
}

static node_t *parse_and(lexer_t *lx) {
    node_t *n = parse_cmp(lx);
    while (lx->cur.type == TOK_AND) {
        lex_next_token(lx);
        node_t *bin = new_node(NODE_BINOP);
        bin->num_val = TOK_AND;
        bin->left = n;
        bin->right = parse_cmp(lx);
        n = bin;
    }
    return n;
}

static node_t *parse_cmp(lexer_t *lx) {
    node_t *n = parse_add(lx);
    tok_type_t ops[] = {TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ, 0};
    while (1) {
        int found = 0;
        for (int i = 0; ops[i]; i++) {
            if (lx->cur.type == ops[i]) {
                tok_type_t op = ops[i];
                lex_next_token(lx);
                node_t *bin = new_node(NODE_BINOP);
                bin->num_val = op;
                bin->left = n;
                bin->right = parse_add(lx);
                n = bin;
                found = 1;
                break;
            }
        }
        if (!found) break;
    }
    return n;
}

static node_t *parse_add(lexer_t *lx) {
    node_t *n = parse_mul(lx);
    while (lx->cur.type == TOK_PLUS || lx->cur.type == TOK_MINUS) {
        tok_type_t op = lx->cur.type;
        lex_next_token(lx);
        node_t *bin = new_node(NODE_BINOP);
        bin->num_val = op;
        bin->left = n;
        bin->right = parse_mul(lx);
        n = bin;
    }
    return n;
}

static node_t *parse_mul(lexer_t *lx) {
    /* script engine fix: parse the first operand with parse_unary() so that a
     * leading unary `not`/`-` parses. The original used parse_postfix(),
     * which skips unary operators entirely. */
    node_t *n = parse_unary(lx);
    while (lx->cur.type == TOK_STAR || lx->cur.type == TOK_SLASH || lx->cur.type == TOK_PERCENT) {
        tok_type_t op = lx->cur.type;
        lex_next_token(lx);
        node_t *bin = new_node(NODE_BINOP);
        bin->num_val = op;
        bin->left = n;
        bin->right = parse_unary(lx);
        n = bin;
    }
    return n;
}

static node_t *parse_postfix(lexer_t *lx) {
    node_t *n = parse_primary(lx);
    if (!n) return 0;
    while (1) {
        if (lex_accept(lx, TOK_DOT)) {
            if (lx->cur.type != TOK_IDENT) { free_node(n); return 0; }
            char *prop = lx->cur.str_val; lx->cur.str_val = 0;
            lex_next_token(lx);
            node_t *access = new_node(NODE_PROP_ACCESS);
            access->left = n;
            access->str_val = prop;
            n = access;
        } else if (lex_accept(lx, TOK_LBRACKET)) {
            node_t *idx = parse_expr(lx);
            if (!idx) { free_node(n); return 0; }
            lex_expect(lx, TOK_RBRACKET);
            node_t *access = new_node(NODE_INDEX_ACCESS);
            access->left = n;
            access->right = idx;
            n = access;
        } else if (lex_accept(lx, TOK_LPAREN)) {
            /* Method call */
            node_t *call = new_node(NODE_METHOD_CALL);
            call->left = n;
            node_t **tail = &call->right;
            while (lx->cur.type != TOK_RPAREN && lx->cur.type != TOK_EOF) {
                *tail = parse_expr(lx);
                tail = &(*tail)->extra;
                if (!lex_accept(lx, TOK_COMMA)) break;
            }
            lex_expect(lx, TOK_RPAREN);
            n = call;
        } else {
            break;
        }
    }
    return n;
}

static node_t *parse_unary(lexer_t *lx) {
    if (lx->cur.type == TOK_MINUS) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_UNARY);
        n->num_val = TOK_MINUS;
        n->left = parse_unary(lx);
        return n;
    }
    if (lx->cur.type == TOK_NOT) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_UNARY);
        n->num_val = TOK_NOT;
        n->left = parse_unary(lx);
        return n;
    }
    return parse_postfix(lx);
}

static node_t *parse_primary(lexer_t *lx) {
    if (lx->cur.type == TOK_NUMBER) {
        node_t *n = new_node(NODE_NUMBER);
        n->num_val = lx->cur.num_val;
        lex_next_token(lx);
        return n;
    }
    if (lx->cur.type == TOK_STRING) {
        node_t *n = new_node(NODE_STRING);
        n->str_val = lx->cur.str_val; lx->cur.str_val = 0;
        lex_next_token(lx);
        return n;
    }
    if (lx->cur.type == TOK_TRUE) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_TRUE);
        return n;
    }
    if (lx->cur.type == TOK_FALSE) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_FALSE);
        return n;
    }
    if (lx->cur.type == TOK_NULL) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_NULL);
        return n;
    }
    if (lx->cur.type == TOK_THIS) {
        lex_next_token(lx);
        node_t *n = new_node(NODE_THIS);
        return n;
    }
    if (lx->cur.type == TOK_IDENT) {
        char *name = lx->cur.str_val; lx->cur.str_val = 0;
        lex_next_token(lx);
        /* Single-param arrow: param => body */
        if (lx->cur.type == TOK_ARROW) {
            node_t *n = new_node(NODE_ARROW_FUNC);
            node_t *p = new_node(NODE_PARAM);
            p->str_val = name;
            n->left = p;
            lex_expect(lx, TOK_ARROW);
            if (lx->cur.type == TOK_LBRACE) {
                n->right = parse_block(lx);
            } else {
                n->right = parse_expr(lx);
            }
            return n;
        }
        if (lx->cur.type == TOK_LPAREN) {
            lex_next_token(lx);
            node_t *n = new_node(NODE_CALL);
            n->str_val = name;
            node_t **tail = &n->left;
            while (lx->cur.type != TOK_RPAREN && lx->cur.type != TOK_EOF) {
                *tail = parse_expr(lx);
                tail = &(*tail)->extra;
                if (!lex_accept(lx, TOK_COMMA)) break;
            }
            if (!lex_expect(lx, TOK_RPAREN)) { free_node(n); return 0; }
            return n;
        }
        node_t *n = new_node(NODE_VAR);
        n->str_val = name;
        /* assignment also allowed in expression position, e.g. for-loop
           update `i = i + 1` or a bare `x = 5;` statement */
        if (lx->cur.type == TOK_ASSIGN) {
            lex_next_token(lx);
            n->type = NODE_ASSIGN;
            n->left = parse_expr(lx);
        }
        return n;
    }
    if (lx->cur.type == TOK_LPAREN) {
        /* Parenthesized: either (expr) or (params) => body */
        lex_next_token(lx);
        /* Peek at first token inside parens to decide: if it's an IDENT
           immediately followed by comma/=> or just =>, treat as params.
           Otherwise parse as grouped expression. */
        const char *scan = lx->pos;
        int paren_depth = 1;
        int is_arrow = 0;
        while (*scan && paren_depth > 0) {
            if (*scan == '(') paren_depth++;
            else if (*scan == ')') {
                paren_depth--;
                if (paren_depth == 0) {
                    const char *after = scan + 1;
                    while (*after == ' ' || *after == '\t') after++;
                    if (after[0] == '=' && after[1] == '>') is_arrow = 1;
                    break;
                }
            }
            scan++;
        }
        if (is_arrow) {
            /* Back up to just after LPAREN and parse as arrow function params */
            node_t *n = new_node(NODE_ARROW_FUNC);
            node_t **param_tail = &n->left;
            while (lx->cur.type != TOK_RPAREN && lx->cur.type != TOK_EOF) {
                if (lx->cur.type != TOK_IDENT) { free_node(n); return 0; }
                char *pname = lx->cur.str_val; lx->cur.str_val = 0;
                node_t *p = new_node(NODE_PARAM);
                p->str_val = pname;
                *param_tail = p;
                param_tail = &p->extra;
                lex_next_token(lx);
                if (!lex_accept(lx, TOK_COMMA)) break;
            }
            lex_expect(lx, TOK_RPAREN);
            lex_expect(lx, TOK_ARROW);
            if (lx->cur.type == TOK_LBRACE) {
                n->right = parse_block(lx);
            } else {
                n->right = parse_expr(lx);
            }
            return n;
        }
        /* Grouped expression */
        node_t *n = parse_expr(lx);
        lex_expect(lx, TOK_RPAREN);
        return n;
    }
    if (lx->cur.type == TOK_LBRACE) {
        return parse_object(lx);
    }
    if (lx->cur.type == TOK_LBRACKET) {
        return parse_array(lx);
    }
    return 0;
}

static node_t *parse_object(lexer_t *lx) {
    if (!lex_expect(lx, TOK_LBRACE)) return 0;
    node_t *n = new_node(NODE_OBJECT);
    node_t **tail = &n->left;
    while (lx->cur.type != TOK_RBRACE && lx->cur.type != TOK_EOF) {
        if (lx->cur.type != TOK_IDENT && lx->cur.type != TOK_STRING) {
            free_node(n);
            return 0;
        }
        char *key = lx->cur.str_val;
        lx->cur.str_val = 0;
        lex_next_token(lx);
        if (!lex_expect(lx, TOK_COLON)) { free(key); free_node(n); return 0; }
        node_t *val = parse_expr(lx);
        if (!val) { free(key); free_node(n); return 0; }
        node_t *prop = new_node(NODE_PROP);
        prop->str_val = key;
        prop->left = val;
        *tail = prop;
        tail = &prop->extra;
        if (!lex_accept(lx, TOK_COMMA)) break;
    }
    lex_expect(lx, TOK_RBRACE);
    return n;
}

static node_t *parse_array(lexer_t *lx) {
    if (!lex_expect(lx, TOK_LBRACKET)) return 0;
    node_t *n = new_node(NODE_ARRAY);
    node_t **tail = &n->left;
    if (lx->cur.type == TOK_RBRACKET) {
        lex_next_token(lx);
        return n;
    }
    while (lx->cur.type != TOK_RBRACKET && lx->cur.type != TOK_EOF) {
        node_t *elem = parse_expr(lx);
        if (!elem) { free_node(n); return 0; }
        *tail = elem;
        tail = &elem->extra;
        if (!lex_accept(lx, TOK_COMMA)) break;
    }
    lex_expect(lx, TOK_RBRACKET);
    return n;
}


/* ---- variable store ---- */

#define VAR_MAX 128
typedef struct {
    char name[32];
    script_val_t val;
    bool is_const;
} var_entry_t;

static var_entry_t vars[VAR_MAX];
static int var_count;

static var_entry_t *find_var(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return 0;
}

/* Value helpers. All string values handled by the interpreter are OWNED:
 * a type-1 value's .str is a fresh malloc that someone is responsible for
 * freeing once, normally the consumer of the returned script_val_t. */

static script_val_t _mknum(int64_t n) {
    script_val_t v; v.type = 0; v.num = n; v.str = 0; return v;
}
static script_val_t _mkstr(const char *s) {
    script_val_t v; v.type = 1; v.num = 0; v.str = s ? strdup(s) : 0; return v;
}
static script_val_t _mknum_from_str(const char *s) {
    return _mknum(script_atoll(s));
}
static void _free(script_val_t *v) {
    if (v->str) { free(v->str); v->str = 0; }
}
static script_val_t _dup(script_val_t v) {
    script_val_t c; c.type = v.type; c.num = v.num;
    c.str = (v.type == 1 && v.str) ? strdup(v.str) : 0;
    return c;
}

static int add_var(const char *name, script_val_t val) {
    var_entry_t *v = find_var(name);
    if (v) {
        if (v->is_const) return -1;  /* Cannot reassign const */
        _free(&v->val);
        v->val = _dup(val);
        return 0;
    }
    if (var_count >= VAR_MAX) return -1;
    int i;
    for (i = 0; name[i] && i < 31; i++) vars[var_count].name[i] = name[i];
    vars[var_count].name[i] = 0;
    vars[var_count].val = _dup(val);
    vars[var_count].is_const = false;
    var_count++;
    return 0;
}

static int add_const(const char *name, script_val_t val) {
    var_entry_t *v = find_var(name);
    if (v) {
        if (v->is_const) return -1;
        _free(&v->val);
        v->val = _dup(val);
        v->is_const = true;
        return 0;
    }
    if (var_count >= VAR_MAX) return -1;
    int i;
    for (i = 0; name[i] && i < 31; i++) vars[var_count].name[i] = name[i];
    vars[var_count].name[i] = 0;
    vars[var_count].val = _dup(val);
    vars[var_count].is_const = true;
    var_count++;
    return 0;
}

/* ---- functions ---- */

#define FUNC_MAX 64
#define FUNC_DEPTH_MAX 32

typedef struct {
    char name[40];
    int nparams;
    node_t *params;   /* chain of NODE_PARAM (deep-copied) */
    node_t *body;     /* deep-copied AST */
} funcdef_t;

static funcdef_t funcs[FUNC_MAX];
static int func_count;

/* ---- native functions ---- */

#define NATIVE_MAX 96
typedef struct {
    char name[40];
    script_native_fn fn;
} native_t;

static native_t natives[NATIVE_MAX];
static int native_count;

int script_register_func(const char *name, script_native_fn fn) {
    if (!name || !fn) return -1;
    if (native_count >= NATIVE_MAX) return -1;
    int i;
    for (i = 0; name[i] && i < 39; i++) natives[native_count].name[i] = name[i];
    natives[native_count].name[i] = 0;
    natives[native_count].fn = fn;
    native_count++;
    return 0;
}

static script_native_fn find_native(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < native_count; i++)
        if (strcmp(natives[i].name, name) == 0) return natives[i].fn;
    return 0;
}

static funcdef_t *find_func(const char *name) {
    for (int i = 0; i < func_count; i++)
        if (strcmp(funcs[i].name, name) == 0) return &funcs[i];
    return 0;
}

/* ---- interpreter control flow ---- */

enum {
    CTL_NONE = 0, CTL_RETURN = 1, CTL_THROW = 2, CTL_BREAK = 3, CTL_CONTINUE = 4
};
static int control_flow;
static script_val_t ctrl_val;

static script_val_t eval_node_body(node_t *n);
static script_val_t eval_node(node_t *n);

/* ---- shell command bridge ---- */
static int shell_call(const char *name, script_val_t *args, int nargs, script_val_t *res);

/* Depth-guarded wrapper for eval_node_body: a hostile or buggy script with
 * deeply nested expressions must not blow the kernel stack through
 * unbounded recursion. */
#define EVAL_DEPTH_MAX 512

static script_val_t eval_node(node_t *n) {
    static int depth = 0;
    if (depth >= EVAL_DEPTH_MAX) {
        kprintf("csl: expression too deeply nested\n");
        return _mknum(0);
    }
    depth++;
    script_val_t r = eval_node_body(n);
    depth--;
    return r;
}

/* eval_node_body (the interpreter switch) is defined below, after all its
 * helper functions. */

static int truthy(script_val_t v) {
    if (v.type == 0) return v.num != 0;
    return v.str != 0 && v.str[0] != 0;
}

/* WHILE-loop condition helper: eval node, test truthiness, free the value. */
static int cond_true(node_t *cond) {
    script_val_t c = eval_node(cond);
    int t = truthy(c);
    _free(&c);
    return t;
}

/* Number-to-string into an owned buffer helper. */
static char *num_to_str(int64_t n) {
    char buf[32];
    int i = 31;
    buf[i] = 0;
    uint64_t u = (n < 0) ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    do { buf[--i] = (char)('0' + (u % 10)); u /= 10; } while (u);
    if (n < 0) buf[--i] = '-';
    return strdup(buf + i);
}

static script_val_t eval_binop(node_t *n) {
    script_val_t l = eval_node(n->left);
    script_val_t r = eval_node(n->right);
    script_val_t res;
    res.type = 0;
    res.num = 0;
    res.str = 0;

    if (l.type == 1 || r.type == 1) {
        /* string concatenation with + */
        if (n->num_val == TOK_PLUS) {
            char buf[512];
            int p = 0;
            if (l.type == 1 && l.str) { for (int i = 0; l.str[i] && p < 500; i++) buf[p++] = l.str[i]; }
            else {
                char *t = num_to_str(l.num);
                for (int i = 0; t[i] && p < 500; i++) buf[p++] = t[i];
                free(t);
            }
            if (r.type == 1 && r.str) { for (int i = 0; r.str[i] && p < 500; i++) buf[p++] = r.str[i]; }
            else {
                char *t = num_to_str(r.num);
                for (int i = 0; t[i] && p < 500; i++) buf[p++] = t[i];
                free(t);
            }
            buf[p] = 0;
            res.type = 1;
            res.str = strdup(buf);
        } else if (n->num_val == TOK_EQ) {
            res.num = strcmp(l.type && l.str ? l.str : "",
                             r.type && r.str ? r.str : "") == 0;
        } else if (n->num_val == TOK_NEQ) {
            res.num = strcmp(l.type && l.str ? l.str : "",
                             r.type && r.str ? r.str : "") != 0;
        } else if (n->num_val == TOK_STAR) {
            /* string * number = repeat */
            int count = (r.type == 0) ? (int)r.num : (int)l.num;
            const char *s = (l.type == 1) ? l.str : r.str;
            char buf[1024];
            int p = 0;
            for (int i = 0; i < count && p < 1000; i++)
                for (int j = 0; s && s[j] && p < 1000; j++) buf[p++] = s[j];
            buf[p] = 0;
            res.type = 1;
            res.str = strdup(buf);
        }
        /* string/repr length comparison fallback: numeric comparison on liens */
        else if (n->num_val == TOK_LT || n->num_val == TOK_GT ||
                 n->num_val == TOK_LEQ || n->num_val == TOK_GEQ) {
            int c = strcmp(l.type && l.str ? l.str : "",
                           r.type && r.str ? r.str : "");
            if (n->num_val == TOK_LT) res.num = c < 0;
            if (n->num_val == TOK_GT) res.num = c > 0;
            if (n->num_val == TOK_LEQ) res.num = c <= 0;
            if (n->num_val == TOK_GEQ) res.num = c >= 0;
        }
        _free(&l); _free(&r);
        return res;
    }

    int64_t a = l.num, b = r.num;
    switch (n->num_val) {
    case TOK_PLUS:   res.num = a + b; break;
    case TOK_MINUS:  res.num = a - b; break;
    case TOK_STAR:   res.num = a * b; break;
    case TOK_SLASH:  if (b) res.num = a / b; break;
    case TOK_PERCENT: if (b) res.num = a % b; break;
    case TOK_EQ:     res.num = a == b; break;
    case TOK_NEQ:    res.num = a != b; break;
    case TOK_LT:     res.num = a < b; break;
    case TOK_GT:     res.num = a > b; break;
    case TOK_LEQ:    res.num = a <= b; break;
    case TOK_GEQ:    res.num = a >= b; break;
    case TOK_AND:    res.num = truthy(l) && truthy(r); break;
    case TOK_OR:     res.num = truthy(l) || truthy(r); break;
    }
    _free(&l); _free(&r);
    return res;
}

/* ---- repr helpers: work on the JSON-like string representation that
 * objects and arrays are stored as (value semantics). ---- */

static int repr_len(const char *s) {
    /* returns element count for "[...]"/"{...}" or byte length otherwise */
    if (!s) return 0;
    if (*s == '[' || *s == '{') {
        int dep = 0, n = 0, q = 0, last = 0;
        for (const char *p = s; *p; p++) {
            if (q) { if (*p == '"' && p[-1] != '\\') q = 0; continue; }
            if (*p == '"') { q = 1; continue; }
            if (*p == '[' || *p == '{') dep++;
            else if (*p == ']' || *p == '}') dep--;
            else if (*p == ',' && dep == 1 && last != '{') n++;
            last = *p;
        }
        return n + 1;
    }
    return (int)strlen(s);
}

static script_val_t repr_at(const char *s, int64_t idx) {
    /* element at idx of a "[...]"/"{...}" repr, or char-at for plain strings */
    if (!s) return _mknum(0);
    if (*s != '[' && *s != '{') {
        int len = (int)strlen(s);
        if (idx < 0) idx += len;
        if (idx < 0 || idx >= len) return _mknum(0);
        char b[2]; b[0] = s[idx]; b[1] = 0;
        return _mkstr(b);
    }
    int is_obj = (*s == '{');
    /* find the idx-th comma-delimited element */
    int dep = 0, q = 0, n = 0;
    const char *start = 0, *end = 0;
    const char *p = s + 1;
    /* skip over whitespace */
    while (*p == ' ') p++;
    if (*p == '}') return _mknum(0);
    start = p;
    while (*p) {
        if (q) { if (*p == '"' && p[-1] != '\\') q = 0; }
        else if (*p == '"') q = 1;
        else if (*p == '[' || *p == '{') dep++;
        else if (*p == ']' || *p == '}') {
            dep--;
            if (dep < 0 && n == idx) { end = p; break; }
        } else if (*p == ',' && dep == 0 && n == idx) { end = p; break; }
        else if (*p == ',' && dep == 0) { n++; if (n == idx) { start = p + 1; while (*start == ' ') start++; } }
        p++;
        if (dep < 0) break;
    }
    if (!end) {
        const char *e = s + (int)strlen(s);
        while (e > p && (*e == ' ' || *e == '}' || *e == ']')) e--;
        end = e + 1;
    }
    if (n != idx && !(n == idx - 1)) return _mknum(0);
    /* extract [start, end) */
    int len = (int)(end - start);
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '}' || start[len-1] == ']')) len--;
    char *buf = malloc(len + 1);
    if (!buf) return _mknum(0);
    for (int i = 0; i < len; i++) buf[i] = start[i];
    buf[len] = 0;
    /* trim leading spaces */
    char *t = buf;
    while (*t == ' ') t++;
    /* if quoted, unquote */
    script_val_t v;
    if (*t == '"') {
        int ql = (int)strlen(t);
        if (ql >= 2 && t[ql-1] == '"') { t[ql-1] = 0; t++; }
        v = _mkstr(t);
    } else {
        v = _mknum_from_str(t);
    }
    /* object member: value is "key: value"; return the value part only after colon */
    if (is_obj) {
        /* find colon */
        /* We already trimmed; re-scan the extracted token */
        int q2 = 0;
        const char *colon = 0;
        for (const char *x = t; *x; x++) {
            if (q2) { if (*x == '"' && x[-1] != '\\') q2 = 0; }
            else if (*x == '"') q2 = 1;
            else if (*x == ':' && q2 == 0) { colon = x; break; }
        }
        if (colon) {
            const char *vs = colon + 1;
            while (*vs == ' ') vs++;
            if (*vs == '"') {
                const char *ve = vs + 1;
                while (*ve && !(*ve == '"' && ve[-1] != '\\')) ve++;
                char rb[256]; int rl = 0;
                for (const char *z = vs + 1; z < ve && rl < 255; z++) rb[rl++] = *z;
                rb[rl] = 0;
                _free(&v); v = _mkstr(rb);
            } else {
                _free(&v); v = _mknum_from_str(vs);
            }
        }
    }
    free(buf);
    return v;
}

static script_val_t repr_obj_key(const char *s, const char *key) {
    /* find "key": value inside a { ... } repr */
    if (!s || !key || *s != '{') return _mknum(0);
    int len = (int)strlen(key);
    const char *p = s;
    while (*p) {
        if (*p == '"') {
            const char *ks = p + 1;
            const char *ke = ks;
            while (*ke && !(*ke == '"' && ke[-1] != '\\')) ke++;
            if (ke - ks == len && strncmp(ks, key, len) == 0) {
                /* find colon after */
                const char *cl = ke + 1;
                while (*cl && *cl != ':') cl++;
                if (*cl == ':') {
                    const char *vs = cl + 1;
                    while (*vs == ' ') vs++;
                    if (*vs == '"') {
                        const char *ve = vs + 1;
                        while (*ve && !(*ve == '"' && ve[-1] != '\\')) ve++;
                        char rb[256]; int rl = 0;
                        for (const char *z = vs + 1; z < ve && rl < 255; z++) rb[rl++] = *z;
                        rb[rl] = 0;
                        return _mkstr(rb);
                    }
                    return _mknum_from_str(vs);
                }
            }
            p = ke + 1;
        } else {
            p++;
        }
    }
    return _mknum(0);
}

/* ---- function invocation ---- */

/* script engine fix: the original kept a single save slot, so nested function
 * calls overwrote the in-flight frame and corrupted variables whenever a body
 * made more than one call (e.g. fib(n-1) + fib(n-2)). Use a stack of frames;
 * call depth is bounded by FUNC_DEPTH_MAX (32), so that many frames suffice. */
#define VAR_SAVE_STACK 32
static var_entry_t saved_vars[VAR_SAVE_STACK][VAR_MAX];
static int saved_var_count[VAR_SAVE_STACK];
static int save_depth;

static void vars_save(void) {
    if (save_depth >= VAR_SAVE_STACK) return;
    saved_var_count[save_depth] = var_count;
    for (int i = 0; i < var_count; i++) {
        saved_vars[save_depth][i] = vars[i];
        saved_vars[save_depth][i].val = _dup(vars[i].val);
    }
    save_depth++;
}

static void vars_restore(void) {
    if (save_depth == 0) {
        var_count = 0;
        return;
    }
    save_depth--;
    for (int i = 0; i < var_count; i++) _free(&vars[i].val);
    var_count = saved_var_count[save_depth];
    for (int i = 0; i < var_count; i++) {
        vars[i] = saved_vars[save_depth][i];
        saved_vars[save_depth][i].val.str = 0;  /* ownership moved to vars */
    }
}

static script_val_t run_impl(node_t *params, node_t *body, int nparams, script_val_t *args, int nargs) {
    (void)nparams;
    static int depth = 0;
    if (depth >= FUNC_DEPTH_MAX) {
        kprintf("csl: recursion depth exceeded\n");
        return _mknum(0);
    }
    depth++;

    int prev_ctrl = control_flow;
    script_val_t prev_ctrlv = ctrl_val;
    control_flow = CTL_NONE;
    ctrl_val = _mknum(0);

    vars_save();

    node_t *p = params;
    int i = 0;
    while (p) {
        const char *pname = p->str_val ? p->str_val : "";
        add_var(pname, (i < nargs) ? args[i] : _mknum(0));
        p = p->extra;
        i++;
    }

    script_val_t r = eval_node(body);

    script_val_t result;
    if (control_flow == CTL_RETURN) {
        result = ctrl_val;
        ctrl_val.str = 0;
        control_flow = CTL_NONE;
        _free(&r);
    } else if (control_flow == CTL_THROW) {
        result = _mknum(0);
        _free(&r);
        /* keep CTL_THROW + ctrl_val */
    } else if (control_flow == CTL_BREAK || control_flow == CTL_CONTINUE) {
        control_flow = CTL_NONE;
        result = _mknum(0);
        _free(&r);
    } else {
        result = r;
    }

    vars_restore();

    if (control_flow == CTL_THROW) {
        /* pending throw propagates; keep ctrl_val */
    } else {
        control_flow = prev_ctrl;
        ctrl_val = prev_ctrlv;
    }

    depth--;
    return result;
}

static script_val_t eval_node_body(node_t *n) {
    script_val_t v = _mknum(0);

    if (!n) return v;

    switch (n->type) {
    case NODE_NUMBER:
        return _mknum(n->num_val);
    case NODE_STRING:
        return _mkstr(n->str_val);
    case NODE_TRUE:
        return _mknum(1);
    case NODE_FALSE:
        return _mknum(0);
    case NODE_NULL:
        return _mknum(0);
    case NODE_THIS:
        return _mknum(0);
    case NODE_VAR: {
        var_entry_t *ve = find_var(n->str_val);
        if (ve) return _dup(ve->val);
        return _mknum(0);
    }
    case NODE_UNARY: {
        script_val_t c = eval_node(n->left);
        if (n->num_val == TOK_MINUS) c.num = -c.num;
        if (n->num_val == TOK_NOT) { c.num = !truthy(c); _free(&c); c.str = 0; }
        return c;
    }
    case NODE_BINOP:
        return eval_binop(n);
    case NODE_ASSIGN: {
        script_val_t val = eval_node(n->left);
        if (add_var(n->str_val, val) < 0) kprintf("csl: cannot assign to const\n");
        _free(&val);
        return _mknum(0);
    }
    case NODE_CONST_ASSIGN: {
        script_val_t val = eval_node(n->left);
        if (add_const(n->str_val, val) < 0) kprintf("csl: const already defined\n");
        _free(&val);
        return _mknum(0);
    }
    case NODE_PRINT: {
        script_val_t val = eval_node(n->left);
        if (val.type == 1) kprintf("%s\n", val.str ? val.str : "");
        else kprintf("%lld\n", val.num);
        _free(&val);
        return _mknum(0);
    }
    case NODE_FUNC_DECL: {
        if (func_count < FUNC_MAX && n->str_val && !find_func(n->str_val)) {
            funcdef_t *f = &funcs[func_count++];
            int i; for (i = 0; n->str_val[i] && i < 39; i++) f->name[i] = n->str_val[i];
            f->name[i] = 0;
            f->params = node_dup(n->left);
            f->body = node_dup(n->right);
            int np = 0;
            for (node_t *p = n->left; p; p = p->extra) np++;
            f->nparams = np;
        }
        return _mknum(0);
    }
    case NODE_ARROW_FUNC: {
        /* store as an anonymous function value */
        if (func_count < FUNC_MAX) {
            funcdef_t *f = &funcs[func_count++];
            int i;
            for (i = 0; i < 38; i++) f->name[i] = '#';
            f->name[38] = 0;
            f->name[0] = '_';
            char idbuf[16];
            int nb = 0;
            int tmp = func_count;
            char rev[16]; int rl = 0;
            do { rev[rl++] = (char)('0' + tmp % 10); tmp /= 10; } while (tmp);
            for (int k = rl - 1; k >= 0 && nb < 14; k--) idbuf[nb++] = rev[k];
            idbuf[nb] = 0;
            for (int k = 0; idbuf[k] && k < 37; k++) f->name[1 + k] = idbuf[k];
            f->params = node_dup(n->left);
            f->body = node_dup(n->right);
            int np = 0;
            for (node_t *p = n->left; p; p = p->extra) np++;
            f->nparams = np;
            script_val_t fv = _mknum(0);
            fv.type = 4;
            fv.num = func_count - 1;
            return fv;
        }
        return _mknum(0);
    }
    case NODE_CALL: {
        int nargs = 0;
        script_val_t args[16];
        node_t *a = n->left;
        while (a && nargs < 16) {
            args[nargs++] = eval_node(a);
            a = a->extra;
        }
        script_val_t r = _mknum(0);

        script_native_fn nat = find_native(n->str_val);
        if (nat) {
            r = nat(args, nargs);
        } else {
            funcdef_t *f = find_func(n->str_val);
            if (f) {
                r = run_impl(f->params, f->body, f->nparams, args, nargs);
            } else {
                var_entry_t *ve = find_var(n->str_val);
                if (ve && ve->val.type == 4 && ve->val.num >= 0 && ve->val.num < func_count) {
                    funcdef_t *f2 = &funcs[ve->val.num];
                    r = run_impl(f2->params, f2->body, f2->nparams, args, nargs);
                } else {
                    shell_call(n->str_val, args, nargs, &r);
                }
            }
        }
        for (int i = 0; i < nargs; i++) _free(&args[i]);
        return r;
    }
    case NODE_METHOD_CALL: {
        /* n->left is PROP_ACCESS( receiver , method ) */
        const char *method = 0;
        script_val_t receiver;
        if (n->left && n->left->type == NODE_PROP_ACCESS) {
            method = n->left->str_val;
            receiver = eval_node(n->left->left);
        } else {
            receiver = eval_node(n->left);
        }
        int nargs = 0;
        script_val_t args[16];
        node_t *a = n->right;
        while (a && nargs < 16) {
            args[nargs++] = eval_node(a);
            a = a->extra;
        }

        script_val_t r = _mknum(0);

        script_native_fn nat = method ? find_native(method) : 0;
        if (method && nat) {
            /* for Math-constants (null receiver) call natives directly */
            script_native_fn m = nat;
            if (receiver.type == 0 && receiver.num == 0 && !receiver.str) {
                r = m(args, nargs);
            } else {
                script_val_t with_rev[17];
                with_rev[0] = receiver;
                for (int i = 0; i < nargs && i < 16; i++) with_rev[i + 1] = args[i];
                r = m(with_rev, nargs < 16 ? nargs + 1 : 16);
                with_rev[0].str = 0;
            }
        } else if (receiver.type == 0 && method) {
            /* plain numeric receiver with an unknown method fall to shell */
            r = _mknum(0);
        } else if (receiver.type == 1) {
            shell_call(method ? method : "", args, nargs, &r);
        } else {
            shell_call(method ? method : "", args, nargs, &r);
        }

        _free(&receiver);
        for (int i = 0; i < nargs; i++) _free(&args[i]);
        return r;
    }
    case NODE_PROP_ACCESS: {
        script_val_t obj = eval_node(n->left);
        const char *key = n->str_val;
        if (obj.type == 1 && obj.str) {
            if (strcmp(key, "length") == 0) {
                int64_t L = repr_len(obj.str);
                _free(&obj);
                return _mknum(L);
            }
            script_val_t kv = repr_obj_key(obj.str, key);
            _free(&obj);
            return kv;
        }
        _free(&obj);
        return _mknum(0);
    }
    case NODE_INDEX_ACCESS: {
        script_val_t obj = eval_node(n->left);
        script_val_t idx = eval_node(n->right);
        script_val_t r = _mknum(0);
        if (obj.type == 1 && obj.str) {
            if (idx.type == 1 && idx.str && obj.str[0] == '{') {
                r = repr_obj_key(obj.str, idx.str);
            } else {
                r = repr_at(obj.str, idx.num);
            }
        }
        _free(&obj);
        _free(&idx);
        return r;
    }
    case NODE_IF: {
        script_val_t c = eval_node(n->left);
        int t = truthy(c);
        _free(&c);
        script_val_t r = t ? (n->right ? eval_node(n->right) : _mknum(0))
                          : (n->extra ? eval_node(n->extra) : _mknum(0));
        return r;
    }
    case NODE_WHILE: {
        script_val_t r = _mknum(0);
        while (1) {
            if (control_flow == CTL_CONTINUE) control_flow = CTL_NONE;
            if (control_flow != CTL_NONE) break;
            if (!cond_true(n->left)) break;
            _free(&r);
            r = eval_node(n->right);
            if (control_flow == CTL_BREAK) { control_flow = CTL_NONE; break; }
        }
        return r;
    }
    case NODE_FOR: {
        if (n->left) { script_val_t iv = eval_node(n->left); _free(&iv); }
        script_val_t r = _mknum(0);
        while (control_flow == CTL_NONE || control_flow == CTL_CONTINUE) {
            if (control_flow == CTL_CONTINUE) control_flow = CTL_NONE;
            if (n->extra) {
                script_val_t c = eval_node(n->extra);
                int t = truthy(c);
                _free(&c);
                if (!t) break;
            }
            _free(&r);
            r = eval_node(n->right);
            if (control_flow == CTL_BREAK) { control_flow = CTL_NONE; break; }
            if (n->extra && n->extra->extra) {
                script_val_t uv = eval_node(n->extra->extra);
                _free(&uv);
            }
        }
        return r;
    }
    case NODE_BLOCK: {
        script_val_t r = _mknum(0);
        node_t *s = n->left;
        while (s) {
            if (control_flow != CTL_NONE) break;
            _free(&r);
            r = eval_node(s);
            s = s->extra;
        }
        return r;
    }
    case NODE_STMT:
        return eval_node(n->left);
    case NODE_OBJECT: {
        char buf[1024];
        int p = 0;
        buf[p++] = '{';
        node_t *prop = n->left;
        bool first = true;
        while (prop) {
            if (control_flow != CTL_NONE) break;
            if (!first && p < 1000) { buf[p++] = ','; buf[p++] = ' '; }
            first = false;
            if (prop->str_val) {
                if (p < 998) buf[p++] = '"';
                for (int i = 0; prop->str_val[i] && p < 995; i++) buf[p++] = prop->str_val[i];
                if (p < 998) buf[p++] = '"';
            }
            if (p < 998) { buf[p++] = ':'; buf[p++] = ' '; }
            script_val_t val = eval_node(prop->left);
            if (val.type == 1) {
                if (p < 998) buf[p++] = '"';
                for (int i = 0; val.str && val.str[i] && p < 995; i++) buf[p++] = val.str[i];
                if (p < 998) buf[p++] = '"';
            } else {
                char *t = num_to_str(val.num);
                for (int i = 0; t[i] && p < 995; i++) buf[p++] = t[i];
                free(t);
            }
            _free(&val);
            prop = prop->extra;
        }
        if (p > 0 && buf[p-1] != '{') {
            while (p > 0 && buf[p-1] == ' ') p--;
        }
        buf[p++] = '}';
        buf[p] = 0;
        script_val_t r = _mkstr(buf);
        return r;
    }
    case NODE_ARRAY: {
        char buf[1024];
        int p = 0;
        buf[p++] = '[';
        node_t *elem = n->left;
        bool first = true;
        while (elem) {
            if (control_flow != CTL_NONE) break;
            if (!first && p < 1000) { buf[p++] = ','; buf[p++] = ' '; }
            first = false;
            script_val_t val = eval_node(elem);
            if (val.type == 1) {
                if (p < 1000) buf[p++] = '"';
                for (int i = 0; val.str && val.str[i] && p < 997; i++) buf[p++] = val.str[i];
                if (p < 1000) buf[p++] = '"';
            } else {
                char *t = num_to_str(val.num);
                for (int i = 0; t[i] && p < 997; i++) buf[p++] = t[i];
                free(t);
            }
            _free(&val);
            elem = elem->extra;
        }
        buf[p++] = ']';
        buf[p] = 0;
        script_val_t r = _mkstr(buf);
        return r;
    }
    case NODE_TRY: {
        int saved = control_flow;
        script_val_t savedv = ctrl_val;
        control_flow = CTL_NONE;
        ctrl_val = _mknum(0);

        script_val_t r = eval_node(n->left);

        if (control_flow == CTL_THROW) {
            /* bind catch var and run catch block */
            if (n->str_val && n->right) {
                script_val_t ev = ctrl_val;
                ctrl_val.str = 0;
                control_flow = CTL_NONE;
                add_var(n->str_val, ev);
                _free(&ev);
                _free(&r);
                r = eval_node(n->right);
            } else {
                /* no catch handler — swallow at top level */
                _free(&ctrl_val); ctrl_val = _mknum(0);
                control_flow = CTL_NONE;
            }
        }
        /* propagate an outer pending control flow from save state */
        if (control_flow == CTL_NONE) {
            control_flow = saved;
            ctrl_val = savedv;
        } else {
            _free(&savedv);
        }
        return r;
    }
    case NODE_THROW: {
        script_val_t val = n->left ? eval_node(n->left) : _mknum(0);
        _free(&ctrl_val);
        ctrl_val = val;
        control_flow = CTL_THROW;
        return _mknum(0);
    }
    case NODE_RETURN: {
        script_val_t val = n->left ? eval_node(n->left) : _mknum(0);
        if (control_flow == CTL_NONE) {
            _free(&ctrl_val);
            ctrl_val = val;
            control_flow = CTL_RETURN;
            val.str = 0;
        } else {
            _free(&val);
        }
        return _mknum(0);
    }
    case NODE_BREAK:
        control_flow = CTL_BREAK;
        return _mknum(0);
    case NODE_CONTINUE:
        control_flow = CTL_CONTINUE;
        return _mknum(0);
    default:
        return _mknum(0);
    }
}

/* ---- shell command bridge ---- */

static int shell_call(const char *name, script_val_t *args, int nargs, script_val_t *res) {
    res->type = 0; res->num = 0; res->str = 0;

    /* Build a command line string from the call */
    char cmd[512];
    int p = 0;
    for (int i = 0; name[i] && p < 500; i++) cmd[p++] = name[i];
    for (int i = 0; i < nargs && p < 500; i++) {
        if (args[i].type == 4) continue;      /* function values can't be shell args */
        if (p < 500) cmd[p++] = ' ';
        if (args[i].type == 1) {
            cmd[p++] = '"';
            for (int j = 0; args[i].str && args[i].str[j] && p < 490; j++) {
                if (args[i].str[j] == '"' || args[i].str[j] == '\\') cmd[p++] = '\\';
                cmd[p++] = args[i].str[j];
            }
            cmd[p++] = '"';
        } else {
            char *t = num_to_str(args[i].num);
            for (int j = 0; t[j] && p < 500; j++) cmd[p++] = t[j];
            free(t);
        }
    }
    cmd[p] = 0;

    /* Execute through shell */
    int code = shell_execute(cmd);
    res->num = code;
    return 0;
}

/* ---- public API ---- */

void script_init(void) {
    var_count = 0;
    func_count = 0;
    native_count = 0;
    control_flow = CTL_NONE;
    ctrl_val = _mknum(0);
    save_depth = 0;
}

int script_eval(const char *input, script_val_t *result) {
    lexer_t lx;
    lx.start = input;
    lx.pos = input;
    lx.error[0] = 0;

    if (lex_next_token(&lx) < 0) return -1;

    node_t *program = new_node(NODE_PROGRAM);
    node_t **tail = &program->left;

    while (lx.cur.type != TOK_EOF) {
        node_t *stmt = parse_stmt(&lx);
        if (!stmt && lx.cur.type != TOK_EOF) {
            free_node(program);
            return -1;
        }
        if (stmt) {
            *tail = stmt;
            tail = &(*tail)->extra;
        }
        lex_accept(&lx, TOK_SEMICOLON);
    }

    script_val_t r = _mknum(0);

    node_t *s = program->left;
    while (s) {
        if (control_flow != CTL_NONE) goto finish;
        _free(&r);
        r = eval_node(s);
        s = s->extra;
    }

finish:
    if (control_flow == CTL_THROW) {
        /* uncaught exception at top level */
        kprintf("csl: uncaught ");
        if (ctrl_val.type == 1) kprintf("%s\n", ctrl_val.str ? ctrl_val.str : "");
        else kprintf("%lld\n", ctrl_val.num);
        _free(&ctrl_val);
        ctrl_val = _mknum(0);
        control_flow = CTL_NONE;
    } else if (control_flow == CTL_RETURN) {
        _free(&r);
        r = ctrl_val;
        ctrl_val.str = 0;
        control_flow = CTL_NONE;
    }
    if (control_flow == CTL_BREAK || control_flow == CTL_CONTINUE)
        control_flow = CTL_NONE;

    free_node(program);
    if (result) *result = r;
    else _free(&r);
    return 0;
}

int script_run_file(const char *path) {
    int sz = 0, dir = 0;
    if (fs_get_info((char*)path, &sz, &dir) < 0 || dir || sz <= 0) {
        kprintf("script: cannot open '%s'\n", path);
        return -1;
    }
    if (sz > 256 * 1024) sz = 256 * 1024;
    char *buf = malloc(sz + 1);
    if (!buf) { kprintf("script: OOM\n"); return -1; }
    int n = fs_read((char*)path, buf, sz);
    if (n <= 0) { free(buf); return -1; }
    buf[n] = 0;
    script_val_t r;
    int rc = script_eval(buf, &r);
    _free(&r);
    free(buf);
    return rc;
}

void script_set_var(const char *name, script_val_t val) {
    add_var(name, val);
}

void script_set_const(const char *name, script_val_t val) {
    add_const(name, val);
}

int script_get_var(const char *name, script_val_t *val) {
    var_entry_t *v = find_var(name);
    if (!v) return -1;
    *val = _dup(v->val);
    return 0;
}

/* ---- repr helpers exported for the csl stdlib ---- */

int script_repr_count(const char *s) {
    return repr_len(s);
}

script_val_t script_repr_at(const char *s, int64_t idx) {
    return repr_at(s, idx);
}

script_val_t script_repr_key(const char *s, const char *key) {
    return repr_obj_key(s, key);
}