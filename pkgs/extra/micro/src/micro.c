/* micro — a small terminal text editor for CodeOS.
 *
 * This is NOT the upstream Go editor (github.com/zyedidia/micro): Go and its
 * terminal/Unicode runtime are not available in the freestanding toolchain.
 * It is a compact editor with the familiar micro-style keys, written against
 * the same syscalls the rest of the userspace tree uses.
 *
 * Keys:
 *   arrows / Ctrl-A / Ctrl-E   move the cursor (Home/End also work)
 *   Enter, Backspace, Tab      edit
 *   Ctrl-K                     delete to end of line
 *   Ctrl-O                     open a file (prompts for the path)
 *   Ctrl-S                     save
 *   Ctrl-Q                     quit (twice if there are unsaved changes)
 *
 * Text lives in a fixed 256 x 200 grid; the kernel caps files at 64 KiB.
 * Rendering uses the same ANSI escape sequences as core/shell.
 */

#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include <stdint.h>

#define MICRO_MAX_LINES  256
#define MICRO_LINE_CAP  200
#define MICRO_FILE_CAP  65536
#define MICRO_PATH_CAP  240

static char lines[MICRO_MAX_LINES][MICRO_LINE_CAP];
static int line_len[MICRO_MAX_LINES];
static int nlines = 1;
static int cur_line, cur_col;
static char path[MICRO_PATH_CAP];
static int modified, quit_armed;
/* A trailing newline is kept as a flag rather than an extra empty line, so
 * opening and saving an untouched file is byte-for-byte neutral. */
static int trailing_nl = 1;

static void puts_(const char *s) {
    sys_write(s, (int)strlen(s));
}

static void put_dec(unsigned v) {
    char buf[12];
    int n = 0;
    if (!v) buf[n++] = '0';
    while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) sys_write(buf + --n, 1);
}

static void redraw(void) {
    puts_("\x1b[2J\x1b[H");
    for (int i = 0; i < nlines; i++) {
        sys_write(lines[i], line_len[i]);
        puts_("\r\n");
    }
    for (int i = nlines; i < 18; i++) puts_("\r\n");
    puts_("micro | ");
    if (path[0]) puts_(path); else puts_("untitled");
    puts_(modified ? "  *  Ctrl-S save  Ctrl-Q quit" : "  Ctrl-S save  Ctrl-Q quit");
    puts_("\r\n");
    puts_("\x1b[");
    put_dec((unsigned)(cur_line + 1));
    puts_(";");
    put_dec((unsigned)(cur_col + 1));
    puts_("H");
}

static void set_line(int row, const char *src, int len) {
    if (len > MICRO_LINE_CAP - 1) len = MICRO_LINE_CAP - 1;
    memcpy(lines[row], src, (size_t)len);
    lines[row][len] = 0;
    line_len[row] = len;
}

static int open_file(const char *name) {
    static uint8_t buf[MICRO_FILE_CAP + 1];
    size_t k = 0;
    for (; name[k] && k < MICRO_PATH_CAP - 1; k++) path[k] = name[k];
    path[k] = 0;
    nlines = 1;
    line_len[0] = 0;
    lines[0][0] = 0;
    cur_line = cur_col = 0;
    modified = 0;
    trailing_nl = 1;

    int fd = sys_open(name, 0);
    if (fd < 0) return -1;
    size_t got = 0, start = 0;
    for (;;) {
        if (got >= sizeof(buf)) break;
        int n = sys_read(fd, buf + got, (int)(sizeof(buf) - got));
        if (n <= 0) break;
        got += (size_t)n;
    }
    sys_close(fd);

    trailing_nl = (got > 0 && buf[got - 1] == '\n');
    for (size_t i = 0; i < got; i++) {
        if (buf[i] != '\n') continue;
        set_line(nlines - 1, (const char *)buf + start, (int)(i - start));
        if (i == got - 1) { start = i + 1; break; }
        if (nlines >= MICRO_MAX_LINES) break;
        nlines++;
        start = i + 1;
    }
    if (start < got && nlines <= MICRO_MAX_LINES)
        set_line(nlines - 1, (const char *)buf + start, (int)(got - start));
    cur_line = cur_col = 0;
    modified = 0;
    return 0;
}

static int save_file(void) {
    if (!path[0]) {
        puts_("No filename: use Ctrl-O to choose a path first.");
        return -1;
    }
    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { puts_("Save failed: cannot create file."); return -1; }
    for (int i = 0; i < nlines; i++) {
        if (line_len[i] && sys_pwrite(fd, lines[i], line_len[i]) != line_len[i]) {
            sys_close(fd);
            puts_("Save failed while writing.");
            return -1;
        }
        if ((i + 1 < nlines || trailing_nl) && sys_pwrite(fd, "\n", 1) != 1) {
            sys_close(fd);
            puts_("Save failed while writing a line break.");
            return -1;
        }
    }
    sys_close(fd);
    modified = 0;
    puts_("Saved.");
    return 0;
}

static int insert(char c) {
    if (cur_col >= MICRO_LINE_CAP - 1) return -1;
    char *s = lines[cur_line];
    int len = line_len[cur_line];
    for (int i = len; i > cur_col; i--) s[i] = s[i - 1];
    s[cur_col] = c;
    s[len + 1] = 0;
    cur_col++;
    line_len[cur_line] = len + 1;
    modified = 1;
    return 0;
}

static void delete_at_cursor(void) {
    char *s = lines[cur_line];
    int len = line_len[cur_line];
    if (cur_col >= len) return;
    for (int i = cur_col; i < len - 1; i++) s[i] = s[i + 1];
    line_len[cur_line] = len - 1;
    s[line_len[cur_line]] = 0;
    modified = 1;
}

static void backspace(void) {
    if (cur_col > 0) {
        char *s = lines[cur_line];
        int len = line_len[cur_line];
        for (int i = cur_col - 1; i < len - 1; i++) s[i] = s[i + 1];
        line_len[cur_line] = len - 1;
        s[line_len[cur_line]] = 0;
        cur_col--;
        modified = 1;
    } else if (cur_line > 0) {
        int prev = line_len[cur_line - 1];
        int avail = MICRO_LINE_CAP - 1 - prev;
        int take = line_len[cur_line];
        if (take > avail) take = avail;
        for (int i = 0; i < take; i++) lines[cur_line - 1][prev + i] = lines[cur_line][i];
        line_len[cur_line - 1] = prev + take;
        lines[cur_line - 1][line_len[cur_line - 1]] = 0;
        cur_col = prev;
        for (int i = cur_line; i < nlines - 1; i++) {
            set_line(i, lines[i + 1], line_len[i + 1]);
        }
        nlines--;
        cur_line--;
        if (cur_line + 1 == nlines) trailing_nl = 0;
        modified = 1;
    }
}

static void newline(void) {
    if (nlines >= MICRO_MAX_LINES) return;
    for (int i = nlines; i > cur_line + 1; i--)
        set_line(i, lines[i - 1], line_len[i - 1]);
    int tail = line_len[cur_line] - cur_col;
    set_line(cur_line + 1, lines[cur_line] + cur_col, tail);
    line_len[cur_line] = cur_col;
    lines[cur_line][cur_col] = 0;
    nlines++;
    cur_line++;
    cur_col = 0;
    trailing_nl = 1;
    modified = 1;
}

/* Ctrl-O: read a path from stdin, echoing it, until Enter. */
static void prompt_open(void) {
    char name[MICRO_PATH_CAP];
    int len = 0;
    puts_("\r\nOpen: ");
    for (;;) {
        char c;
        int n = sys_read(0, &c, 1);
        if (n <= 0) return;
        if (c == '\n' || c == '\r') break;
        if ((c == 8 || c == 127) && len > 0) {
            len--;
            puts_("\b \b");
            continue;
        }
        if (c < 32 || c >= 127 || len + 1 >= (int)sizeof(name)) continue;
        name[len++] = c;
        sys_write(&c, 1);
    }
    name[len] = 0;
    if (!len) return;
    if (open_file(name)) {
        puts_("  cannot open that file");
    } else {
        puts_("  opened");
    }
    quit_armed = 0;
}

static int read_key(char *c) {
    int n = sys_read(0, c, 1);
    return n > 0;
}

static void escape_key(void) {
    char b1, b2;
    if (!read_key(&b1) || b1 != '[' || !read_key(&b2)) return;
    if (b2 == 'A') { if (cur_line > 0) { cur_line--; if (cur_col > line_len[cur_line]) cur_col = line_len[cur_line]; } }
    else if (b2 == 'B') { if (cur_line + 1 < nlines) { cur_line++; if (cur_col > line_len[cur_line]) cur_col = line_len[cur_line]; } }
    else if (b2 == 'C') { if (cur_col < line_len[cur_line]) cur_col++; }
    else if (b2 == 'D') { if (cur_col > 0) cur_col--; }
    else if (b2 == 'H') { cur_col = 0; }
    else if (b2 == 'F') { cur_col = line_len[cur_line]; }
    else if (b2 == '3') { char t; if (read_key(&t)) delete_at_cursor(); }
}

int main(int argc, char **argv) {
    if (argc > 2) {
        puts("usage: micro [file]");
        return 2;
    }
    if (argc == 2 && open_file(argv[1]))
        printf("micro: cannot open %s; starting with an empty buffer\n", argv[1]);
    redraw();
    puts_("Ctrl-O open  Ctrl-S save  Ctrl-Q quit");

    for (;;) {
        char c;
        if (!read_key(&c)) break;             /* EOF is a clean exit */
        if (c == 17) {                        /* Ctrl-Q */
            if (modified && !quit_armed) {
                quit_armed = 1;
                puts_("Unsaved changes - Ctrl-Q again to quit.");
                continue;
            }
            break;
        }
        quit_armed = 0;
        if (c == 19) { save_file(); redraw(); continue; }        /* Ctrl-S */
        if (c == 15) { prompt_open(); redraw(); continue; }      /* Ctrl-O */
        if (c == 11) {                                           /* Ctrl-K */
            line_len[cur_line] = cur_col;
            lines[cur_line][cur_col] = 0;
            modified = 1;
        } else if (c == 27) {
            escape_key();
        } else if (c == '\n' || c == '\r') {
            newline();
        } else if (c == 8 || c == 127) {
            backspace();
        } else if (c == '\t') {
            insert(' ');
            insert(' ');
        } else if (c >= 32 && c < 127) {
            insert(c);
        }
        redraw();
    }
    puts_("\x1b[2J\x1b[H");
    return 0;
}
