#include "unistd.h"
#include "string.h"
#include "wm_protocol.h"

#define FONT_W 8
#define FONT_H 16
#define MAX_LINES 128
#define LINE_BUF 256
#define CMD_BUF 512

static int win_id;
static int cols, rows;
static char lines[MAX_LINES][LINE_BUF];
static int line_count;
static int scroll_pos;
static char cmd_buf[CMD_BUF];
static int cmd_pos;
static uint32_t fg_color = 0xFFcdd6f4;
static uint32_t bg_color = 0xFF313244;
static int running = 1;

static void wm_send(const void *msg, int len) {
    sys_pwrite(WM_PIPE_CMD, msg, len);
}

static void wm_create_win(int w, int h) {
    uint8_t msg[64];
    int p = 0;
    msg[p++] = WM_CREATE_WIN;
    msg[p++] = w & 0xFF; msg[p++] = (w >> 8) & 0xFF;
    msg[p++] = h & 0xFF; msg[p++] = (h >> 8) & 0xFF;
    const char *title = "Terminal";
    msg[p++] = (uint8_t)strlen(title);
    while (*title) msg[p++] = *title++;
    wm_send(msg, p);
}

static void wm_close_win(void) {
    uint8_t msg[2] = {WM_CLOSE_WIN, (uint8_t)win_id};
    wm_send(msg, 2);
}

static void wm_fill_rect(int x, int y, int w, int h, uint32_t color) {
    uint8_t msg[16];
    msg[0] = WM_FILL_RECT;
    msg[1] = (uint8_t)win_id;
    msg[2] = x & 0xFF; msg[3] = (x >> 8) & 0xFF;
    msg[4] = y & 0xFF; msg[5] = (y >> 8) & 0xFF;
    msg[6] = w & 0xFF; msg[7] = (w >> 8) & 0xFF;
    msg[8] = h & 0xFF; msg[9] = (h >> 8) & 0xFF;
    msg[10] = color & 0xFF; msg[11] = (color >> 8) & 0xFF;
    msg[12] = (color >> 16) & 0xFF; msg[13] = (color >> 24) & 0xFF;
    wm_send(msg, 14);
}

static void wm_draw_str(int x, int y, uint32_t color, const char *s) {
    int slen = (int)strlen(s);
    if (slen > 254) slen = 254;
    uint8_t msg[270];
    int p = 0;
    msg[p++] = WM_DRAW_STR;
    msg[p++] = (uint8_t)win_id;
    msg[p++] = x & 0xFF; msg[p++] = (x >> 8) & 0xFF;
    msg[p++] = y & 0xFF; msg[p++] = (y >> 8) & 0xFF;
    msg[p++] = color & 0xFF; msg[p++] = (color >> 8) & 0xFF;
    msg[p++] = (color >> 16) & 0xFF; msg[p++] = (color >> 24) & 0xFF;
    msg[p++] = (uint8_t)slen;
    for (int i = 0; i < slen; i++) msg[p++] = (uint8_t)s[i];
    wm_send(msg, p);
}



static void wm_flush(void) {
    uint8_t msg[2] = {WM_FLUSH, (uint8_t)win_id};
    wm_send(msg, 2);
}

static void add_line(const char *text) {
    if (line_count < MAX_LINES) {
        int slen = (int)strlen(text);
        if (slen >= LINE_BUF) slen = LINE_BUF - 1;
        for (int i = 0; i < slen; i++) lines[line_count][i] = text[i];
        lines[line_count][slen] = 0;
        line_count++;
    } else {
        for (int i = 1; i < MAX_LINES; i++)
            for (int j = 0; j < LINE_BUF; j++) lines[i-1][j] = lines[i][j];
        int slen = (int)strlen(text);
        if (slen >= LINE_BUF) slen = LINE_BUF - 1;
        for (int i = 0; i < slen; i++) lines[MAX_LINES-1][i] = text[i];
        lines[MAX_LINES-1][slen] = 0;
    }
    if (line_count > rows) scroll_pos = line_count - rows;
    else scroll_pos = 0;
}

static void redraw_all(void) {
    int win_w = cols * FONT_W;
    int win_h = rows * FONT_H;
    wm_fill_rect(0, 0, win_w, win_h, bg_color);

    int start = scroll_pos;
    int end = line_count;
    if (end - start > rows) end = start + rows;

    for (int i = start; i < end; i++) {
        int y = (i - start) * FONT_H;
        wm_draw_str(0, y, fg_color, lines[i]);
    }

    int cur_line = line_count - scroll_pos;
    int prompt_y = cur_line * FONT_H;
    if (prompt_y >= 0 && prompt_y < win_h) {
        char prompt_buf[CMD_BUF + 8];
        prompt_buf[0] = '$';
        prompt_buf[1] = ' ';
        for (int i = 0; i < cmd_pos; i++) prompt_buf[i + 2] = cmd_buf[i];
        prompt_buf[cmd_pos + 2] = 0;
        wm_draw_str(0, prompt_y, fg_color, prompt_buf);
    }

    wm_flush();
}

static void run_cmd(char *line) {
    char *argv[16];
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= 16) break;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return;

    if (strcmp(argv[0], "exit") == 0) {
        running = 0;
    } else if (strcmp(argv[0], "echo") == 0) {
        char out[LINE_BUF];
        int pos = 0;
        for (int i = 1; i < argc; i++) {
            if (i > 1) out[pos++] = ' ';
            for (int j = 0; argv[i][j] && pos < LINE_BUF - 1; j++)
                out[pos++] = argv[i][j];
        }
        out[pos] = 0;
        add_line(out);
    } else if (strcmp(argv[0], "clear") == 0) {
        line_count = 0;
        scroll_pos = 0;
    } else if (strcmp(argv[0], "help") == 0) {
        add_line("Commands: echo, ls, cat, clear, help, ps, exit, uname");
    } else if (strcmp(argv[0], "ps") == 0) {
        add_line("PID  NAME");
        char pidstr[32];
        int pid = sys_getpid();
        int len = 0;
        int tmp = pid;
        do { len++; tmp /= 10; } while (tmp > 0);
        char pidbuf[16];
        tmp = pid;
        for (int i = len - 1; i >= 0; i--) { pidbuf[i] = '0' + (tmp % 10); tmp /= 10; }
        pidbuf[len] = 0;
        int pos2 = 0;
        for (int i = 0; pidbuf[i]; i++) pidstr[pos2++] = pidbuf[i];
        pidstr[pos2++] = ' ';
        pidstr[pos2++] = ' ';
        const char *n = "terminal";
        for (int i = 0; n[i]; i++) pidstr[pos2++] = n[i];
        pidstr[pos2] = 0;
        add_line(pidstr);
    } else if (strcmp(argv[0], "uname") == 0) {
        add_line("CodeOS");
    } else if (strcmp(argv[0], "ls") == 0) {
        const char *path = argc > 1 ? argv[1] : "/bin";
        int fd = sys_open(path, 0);
        if (fd < 0) {
            char err[LINE_BUF];
            int p2 = 0;
            const char *m = "ls: ";
            for (int i = 0; m[i]; i++) err[p2++] = m[i];
            for (int i = 0; path[i]; i++) err[p2++] = path[i];
            const char *m2 = ": not found";
            for (int i = 0; m2[i]; i++) err[p2++] = m2[i];
            err[p2] = 0;
            add_line(err);
            return;
        }
        char buf[512];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            char out[LINE_BUF];
            int op = 0;
            for (int i = 0; i < n && buf[i]; i++) {
                if (buf[i] == '\n' || buf[i] == 0) {
                    out[op] = 0;
                    if (op > 0) add_line(out);
                    op = 0;
                } else {
                    out[op++] = buf[i];
                    if (op >= LINE_BUF - 1) { out[op] = 0; add_line(out); op = 0; }
                }
            }
            if (op > 0) { out[op] = 0; add_line(out); }
        }
        sys_close(fd);
    } else if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) { add_line("cat: missing file"); return; }
        int fd = sys_open(argv[1], 0);
        if (fd < 0) {
            char err[LINE_BUF];
            int p2 = 0;
            const char *m = "cat: ";
            for (int i = 0; m[i]; i++) err[p2++] = m[i];
            for (int i = 0; argv[1][i]; i++) err[p2++] = argv[1][i];
            const char *m2 = ": not found";
            for (int i = 0; m2[i]; i++) err[p2++] = m2[i];
            err[p2] = 0;
            add_line(err);
            return;
        }
        char buf[512];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            char out[LINE_BUF];
            int op = 0;
            for (int i = 0; i < n && buf[i]; i++) {
                if (buf[i] == '\n') {
                    out[op] = 0;
                    if (op > 0) add_line(out);
                    op = 0;
                } else {
                    out[op++] = buf[i];
                    if (op >= LINE_BUF - 1) { out[op] = 0; add_line(out); op = 0; }
                }
            }
            if (op > 0) { out[op] = 0; add_line(out); }
        }
        sys_close(fd);
    } else {
        char err[LINE_BUF];
        int p2 = 0;
        const char *m = "terminal: ";
        for (int i = 0; m[i]; i++) err[p2++] = m[i];
        for (int i = 0; argv[0][i]; i++) err[p2++] = argv[0][i];
        const char *m2 = ": not found";
        for (int i = 0; m2[i]; i++) err[p2++] = m2[i];
        err[p2] = 0;
        add_line(err);
    }
}

int main(void) {
    cols = 80;
    rows = 24;
    int win_w = cols * FONT_W;
    int win_h = rows * FONT_H;
    if (win_w < 320) win_w = 640;
    if (win_h < 200) win_h = 400;
    cols = win_w / FONT_W;
    rows = win_h / FONT_H;

    wm_create_win(win_w, win_h);
    sys_sleep(50);

    uint8_t buf[8];
    int got_conn = 0;
    for (int tries = 0; tries < 100 && !got_conn; tries++) {
        int n = sys_read(WM_PIPE_EVENT, buf, 2);
        if (n >= 2 && buf[0] == WM_EVENT_CONN) {
            win_id = buf[1];
            got_conn = 1;
        }
        sys_sleep(10);
    }
    if (!got_conn) {
        sys_write("terminal: no connection from compositor\n", 40);
        sys_exit(1);
    }

    add_line("CodeOS Terminal");
    add_line("Type 'help' for commands.");
    redraw_all();

    while (running) {
        uint8_t ev[8];
        int n = sys_read(WM_PIPE_EVENT, ev, sizeof(ev));
        if (n > 0) {
            if (ev[0] == WM_EVENT_KEY && ev[1] == win_id) {
                int key = (int)ev[2] | ((int)ev[3] << 8) |
                          ((int)ev[4] << 16) | ((int)ev[5] << 24);
                if (key == '\r' || key == '\n') {
                    cmd_buf[cmd_pos] = 0;
                    add_line(cmd_buf);
                    char *cmd = cmd_buf;
                    while (*cmd == ' ') cmd++;
                    if (*cmd) run_cmd(cmd);
                    cmd_pos = 0;
                    redraw_all();
                } else if (key == '\b' || key == 127) {
                    if (cmd_pos > 0) cmd_pos--;
                    redraw_all();
                } else if (key >= ' ' && key < 127 && cmd_pos < CMD_BUF - 1) {
                    cmd_buf[cmd_pos++] = (char)key;
                    redraw_all();
                }
            } else if (ev[0] == WM_EVENT_CLOSED && ev[1] == win_id) {
                running = 0;
            }
        }
        sys_sleep(5);
    }

    wm_close_win();
    sys_exit(0);
    return 0;
}
