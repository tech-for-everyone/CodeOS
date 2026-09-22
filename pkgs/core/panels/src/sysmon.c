#include "sysmon.h"
#include "desktop.h"
#include "windows.h"
#include "sched.h"
#include "process.h"
#include "pmm.h"
#include "mm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
#include "mouse.h"
#include "keyboard.h"
#include "net.h"
#include "net.h"

#define SM_WIN_W  640
#define SM_WIN_H  460
#define SM_TAB_H  28
#define SM_ROW_H  20

#define C_SM_BG     C_CRUST
#define C_SM_CARD   C_SURFACE0
#define C_SM_CARD2  C_MANTLE
#define C_SM_BAR    C_SURFACE1
#define C_SM_BARF   C_BLUE
#define C_SM_BARF2  C_SKY
#define C_SM_BARF3  C_GREEN
#define C_SM_BARF4  C_PEACH
#define C_SM_TEXT   C_TEXT
#define C_SM_SUB    C_SUBTEXT0
#define C_SM_DIM    C_OVERLAY0
#define C_SM_GREEN  C_GREEN
#define C_SM_RED    C_RED
#define C_SM_BLUE   C_SKY
#define C_SM_PURPLE C_MAUVE
#define C_SM_CYAN   C_TEAL
#define C_SM_YELLOW C_YELLOW
#define C_SM_BORDER  0x2264D2FF

#define SM_TAB_OVERVIEW  0
#define SM_TAB_PROCESSES 1
#define SM_TAB_MEMORY    2
#define SM_TAB_COUNT     3

static int sm_open, sm_win;
static int sm_tab;
static int sm_scroll;
static int sm_cpu_hist[60];
static int sm_cpu_hist_idx;
static uint64_t sm_last_tick;
static uint64_t sm_uptime_base;

static const char *tab_names[SM_TAB_COUNT] = {"Overview", "Processes", "Memory"};

static void sm_update_cpu(void) {
    uint64_t now = timer_get_milliseconds();
    if (now - sm_last_tick < 500) return;

    int threads = sched_thread_count();

    int cpu_pct = 0;
    if (threads > 1) cpu_pct = (threads - 1) * 8;
    if (cpu_pct > 100) cpu_pct = 100;
    if (cpu_pct < 5 && threads > 1) cpu_pct = 5 + (threads * 2);
    if (cpu_pct > 100) cpu_pct = 100;

    sm_cpu_hist[sm_cpu_hist_idx] = cpu_pct;
    sm_cpu_hist_idx = (sm_cpu_hist_idx + 1) % 60;
    sm_last_tick = now;
}

static void sm_draw_bar(int x, int y, int w, int h, int pct, uint32_t fill_col) {
    ui_draw_progress(x, y, w, h, pct, C_SM_BAR, fill_col);
}

static void sm_draw_mini_graph(int x, int y, int w, int h, int *data, int count, int idx, uint32_t col) {
    fb_fill_rounded_rect(x, y, w, h, 4, C_SM_BAR);
    int bar_w = w / count;
    if (bar_w < 1) bar_w = 1;
    for (int i = 0; i < count; i++) {
        int di = (idx + i) % count;
        int val = data[di];
        int bh = (val * (h - 4)) / 100;
        if (bh < 1 && val > 0) bh = 1;
        if (bh > h - 4) bh = h - 4;
        if (bh > 0) {
            int bx = x + i * bar_w;
            int bw = bar_w - 1;
            if (bw < 1) bw = 1;
            fb_fill_rounded_rect(bx, y + h - 2 - bh, bw, bh, 1, col);
        }
    }
}

static void sm_draw_overview(int cx, int cy, int cw, int ch) {
    sm_update_cpu();

    int total_mem = (int)(mm_total() / 1024);
    int used_mem = (int)(mm_used() / 1024);
    int free_mem = total_mem - used_mem;
    int mem_pct = total_mem > 0 ? (used_mem * 100 / total_mem) : 0;

    size_t heap_used = mm_heap_used();
    size_t heap_total = mm_heap_total();
    int heap_pct = heap_total > 0 ? (int)(heap_used * 100 / heap_total) : 0;

    int threads = sched_thread_count();
    int procs = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid > 0 && proc_table[i].state != PROC_DEAD)
            procs++;
    }

    uint64_t uptime_ms = timer_get_milliseconds() - sm_uptime_base;
    int uptime_s = (int)(uptime_ms / 1000);
    int up_h = uptime_s / 3600;
    int up_m = (uptime_s % 3600) / 60;
    int up_sec = uptime_s % 60;

    int card_h = 90;
    int gap = 8;
    int card_w = (cw - gap * 3) / 2;
    int row1_y = cy + 4;
    int row2_y = row1_y + card_h + gap;
    int row3_y = row2_y + card_h + gap;

    fb_draw_shadow_layered(cx + gap - 3, row1_y - 3, card_w + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap, row1_y, card_w, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap, row1_y, card_w, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap + 10, row1_y + 6, "CPU Usage", C_SM_PURPLE, C_SM_CARD);
    {
        char buf[16];
        int pct = sm_cpu_hist[(sm_cpu_hist_idx + 59) % 60];
        buf[0] = '0' + pct / 100; buf[1] = '0' + (pct / 10) % 10; buf[2] = '0' + pct % 10;
        buf[3] = '%'; buf[4] = 0;
        if (pct < 100) { buf[0] = buf[1]; buf[1] = buf[2]; buf[2] = buf[3]; buf[3] = buf[4]; buf[4] = 0; }
        if (pct < 10)  { buf[0] = buf[1]; buf[1] = buf[2]; buf[2] = buf[3]; buf[3] = buf[4]; buf[4] = 0; }
        fb_drawstr_px(cx + gap + card_w - 10 - strlen(buf) * 8, row1_y + 6, buf, C_SM_TEXT, C_SM_CARD);
    }
    sm_draw_mini_graph(cx + gap + 10, row1_y + 28, card_w - 20, 50,
                       sm_cpu_hist, 60, sm_cpu_hist_idx, C_SM_PURPLE);

    fb_draw_shadow_layered(cx + gap * 2 + card_w - 3, row1_y - 3, card_w + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap * 2 + card_w, row1_y, card_w, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap * 2 + card_w, row1_y, card_w, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap * 2 + card_w + 10, row1_y + 6, "Memory", C_SM_BLUE, C_SM_CARD);
    {
        char buf[32];
        int ml = 0;
        int mv = used_mem;
        if (mv == 0) buf[ml++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; }
            while (ti > 0) buf[ml++] = tmp[--ti];
        }
        buf[ml++] = 'M'; buf[ml++] = 'B';
        buf[ml++] = ' ';
        buf[ml++] = '/';
        buf[ml++] = ' ';
        mv = total_mem;
        if (mv == 0) buf[ml++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; }
            while (ti > 0) buf[ml++] = tmp[--ti];
        }
        buf[ml++] = 'M'; buf[ml++] = 'B'; buf[ml] = 0;
        fb_drawstr_px(cx + gap * 2 + card_w + 10, row1_y + 6 + 18, buf, C_SM_TEXT, C_SM_CARD);
    }
    sm_draw_bar(cx + gap * 2 + card_w + 10, row1_y + card_h - 20, card_w - 20, 10, mem_pct, C_SM_BLUE);

    fb_draw_shadow_layered(cx + gap - 3, row2_y - 3, card_w + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap, row2_y, card_w, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap, row2_y, card_w, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap + 10, row2_y + 6, "Heap", C_SM_GREEN, C_SM_CARD);
    {
        char buf[32];
        int ml = 0;
        int hv = (int)(heap_used / 1024);
        if (hv == 0) buf[ml++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (hv > 0) { tmp[ti++] = '0' + hv % 10; hv /= 10; }
            while (ti > 0) buf[ml++] = tmp[--ti];
        }
        buf[ml++] = 'K'; buf[ml++] = 'B';
        buf[ml++] = ' '; buf[ml++] = '/'; buf[ml++] = ' ';
        hv = (int)(heap_total / 1024);
        if (hv == 0) buf[ml++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (hv > 0) { tmp[ti++] = '0' + hv % 10; hv /= 10; }
            while (ti > 0) buf[ml++] = tmp[--ti];
        }
        buf[ml++] = 'K'; buf[ml++] = 'B'; buf[ml] = 0;
        fb_drawstr_px(cx + gap + 10, row2_y + 6 + 18, buf, C_SM_TEXT, C_SM_CARD);
    }
    sm_draw_bar(cx + gap + 10, row2_y + card_h - 20, card_w - 20, 10, heap_pct, C_SM_GREEN);

    fb_draw_shadow_layered(cx + gap * 2 + card_w - 3, row2_y - 3, card_w + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap * 2 + card_w, row2_y, card_w, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap * 2 + card_w, row2_y, card_w, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap * 2 + card_w + 10, row2_y + 6, "Uptime", C_SM_CYAN, C_SM_CARD);
    {
        char buf[24];
        int ml = 0;
        buf[ml++] = '0' + up_h / 10; buf[ml++] = '0' + up_h % 10;
        buf[ml++] = ':'; buf[ml++] = '0' + up_m / 10; buf[ml++] = '0' + up_m % 10;
        buf[ml++] = ':'; buf[ml++] = '0' + up_sec / 10; buf[ml++] = '0' + up_sec % 10;
        buf[ml] = 0;
        fb_drawstr_px(cx + gap * 2 + card_w + 10, row2_y + 6 + 18, buf, C_SM_TEXT, C_SM_CARD);
    }
    {
        char buf2[16];
        int ml = 0;
        if (up_h == 0) buf2[ml++] = '0';
        else {
            char tmp[8]; int ti = 0;
            int v = up_h;
            while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; }
            while (ti > 0) buf2[ml++] = tmp[--ti];
        }
        buf2[ml++] = 'h';
        if (up_m == 0) buf2[ml++] = '0';
        else {
            char tmp[8]; int ti = 0;
            int v = up_m;
            while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; }
            while (ti > 0) buf2[ml++] = tmp[--ti];
        }
        buf2[ml++] = 'm'; buf2[ml] = 0;
        fb_drawstr_px(cx + gap * 2 + card_w + 10, row2_y + 6 + 36, buf2, C_SM_DIM, C_SM_CARD);
    }

    int row3_h = ch - (row3_y - cy) - 4;
    if (row3_h > 60) {
        fb_draw_shadow_layered(cx + gap - 3, row3_y - 3, cw - gap * 2 + 6, row3_h + 6, 8, 30, 4, 3);
        ui_draw_card(cx + gap, row3_y, cw - gap * 2, row3_h, C_SM_CARD);
        fb_draw_rounded_rect(cx + gap, row3_y, cw - gap * 2, row3_h, 8, C_SM_BORDER);
        fb_drawstr_px(cx + gap + 10, row3_y + 6, "System", C_SM_YELLOW, C_SM_CARD);

        int lx = cx + gap + 10;
        int ly = row3_y + 28;
        fb_drawstr_px(lx, ly, "Threads:", C_SM_SUB, C_SM_CARD);
        {
            char buf[8]; int ml = 0;
            if (threads == 0) buf[ml++] = '0';
            else {
                char tmp[8]; int ti = 0;
                int v = threads;
                while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; }
                while (ti > 0) buf[ml++] = tmp[--ti];
            }
            buf[ml] = 0;
            fb_drawstr_px(lx + 8 * 9, ly, buf, C_SM_TEXT, C_SM_CARD);
        }
        ly += SM_ROW_H;
        fb_drawstr_px(lx, ly, "Processes:", C_SM_SUB, C_SM_CARD);
        {
            char buf[8]; int ml = 0;
            if (procs == 0) buf[ml++] = '0';
            else {
                char tmp[8]; int ti = 0;
                int v = procs;
                while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; }
                while (ti > 0) buf[ml++] = tmp[--ti];
            }
            buf[ml] = 0;
            fb_drawstr_px(lx + 8 * 11, ly, buf, C_SM_TEXT, C_SM_CARD);
        }
        ly += SM_ROW_H;
        fb_drawstr_px(lx, ly, "Free RAM:", C_SM_SUB, C_SM_CARD);
        {
            char buf[16]; int ml = 0;
            int fv = free_mem;
            if (fv == 0) buf[ml++] = '0';
            else {
                char tmp[16]; int ti = 0;
                while (fv > 0) { tmp[ti++] = '0' + fv % 10; fv /= 10; }
                while (ti > 0) buf[ml++] = tmp[--ti];
            }
            buf[ml++] = 'M'; buf[ml++] = 'B'; buf[ml] = 0;
            fb_drawstr_px(lx + 8 * 11, ly, buf, C_SM_GREEN, C_SM_CARD);
        }

        int rx = cx + gap + 10 + (cw - gap * 2) / 2;
        int ry = row3_y + 28;
        fb_drawstr_px(rx, ry, "Display:", C_SM_SUB, C_SM_CARD);
        {
            char buf[32]; int ml = 0;
            int vw = (int)fb_getwidth();
            int vh = (int)fb_getheight();
            int v = vw; char tmp[8]; int ti = 0;
            if (v == 0) buf[ml++] = '0';
            else { while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
            buf[ml++] = 'x';
            ti = 0; v = vh;
            if (v == 0) buf[ml++] = '0';
            else { while (v > 0) { tmp[ti++] = '0' + v % 10; v /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
            buf[ml++] = '@'; buf[ml++] = '3'; buf[ml++] = '2'; buf[ml] = 0;
            fb_drawstr_px(rx + 8 * 9, ry, buf, C_SM_TEXT, C_SM_CARD);
        }
        ry += SM_ROW_H;
        fb_drawstr_px(rx, ry, "BPP:", C_SM_SUB, C_SM_CARD);
        {
            char buf[4]; int ml = 0;
            int bpp = fb_get_bpp();
            if (bpp == 0) buf[ml++] = '0';
            else {
                char tmp[4]; int ti = 0;
                while (bpp > 0) { tmp[ti++] = '0' + bpp % 10; bpp /= 10; }
                while (ti > 0) buf[ml++] = tmp[--ti];
            }
            buf[ml] = 0;
            fb_drawstr_px(rx + 8 * 5, ry, buf, C_SM_TEXT, C_SM_CARD);
        }
        ry += SM_ROW_H;
        fb_drawstr_px(rx, ry, "Network:", C_SM_SUB, C_SM_CARD);
        fb_drawstr_px(rx + 8 * 9, ry, net_ready() ? "Online" : "Offline",
                      net_ready() ? C_SM_GREEN : C_SM_RED, C_SM_CARD);
    }
}

static void sm_draw_processes(int cx, int cy, int cw, int ch) {
    int header_h = 22;
    int row_h = 20;
    int max_vis = (ch - header_h - 4) / row_h;
    if (max_vis < 1) return;

    fb_fillrect(cx, cy, cw, header_h, C_SM_CARD);
    fb_drawstr_px(cx + 8, cy + 3, "PID", C_SM_SUB, C_SM_CARD);
    fb_drawstr_px(cx + 60, cy + 3, "Name", C_SM_SUB, C_SM_CARD);
    fb_drawstr_px(cx + 260, cy + 3, "State", C_SM_SUB, C_SM_CARD);
    fb_drawstr_px(cx + 370, cy + 3, "Memory", C_SM_SUB, C_SM_CARD);
    fb_fillrect(cx, cy + header_h - 1, cw, 1, C_SM_CARD2);

    int count = 0;
    int drawn = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        if (proc_table[i].pid <= 0 || proc_table[i].state == PROC_DEAD) continue;
        count++;
        if (count - 1 < sm_scroll) continue;
        if (drawn >= max_vis) break;

        int ry = cy + header_h + drawn * row_h;
        uint32_t bg = (drawn % 2 == 0) ? C_SM_CARD : C_SM_CARD2;
        fb_fillrect(cx, ry, cw, row_h, bg);

        char buf[16];
        int ml = 0;
        int pid = proc_table[i].pid;
        if (pid == 0) buf[ml++] = '0';
        else {
            char tmp[8]; int ti = 0;
            while (pid > 0) { tmp[ti++] = '0' + pid % 10; pid /= 10; }
            while (ti > 0) buf[ml++] = tmp[--ti];
        }
        buf[ml] = 0;
        fb_drawstr_px(cx + 8, ry + 2, buf, C_SM_TEXT, bg);

        fb_drawstr_px(cx + 60, ry + 2, proc_table[i].name, C_SM_PURPLE, bg);

        const char *state_str;
        uint32_t state_col;
        switch (proc_table[i].state) {
            case PROC_RUNNING: state_str = "Running"; state_col = C_SM_GREEN; break;
            case PROC_READY:   state_str = "Ready";   state_col = C_SM_CYAN; break;
            case PROC_SLEEPING: state_str = "Sleeping"; state_col = C_SM_YELLOW; break;
            case PROC_ZOMBIE:  state_str = "Zombie";   state_col = C_SM_RED; break;
            default:           state_str = "Unknown";  state_col = C_SM_DIM; break;
        }
        fb_drawstr_px(cx + 260, ry + 2, state_str, state_col, bg);

        drawn++;
    }

    if (count > max_vis) {
        int sb_x = cx + cw - 10;
        int sb_h = ch - header_h - 4;
        fb_fill_rounded_rect(sb_x, cy + header_h, 6, sb_h, 3, C_SM_BAR);
        int thumb_h = (sb_h * max_vis) / count;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = cy + header_h + (sb_h - thumb_h) * sm_scroll / (count - max_vis + 1);
        fb_fill_rounded_rect(sb_x + 1, thumb_y + 1, 4, thumb_h - 2, 2, C_SM_PURPLE);
    }
}

static void sm_draw_memory(int cx, int cy, int cw, int ch) {
    (void)ch;
    int total_mem = (int)(mm_total() / 1024);
    int used_mem = (int)(mm_used() / 1024);
    int mem_pct = total_mem > 0 ? (used_mem * 100 / total_mem) : 0;

    size_t heap_used = mm_heap_used();
    size_t heap_total = mm_heap_total();
    int heap_pct = heap_total > 0 ? (int)(heap_used * 100 / heap_total) : 0;

    uint64_t total_phys = pmm_total_pages() * PAGE_SIZE;
    uint64_t used_phys = total_phys - pmm_count_free() * PAGE_SIZE;
    int phys_pct = total_phys > 0 ? (int)(used_phys * 100 / total_phys) : 0;

    int y = cy + 8;
    int gap = 8;
    int card_h = 100;

    fb_draw_shadow_layered(cx + gap - 3, y - 3, cw - gap * 2 + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap, y, cw - gap * 2, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap, y, cw - gap * 2, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap + 10, y + 6, "Physical Memory", C_SM_BLUE, C_SM_CARD);
    sm_draw_bar(cx + gap + 10, y + 28, cw - gap * 2 - 20, 14, phys_pct, C_SM_BLUE);
    {
        char buf[32]; int ml = 0;
        int mv = (int)(used_phys / (1024 * 1024));
        if (mv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'M'; buf[ml++] = 'B';
        buf[ml++] = ' '; buf[ml++] = '/'; buf[ml++] = ' ';
        mv = (int)(total_phys / (1024 * 1024));
        if (mv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'M'; buf[ml++] = 'B'; buf[ml] = 0;
        fb_drawstr_px(cx + gap + 10, y + 50, buf, C_SM_TEXT, C_SM_CARD);
    }
    {
        char buf[8]; int ml = 0;
        int p = phys_pct;
        if (p == 0) buf[ml++] = '0';
        else { char tmp[8]; int ti = 0; while (p > 0) { tmp[ti++] = '0' + p % 10; p /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = '%'; buf[ml] = 0;
        fb_drawstr_px(cx + cw - gap - 10 - ml * 8, y + 6, buf, C_SM_BLUE, C_SM_CARD);
    }

    y += card_h + gap;

    fb_draw_shadow_layered(cx + gap - 3, y - 3, cw - gap * 2 + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap, y, cw - gap * 2, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap, y, cw - gap * 2, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap + 10, y + 6, "Virtual Memory", C_SM_PURPLE, C_SM_CARD);
    sm_draw_bar(cx + gap + 10, y + 28, cw - gap * 2 - 20, 14, mem_pct, C_SM_PURPLE);
    {
        char buf[32]; int ml = 0;
        int mv = used_mem;
        if (mv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'M'; buf[ml++] = 'B';
        buf[ml++] = ' '; buf[ml++] = '/'; buf[ml++] = ' ';
        mv = total_mem;
        if (mv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (mv > 0) { tmp[ti++] = '0' + mv % 10; mv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'M'; buf[ml++] = 'B'; buf[ml] = 0;
        fb_drawstr_px(cx + gap + 10, y + 50, buf, C_SM_TEXT, C_SM_CARD);
    }
    {
        char buf[8]; int ml = 0;
        int p = mem_pct;
        if (p == 0) buf[ml++] = '0';
        else { char tmp[8]; int ti = 0; while (p > 0) { tmp[ti++] = '0' + p % 10; p /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = '%'; buf[ml] = 0;
        fb_drawstr_px(cx + cw - gap - 10 - ml * 8, y + 6, buf, C_SM_PURPLE, C_SM_CARD);
    }

    y += card_h + gap;

    fb_draw_shadow_layered(cx + gap - 3, y - 3, cw - gap * 2 + 6, card_h + 6, 8, 30, 4, 3);
    ui_draw_card(cx + gap, y, cw - gap * 2, card_h, C_SM_CARD);
    fb_draw_rounded_rect(cx + gap, y, cw - gap * 2, card_h, 8, C_SM_BORDER);
    fb_drawstr_px(cx + gap + 10, y + 6, "Heap", C_SM_GREEN, C_SM_CARD);
    sm_draw_bar(cx + gap + 10, y + 28, cw - gap * 2 - 20, 14, heap_pct, C_SM_GREEN);
    {
        char buf[32]; int ml = 0;
        int hv = (int)(heap_used / 1024);
        if (hv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (hv > 0) { tmp[ti++] = '0' + hv % 10; hv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'K'; buf[ml++] = 'B';
        buf[ml++] = ' '; buf[ml++] = '/'; buf[ml++] = ' ';
        hv = (int)(heap_total / 1024);
        if (hv == 0) buf[ml++] = '0';
        else { char tmp[16]; int ti = 0; while (hv > 0) { tmp[ti++] = '0' + hv % 10; hv /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = 'K'; buf[ml++] = 'B'; buf[ml] = 0;
        fb_drawstr_px(cx + gap + 10, y + 50, buf, C_SM_TEXT, C_SM_CARD);
    }
    {
        char buf[8]; int ml = 0;
        int p = heap_pct;
        if (p == 0) buf[ml++] = '0';
        else { char tmp[8]; int ti = 0; while (p > 0) { tmp[ti++] = '0' + p % 10; p /= 10; } while (ti > 0) buf[ml++] = tmp[--ti]; }
        buf[ml++] = '%'; buf[ml] = 0;
        fb_drawstr_px(cx + cw - gap - 10 - ml * 8, y + 6, buf, C_SM_GREEN, C_SM_CARD);
    }
}

void sysmon_open(void) {
    if (sm_open) return;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    sm_win = desktop_new_window((sw - SM_WIN_W) / 2, (sh - SM_WIN_H) / 3,
                                 SM_WIN_W, SM_WIN_H, "System Monitor", 0xFFFFFFFF, C_SM_BG);
    if (sm_win < 0) return;
    sm_open = 1;
    sm_tab = SM_TAB_OVERVIEW;
    sm_scroll = 0;
    sm_cpu_hist_idx = 0;
    sm_last_tick = 0;
    sm_uptime_base = timer_get_milliseconds();
    for (int i = 0; i < 60; i++) sm_cpu_hist[i] = 0;
    desktop_redraw();
}

int sysmon_is_open(void) { return sm_open; }
void sysmon_close(void) { sm_open = 0; }

void sysmon_draw(void) {
    if (!sm_open) return;
    window_t *w = desktop_get_window(sm_win);
    if (!w || !w->visible) return;
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    fb_fillrect(cx, cy, cw, ch, C_SM_BG);

    int tab_w = cw / SM_TAB_COUNT;
    for (int i = 0; i < SM_TAB_COUNT; i++) {
        int tx = cx + i * tab_w;
        uint32_t tc = (i == sm_tab) ? C_SM_PURPLE : C_SM_DIM;
        fb_drawstr_px(tx + (tab_w - strlen(tab_names[i]) * 8) / 2, cy + 6, tab_names[i], tc, C_SM_BG);
        if (i == sm_tab) {
            int tw = strlen(tab_names[i]) * 8;
            fb_fill_rounded_rect(tx + (tab_w - tw) / 2, cy + SM_TAB_H - 4, tw, 2, 1, C_SM_PURPLE);
        }
    }
    fb_fillrect(cx, cy + SM_TAB_H, cw, 1, C_SM_CARD2);

    int content_y = cy + SM_TAB_H + 4;
    int content_h = ch - SM_TAB_H - 8;

    switch (sm_tab) {
        case SM_TAB_OVERVIEW:  sm_draw_overview(cx, content_y, cw, content_h); break;
        case SM_TAB_PROCESSES: sm_draw_processes(cx, content_y, cw, content_h); break;
        case SM_TAB_MEMORY:    sm_draw_memory(cx, content_y, cw, content_h); break;
    }
}

int sysmon_click(int mx, int my) {
    if (!sm_open) return 0;
    window_t *w = desktop_get_window(sm_win);
    if (!w || !w->visible) return 0;
    desktop_set_focused(sm_win);
    int cx = w->x + 4, cy = w->y + 22, cw = w->w - 8, ch = w->h - 26;
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return 0;

    if (my < cy + SM_TAB_H) {
        int tab_w = cw / SM_TAB_COUNT;
        int tab = (mx - cx) / tab_w;
        if (tab >= 0 && tab < SM_TAB_COUNT) {
            sm_tab = tab;
            sm_scroll = 0;
            desktop_redraw();
        }
        return 1;
    }
    return 1;
}

void sysmon_key(int key) {
    if (!sm_open) return;
    window_t *w = desktop_get_window(sm_win);
    if (!w || !w->visible) return;
    if (desktop_focused_window() != sm_win) return;

    switch (key) {
        case 'w': case 'W':
            sysmon_close(); desktop_redraw(); return;
        case KEY_UP: case 'k':
            if (sm_scroll > 0) { sm_scroll--; desktop_redraw(); } break;
        case KEY_DOWN: case 'j':
            sm_scroll++; desktop_redraw(); break;
        case '1': sm_tab = SM_TAB_OVERVIEW;  sm_scroll = 0; desktop_redraw(); break;
        case '2': sm_tab = SM_TAB_PROCESSES; sm_scroll = 0; desktop_redraw(); break;
        case '3': sm_tab = SM_TAB_MEMORY;    sm_scroll = 0; desktop_redraw(); break;
    }
}
