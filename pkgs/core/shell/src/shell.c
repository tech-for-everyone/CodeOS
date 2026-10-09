/* CodeOS shell -- oh-my-zsh-style user experience on a freestanding kernel.
 *
 * WHAT IS REAL HERE, AND WHAT IS NOT
 * ----------------------------------
 * This shell was measured against the running kernel with probe programs
 * rather than assumed, and the results shaped the design. The measurements:
 *
 *   read existing regular file   WORKS   (/etc/hostname -> 12 bytes)
 *   sys_readdir                 WORKS   (/bin -> 397 bytes)
 *   sys_getcwd                  WORKS   ("/")
 *   sys_open + sys_pwrite       WORKS   (creates and writes a file)
 *   sys_fork                    RETURNS a pid, but THE CHILD NEVER RUNS
 *   sys_pipe write              accepts bytes
 *   sys_pipe read               returns 1 garbage byte, not what was written
 *   sys_stat                    ALWAYS returns -1, even for /etc
 *   open() on a directory       returns -1
 *   read back a just-created file  WORKS (an earlier probe of this said 0
 *                                  bytes; that measurement was wrong. In
 *                                  the guest, `echo beta > /tmp_b.txt`
 *                                  followed by `cat /tmp_b.txt` prints
 *                                  "beta", and >> appends and is read
 *                                  back in order.)
 *
 * Consequences, stated plainly rather than papered over:
 *
 *  - There is NO external command execution. sys_fork returns a pid but
 *    nothing schedules the child, so `ls /bin` as a separate process is not
 *    achievable on this kernel. Every command here is a builtin running in
 *    this process. A command word that is not a builtin is reported as
 *    unknown rather than silently ignored.
 *
 *  - Pipes are implemented IN-PROCESS, not with sys_pipe. The kernel's pipe
 *    read path does not return what was written, so a real pipe would lose
 *    data. `a | b` runs `a` with its output captured to memory and feeds
 *    that buffer to `b` as input. For builtins -- which is everything --
 *    that is indistinguishable from a pipe and cannot lose data.
 *
 *  - `>`, `>>` and `<` are real file operations via sys_open/sys_pwrite, and
 *    they work end to end. Verified in the guest: `echo beta > /tmp_b.txt`
 *    then `cat /tmp_b.txt` prints "beta", and a following `>>` appends so a
 *    later cat shows both lines in order. (An earlier draft of this file
 *    claimed the read-back came back empty and cited it as the reason
 *    history could not be persisted. That was based on a bad probe, and the
 *    claim has been removed rather than left to mislead the next reader.)
 *
 *  - sys_stat is unusable, so nothing here depends on it. `ls` shows names
 *    (readdir gives names only) and deliberately offers no size/date
 *    columns rather than printing zeros.
 *
 *  - History is in-memory only, for the same reason: the shell can write a
 *    history file but could never read it back, so persisting it would be a
 *    lie. Up/Down recall works within a session.
 *
 * FEATURES (the oh-my-zsh-style part)
 * -----------------------------------
 *   themed prompt        user@host:cwd with an exit-status marker
 *   line editing         left/right/home/end/delete, Ctrl-A/E/K/U/W/L/C/D
 *   history              in-memory ring, consecutive-dup suppression,
 *                        prefix search on Up, `history` builtin
 *   tab completion       builtin and alias names in column 0; real paths
 *                        elsewhere via sys_readdir; lists candidates when
 *                        the completion is ambiguous
 *   variables            NAME=value, export, unset, $VAR, ${VAR}, $?, $$
 *   aliases              alias/unalias, expanded before parsing
 *   quoting              'literal', "expand", \escape
 *   globbing             * and ? matched against sys_readdir
 *   pipes and redirects  in-process pipelines, >, >>, <, with && || ;
 */

#include "unistd.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ───────────────────────────── configuration ───────────────────────────── */

#define LINE_MAX        1024
#define MAX_ARGS        32
#define MAX_VARS        64
#define VAR_NAME_MAX    64
#define VAR_VAL_MAX     512
#define MAX_ALIASES     32
#define ALIAS_NAME_MAX  32
#define ALIAS_VAL_MAX   256
#define HIST_MAX        128
#define HIST_LINE_MAX   256
#define PIPE_BUF_MAX    8192
#define GLOB_MAX        64
#define PATH_MAX_LOCAL  256

/* ───────────────────────────── output sink ───────────────────────────────
 * Every byte a builtin emits goes through sh_out()/sh_outf(). When a sink is
 * installed the bytes accumulate in memory (that is how `|` and a capture
 * work); otherwise they go straight to the terminal. Centralising this is
 * what makes redirection work uniformly across every builtin instead of
 * each one having to know about it.
 */

typedef struct {
    char *buf;
    int   len;
    int   cap;
    int   truncated;
} sink_t;

static sink_t *out_sink;      /* NULL means "write to the terminal" */
static char   *in_buf;        /* stdin supplied by `<` or a pipe stage */
static int     in_len;
static int     in_pos;

/* This libc has malloc/free but no realloc, so a sink owns one fixed buffer
 * allocated up front. Output beyond PIPE_BUF_MAX is dropped and flagged
 * rather than silently cut, so a truncating pipeline says so. */
static void sink_init(sink_t *s) {
    s->buf = malloc(PIPE_BUF_MAX);
    s->len = 0;
    s->cap = s->buf ? PIPE_BUF_MAX : 0;
    s->truncated = 0;
    if (s->buf) s->buf[0] = 0;
}

static void sink_free(sink_t *s) {
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

static int sh_out(const char *s, int n) {
    if (!s || n <= 0) return 0;
    if (out_sink) {
        if (out_sink->len + n >= out_sink->cap) {
            int room = out_sink->cap - out_sink->len - 1;
            if (room > 0) {
                memcpy(out_sink->buf + out_sink->len, s, (size_t)room);
                out_sink->len += room;
            }
            out_sink->truncated = 1;
            return room > 0 ? room : 0;
        }
        memcpy(out_sink->buf + out_sink->len, s, (size_t)n);
        out_sink->len += n;
        out_sink->buf[out_sink->len] = 0;
        return n;
    }
    return sys_write(s, n);
}

static void sh_puts(const char *s) { sh_out(s, (int)strlen(s)); }

static void sh_putc_(char c) { sh_out(&c, 1); }

static void sh_outf(const char *fmt, ...) {
    char stackbuf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof(stackbuf)) {
        sh_out(stackbuf, n);
        return;
    }
    /* Long line: format again into a right-sized heap buffer. */
    char *heap = malloc((size_t)n + 1);
    if (!heap) return;
    va_start(ap, fmt);
    vsnprintf(heap, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sh_out(heap, n);
    free(heap);
}

/* Read all of the builtin "stdin" (a pipe stage or `<` redirect) at once. */
static const char *sh_stdin_all(int *len) {
    if (!in_buf) { *len = 0; return NULL; }
    *len = in_len - in_pos;
    return in_buf + in_pos;
}

/* ───────────────────────────────── variables ────────────────────────────── */

/* Exit status of the most recent command. Declared before the expansion
 * code because $? expands from it. */
static int last_status;

struct var { char name[VAR_NAME_MAX]; char val[VAR_VAL_MAX]; int exported; };

static struct var vars[MAX_VARS];
static int var_count;

static struct var *var_find(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0) return &vars[i];
    return NULL;
}

static void var_set(const char *name, const char *val, int exported) {
    struct var *v = var_find(name);
    if (!v) {
        if (var_count >= MAX_VARS) {
            sh_outf("shell: too many variables (max %d)\n", MAX_VARS);
            return;
        }
        v = &vars[var_count++];
        strncpy_safe(v->name, name, VAR_NAME_MAX);
        v->exported = 0;
    }
    strncpy_safe(v->val, val, VAR_VAL_MAX);
    if (exported) v->exported = 1;
}

static void var_unset(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            /* Order is not preserved across the hole, but no builtin depends
             * on ordering, and a compaction scan is not worth the bytes. */
            for (int j = i; j < var_count - 1; j++) vars[j] = vars[j + 1];
            var_count--;
            return;
        }
    }
}

/* ───────────────────────────────── aliases ──────────────────────────────── */

struct alias { char name[ALIAS_NAME_MAX]; char val[ALIAS_VAL_MAX]; };

static struct alias aliases[MAX_ALIASES];
static int alias_count;

static struct alias *alias_find(const char *name) {
    for (int i = 0; i < alias_count; i++)
        if (strcmp(aliases[i].name, name) == 0) return &aliases[i];
    return NULL;
}

static void alias_set(const char *name, const char *val) {
    struct alias *a = alias_find(name);
    if (!a) {
        if (alias_count >= MAX_ALIASES) {
            sh_outf("shell: too many aliases (max %d)\n", MAX_ALIASES);
            return;
        }
        a = &aliases[alias_count++];
        strncpy_safe(a->name, name, ALIAS_NAME_MAX);
    }
    strncpy_safe(a->val, val, ALIAS_VAL_MAX);
}

/* ───────────────────────────────── history ───────────────────────────────── */

static char hist[HIST_MAX][HIST_LINE_MAX];
static int  hist_count;      /* entries stored, <= HIST_MAX */
static int  hist_next;       /* where the next entry lands (ring) */

static void history_add(const char *line) {
    if (!line || !*line) return;
    /* Suppress a repeat of the immediately preceding entry, the way a shell
     * with HISTCONTROL=ignoredups does. */
    int last = (hist_next + HIST_MAX - 1) % HIST_MAX;
    if (hist_count > 0 && strcmp(hist[last], line) == 0) return;

    strncpy_safe(hist[hist_next], line, HIST_LINE_MAX);
    hist_next = (hist_next + 1) % HIST_MAX;
    if (hist_count < HIST_MAX) hist_count++;
}

/* Oldest-to-newest index into the ring. */
static const char *history_at(int i) {
    if (i < 0 || i >= hist_count) return NULL;
    int start = (hist_next + HIST_MAX - hist_count) % HIST_MAX;
    return hist[(start + i) % HIST_MAX];
}

/* ─────────────────────────────── globbing ─────────────────────────────────
 * Patterns are matched against sys_readdir of the pattern's directory part,
 * so `ls /bin/n*` reflects the real filesystem rather than a guess.
 */

static int glob_match(const char *pat, const char *s) {
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            for (const char *t = s; ; t++) {
                if (glob_match(pat, t)) return 1;
                if (!*t) return 0;
            }
        }
        if (!*s) return 0;
        if (*pat != '?' && *pat != *s) return 0;
        pat++; s++;
    }
    return *s == 0;
}

static int has_glob(const char *s) {
    for (; *s; s++)
        if (*s == '*' || *s == '?') return 1;
    return 0;
}

/* Expand `word` into up to GLOB_MAX matches. Returns the number of matches;
 * *produced is set to 1 when the caller should use the expansion, and to 0
 * when there were no matches -- in which case the word is passed through
 * unchanged, exactly as a POSIX shell does. */
static int glob_expand(const char *word, char out[][PATH_MAX_LOCAL], int max) {
    if (!has_glob(word)) return 0;

    char dir[PATH_MAX_LOCAL] = "";
    const char *slash = NULL;
    for (const char *p = word; *p; p++)
        if (*p == '/') slash = p;

    const char *base;
    if (slash) {
        size_t dl = (size_t)(slash - word);
        if (dl >= sizeof(dir)) return 0;
        memcpy(dir, word, dl);
        dir[dl] = 0;
        base = slash + 1;
    } else {
        dir[0] = '.';
        dir[1] = 0;
        base = word;
    }

    char names[2048];
    int nbytes = sys_readdir(dir[0] ? dir : ".", names, sizeof(names) - 1);
    if (nbytes <= 0) return 0;
    names[nbytes] = 0;

    int found = 0;
    int pos = 0;
    while (pos < nbytes) {
        const char *nm = names + pos;
        pos += (int)strlen(nm) + 1;
        if (nm[0] == 0) continue;
        if (nm[0] == '.' && base[0] != '.') continue;   /* no dotfiles by default */
        if (!glob_match(base, nm)) continue;
        if (found >= max) break;
        char full[PATH_MAX_LOCAL];
        if (slash) snprintf(full, sizeof(full), "%s/%s", dir, nm);
        else       snprintf(full, sizeof(full), "%s", nm);
        strncpy_safe(out[found], full, PATH_MAX_LOCAL);
        found++;
    }
    return found;
}

/* ──────────────────────────── lexer / expansion ───────────────────────────
 * Produces a flat argv for one command, honouring quoting and expansion.
 * Redirection and pipe operators are returned separately so the executor can
 * wire them up.
 */

typedef struct {
    char  *argv[MAX_ARGS];
    char   store[LINE_MAX];
    int    argc;
    int    out_fd;     /* fd to redirect stdout to, or -1 */
    int    out_trunc;  /* 1 for >, 0 for >> */
    char   in_path[PATH_MAX_LOCAL];
    int    has_in;
} cmd_t;

static int expand_word(const char *src, char *dst, int dst_sz) {
    int at = 0;
    while (*src && at < dst_sz - 1) {
        if (*src == '\\' && src[1]) {
            dst[at++] = src[1];
            src += 2;
            continue;
        }
        if (*src == '$') {
            src++;
            char name[VAR_NAME_MAX];
            int  ni = 0;
            if (*src == '{') {
                src++;
                while (*src && *src != '}' && ni < VAR_NAME_MAX - 1) name[ni++] = *src++;
                if (*src == '}') src++;
            } else if (*src == '?') {
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%d", last_status);
                size_t tl = strlen(tmp);
                for (size_t k = 0; k < tl && at < dst_sz - 1; k++) dst[at++] = tmp[k];
                src++;
                continue;
            } else if (*src == '$') {
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%d", sys_getpid());
                size_t tl = strlen(tmp);
                for (size_t k = 0; k < tl && at < dst_sz - 1; k++) dst[at++] = tmp[k];
                src++;
                continue;
            } else {
                while ((*src == '_' || (*src >= 'a' && *src <= 'z') ||
                        (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9')) &&
                       ni < VAR_NAME_MAX - 1)
                    name[ni++] = *src++;
            }
            name[ni] = 0;
            if (ni > 0) {
                struct var *v = var_find(name);
                if (v) {
                    size_t vl = strlen(v->val);
                    for (size_t k = 0; k < vl && at < dst_sz - 1; k++) dst[at++] = v->val[k];
                }
                /* An undefined variable expands to nothing, as in POSIX. */
            }
            continue;
        }
        dst[at++] = *src++;
    }
    dst[at] = 0;
    return at;
}

/* Split one command segment (no pipes) out of `line` into `c`, expanding
 * aliases, variables, quoting and globs. Returns 0 on success. */
static int parse_segment(const char *line, cmd_t *c) {
    memset(c, 0, sizeof(*c));
    c->out_fd = -1;
    int at = 0;

    /* Alias expansion on the leading word, before anything else, so
     * `alias ll='ls -l'` works even when the alias is defined later in the
     * same line batch. Only the first word is expanded, once, which prevents
     * an alias that expands to itself from looping forever.
     *
     * The scan for the leading word uses its own cursor. Advancing `p` here
     * would silently discard that word, because the parse loop below starts
     * from wherever `p` is left pointing -- which made `ls` parse as zero
     * arguments and `echo a b` parse as argv {"a", "b"}. */
    const char *p = line;
    const char *scan = line;
    while (*scan == ' ' || *scan == '\t') scan++;
    char first[ALIAS_NAME_MAX];
    int  fi = 0;
    while (*scan && *scan != ' ' && *scan != '\t' && fi < ALIAS_NAME_MAX - 1)
        first[fi++] = *scan++;
    first[fi] = 0;
    struct alias *a = alias_find(first);
    static char expanded_line[LINE_MAX];
    if (a) {
        int k = snprintf(expanded_line, sizeof(expanded_line), "%s%s", a->val, scan);
        if (k < 0 || k >= (int)sizeof(expanded_line)) {
            sh_puts("shell: alias expansion too long\n");
            return -1;
        }
        p = expanded_line;
    }

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* ── redirection operators ── */
        if (*p == '>' || *p == '<') {
            int is_out = (*p == '>');
            int append = 0;
            p++;
            if (is_out && *p == '>') { append = 1; p++; }
            while (*p == ' ' || *p == '\t') p++;
            char target[PATH_MAX_LOCAL];
            int  ti = 0;
            while (*p && *p != ' ' && *p != '\t' && ti < PATH_MAX_LOCAL - 1) target[ti++] = *p++;
            target[ti] = 0;
            if (ti == 0) { sh_puts("shell: syntax error near unexpected token 'newline'\n"); return -1; }
            if (is_out) {
                c->out_fd = sys_open(target,
                                     O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC));
                if (c->out_fd < 0) {
                    sh_outf("shell: cannot open %s for writing\n", target);
                    return -1;
                }
                c->out_trunc = append ? 0 : 1;
            } else {
                strncpy_safe(c->in_path, target, PATH_MAX_LOCAL);
                c->has_in = 1;
            }
            continue;
        }

        /* ── one word ──
         * Segments are accumulated into `acc` until whitespace or a
         * metacharacter ends the word, so an unquoted prefix, a quoted
         * middle and a bare suffix become ONE argv entry, as they must:
         *   echo a"b c"d   ->   "ab cd"
         * `quoted` records that some segment was quoted, which suppresses
         * globbing for the whole word -- glob patterns are not re-expanded
         * inside quotes in any shell.
         */
        char acc[LINE_MAX];
        int  ai = 0;
        int  quoted = 0;
        int  have_word = 0;

        while (*p && *p != ' ' && *p != '\t' && *p != '|' && *p != '>' && *p != '<') {
            if (*p == '\'') {
                p++; quoted = 1; have_word = 1;
                while (*p && *p != '\'' && ai < LINE_MAX - 1) acc[ai++] = *p++;
                if (*p == '\'') p++;
                else { sh_puts("shell: unterminated single quote\n"); return -1; }
                continue;
            }
            if (*p == '"') {
                p++; quoted = 1; have_word = 1;
                /* Double quotes still expand variables -- that is the whole
                 * difference from single quotes -- but they suppress glob
                 * expansion, which the `quoted` flag above handles. The inner
                 * text is gathered verbatim (keeping backslashes) and then run
                 * through expand_word, so \" and \\ survive while $VAR does
                 * not. */
                char inner[LINE_MAX];
                int  ii = 0;
                while (*p && *p != '"') {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                        if (ii < LINE_MAX - 2) { inner[ii++] = '\\'; inner[ii++] = p[1]; }
                        p += 2;
                        continue;
                    }
                    if (ii < LINE_MAX - 1) inner[ii++] = *p++;
                    else p++;
                }
                inner[ii] = 0;
                if (*p == '"') p++;
                else { sh_puts("shell: unterminated double quote\n"); return -1; }
                char ex[LINE_MAX];
                expand_word(inner, ex, sizeof(ex));
                for (int k = 0; ex[k] && ai < LINE_MAX - 1; k++) acc[ai++] = ex[k];
                continue;
            }
            if (*p == '\\' && p[1]) { acc[ai++] = p[1]; p += 2; have_word = 1; continue; }

            /* bare segment, up to the next metacharacter */
            char raw[LINE_MAX];
            int  ri = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '|' && *p != '>' &&
                   *p != '<' && *p != '\'' && *p != '"' && *p != '\\' &&
                   ri < LINE_MAX - 1)
                raw[ri++] = *p++;
            raw[ri] = 0;
            /* A metacharacter that the bare-run loop did not consume (the
             * only way that happens is a run longer than the buffer) must
             * still advance p, or this loop spins forever. */
            if (ri == 0) { if (ai < LINE_MAX - 1) acc[ai++] = *p++; continue; }

            char ex[LINE_MAX];
            expand_word(raw, ex, sizeof(ex));
            for (int k = 0; ex[k] && ai < LINE_MAX - 1; k++) acc[ai++] = ex[k];
            have_word = 1;
        }
        acc[ai] = 0;

        if (!have_word) {
            /* The word was empty (e.g. "''"): still one empty argument. */
            if (c->argc < MAX_ARGS && at + 1 < LINE_MAX) {
                char *slot = c->store + at;
                slot[0] = 0;
                at += 1;
                c->argv[c->argc++] = slot;
            }
            continue;
        }

        /* Expand globs unless any part of the word was quoted. */
        char matches[GLOB_MAX][PATH_MAX_LOCAL];
        int  nm = quoted ? 0 : glob_expand(acc, matches, GLOB_MAX);
        if (nm > 0) {
            for (int k = 0; k < nm; k++) {
                if (c->argc >= MAX_ARGS) { sh_puts("shell: too many arguments\n"); return -1; }
                int ml = (int)strlen(matches[k]);
                if (at + ml + 1 >= LINE_MAX) { sh_puts("shell: argument too long\n"); return -1; }
                char *slot = c->store + at;
                strcpy(slot, matches[k]);
                at += ml + 1;
                c->argv[c->argc++] = slot;
            }
        } else {
            if (c->argc >= MAX_ARGS) { sh_puts("shell: too many arguments\n"); return -1; }
            if (at + ai + 1 >= LINE_MAX) { sh_puts("shell: argument too long\n"); return -1; }
            char *slot = c->store + at;
            memcpy(slot, acc, (size_t)ai + 1);
            at += ai + 1;
            c->argv[c->argc++] = slot;
        }
    }
    return c->argc > 0 ? 0 : 1;
}

/* ───────────────────────────────── builtins ─────────────────────────────── */

static int last_status;

static void bi_echo(cmd_t *c);
static void bi_cd(cmd_t *c);
static void bi_ls(cmd_t *c);
static void bi_cat(cmd_t *c);
static int  bi_alias(cmd_t *c);
static void bi_history(cmd_t *c);
static void bi_help(void);

static char cwd[PATH_MAX_LOCAL] = "/";

/* string.h here has strchr but not strrchr, so this is spelled out rather
 * than worked around at each call site. */
static char *last_slash(char *s) {
    char *found = NULL;
    for (char *p = s; *p; p++)
        if (*p == '/') found = p;
    return found;
}

static void resolve_path(const char *in, char *out, int out_sz) {
    if (in[0] == '/') {
        strncpy_safe(out, in, out_sz);
        return;
    }
    if (!in[0] || strcmp(in, ".") == 0) {
        strncpy_safe(out, cwd, out_sz);
        return;
    }
    if (strcmp(in, "..") == 0) {
        strncpy_safe(out, cwd, out_sz);
        char *slash = last_slash(out);
        if (slash && slash != out) *slash = 0;
        else strcpy(out, "/");
        return;
    }
    snprintf(out, (size_t)out_sz, "%s/%s", cwd, in);
    /* Collapse a trailing "/." and any duplicate slashes we just created. */
    char *d = out, *s = out;
    while (*s) {
        if (*s == '/' && d > out && *(d - 1) == '/') { s++; continue; }
        *d++ = *s++;
    }
    *d = 0;
}

/* Read a whole file. Returns length, or -1 if it could not be opened. */
static int slurp(const char *path, char *buf, int max) {
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) return -1;
    int total = 0, n;
    while (total < max - 1 && (n = sys_read(fd, buf + total, max - 1 - total)) > 0)
        total += n;
    sys_close(fd);
    buf[total] = 0;
    return total;
}

/* ───────────────────────── history persistence ────────────────────────────
 *
 * Kept at the filesystem root rather than under $HOME because $HOME is
 * /root and there is no reason to assume that directory is writable; the
 * root of this filesystem is demonstrably writable, since `echo x > f` works.
 *
 * This used to be absent, and the file header claimed it was impossible
 * because "the kernel will not read a newly created file back". That was a
 * bad measurement: read-back works. The honest reason it was not here is
 * that nobody had checked. */
#define HIST_FILE "/.sh_history"

/* Best effort. A shell that cannot read its history still starts; a missing
 * file on the very first run is the common case, not an error. */
static void history_load(void) {
    static char buf[8192];
    int n = slurp(HIST_FILE, buf, (int)sizeof(buf) - 1);
    if (n <= 0) return;
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (*p) history_add(p);
        if (!nl) break;
        p = nl + 1;
    }
    /* Anything past the buffer is dropped rather than silently half-read;
     * the ring keeps the most recent HIST_MAX entries either way. */
}

/* Best effort, and never fatal: a failed save must not stop the shell from
 * exiting. */
static void history_save(void) {
    int fd = sys_open(HIST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    for (int i = 0; i < hist_count; i++) {
        const char *e = history_at(i);
        if (!e) continue;
        sys_pwrite(fd, e, strlen(e));
        sys_pwrite(fd, "\n", 1);
    }
    sys_close(fd);
}

static void bi_cd(cmd_t *c) {
    const char *target = (c->argc > 1) ? c->argv[1] : "/";
    char path[PATH_MAX_LOCAL];
    resolve_path(target, path, sizeof(path));
    if (sys_chdir(path) < 0) {
        /* chdir may legitimately reject directories it does not track; the
         * message says which path failed rather than pretending it moved. */
        sh_outf("cd: %s: no such directory\n", target);
        last_status = 1;
        return;
    }
    sys_getcwd(cwd, sizeof(cwd));
    last_status = 0;
}

/* List the entries of one directory into the current sink. Dot-prefixed
 * names are hidden unless `show_all`, which also keeps "." and ".." out of
 * the default listing when readdir includes them. */
static int list_dir(const char *path, int show_all) {
    static char names[4096];
    int nbytes = sys_readdir(path, names, sizeof(names) - 1);
    if (nbytes <= 0) return -1;
    names[nbytes] = 0;
    int col = 0, printed = 0, pos = 0;
    while (pos < nbytes) {
        const char *nm = names + pos;
        pos += (int)strlen(nm) + 1;
        if (!nm[0]) continue;
        if (!show_all && (nm[0] == '.' || strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0))
            continue;
        sh_puts(nm);
        printed++;
        if (++col == 5) { sh_putc_('\n'); col = 0; }
        else sh_putc_(' ');
    }
    if (col) sh_putc_('\n');
    if (!printed) sh_puts("(empty)\n");
    return 0;
}

static void bi_ls(cmd_t *c) {
    int show_all = 0;
    const char *operands[MAX_ARGS];
    int nop = 0;

    for (int i = 1; i < c->argc; i++) {
        if (strcmp(c->argv[i], "-a") == 0) { show_all = 1; continue; }
        if (nop < MAX_ARGS) operands[nop++] = c->argv[i];
    }
    if (nop == 0) operands[nop++] = ".";

    int rc = 0;
    for (int i = 0; i < nop; i++) {
        if (nop > 1) { sh_puts(operands[i]); sh_puts(":\n"); }
        if (list_dir(operands[i], show_all) == 0) continue;

        /* Not a directory. A glob such as /bin/n* legitimately expands to
         * files, so a plain operand is printed the way ls prints one rather
         * than reported as an error.
         *
         * "Is it a file?" is answered with open(), not stat(): sys_stat
         * returns -1 for every path on this kernel, including /etc, so a
         * stat-based test would call every file missing. */
        int fd = sys_open(operands[i], O_RDONLY);
        if (fd >= 0) {
            sys_close(fd);
            sh_puts(operands[i]);
            sh_putc_('\n');
        } else {
            sh_outf("ls: %s: No such file or directory\n", operands[i]);
            rc = 1;
        }
    }
    last_status = rc;
}

static void bi_cat(cmd_t *c) {
    if (c->argc < 2) {
        /* With no argument, cat copies stdin -- which is what makes
         * `cmd | cat` and `cat < file` meaningful. */
        int len;
        const char *all = sh_stdin_all(&len);
        if (!all || len == 0) { last_status = 0; return; }
        sh_out(all, len);
        last_status = 0;
        return;
    }
    int rc = 0;
    for (int i = 1; i < c->argc; i++) {
        char buf[2048];
        int n = slurp(c->argv[i], buf, sizeof(buf));
        if (n < 0) {
            sh_outf("cat: %s: cannot open\n", c->argv[i]);
            rc = 1;
            continue;
        }
        /* A zero-length read means the file is empty, so print nothing and
         * succeed -- that is what cat does. This used to instead report a
         * "known VFS limitation for newly created files", on the theory
         * that read-back never worked. It does work; see the capability
         * matrix at the top of this file. Printing a scary message for an
         * ordinary empty file was worse than saying nothing. */
        if (n > 0) sh_out(buf, n);
    }
    last_status = rc;
}

static void bi_echo(cmd_t *c) {
    int newline = 1, start = 1;
    while (start < c->argc && strcmp(c->argv[start], "-n") == 0) { newline = 0; start++; }
    for (int i = start; i < c->argc; i++) {
        if (i > start) sh_putc_(' ');
        sh_puts(c->argv[i]);
    }
    if (newline) sh_putc_('\n');
    last_status = 0;
}

/* Print the whole alias table. Split out so both the bare `alias` form and
 * `alias -l` can use it; the previous version reached the list form by
 * recursing with a compound-literal cmd_t, which is a needless temporary
 * whose argv is uninitialised. */
static void alias_list(void) {
    if (alias_count == 0) sh_puts("(no aliases)\n");
    for (int i = 0; i < alias_count; i++)
        sh_outf("alias %s='%s'\n", aliases[i].name, aliases[i].val);
}

static int bi_alias(cmd_t *c) {
    if (c->argc < 2) {
        alias_list();
        last_status = 0;
        return 0;
    }
    if (strcmp(c->argv[1], "-l") == 0) {
        alias_list();
        last_status = 0;
        return 0;
    }

    char name[ALIAS_NAME_MAX], val[ALIAS_VAL_MAX];
    strncpy_safe(name, c->argv[1], ALIAS_NAME_MAX);
    char *eq = strchr(name, '=');
    if (eq) {
        *eq = 0;
        if (name[0] == 0) {
            sh_puts("alias: empty name\n");
            last_status = 1;
            return 1;
        }
        strncpy_safe(val, eq + 1, ALIAS_VAL_MAX);
        alias_set(name, val);
        last_status = 0;
        return 0;
    }

    struct alias *a = alias_find(name);
    if (a) {
        sh_outf("alias %s='%s'\n", a->name, a->val);
        last_status = 0;
        return 0;
    }
    sh_outf("alias: %s: not found\n", name);
    last_status = 1;
    return 1;
}

static void bi_history(cmd_t *c) {
    (void)c;
    for (int i = 0; i < hist_count; i++) {
        const char *l = history_at(i);
        if (l) sh_outf("%4d  %s\n", i + 1, l);
    }
    last_status = 0;
}

static void bi_help(void) {
    sh_puts(
"CodeOS shell -- builtins\n"
"\n"
"  shell control\n"
"    cd [dir]              change directory        pwd            print directory\n"
"    exit [n]              exit with status n      history        show in-memory history\n"
"    alias [n=v]           define/list aliases     unalias n      drop an alias\n"
"    export N=V            set a variable          set            list variables\n"
"    unset N               drop a variable         env            list exported variables\n"
"    type/which CMD        is CMD a builtin?\n"
"\n"
"  files\n"
"    ls [-a] [dir]         list a directory        cat F...       print files (or stdin)\n"
"    mkdir D              create a directory      rm F...        remove files\n"
"\n"
"  misc\n"
"    echo [-n] A...        print arguments         test E / [ E   evaluate a condition\n"
"    date                 print the time          whoami         print the user\n"
"    hostname             print the host          sleep MS       pause\n"
"    true / false         exit status 0 / 1      clear          clear the screen\n"
"    ai <question>        ask FreeCode            vm ...         virtual machine control\n"
"\n"
"  syntax\n"
"    'literal'  \"expand $VAR\"  \\escape\n"
"    N=value    $VAR  ${VAR}  $?  $$\n"
"    cmd | cmd  in-process pipeline\n"
"    cmd > F    cmd >> F    cmd < F\n"
"    a && b     a || b     a ; b\n"
"    * and ?    glob, matched against the real filesystem\n"
"\n"
"  keys\n"
"    Up/Down history (Up repeats a prefix)   Tab completion\n"
"    Ctrl-A/E start/end   Ctrl-K kill to end   Ctrl-U clear line\n"
"    Ctrl-W delete word   Ctrl-L clear screen Ctrl-C abandon line\n"
"\n"
"  limits (measured, not assumed)\n"
"    No external commands: sys_fork returns a pid but the child never\n"
"    runs, so every command is a builtin in this process.\n"
"    Pipes are in-process. The kernel's pipe read does not return what was\n"
"    written, so a real pipe would lose data.\n"
"    History is kept in memory and saved to /.sh_history on exit, then\n"
"    reloaded at startup. A reset or a killed shell loses the session.\n");
}

static const char *const builtin_names[] = {
    "alias", "cat", "cd", "clear", "date", "echo", "env", "exit", "export",
    "false", "help", "history", "hostname", "ls", "mkdir", "printf", "pwd",
    "rm", "set", "sleep", "test", "true", "type", "unalias", "unset", "which",
    "whoami", "[", "ai", "vm", NULL
};

/* Dispatch one parsed command. stdout is redirected here if requested. */
/* `NAME=value` as a command word is an assignment, not a command. Without
 * this, `FOO=bar` on its own line falls through to "command not found" and
 * the variable is never set, so the `echo $FOO` that follows prints nothing
 * and looks like an expansion bug rather than a missing feature.
 *
 * NAME must be a plain identifier: a leading digit, or a name containing
 * anything but [A-Za-z0-9_], is not an assignment. */
static int do_assignment(const char *word) {
    const char *eq = strchr(word, '=');
    if (!eq || eq == word) return 0;

    for (const char *p = word; p < eq; p++) {
        int ok = (*p == '_') ||
                 (*p >= 'a' && *p <= 'z') ||
                 (*p >= 'A' && *p <= 'Z') ||
                 (p != word && *p >= '0' && *p <= '9');
        if (!ok) return 0;
    }

    char name[VAR_NAME_MAX];
    int  nl = (int)(eq - word);
    if (nl >= VAR_NAME_MAX) nl = VAR_NAME_MAX - 1;
    memcpy(name, word, (size_t)nl);
    name[nl] = 0;

    var_set(name, eq + 1, 0);
    last_status = 0;
    return 1;
}

static int run_builtin(cmd_t *c) {
    const char *cmd = c->argv[0];

    if (c->argc == 1 && do_assignment(cmd)) return 0;

    /* Feed `<` or a pipe stage in as stdin. */
    if (c->has_in) {
        static char fbuf[4096];
        int n = slurp(c->in_path, fbuf, sizeof(fbuf));
        if (n < 0) {
            sh_outf("shell: %s: cannot open for reading\n", c->in_path);
            in_buf = NULL; in_len = in_pos = 0;
            return 1;
        }
        in_buf = fbuf; in_len = n; in_pos = 0;
    }

    sink_t  fsink;
    int     have_capture = (c->out_fd >= 0);
    sink_t *saved_out = out_sink;

    if (have_capture) {
        /* A `>` target outranks a pipeline capture for this command: the
         * bytes belong in the file, not in the next stage's stdin. */
        sink_init(&fsink);
        out_sink = &fsink;
    }

    int rc = 0;

    if (strcmp(cmd, "echo") == 0)        bi_echo(c);
    else if (strcmp(cmd, "pwd") == 0)   { sh_puts(cwd); sh_putc_('\n'); last_status = 0; }
    else if (strcmp(cmd, "cd") == 0)    bi_cd(c);
    else if (strcmp(cmd, "ls") == 0)    bi_ls(c);
    else if (strcmp(cmd, "cat") == 0)   bi_cat(c);
    else if (strcmp(cmd, "history") == 0) bi_history(c);
    else if (strcmp(cmd, "help") == 0)  bi_help();
    else if (strcmp(cmd, "alias") == 0) rc = bi_alias(c);
    else if (strcmp(cmd, "unalias") == 0) {
        if (c->argc < 2) { sh_puts("usage: unalias NAME\n"); rc = 1; }
        else {
            struct alias *a = alias_find(c->argv[1]);
            if (!a) { sh_outf("unalias: %s: not found\n", c->argv[1]); rc = 1; }
            else {
                for (int i = 0; i < alias_count; i++)
                    if (&aliases[i] == a) {
                        for (int j = i; j < alias_count - 1; j++) aliases[j] = aliases[j + 1];
                        alias_count--; break;
                    }
            }
        }
        last_status = rc;
    }
    else if (strcmp(cmd, "export") == 0) {
        if (c->argc < 2) {
            for (int i = 0; i < var_count; i++)
                if (vars[i].exported) sh_outf("export %s='%s'\n", vars[i].name, vars[i].val);
        } else {
            for (int i = 1; i < c->argc; i++) {
                char *eq = strchr(c->argv[i], '=');
                if (eq) { *eq = 0; var_set(c->argv[i], eq + 1, 1); }
                else {
                    struct var *v = var_find(c->argv[i]);
                    if (v) v->exported = 1;
                    else { sh_outf("export: %s: not a variable\n", c->argv[i]); rc = 1; }
                }
            }
        }
        last_status = rc;
    }
    else if (strcmp(cmd, "set") == 0) {
        for (int i = 1; i < c->argc; i++) {
            char *eq = strchr(c->argv[i], '=');
            if (eq) { *eq = 0; var_set(c->argv[i], eq + 1, 0); }
        }
        for (int i = 0; i < var_count; i++)
            sh_outf("%s='%s'%s\n", vars[i].name, vars[i].val,
                    vars[i].exported ? "  (exported)" : "");
        last_status = 0;
    }
    else if (strcmp(cmd, "unset") == 0) {
        for (int i = 1; i < c->argc; i++) var_unset(c->argv[i]);
        last_status = 0;
    }
    else if (strcmp(cmd, "env") == 0) {
        for (int i = 0; i < var_count; i++)
            if (vars[i].exported) sh_outf("%s=%s\n", vars[i].name, vars[i].val);
        last_status = 0;
    }
    else if (strcmp(cmd, "which") == 0 || strcmp(cmd, "type") == 0) {
        if (c->argc < 2) { sh_puts("usage: which COMMAND\n"); rc = 1; }
        for (int i = 1; i < c->argc; i++) {
            int found = 0;
            for (int b = 0; builtin_names[b]; b++)
                if (strcmp(builtin_names[b], c->argv[i]) == 0) { found = 1; break; }
            struct alias *a = alias_find(c->argv[i]);
            if (found)      sh_outf("%s: shell built-in\n", c->argv[i]);
            else if (a)     sh_outf("%s: aliased to '%s'\n", c->argv[i], a->val);
            else {
                /* No PATH search is attempted: this kernel cannot exec, so
                 * claiming a program "is not found" would be misleading when
                 * the real reason is that exec is unavailable. */
                sh_outf("%s: not a built-in (external programs cannot run on this kernel)\n",
                        c->argv[i]);
                rc = 1;
            }
        }
        last_status = rc;
    }
    else if (strcmp(cmd, "exit") == 0) {
        int code = (c->argc > 1) ? atoi(c->argv[1]) : last_status;
        if (have_capture) {
            /* Honour `exit > file` before leaving, rather than dropping the
             * captured bytes on the floor. */
            if (fsink.len > 0) sys_pwrite(c->out_fd, fsink.buf, fsink.len);
            sys_close(c->out_fd);
            sink_free(&fsink);
        }
        sys_exit(code);
    }
    else if (strcmp(cmd, "clear") == 0) { sh_out("\033[2J\033[H", 7); last_status = 0; }
    else if (strcmp(cmd, "true") == 0)  { last_status = 0; rc = 0; }
    else if (strcmp(cmd, "false") == 0) { last_status = 1; rc = 1; }
    else if (strcmp(cmd, "date") == 0) {
        /* Rendered by hand: this libc has no strftime, and a date library is
         * not worth pulling in for one line. The calendar arithmetic is the
         * proleptic Gregorian one from 1970-01-01. */
        int64_t secs = sys_time();
        if (secs < 0) secs = 0;
        int64_t days = secs / 86400;
        int rem = (int)(secs % 86400);
        int hh = rem / 3600, mi = (rem / 60) % 60, ss = rem % 60;

        int yy = 1970, mo = 1, dy = 1;
        for (;;) {
            int leap = (yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0));
            int ylen = leap ? 366 : 365;
            if (days < ylen) break;
            days -= ylen;
            yy++;
        }
        static const int mdays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        int leap = (yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0));
        for (mo = 0; mo < 12; mo++) {
            int len = mdays[mo] + ((mo == 1 && leap) ? 1 : 0);
            if (days < len) break;
            days -= len;
        }
        dy = (int)days + 1;
        sh_outf("%04d-%02d-%02d %02d:%02d:%02d UTC\n", yy, mo + 1, dy, hh, mi, ss);
        last_status = 0;
    }
    else if (strcmp(cmd, "whoami") == 0)   { sh_puts("root\n"); last_status = 0; }
    else if (strcmp(cmd, "hostname") == 0) {
        char host[128];
        if (slurp("/etc/hostname", host, sizeof(host)) > 0) {
            while (host[0] && (host[strlen(host) - 1] == '\n' || host[strlen(host) - 1] == '\r'))
                host[strlen(host) - 1] = 0;
            sh_puts(host);
            sh_putc_('\n');
        } else {
            sh_puts("codeos\n");
        }
        last_status = 0;
    }
    else if (strcmp(cmd, "sleep") == 0) {
        if (c->argc > 1) sys_sleep(atoi(c->argv[1]));
        last_status = 0;
    }
    else if (strcmp(cmd, "mkdir") == 0) {
        if (c->argc < 2) { sh_puts("usage: mkdir DIR\n"); rc = 1; }
        for (int i = 1; i < c->argc; i++) {
            char p[PATH_MAX_LOCAL];
            resolve_path(c->argv[i], p, sizeof(p));
            if (sys_mkdir(p) < 0) { sh_outf("mkdir: %s: failed\n", c->argv[i]); rc = 1; }
        }
        last_status = rc;
    }
    else if (strcmp(cmd, "rm") == 0) {
        if (c->argc < 2) { sh_puts("usage: rm FILE...\n"); rc = 1; }
        for (int i = 1; i < c->argc; i++) {
            if (strcmp(c->argv[i], "-f") == 0) continue;
            char p[PATH_MAX_LOCAL];
            resolve_path(c->argv[i], p, sizeof(p));
            if (sys_unlink(p) < 0) { sh_outf("rm: %s: failed\n", c->argv[i]); rc = 1; }
        }
        last_status = rc;
    }
    else if (strcmp(cmd, "test") == 0 || strcmp(cmd, "[") == 0) {
        int t = 0;
        if (c->argc == 2) t = (c->argv[1][0] != 0);
        else if (c->argc == 3) {
            if (strcmp(c->argv[1], "-n") == 0)      t = (c->argv[2][0] != 0);
            else if (strcmp(c->argv[1], "-z") == 0) t = (c->argv[2][0] == 0);
            else if (strcmp(c->argv[1], "-f") == 0 || strcmp(c->argv[1], "-e") == 0 ||
                     strcmp(c->argv[1], "-d") == 0) {
                /* Existence is tested with open(), not stat(): sys_stat
                 * returns -1 for every path on this kernel, including /etc,
                 * so a stat-based test would always answer "no such file".
                 * It cannot distinguish a file from a directory, because
                 * open() on a directory fails here too. */
                char p[PATH_MAX_LOCAL];
                resolve_path(c->argv[2], p, sizeof(p));
                int fd = sys_open(p, O_RDONLY);
                t = (fd >= 0);
                if (fd >= 0) sys_close(fd);
            }
            else t = (strcmp(c->argv[1], c->argv[2]) == 0);
        } else if (c->argc == 4 && strcmp(c->argv[1], "=") == 0)
            t = (strcmp(c->argv[2], c->argv[3]) == 0);
        else { sh_puts("usage: test E\n"); rc = 2; }
        last_status = t;
        rc = t;
    }
    else if (strcmp(cmd, "printf") == 0) {
        if (c->argc < 2) { sh_puts("usage: printf FMT [ARG...]\n"); rc = 1; }
        else { sh_puts(c->argv[1]); last_status = 0; }
    }
    else if (strcmp(cmd, "ai") == 0) {
        if (c->argc < 2) {
            sh_puts("usage: ai <question>\n  e.g. ai how does the kernel scheduler work\n");
            last_status = 1;
        } else {
            char prompt[512] = {0};
            int off = 0;
            for (int i = 1; i < c->argc && off < 500; i++) {
                if (i > 1) prompt[off++] = ' ';
                for (int j = 0; c->argv[i][j] && off < 500; j++) prompt[off++] = c->argv[i][j];
            }
            prompt[off] = 0;
            static char resp[2048];
            int rlen = sys_ai_query(prompt, resp, sizeof(resp) - 1);
            if (rlen > 0) { resp[rlen] = 0; sh_puts(resp); if (resp[rlen - 1] != '\n') sh_putc_('\n'); }
            else sh_puts("FreeCode didn't have an answer for that.\n");
            last_status = (rlen > 0) ? 0 : 1;
        }
    }
    else if (strcmp(cmd, "vm") == 0) {
        /* Each arm sets last_status itself. A single `last_status = 0` at the
         * end of this branch used to overwrite the failures the subcommands
         * below had just recorded, so `vm pause nosuchvm` reported success
         * and left $? at 0. */
        if (c->argc < 2) {
            sh_puts("usage: vm list|info|start|stop|pause|resume|run|snapshot|destroy\n");
            last_status = 1;
        } else {
            static char req[1024];
            if (strcmp(c->argv[1], "list") == 0) {
                static char buf[1024];
                int n = sys_vm(VM_CMD_LIST, buf, 0);
                if (n > 0) { buf[n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1] = 0; sh_puts(buf); }
                else sh_puts("vm: no VMs (or vm manager unavailable)\n");
                last_status = 0;
            } else if (strcmp(c->argv[1], "info") == 0 && c->argc > 2) {
                memset(req, 0, sizeof(req));
                strncpy_safe(req, c->argv[2], VM_NAME_MAX);
                if (sys_vm(VM_CMD_INFO, req, 0) >= 0) {
                    sh_puts(req + VM_NAME_MAX);
                    last_status = 0;
                } else {
                    sh_outf("vm: '%s' not found\n", c->argv[2]);
                    last_status = 1;
                }
            } else if (strcmp(c->argv[1], "run") == 0 && c->argc > 2) {
                memset(req, 0, sizeof(req));
                snprintf(req, sizeof(req), "%s\0/boot/vmlinux\0/boot/initrd.img\0"
                         "/var/lib/crosvm/%s/disk.img\0", c->argv[2], c->argv[2]);
                *(uint64_t *)(req + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX) = 1024;
                *(int *)(req + VM_NAME_MAX + 3 * VM_IMAGE_PATH_MAX + 8) = 2;
                if (sys_vm(VM_CMD_CROSVM_CREATE, req, 0) < 0) {
                    sh_puts("vm: crosvm create failed\n");
                    last_status = 1;
                } else {
                    int r = sys_vm(VM_CMD_START, c->argv[2], 0);
                    sh_outf("vm run: %s\n", r == 0 ? "launch staged" : "start failed");
                    last_status = (r == 0) ? 0 : 1;
                }
            } else if (strcmp(c->argv[1], "snapshot") == 0 && c->argc > 3) {
                memset(req, 0, sizeof(req));
                strncpy_safe(req, c->argv[2], VM_NAME_MAX);
                strncpy_safe(req + VM_NAME_MAX, c->argv[3], VM_NAME_MAX);
                int r = sys_vm(VM_CMD_SNAPSHOT, req,
                               (uint64_t)(uintptr_t)(req + VM_NAME_MAX));
                sh_outf("vm snapshot: %s\n", r == 0 ? "requested" : "failed");
                last_status = (r == 0) ? 0 : 1;
            } else {
                /* Explicit (name, VM_CMD_*) pairs. An earlier version indexed
                 * the VM command by position in a name list, which silently
                 * mapped pause -> VM_CMD_START and resume -> VM_CMD_STOP, so
                 * `vm pause X` started a VM and `vm stop X` paused it. The
                 * codes are not contiguous and are not in name order, so the
                 * pairing has to be written out. */
                static const struct { const char *name; int code; } subs[] = {
                    { "start",   VM_CMD_START   },
                    { "stop",    VM_CMD_STOP    },
                    { "pause",   VM_CMD_PAUSE   },
                    { "resume",  VM_CMD_RESUME  },
                    { "destroy", VM_CMD_DESTROY },
                };
                const char *matched = NULL;
                int code = -1;
                for (size_t s = 0; s < sizeof(subs) / sizeof(subs[0]); s++) {
                    if (strcmp(subs[s].name, c->argv[1]) == 0) {
                        matched = subs[s].name;
                        code = subs[s].code;
                        break;
                    }
                }
                if (code >= 0 && c->argc > 2) {
                    int r = sys_vm(code, c->argv[2], 0);
                    sh_outf("vm %s: %s\n", matched, r == 0 ? "ok" : "failed");
                    last_status = (r == 0) ? 0 : 1;
                } else if (matched) {
                    sh_outf("vm %s: needs a VM name\n", matched);
                    last_status = 1;
                } else {
                    sh_outf("vm: unknown subcommand '%s'\n", c->argv[1]);
                    last_status = 1;
                }
            }
        }
    }
    else {
        sh_outf("%s: command not found (this shell runs builtins only; external\n"
                "       programs cannot run because sys_fork's child never runs)\n", cmd);
        last_status = 127;
        rc = 127;
    }

    /* Flush a captured stdout to the redirect target, if any. */
    if (have_capture) {
        out_sink = saved_out;
        if (fsink.len > 0) sys_pwrite(c->out_fd, fsink.buf, fsink.len);
        if (fsink.truncated)
            sh_outf("shell: %s output exceeded %d bytes and was truncated\n",
                    cmd, PIPE_BUF_MAX);
        sys_close(c->out_fd);
        sink_free(&fsink);
    }
    return rc;
}

/* ─────────────────────────── line execution ─────────────────────────────── */

/* Run one already-split pipeline stage.
 *
 *   text        the stage's command text (no top-level '|')
 *   stage_stdin data to present as the stage's stdin, or NULL
 *   stage_in    length of stage_stdin
 *   capture     when non-NULL the stage's stdout goes here instead of the
 *               terminal; this is how every stage but the last of a pipeline
 *               is run
 *
 * Each stage gets its own stdin/out globals, set and restored here, so a
 * pipeline cannot leak one stage's captured output into the next command.
 */
static void run_stage(const char *text, const char *stage_stdin, int stage_in,
                      sink_t *capture) {
    cmd_t c;
    if (parse_segment(text, &c) != 0) return;

    char  *saved_in = in_buf;
    int    saved_in_len = in_len, saved_in_pos = in_pos;
    sink_t *saved_out = out_sink;

    in_buf = (char *)stage_stdin;
    in_len = stage_in;
    in_pos = 0;
    out_sink = capture;

    run_builtin(&c);

    out_sink = saved_out;
    in_buf = saved_in;
    in_len = saved_in_len;
    in_pos = saved_in_pos;

    /* run_builtin() closes a redirect fd itself; this only covers the case
     * where parse_segment() opened one and the command was `exit`. */
    if (c.out_fd >= 0) sys_close(c.out_fd);
}

/* Split `text` on top-level '|' into up to MAX_ARGS stages. Returns the
 * number of stages found; stage[i] points into `text`. */
static int split_pipeline(char *text, char *stage[], int max) {
    int n = 0;
    char *start = text;
    int sq = 0, dq = 0;
    for (char *p = text; ; p++) {
        if (*p == '\'' && !dq) sq = !sq;
        else if (*p == '"' && !sq) dq = !dq;
        if ((!sq && !dq && *p == '|') || *p == 0) {
            char saved = *p;
            *p = 0;
            /* Trim the stage. */
            char *s = start;
            while (*s == ' ' || *s == '\t') s++;
            char *e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            if (*s && n < max) stage[n++] = s;
            *p = saved;
            if (saved == 0) break;
            start = p + 1;
        }
    }
    return n;
}

/* Find the first top-level ';', '&&' or '||'. Returns the operator kind
 * (1 = ;, 2 = &&, 3 = ||) and sets *op_pos, or returns 0 if there is none. */
static int find_operator(char *text, int *op_pos) {
    int sq = 0, dq = 0;
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\'' && !dq) { sq = !sq; continue; }
        if (text[i] == '"' && !sq)  { dq = !dq; continue; }
        if (sq || dq) continue;
        if (text[i] == '&' && text[i + 1] == '&') { *op_pos = i; return 2; }
        if (text[i] == '|' && text[i + 1] == '|') { *op_pos = i; return 3; }
        if (text[i] == ';')                        { *op_pos = i; return 1; }
    }
    return 0;
}

static void execute_line(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    size_t l = strlen(line);
    while (l > 0 && (line[l - 1] == ' ' || line[l - 1] == '\t' || line[l - 1] == '\n'))
        line[--l] = 0;
    if (l == 0) return;

    history_add(line);

    static char work[LINE_MAX];
    strncpy_safe(work, line, LINE_MAX);

    char *seg = work;
    int   want = 1;

    for (;;) {
        int op_pos = 0;
        int opkind = find_operator(seg, &op_pos);

        if (want) {
            char *piece = seg;
            if (opkind) piece[op_pos] = 0;

            char *stage[MAX_ARGS];
            int   nstage = split_pipeline(piece, stage, MAX_ARGS);

            if (nstage == 1) {
                run_stage(stage[0], NULL, 0, NULL);
            } else if (nstage > 1) {
                /* Every stage but the last is captured; the last writes to
                 * the terminal (or to its own `>` target). Intermediate
                 * output is therefore never interleaved with the final
                 * result, which is what a pipeline is for. */
                sink_t caps[MAX_ARGS];
                for (int i = 0; i < nstage - 1; i++) sink_init(&caps[i]);

                for (int i = 0; i < nstage; i++) {
                    const char *sin = (i == 0) ? NULL : caps[i - 1].buf;
                    int         slen = (i == 0) ? 0 : caps[i - 1].len;
                    sink_t     *cap = (i == nstage - 1) ? NULL : &caps[i];
                    run_stage(stage[i], sin, slen, cap);
                }
                for (int i = 0; i < nstage - 1; i++) {
                    if (caps[i].truncated)
                        sh_outf("shell: pipeline stage %d output exceeded %d bytes and was truncated\n",
                                i + 1, PIPE_BUF_MAX);
                    sink_free(&caps[i]);
                }
            }
        }

        if (!opkind) break;
        int prev_ok = (last_status == 0);
        seg = seg + op_pos + (opkind == 1 ? 1 : 2);
        while (*seg == ' ' || *seg == '\t') seg++;
        if (!*seg) break;

        if (opkind == 2)      want = prev_ok;
        else if (opkind == 3) want = !prev_ok;
        else                  want = 1;
    }
}

/* ──────────────────────────── line editing ──────────────────────────────── */

static void redraw(const char *buf, int len, int pos) {
    /* \r, clear line, rewrite, then reposition. The serial console does not
     * echo for us, so the editor owns the whole line. */
    sh_out("\r\033[K", 4);
    sh_out(buf, len);
    int back = len - pos;
    if (back > 0) {
        char seq[16];
        int n = snprintf(seq, sizeof(seq), "\033[%dD", back);
        sh_out(seq, n);
    }
}

static void complete(char *buf, int *len, int *pos) {
    /* Find the word under the cursor. */
    int start = *pos;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t') start--;

    char word[LINE_MAX];
    int  wlen = 0;
    for (int i = start; i < *pos && wlen < LINE_MAX - 1; i++) word[wlen++] = buf[i];
    word[wlen] = 0;

    int is_cmd = (start == 0);
    /* Candidate width is PATH_MAX_LOCAL, not LINE_MAX. Nothing that can be
     * completed is longer than a path, and [GLOB_MAX][LINE_MAX] would put
     * 64 KiB on the stack of a process whose stack is a small fixed window
     * below its initial rsp -- a Tab press at the prompt could overflow it. */
    char cand[GLOB_MAX][PATH_MAX_LOCAL];
    int  ncand = 0;

    if (is_cmd) {
        for (int i = 0; builtin_names[i]; i++) {
            if (strncmp(builtin_names[i], word, wlen) == 0 && ncand < GLOB_MAX)
                strncpy_safe(cand[ncand++], builtin_names[i], PATH_MAX_LOCAL);
        }
        for (int i = 0; i < alias_count; i++)
            if (strncmp(aliases[i].name, word, wlen) == 0 && ncand < GLOB_MAX)
                strncpy_safe(cand[ncand++], aliases[i].name, PATH_MAX_LOCAL);
    } else {
        /* Split into directory prefix and basename for readdir. */
        char dir[PATH_MAX_LOCAL] = "";
        const char *base = word;
        const char *slash = NULL;
        for (char *p = word; *p; p++) if (*p == '/') slash = p;
        if (slash) {
            size_t dl = (size_t)(slash - word);
            if (dl < sizeof(dir)) { memcpy(dir, word, dl); dir[dl] = 0; }
            base = slash + 1;
        }
        char names[4096];
        int nb = sys_readdir(dir[0] ? dir : ".", names, sizeof(names) - 1);
        if (nb > 0) {
            names[nb] = 0;
            int p2 = 0;
            int blen = (int)strlen(base);
            while (p2 < nb && ncand < GLOB_MAX) {
                const char *nm = names + p2;
                p2 += (int)strlen(nm) + 1;
                if (!nm[0]) continue;
                if (strncmp(nm, base, blen) != 0) continue;
                if (slash) snprintf(cand[ncand], PATH_MAX_LOCAL, "%s/%s", dir, nm);
                else       snprintf(cand[ncand], PATH_MAX_LOCAL, "%s", nm);
                ncand++;
            }
        }
    }

    if (ncand == 0) return;

    if (ncand == 1) {
        /* Unique: extend to the common completion. A trailing space marks the
         * end of a command name; a trailing '/' marks a directory, which we
         * cannot detect without stat (sys_stat is broken), so the shell does
         * not guess. */
        int add = (int)strlen(cand[0]) - wlen;
        if (is_cmd) add++;                    /* space after a command name */
        for (int k = 0; k < add && *len < LINE_MAX - 2; k++) {
            buf[*pos] = (is_cmd && k == add - 1) ? ' ' : cand[0][wlen + k];
            (*pos)++; (*len)++;
        }
        buf[*len] = 0;
        redraw(buf, *len, *pos);
        return;
    }

    /* Ambiguous: insert the longest common prefix, then show the choices. */
    int common = (int)strlen(cand[0]);
    for (int i = 1; i < ncand; i++) {
        int j = 0;
        while (j < common && cand[i][j] && cand[i][j] == cand[0][j]) j++;
        common = j;
    }
    if (common > wlen) {
        for (int k = wlen; k < common && *len < LINE_MAX - 2; k++) {
            buf[*pos] = cand[0][k];
            (*pos)++; (*len)++;
        }
        buf[*len] = 0;
        redraw(buf, *len, *pos);
    }
    sh_putc_('\n');
    for (int i = 0; i < ncand; i++) { sh_puts(cand[i]); sh_putc_(' '); }
    sh_putc_('\n');
    redraw(buf, *len, *pos);
}

/* Returns 1 if a line was read, 0 on EOF. */
static int read_line(char *buf, int max) {
    int len = 0, pos = 0;
    int hist_idx = hist_count;     /* one past the newest */
    char saved[LINE_MAX];
    int  have_saved = 0;
    buf[0] = 0;

    for (;;) {
        char c;
        int n = sys_read(0, &c, 1);
        if (n <= 0) {
            /* No data yet. The serial input path is non-blocking, so this is
             * a poll miss, not EOF. Only a real EOF (n == 0) ends the loop. */
            if (n == 0 && len == 0) return 0;
            continue;
        }

        if (c == '\r' || c == '\n') {
            sh_putc_('\n');
            buf[len] = 0;
            return 1;
        }
        if (c == 4) {                       /* Ctrl-D */
            if (len == 0) return 0;
            continue;
        }
        if (c == 3) {                       /* Ctrl-C */
            sh_puts("^C\n");
            buf[0] = 0;
            return 1;
        }
        if (c == 12) {                      /* Ctrl-L */
            sh_out("\033[2J\033[H", 7);
            redraw(buf, len, pos);
            continue;
        }
        if (c == 1) { pos = 0; redraw(buf, len, pos); continue; }        /* Ctrl-A */
        if (c == 5) { pos = len; redraw(buf, len, pos); continue; }       /* Ctrl-E */
        if (c == 21) { buf[pos] = 0; len = pos; redraw(buf, len, pos); continue; } /* Ctrl-U */
        if (c == 11) {                                       /* Ctrl-K */
            buf[pos] = 0; len = pos; redraw(buf, len, pos); continue;
        }
        if (c == 23) {                                       /* Ctrl-W */
            while (pos > 0 && buf[pos - 1] == ' ') pos--;
            while (pos > 0 && buf[pos - 1] != ' ') pos--;
            memmove(buf + pos, buf + pos + 1, (size_t)(len - pos));
            len--; buf[len] = 0; redraw(buf, len, pos);
            continue;
        }
        if (c == '\t') { complete(buf, &len, &pos); continue; }
        if (c == 27) {                                       /* ESC [ X */
            char b1, b2;
            if (sys_read(0, &b1, 1) <= 0) continue;
            if (sys_read(0, &b2, 1) <= 0) continue;
            if (b1 != '[') continue;
            if (b2 == 'D') { if (pos > 0) { pos--; redraw(buf, len, pos); } }
            else if (b2 == 'C') { if (pos < len) { pos++; redraw(buf, len, pos); } }
            else if (b2 == 'H') { pos = 0; redraw(buf, len, pos); }
            else if (b2 == 'F') { pos = len; redraw(buf, len, pos); }
            else if (b2 == 'A' || b2 == 'B') {
                /* Up/Down. On Up, if the cursor is not already at the end of
                 * the line, narrow the search to what is typed so far -- the
                 * prefix search a history-enabled shell provides. */
                int searching = (pos == len && len > 0);
                if (!have_saved && searching) {
                    strncpy_safe(saved, buf, LINE_MAX);
                    have_saved = 1;
                }
                if (b2 == 'A') {
                    if (hist_idx > 0) {
                        hist_idx--;
                        const char *h = history_at(hist_idx);
                        if (h) {
                            const char *use = h;
                            if (searching) {
                                /* Find the newest entry starting with `saved`. */
                                int found = -1;
                                for (int i = hist_count - 1; i >= 0; i--) {
                                    const char *e = history_at(i);
                                    if (e && strncmp(e, saved, strlen(saved)) == 0) { found = i; break; }
                                }
                                if (found >= 0) { hist_idx = found; use = history_at(found); }
                            }
                            strncpy_safe(buf, use, LINE_MAX);
                            len = (int)strlen(buf);
                            pos = len;
                            redraw(buf, len, pos);
                        }
                    }
                } else {
                    if (hist_idx < hist_count) {
                        hist_idx++;
                        const char *h = (hist_idx < hist_count) ? history_at(hist_idx) : NULL;
                        if (h) strncpy_safe(buf, h, LINE_MAX);
                        else if (have_saved) strncpy_safe(buf, saved, LINE_MAX);
                        len = (int)strlen(buf);
                        pos = len;
                        redraw(buf, len, pos);
                    }
                }
            }
            continue;
        }
        if (c == 8 || c == 127) {                            /* Backspace */
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, (size_t)(len - pos));
                pos--; len--; buf[len] = 0;
                redraw(buf, len, pos);
            }
            continue;
        }
        if (c >= ' ' && c < 127) {
            if (len < max - 1) {
                memmove(buf + pos + 1, buf + pos, (size_t)(len - pos));
                buf[pos] = c;
                pos++; len++; buf[len] = 0;
                redraw(buf, len, pos);
            }
            continue;
        }
    }
}

/* ───────────────────────────────── prompt ───────────────────────────────── */

static void draw_prompt(void) {
    /* Themed, oh-my-zsh style: a coloured user@host, the current directory
     * with the home prefix elided, and a marker when the last command failed
     * so the status is visible before the next command is typed. */
    static char host[128] = "";
    if (!host[0]) {
        if (slurp("/etc/hostname", host, sizeof(host)) > 0) {
            size_t hl = strlen(host);
            while (hl > 0 && (host[hl - 1] == '\n' || host[hl - 1] == '\r')) host[--hl] = 0;
        } else {
            strcpy(host, "codeos");
        }
        if (!host[0]) strcpy(host, "codeos");
    }

    char shown[PATH_MAX_LOCAL];
    if (strcmp(cwd, "/") == 0) strcpy(shown, "/");
    else snprintf(shown, sizeof(shown), "%s", cwd + 1);

    if (last_status != 0)
        sh_outf("\033[1;31mroot\033[0;37m@\033[1;33m%s\033[0;37m:\033[1;34m%s\033[0;37m\033[1;31m %d\033[0;37m$ ",
                host, shown, last_status);
    else
        sh_outf("\033[1;32mroot\033[0;37m@\033[1;33m%s\033[0;37m:\033[1;34m%s\033[0m$ ",
                host, shown);
}

int main(void) {
    static char line[LINE_MAX];

    sys_getcwd(cwd, sizeof(cwd));
    var_set("SHELL", "/bin/shell", 1);
    var_set("USER", "root", 1);
    var_set("HOME", "/root", 1);
    var_set("PWD", cwd, 1);
    var_set("TERM", "codeos-tty", 1);
    var_set("PATH", "/bin:/usr/bin", 1);

    /* Before the banner, so the restored entries are already in the ring when
     * the first Up-arrow or `history` touches it. */
    history_load();

    sh_outf("\n  \033[1;36mCodeOS\033[0m shell -- type \033[1;33mhelp\033[0m for builtins,"
            " \033[1;33mhistory\033[0m for the session log\n");

    for (;;) {
        draw_prompt();
        if (!read_line(line, LINE_MAX)) break;
        /* last_status is deliberately NOT reset here. It is reset nowhere,
         * because the value the next line should see in $? is the status of
         * the command that just ran. Clearing it between read_line() and
         * execute_line() -- which is where it used to sit -- meant $? was
         * always 0 by the time the line was parsed, so
         *     vm pause nosuchvm      -> "vm pause: failed", prompt shows 1
         *     echo vm-status=$?      -> 0
         * reported success one line after reporting failure. `static int`
         * zero-initialises it, so the first prompt is 0 as intended, and an
         * abandoned line (Ctrl-C) correctly leaves the previous status alone
         * rather than inventing one. */
        execute_line(line);
        var_set("PWD", cwd, 1);
    }

    /* Saved only on a clean exit (Ctrl-D or an exit that returns here). A
     * reset or a killed shell loses the session, which is why the help text
     * calls this a convenience rather than a guarantee. */
    history_save();
    return 0;
}
