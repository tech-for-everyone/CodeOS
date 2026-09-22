#include "editor.h"
#include "kprintf.h"
#include "string.h"
#include "ext2.h"
#include "pmm.h"
#include "vmm.h"
#include "../drivers/keyboard.h"
#include "../arch/x86_64/fb.h"

#define EDITOR_TAB 4
#define MAX_LINES  2048
#define MAX_COLS   1024

struct line {
    char *data;
    int len;
    int cap;
};

static struct line lines[MAX_LINES];
static int line_count;
static int cur_row;
static int cur_col;
static int top_row;
static int scr_w, scr_h;
static char filename[256];
static char msg_buf[80];
static int msg_timer;
static char input_buf[256];
static int running;
static int vim_mode;
static char yank_buf[4096];
static int yank_len;
static int vis_row_start, vis_col_start;
static int vis_mode;

#define COL_NORMAL_FG  FB_RGB(192,192,192)
#define COL_NORMAL_BG  FB_RGB(0,0,0)
#define COL_STATUS_FG  FB_RGB(255,255,255)
#define COL_STATUS_BG  FB_RGB(0,0,170)
#define COL_REVERSE_FG  FB_RGB(0,0,0)
#define COL_REVERSE_BG  FB_RGB(192,192,192)
#define COL_INSERT_FG  FB_RGB(0,0,0)
#define COL_INSERT_BG  FB_RGB(0,170,0)
#define COL_VISUAL_FG  FB_RGB(255,255,255)
#define COL_VISUAL_BG  FB_RGB(0,170,170)

static void editor_cleanup(void) {
    for (int i = 0; i < line_count; i++)
        if (lines[i].data) pmm_free_pages((uint64_t)lines[i].data, (lines[i].cap + 4095) / 4096);
    line_count = 0;
}

static int line_grow(struct line *l, int min_cap) {
    int cap = 64;
    while (cap < min_cap + 1) cap *= 2;
    void *raw = (void*)pmm_alloc_pages((cap + 4095) / 4096);
    if (!raw) return -1;
    char *nd = (char*)phys_to_virt((uint64_t)raw);
    if (l->data) {
        memcpy(nd, l->data, l->len + 1);
        pmm_free_pages(virt_to_phys((uint64_t)l->data), (l->cap + 4095) / 4096);
    }
    l->data = nd;
    l->cap = cap;
    return 0;
}

static int line_ins(struct line *l, int pos, char c) {
    if (pos < 0 || pos > l->len) return -1;
    if (l->len + 2 > l->cap && line_grow(l, l->len + 2) < 0) return -1;
    memmove(l->data + pos + 1, l->data + pos, l->len - pos + 1);
    l->data[pos] = c;
    l->len++;
    return 0;
}

static int load_file(const char *path) {
    int fs = ext2_read_file_path(path, 0, 0);
    if (fs <= 0) return 0;
    void *raw = (void*)pmm_alloc_pages((fs + 4095) / 4096);
    if (!raw) return -1;
    char *buf = (char*)phys_to_virt((uint64_t)raw);
    if (ext2_read_file_path(path, buf, fs) != fs) { pmm_free_pages((uint64_t)raw, (fs + 4095) / 4096); return -1; }
    line_count = 0;
    int pos = 0;
    while (pos < fs && line_count < MAX_LINES) {
        struct line *l = &lines[line_count];
        memset(l, 0, sizeof(*l));
        if (line_grow(l, 128) < 0) break;
        while (pos < fs && buf[pos] != '\n' && l->len < MAX_COLS)
            line_ins(l, l->len, buf[pos++]);
        if (pos < fs && buf[pos] == '\n') pos++;
        line_count++;
    }
    pmm_free_pages((uint64_t)buf, (fs + 4095) / 4096);
    cur_row = 0;
    cur_col = 0;
    top_row = 0;
    return line_count;
}

static void set_msg(const char *s) {
    int n = strlen(s);
    if (n > 78) n = 78;
    memcpy(msg_buf, s, n);
    msg_buf[n] = 0;
    msg_timer = 200;
}

static void draw_screen(void) {
    for (int i = 0; i < scr_h - 1; i++) {
        int li = top_row + i;
        fb_fill_row_bg(i, COL_NORMAL_BG);
        if (li >= 0 && li < line_count)
            fb_write_styled(i, 0, lines[li].data, COL_NORMAL_FG, COL_NORMAL_BG);
    }

    if (vim_mode == 4) {
        int vr1 = vis_row_start, vc1 = vis_col_start;
        int vr2 = cur_row, vc2 = cur_col;
        if (vr1 > vr2 || (vr1 == vr2 && vc1 > vc2)) {
            int tmp = vr1; vr1 = vr2; vr2 = tmp;
            tmp = vc1; vc1 = vc2; vc2 = tmp;
        }
        if (vis_mode == 1) {
            vc1 = 0;
            vc2 = 9999;
        }
        for (int i = 0; i < scr_h - 1; i++) {
            int li = top_row + i;
            if (li < 0 || li >= line_count) continue;
            int sc = 0, ec = lines[li].len;
            if (li == vr1) sc = vc1;
            if (li == vr2) ec = vc2;
            if (li < vr1) continue;
            if (li > vr2) continue;
            for (int c = sc; c < lines[li].len && c <= ec; c++)
                fb_drawchar(c, i, lines[li].data[c], COL_VISUAL_FG, COL_VISUAL_BG);
        }
    }

    char bar[80];
    int n = 0;
    const char *p = filename;
    const char *sl = strchr(p, '/');
    while (sl) { p = sl + 1; sl = strchr(p, '/'); }
    int pn = 0;
    while (*p && pn < 24 && n < 78) { bar[n++] = *p++; pn++; }
    if (line_count > 0 && n < 65)
        n += sprintf(bar + n, " %d/%d", cur_row + 1, line_count);
    if (vim_mode == 1 && n < 72) { const char *ms = " NORMAL"; while (*ms && n < 79) bar[n++] = *ms++; }
    else if (vim_mode == 2 && n < 72) { const char *ms = " INSERT"; while (*ms && n < 79) bar[n++] = *ms++; }
    else if (vim_mode == 4 && n < 72) { const char *ms = " VISUAL"; while (*ms && n < 79) bar[n++] = *ms++; }
    bar[n] = 0;
    fb_fill_row_bg(scr_h - 1, vim_mode == 2 ? COL_INSERT_BG : COL_STATUS_BG);
    fb_write_styled(scr_h - 1, 0, bar,
        vim_mode == 2 ? COL_INSERT_FG : COL_STATUS_FG,
        vim_mode == 2 ? COL_INSERT_BG : COL_STATUS_BG);

    if (msg_timer > 0 && msg_buf[0]) {
        int msg_col = (int)scr_w - 1 - (int)strlen(msg_buf);
        if (msg_col < 0) msg_col = 0;
        fb_write_styled(scr_h - 1, msg_col, msg_buf, COL_REVERSE_FG, COL_REVERSE_BG);
    }
}

static void line_del_char(int row, int col) {
    if (row < 0 || row >= line_count || col < 0 || col >= lines[row].len) return;
    memmove(lines[row].data + col, lines[row].data + col + 1, lines[row].len - col);
    lines[row].len--;
}

static void line_insert_line(int row) {
    if (line_count >= MAX_LINES) return;
    if (row < 0) row = 0;
    if (row > line_count) row = line_count;
    memmove(&lines[row + 1], &lines[row], (line_count - row) * sizeof(struct line));
    memset(&lines[row], 0, sizeof(struct line));
    line_grow(&lines[row], 128);
    lines[row].data[0] = 0;
    line_count++;
}

static void line_delete_line(int row) {
    if (row < 0 || row >= line_count) return;
    if (lines[row].data) pmm_free_pages((uint64_t)lines[row].data, (lines[row].cap + 4095) / 4096);
    memmove(&lines[row], &lines[row + 1], (line_count - row - 1) * sizeof(struct line));
    line_count--;
}

static int line_join(int row) {
    if (row < 0 || row >= line_count - 1) return -1;
    struct line *a = &lines[row];
    struct line *b = &lines[row + 1];
    if (a->len + b->len + 1 > a->cap && line_grow(a, a->len + b->len + 1) < 0) return -1;
    memcpy(a->data + a->len, b->data, b->len + 1);
    a->len += b->len;
    line_delete_line(row + 1);
    return 0;
}

static void move_to(int row, int col) {
    if (row < 0) row = 0;
    if (row >= line_count) row = line_count > 0 ? line_count - 1 : 0;
    cur_row = row;
    if (col < 0) col = 0;
    if (line_count > 0 && col > lines[cur_row].len) col = lines[cur_row].len;
    cur_col = col;
    if (cur_row < top_row) top_row = cur_row;
    if (cur_row >= top_row + scr_h - 1) top_row = cur_row - scr_h + 2;
    if (top_row < 0) top_row = 0;
}

static int edit_readline(const char *prompt, char *buf, int max) {
    int len = 0;
    buf[0] = 0;
    for (;;) {
        fb_fill_row_bg(scr_h - 1, COL_STATUS_BG);
        fb_write_styled(scr_h - 1, 0, prompt, COL_STATUS_FG, COL_STATUS_BG);
        fb_write_styled(scr_h - 1, strlen(prompt), buf, COL_REVERSE_FG, COL_REVERSE_BG);
        fb_move_cursor(scr_h - 1, strlen(prompt) + len);
        int c = keyboard_getchar();
        if (c == '\n' || c == '\r') { buf[len] = 0; return len; }
        if (c == 0x1B) { buf[0] = 0; return -1; }
        if ((c == 0x7F || c == '\b') && len > 0) { len--; buf[len] = 0; }
        else if (c >= 0x20 && c < 0x7F && len < max - 1) { buf[len++] = c; buf[len] = 0; }
    }
}

static int save_file(void) {
    int total_size = 0;
    for (int i = 0; i < line_count; i++)
        total_size += lines[i].len + 1;
    void *raw = (void*)pmm_alloc_pages((total_size + 4095) / 4096);
    if (!raw) return -1;
    char *out = (char*)phys_to_virt((uint64_t)raw);
    int pos = 0;
    for (int i = 0; i < line_count; i++) {
        memcpy(out + pos, lines[i].data, lines[i].len);
        pos += lines[i].len;
        out[pos++] = '\n';
    }
    if (pos > 0) pos--;
    int w = ext2_write_file_path(filename, out, pos);
    pmm_free_pages((uint64_t)raw, (total_size + 4095) / 4096);
    return w;
}

static void nano_loop(void) {
    running = 1;
    while (running) {
        draw_screen();
        fb_move_cursor(cur_row - top_row, cur_col);
        if (msg_timer > 0) msg_timer--;

        int ch = keyboard_getchar();

        if (ch == 0x11) { running = 0; break; }

        if (ch == KEY_UP)    { move_to(cur_row - 1, cur_col); continue; }
        if (ch == KEY_DOWN)  { move_to(cur_row + 1, cur_col); continue; }
        if (ch == KEY_LEFT)  { move_to(cur_row, cur_col - 1); continue; }
        if (ch == KEY_RIGHT) { move_to(cur_row, cur_col + 1); continue; }
        if (ch == KEY_HOME)  { move_to(cur_row, 0); continue; }
        if (ch == KEY_END)   { move_to(cur_row, lines[cur_row].len); continue; }
        if (ch == KEY_PGUP)  { move_to(cur_row - scr_h + 3, cur_col); continue; }
        if (ch == KEY_PGDN)  { move_to(cur_row + scr_h - 3, cur_col); continue; }
        if (ch == 0x04) { move_to(cur_row + scr_h / 2, cur_col); continue; }
        if (ch == 0x15) { move_to(cur_row - scr_h / 2, cur_col); continue; }

        if (ch == 0x0F) {
            int w = save_file();
            if (w < 0) set_msg("Error saving file");
            else { sprintf(msg_buf, "Saved %d bytes", w); msg_timer = 100; }
            continue;
        }

        if (ch == 0x17) {
            if (edit_readline("Search: ", input_buf, 128) > 0) {
                int found = 0;
                for (int r = cur_row; r < line_count && !found; r++) {
                    char *p = strstr(lines[r].data, input_buf);
                    if (p) { cur_row = r; cur_col = (int)(p - lines[r].data); found = 1; }
                }
                if (!found) set_msg("Not found");
            }
            continue;
        }
    }
}

static void vim_cmdline(void) {
    char buf[128];
    if (edit_readline(":", buf, 127) <= 0) { vim_mode = 1; return; }
    if (strcmp(buf, "w") == 0) {
        int w = save_file();
        if (w < 0) set_msg("Error saving file");
        else { sprintf(msg_buf, "Saved %d bytes", w); msg_timer = 100; }
    } else if (strcmp(buf, "q") == 0 || strcmp(buf, "q!") == 0) {
        running = 0;
    } else if (strcmp(buf, "wq") == 0) {
        int w = save_file();
        if (w >= 0) running = 0;
        else set_msg("Error saving file");
    } else {
        set_msg("Unknown command");
    }
    vim_mode = 1;
}

static int vim_moveto_word(void) {
    int r = cur_row, c = cur_col;
    if (r < line_count && c >= lines[r].len) { r++; c = 0; }
    while (r < line_count) {
        c++;
        if (c >= lines[r].len) { r++; c = 0; }
        else if (lines[r].data[c] != ' ') break;
    }
    if (r < line_count) { move_to(r, c); return 1; }
    return 0;
}

static int vim_moveto_bword(void) {
    int r = cur_row, c = cur_col;
    c--;
    if (c < 0) { r--; if (r >= 0) c = lines[r].len - 1; }
    while (r >= 0 && c > 0 && lines[r].data[c - 1] == ' ') c--;
    if (r >= 0) { move_to(r, c); return 1; }
    return 0;
}

static void vim_normal(int ch) {
    int count = 0;
    int got_count = 0;
    while (ch >= '1' && ch <= '9') {
        got_count = 1;
        count = count * 10 + (ch - '0');
        ch = keyboard_getchar();
    }
    if (!got_count) count = 1;

    if (ch == 0x1B) return;
    if (ch == 'h' || ch == KEY_LEFT)  { for (int i = 0; i < count; i++) move_to(cur_row, cur_col - 1); return; }
    if (ch == 'j' || ch == KEY_DOWN)  { for (int i = 0; i < count; i++) move_to(cur_row + 1, cur_col); return; }
    if (ch == 'k' || ch == KEY_UP)    { for (int i = 0; i < count; i++) move_to(cur_row - 1, cur_col); return; }
    if (ch == 'l' || ch == KEY_RIGHT) { for (int i = 0; i < count; i++) move_to(cur_row, cur_col + 1); return; }
    if (ch == KEY_HOME || ch == '0')  { move_to(cur_row, 0); return; }
    if (ch == KEY_END || ch == '$')   { move_to(cur_row, lines[cur_row].len); return; }
    if (ch == KEY_PGUP)  { move_to(cur_row - scr_h + 3, cur_col); return; }
    if (ch == KEY_PGDN)  { move_to(cur_row + scr_h - 3, cur_col); return; }

    if (ch == 0x04) { for (int i = 0; i < count; i++) move_to(cur_row + scr_h / 2, cur_col); set_msg(""); return; }
    if (ch == 0x15) { for (int i = 0; i < count; i++) move_to(cur_row - scr_h / 2, cur_col); set_msg(""); return; }

    if (ch == 'g') {
        int c2 = keyboard_getchar();
        if (c2 == 'g') {
            if (got_count) {
                int target = count - 1;
                if (target >= line_count) target = line_count - 1;
                if (target < 0) target = 0;
                move_to(target, 0);
            } else {
                move_to(0, 0);
            }
        }
        return;
    }
    if (ch == 'G') {
        if (got_count) {
            int target = count - 1;
            if (target >= line_count) target = line_count - 1;
            if (target < 0) target = 0;
            move_to(target, 0);
        } else {
            if (line_count > 0) move_to(line_count - 1, 0);
        }
        return;
    }

    if (ch == 'w') { for (int i = 0; i < count; i++) { if (!vim_moveto_word()) break; } return; }
    if (ch == 'b') { for (int i = 0; i < count; i++) { if (!vim_moveto_bword()) break; } return; }
    if (ch == 'e') {
        for (int i = 0; i < count; i++) {
            int r = cur_row, c = cur_col + 1;
            while (r < line_count) {
                if (c >= lines[r].len) { r++; c = 0; }
                else if (lines[r].data[c] != ' ') {
                    if (c + 1 >= lines[r].len || lines[r].data[c + 1] == ' ') break;
                    c++;
                } else c++;
            }
            if (r < line_count) move_to(r, c);
        }
        return;
    }

    if (ch == 'i') { vim_mode = 2; return; }
    if (ch == 'I') { move_to(cur_row, 0); vim_mode = 2; return; }
    if (ch == 'a') { move_to(cur_row, cur_col + 1); vim_mode = 2; return; }
    if (ch == 'A') { move_to(cur_row, lines[cur_row].len); vim_mode = 2; return; }
    if (ch == 'o') {
        line_insert_line(cur_row + 1);
        move_to(cur_row + 1, 0);
        vim_mode = 2;
        return;
    }
    if (ch == 'O') {
        line_insert_line(cur_row);
        move_to(cur_row, 0);
        vim_mode = 2;
        return;
    }

    if (ch == 'x') {
        for (int i = 0; i < count; i++) line_del_char(cur_row, cur_col);
        return;
    }

    if (ch == 'd') {
        int c2 = keyboard_getchar();
        if (c2 == 'd') {
            int dcount = got_count ? count : 1;
            int dr = cur_row;
            yank_len = 0;
            for (int i = 0; i < dcount && dr < line_count; i++) {
                if (yank_len + lines[dr].len <= 4095) {
                    memcpy(yank_buf + yank_len, lines[dr].data, lines[dr].len);
                    yank_len += lines[dr].len;
                    if (i < dcount - 1) yank_buf[yank_len++] = '\n';
                }
                line_delete_line(dr);
            }
            if (line_count == 0) {
                memset(&lines[0], 0, sizeof(struct line));
                line_grow(&lines[0], 128);
                lines[0].data[0] = 0;
                line_count = 1;
            }
            if (dr >= line_count) dr = line_count - 1;
            move_to(dr, 0);
        }
        return;
    }
    if (ch == 'y') {
        int c2 = keyboard_getchar();
        if (c2 == 'y') {
            int ycount = got_count ? count : 1;
            yank_len = 0;
            for (int i = 0; i < ycount && cur_row + i < line_count; i++) {
                int l = lines[cur_row + i].len;
                if (yank_len + l > 4095) l = 4095 - yank_len;
                if (l > 0) {
                    memcpy(yank_buf + yank_len, lines[cur_row + i].data, l);
                    yank_len += l;
                }
                if (i < ycount - 1 && yank_len < 4095) yank_buf[yank_len++] = '\n';
            }
            sprintf(msg_buf, "Yanked %d lines", ycount); msg_timer = 50;
        }
        return;
    }
    if (ch == 'p') {
        if (yank_len > 0) {
            line_insert_line(cur_row + 1);
            struct line *l = &lines[cur_row + 1];
            if (l->cap < yank_len + 1) line_grow(l, yank_len + 1);
            memcpy(l->data, yank_buf, yank_len);
            l->len = yank_len;
            l->data[yank_len] = 0;
            move_to(cur_row + 1, 0);
        }
        return;
    }
    if (ch == 'P') {
        if (yank_len > 0) {
            line_insert_line(cur_row);
            struct line *l = &lines[cur_row];
            if (l->cap < yank_len + 1) line_grow(l, yank_len + 1);
            memcpy(l->data, yank_buf, yank_len);
            l->len = yank_len;
            l->data[yank_len] = 0;
            move_to(cur_row, 0);
        }
        return;
    }
    if (ch == 'u') { set_msg("Undo not implemented"); return; }
    if (ch == 'J') { line_join(cur_row); return; }

    if (ch == ':') { vim_cmdline(); return; }
    if (ch == 'v') { vis_row_start = cur_row; vis_col_start = cur_col; vis_mode = 0; vim_mode = 4; return; }
    if (ch == 'V') { vis_row_start = cur_row; vis_col_start = 0; vis_mode = 1; vim_mode = 4; return; }
    if (ch == '/') {
        if (edit_readline("/", input_buf, 128) > 0) {
            int found = 0;
            for (int r = cur_row; r < line_count && !found; r++) {
                char *p = strstr(lines[r].data, input_buf);
                if (p) { cur_row = r; cur_col = (int)(p - lines[r].data); found = 1; }
            }
            if (!found) set_msg("Not found");
        }
        return;
    }

    if (ch == 0x11) { running = 0; return; }
}

static void vim_insert(int ch) {
    if (ch == 0x1B) { vim_mode = 1; return; }

    if (ch == KEY_UP)    { move_to(cur_row - 1, cur_col); return; }
    if (ch == KEY_DOWN)  { move_to(cur_row + 1, cur_col); return; }
    if (ch == KEY_LEFT)  { move_to(cur_row, cur_col - 1); return; }
    if (ch == KEY_RIGHT) { move_to(cur_row, cur_col + 1); return; }
    if (ch == KEY_HOME)  { move_to(cur_row, 0); return; }
    if (ch == KEY_END)   { move_to(cur_row, lines[cur_row].len); return; }

    if (ch == '\n' || ch == '\r') {
        struct line *l = &lines[cur_row];
        line_insert_line(cur_row + 1);
        if (cur_col < l->len) {
            struct line *nl = &lines[cur_row + 1];
            int tail_len = l->len - cur_col;
            if (nl->cap < tail_len + 1) line_grow(nl, tail_len + 1);
            memcpy(nl->data, l->data + cur_col, tail_len + 1);
            nl->len = tail_len;
            l->len = cur_col;
            l->data[cur_col] = 0;
        }
        move_to(cur_row + 1, 0);
        return;
    }
    if (ch == 0x7F || ch == '\b') {
        if (cur_col > 0) { line_del_char(cur_row, cur_col - 1); move_to(cur_row, cur_col - 1); }
        else if (cur_row > 0) {
            int old_len = lines[cur_row - 1].len;
            line_join(cur_row - 1);
            move_to(cur_row, old_len);
        }
        return;
    }
    if (ch == KEY_DEL) {
        if (cur_col < lines[cur_row].len) line_del_char(cur_row, cur_col);
        else if (cur_row < line_count - 1) { line_join(cur_row); }
        return;
    }
    if (ch >= 0x20 && ch < 0x7F) {
        line_ins(&lines[cur_row], cur_col, ch);
        cur_col++;
        return;
    }
}

static void vim_visual(int ch) {
    if (ch == 0x1B) { vim_mode = 1; return; }
    if (ch == 'v' && vis_mode == 0) { vim_mode = 1; return; }
    if (ch == 'V') { vis_mode = 1; return; }

    if (ch == 'h' || ch == KEY_LEFT)  { move_to(cur_row, cur_col - 1); return; }
    if (ch == 'j' || ch == KEY_DOWN)  { move_to(cur_row + 1, cur_col); return; }
    if (ch == 'k' || ch == KEY_UP)    { move_to(cur_row - 1, cur_col); return; }
    if (ch == 'l' || ch == KEY_RIGHT) { move_to(cur_row, cur_col + 1); return; }
    if (ch == KEY_HOME || ch == '0')  { move_to(cur_row, 0); return; }
    if (ch == KEY_END || ch == '$')   { move_to(cur_row, lines[cur_row].len); return; }
    if (ch == 'w') { vim_moveto_word(); return; }
    if (ch == 'b') { vim_moveto_bword(); return; }
    if (ch == 'G') { if (line_count > 0) move_to(line_count - 1, 0); return; }
    if (ch == 'g') { int c2 = keyboard_getchar(); if (c2 == 'g') move_to(0, 0); return; }
    if (ch == KEY_PGUP)  { move_to(cur_row - scr_h + 3, cur_col); return; }
    if (ch == KEY_PGDN)  { move_to(cur_row + scr_h - 3, cur_col); return; }
    if (ch == 0x04) { move_to(cur_row + scr_h / 2, cur_col); return; }
    if (ch == 0x15) { move_to(cur_row - scr_h / 2, cur_col); return; }

    if (ch == 'd' || ch == 'x') {
        int vr1 = vis_row_start, vc1 = vis_col_start;
        int vr2 = cur_row, vc2 = cur_col;
        if (vr1 > vr2 || (vr1 == vr2 && vc1 > vc2)) {
            int tmp = vr1; vr1 = vr2; vr2 = tmp;
            tmp = vc1; vc1 = vc2; vc2 = tmp;
        }
        yank_len = 0;
        if (vis_mode == 1) {
            for (int r = vr2; r >= vr1; r--) {
                int l = lines[r].len;
                if (l > 0) { memcpy(yank_buf + yank_len, lines[r].data, l); yank_len += l; }
                if (r > vr1 && yank_len < 4095) yank_buf[yank_len++] = '\n';
                line_delete_line(r);
            }
        } else {
            if (vr1 == vr2) {
                int l = vc2 - vc1;
                if (l > 0) {
                    memcpy(yank_buf, lines[vr1].data + vc1, l);
                    yank_len = l;
                    memmove(lines[vr1].data + vc1, lines[vr1].data + vc2, lines[vr1].len - vc2 + 1);
                    lines[vr1].len -= l;
                }
            } else {
                int l1 = lines[vr1].len - vc1;
                if (l1 > 0) { memcpy(yank_buf, lines[vr1].data + vc1, l1); yank_len = l1; }
                lines[vr1].len = vc1;
                lines[vr1].data[vc1] = 0;
                for (int r = vr1 + 1; r < vr2; r++) {
                    int l = lines[r].len;
                    if (yank_len + l <= 4095) { memcpy(yank_buf + yank_len, lines[r].data, l); yank_len += l; }
                    if (yank_len < 4095) yank_buf[yank_len++] = '\n';
                }
                int l2 = vc2;
                if (l2 > 0 && yank_len + l2 <= 4095) { memcpy(yank_buf + yank_len, lines[vr2].data, l2); yank_len += l2; }
                if (yank_len < 4095) yank_buf[yank_len++] = '\n';
                memmove(lines[vr2].data, lines[vr2].data + vc2, lines[vr2].len - vc2 + 1);
                lines[vr2].len -= vc2;
                for (int r = vr1 + 1; r <= vr2; r++) line_delete_line(vr1 + 1);
            }
        }
        if (line_count == 0) {
            memset(&lines[0], 0, sizeof(struct line));
            line_grow(&lines[0], 128);
            lines[0].data[0] = 0;
            line_count = 1;
        }
        int r = vr1;
        if (r >= line_count) r = line_count - 1;
        move_to(r, vc1);
        vim_mode = 1;
        return;
    }
    if (ch == 'y') {
        int vr1 = vis_row_start, vc1 = vis_col_start;
        int vr2 = cur_row, vc2 = cur_col;
        if (vr1 > vr2 || (vr1 == vr2 && vc1 > vc2)) {
            int tmp = vr1; vr1 = vr2; vr2 = tmp;
            tmp = vc1; vc1 = vc2; vc2 = tmp;
        }
        yank_len = 0;
        if (vis_mode == 1) {
            for (int r = vr1; r <= vr2; r++) {
                int l = lines[r].len;
                if (yank_len + l > 4095) l = 4095 - yank_len;
                if (l > 0) { memcpy(yank_buf + yank_len, lines[r].data, l); yank_len += l; }
                if (r < vr2 && yank_len < 4095) yank_buf[yank_len++] = '\n';
            }
        } else {
            if (vr1 == vr2) {
                int l = vc2 - vc1;
                if (l > 0) { memcpy(yank_buf, lines[vr1].data + vc1, l); yank_len = l; }
            } else {
                int l1 = lines[vr1].len - vc1;
                if (l1 > 0) { memcpy(yank_buf, lines[vr1].data + vc1, l1); yank_len = l1; }
                if (yank_len < 4095) yank_buf[yank_len++] = '\n';
                for (int r = vr1 + 1; r < vr2; r++) {
                    int l = lines[r].len;
                    if (yank_len + l > 4095) l = 4095 - yank_len;
                    if (l > 0) { memcpy(yank_buf + yank_len, lines[r].data, l); yank_len += l; }
                    if (yank_len < 4095) yank_buf[yank_len++] = '\n';
                }
                int l2 = vc2;
                if (l2 > 0 && yank_len + l2 <= 4095) { memcpy(yank_buf + yank_len, lines[vr2].data, l2); yank_len += l2; }
            }
        }
        vim_mode = 1;
        sprintf(msg_buf, "Yanked %d chars", yank_len); msg_timer = 50;
        return;
    }
}

static void vim_loop(void) {
    vim_mode = 1;
    running = 1;
    while (running) {
        draw_screen();
        fb_move_cursor(cur_row - top_row, cur_col);
        if (msg_timer > 0) msg_timer--;

        int ch = keyboard_getchar();

        if (vim_mode == 1) vim_normal(ch);
        else if (vim_mode == 2) vim_insert(ch);
        else if (vim_mode == 4) vim_visual(ch);
    }
}

static int editor_open_common(const char *path, int is_vim) {
    if (!path || !*path) return -1;
    scr_w = fb_get_cols();
    scr_h = fb_get_rows();
    int i;
    for (i = 0; path[i] && i < 255; i++) filename[i] = path[i];
    filename[i] = 0;
    line_count = 0;
    int r = load_file(path);
    if (r < 0) {
        memset(&lines[0], 0, sizeof(struct line));
        line_grow(&lines[0], 128);
        lines[0].data[0] = 0;
        line_count = 1;
    }
    cur_row = 0;
    cur_col = 0;
    top_row = 0;
    msg_buf[0] = 0;
    msg_timer = 0;
    yank_len = 0;
    fb_clear();
    if (is_vim) {
        set_msg("Vim: Esc=normal i=insert :w=save :q=quit hjkl=move");
        vim_loop();
    } else {
        set_msg("Nano: Ctrl+Q=quit Ctrl+W=search Ctrl+O=save");
        nano_loop();
    }
    fb_clear();
    fb_move_cursor(0, 0);
    editor_cleanup();
    return 0;
}

int editor_nano(const char *path) {
    return editor_open_common(path, 0);
}

int editor_vi(const char *path) {
    return editor_open_common(path, 1);
}

int editor_vim(const char *path) {
    return editor_open_common(path, 1);
}

int editor_nvim(const char *path) {
    return editor_open_common(path, 1);
}
