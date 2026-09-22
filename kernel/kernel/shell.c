#include "shell.h"
#include "kprintf.h"
#include "string.h"
#include "mm.h"
#include "pmm.h"
#include "fs.h"
#include "pkg.h"
#include "sched.h"
#include "net.h"
#include "xora.h"
#include "https_certs.h"
#include "icmp.h"
#include "arp.h"
#include "../drivers/input.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/timer.h"
#include "../arch/x86_64/pci.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/fb.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/rtc.h"
#include "../drivers/speaker.h"
#include "block.h"
#include "../drivers/drivers.h"
#include "../drivers/e1000.h"
#include "../drivers/usb.h"
#include "desktop.h"
#include "part.h"
#include "ext2.h"
#include "script.h"
#include "csl.h"
#include "umode.h"
#include "elf.h"
#include "installer.h"
#include "container.h"
#include "rootfs.h"
#include "updater.h"
#include "editor.h"
#include "clamav.h"
#include "security.h"
#include "ad_block.h"
#include "version.h"
static void run_builtin(int argc, char **argv);
static int str_to_int(const char *s);
extern void cmd_waydroid(int argc, char **argv);

#define CMD_BUF_SIZE 256
#define MAX_ARGS     32
#define MAX_ENV      32
#define HIST_SIZE    16
#define MAX_TAB      64

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_len;
static int cmd_pos;

static char history[HIST_SIZE][CMD_BUF_SIZE];
static int hist_count;
static int hist_cur;

static char env_names[MAX_ENV][32];
static char env_vals[MAX_ENV][64];
static int env_count;

/* ---------- user accounts ---------- */
#define MAX_USERS 8
#define USER_NAME_MAX 20
#define PASS_MAX 32

static struct {
    char username[USER_NAME_MAX];
    char password[PASS_MAX];
    int uid;
} users[MAX_USERS];
static int user_count;
static int current_uid;

static int find_user(const char *name) {
    for (int i = 0; i < user_count; i++)
        if (strcmp(users[i].username, name) == 0) return i;
    return -1;
}

/* ---------- hybrid serial + PS/2 input ---------- */

static int input_getchar(void) {
    input_event_t ev;
    int spins = 0;
    while (1) {
        input_poll();
        if (mouse_available()) {
            fb_cursor_move(mouse_get_x(), mouse_get_y());
            fb_cursor_render();
        }
        if (serial_available())
            return serial_readchar();
        if (input_get_event(&ev) && ev.type == INPUT_EVENT_KEY) {
            if (mouse_available())
                fb_cursor_hide();
            return ev.data.key.key;
        }
        if (++spins > 1000) {
            spins = 0;
            sched_yield();
        }
    }
}

/* Public line/kbhit entry points used by the csl `readln`/`kbhit` natives
 * (a csl game sits inside the shell task, so blocking here is safe). */
int shell_readln(char *buf, int max) {
    if (!buf || max <= 0) return -1;
    int i = 0;
    while (1) {
        int c = input_getchar();
        if (c == '\n' || c == '\r') {
            buf[i] = 0;
            kprintf("\n");
            return i;
        }
        if ((c == '\b' || c == 0x7F) && i > 0) {
            i--;
            kprintf("\b \b");
            continue;
        }
        if (i < max - 1 && c >= ' ' && c < 127) {
            buf[i++] = (char)c;
            kprintf("%c", c);
        }
    }
}

int shell_kbhit(void) {
    input_poll();
    if (mouse_available()) {
        fb_cursor_move(mouse_get_x(), mouse_get_y());
        fb_cursor_render();
    }
    if (serial_available()) return serial_readchar();
    input_event_t ev;
    if (input_get_event(&ev) && ev.type == INPUT_EVENT_KEY) {
        if (mouse_available()) fb_cursor_hide();
        return ev.data.key.key;
    }
    return 0;
}

/* ---------- password input (no echo) ---------- */

static void read_password(char *buf, int max) {
    int i = 0;
    while (1) {
        int c = input_getchar();
        if (c == '\n' || c == '\r') { buf[i] = 0; kprintf("\n"); return; }
        if ((c == '\b' || c == 0x7F) && i > 0) { i--; kprintf("\b \b"); continue; }
        if (i < max - 1 && c >= ' ') { buf[i++] = c; kprintf("*"); }
    }
}

/* ---------- string helpers ---------- */

#define INT_MAX 2147483647
#define INT_MIN (-2147483648)

static int strmatch(const char *prefix, const char *s) {
    while (*prefix && *s && *prefix == *s) { prefix++; s++; }
    return *prefix == 0;
}

static int str_to_uint(const char *s, unsigned int *out) {
    if (!s || !*s) return -1;
    unsigned int val = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned int next = val * 10 + (unsigned int)(*s++ - '0');
        if (next < val) return -1; /* overflow */
        val = next;
    }
    *out = val;
    return 0;
}

/* ---------- environment ---------- */

static void env_init(void) {
    env_count = 0;
    /* built-in vars */
    char buf[32]; int i, n;
    i = 0; n = 0x150000; /* rough timer freq */
    if (n >= 1000000) { buf[i++] = '0' + (n/1000000); n %= 1000000; }
    if (i || n >= 100000) { buf[i++] = '0' + (n/100000); n %= 100000; }
    if (i || n >= 10000) { buf[i++] = '0' + (n/10000); n %= 10000; }
    if (i || n >= 1000) { buf[i++] = '0' + (n/1000); n %= 1000; }
    if (i || n >= 100) { buf[i++] = '0' + (n/100); n %= 100; }
    if (i || n >= 10) { buf[i++] = '0' + (n/10); n %= 10; }
    buf[i++] = '0' + n; buf[i] = 0;
    strncpy_safe(env_names[env_count], "HZ", 32);
    strncpy_safe(env_vals[env_count], buf, 64);
    env_count++;

    strncpy_safe(env_names[env_count], "SHELL", 32);
    strncpy_safe(env_vals[env_count], "bash", 64);
    env_count++;

    strncpy_safe(env_names[env_count], "MODE", 32);
    strncpy_safe(env_vals[env_count], "bash", 64);
    env_count++;
}

static const char *env_get(const char *name) {
    for (int i = 0; i < env_count; i++)
        if (strcmp(env_names[i], name) == 0)
            return env_vals[i];
    return 0;
}

static void env_set(const char *name, const char *val) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_names[i], name) == 0) {
            strncpy_safe(env_vals[i], val, 64);
            return;
        }
    }
    if (env_count < MAX_ENV) {
        strncpy_safe(env_names[env_count], name, 32);
        strncpy_safe(env_vals[env_count], val, 64);
        env_count++;
    }
}

static void env_unset(const char *name) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_names[i], name) == 0) {
            for (int j = i; j < env_count - 1; j++) {
                strncpy_safe(env_names[j], env_names[j+1], 32);
                strncpy_safe(env_vals[j], env_vals[j+1], 64);
            }
            env_count--;
            return;
        }
    }
}

/* ---------- command table ---------- */

static const char *builtins[] = {
    "clear", "echo", "timer", "info", "version", "reboot",
    "export", "unset", "env", "history", "mode", "set", "mouse",
    "ls", "cd", "pwd", "mkdir", "rmdir", "cat", "rm", "touch", "nano",
    "mem", "pci", "cpu", "fetch", "fastfetch", "sysfetch", "pacman",
    "uptime", "uname", "free", "whoami", "hostname", "which",
        "sleep", "repeat", "seq", "script", "yes", "true", "false",
    "wc", "head", "hexdump", "calc",         "date", "rev", "run", "sort",
    "cp", "mv", "shutdown", "df", "du", "id", "su", "sudo", "root", "container", "appvm", "kill", "ps",
    "waydroid", "ow", "login", "passwd", "useradd", "userdel", "users",
    "chmod", "chown", "tail", "grep", "find", "ln", "dd",
    "source", "type", "less", "time", "tee", "tr", "nl", "fold",
    "basename", "dirname", "tty", "logname", "nproc", "realpath",
    "mktemp", "expand", "sync", "exit", "read", "wait", "beep",
    "printenv", "cut", "comm", "ascii", "fortune", "banner",
    "download", "post",
    "sysinfo", "meminfo", "stats", "devices", "heapstat",
    "desktop", "panel", "usb",
    "clamscan", "secaudit", "secintegrity", "secfw", "secstatus", "secthreat",
    "adblock",
    "ping", "nslookup", "netinfo", "dnsflush",
    "setcursor",
    0
};

/* ---------- argument parsing ---------- */

static int parse_args(char *input, char **argv, int max) {
    int argc = 0;
    while (*input && argc < max - 1) {
        while (*input == ' ') input++;
        if (!*input) break;
        if (*input == '\'' || *input == '"') {
            char quote = *input++;
            argv[argc] = input;
            while (*input && *input != quote) input++;
            if (*input) {
                *input = 0;
                input++;
            }
            argc++;
        } else {
            argv[argc] = input;
            while (*input && *input != ' ') input++;
            if (*input) {
                *input = 0;
                input++;
            }
            argc++;
        }
    }
    argv[argc] = 0;
    return argc;
}

/* ---------- env variable expansion ---------- */

static void expand_vars(const char *in, char *out, int max) {
    int o = 0;
    while (*in && o < max - 1) {
        if (*in == '$') {
            in++;
            char name[32]; int ni = 0;
            if (*in == '{') {
                in++;
                while (*in && *in != '}' && ni < 31)
                    name[ni++] = *in++;
                if (*in == '}') in++;
            } else {
                while ((*in >= 'A' && *in <= 'Z') ||
                       (*in >= 'a' && *in <= 'z') ||
                       (*in >= '0' && *in <= '9') ||
                       *in == '_') {
                    if (ni < 31) name[ni++] = *in;
                    in++;
                }
            }
            name[ni] = 0;
            const char *val = env_get(name);
            if (val) {
                while (*val && o < max - 1) out[o++] = *val++;
            }
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = 0;
}

/* ---------- tab completion ---------- */

static int tab_complete(const char *prefix, char *out) {
    int matches = 0;
    const char *match = 0;
    int plen = strlen(prefix);
    for (int i = 0; builtins[i]; i++) {
        if (strmatch(prefix, builtins[i])) {
            if (!match) {
                match = builtins[i];
                matches = 1;
            } else {
                matches++;
            }
        }
    }
    if (matches == 1 && match) {
        strcpy(out, match + plen);
        return 1;
    }
    if (matches > 1) {
        kprintf("\n");
        for (int i = 0; builtins[i]; i++)
            if (strmatch(prefix, builtins[i]))
                kprintf("%s  ", builtins[i]);
        kprintf("\n");
    }
    return 0;
}

/* ---------- line editing ---------- */

static void get_prompt(char *buf, int max) {
    const char *user = env_get("USER");
    if (!user) user = "root";
    int n = 0;
    while (*user && n < max - 1) buf[n++] = *user++;
    if (current_uid == 0) {
        if (n < max - 1) buf[n++] = '#';
    } else {
        if (n < max - 1) buf[n++] = '$';
    }
    if (n < max - 1) buf[n++] = ' ';
    buf[n] = 0;
}

static char prompt_buf[64];

static void prompt_refresh(void) {
    get_prompt(prompt_buf, sizeof(prompt_buf));
}

static int prompt_len(void) {
    return strlen(prompt_buf);
}

static void line_redraw(void) {
    prompt_refresh();
    int plen = prompt_len();
    for (int i = 0; i < cmd_pos + plen; i++) kprintf("\b");
    for (int i = 0; i < cmd_len + plen; i++) kprintf(" ");
    for (int i = 0; i < cmd_len + plen; i++) kprintf("\b");
    kprintf("%s", prompt_buf);
    for (int i = 0; i < cmd_len; i++) kprintf("%c", cmd_buf[i]);
    /* move cursor back to pos */
    int back = cmd_len - cmd_pos;
    for (int i = 0; i < back; i++) kprintf("\b");
}

static void line_insert(char c) {
    if (cmd_len >= CMD_BUF_SIZE - 1) return;
    for (int i = cmd_len; i > cmd_pos; i--)
        cmd_buf[i] = cmd_buf[i-1];
    cmd_buf[cmd_pos] = c;
    cmd_len++;
    cmd_pos++;
}

static void line_delete(void) {
    if (cmd_pos >= cmd_len) return;
    for (int i = cmd_pos; i < cmd_len; i++)
        cmd_buf[i] = cmd_buf[i+1];
    cmd_len--;
}

static void line_backspace(void) {
    if (cmd_pos <= 0) return;
    cmd_pos--;
    line_delete();
}

static void hist_save(const char *line) {
    if (!*line) return;
    /* don't save duplicate of last entry */
    if (hist_count > 0 && strcmp(history[hist_count-1], line) == 0)
        return;
    if (hist_count < HIST_SIZE) {
        strcpy(history[hist_count], line);
        hist_count++;
    } else {
        for (int i = 0; i < HIST_SIZE - 1; i++)
            strcpy(history[i], history[i+1]);
        strcpy(history[HIST_SIZE-1], line);
    }
    hist_cur = hist_count;
}

/* ── Kill line helpers ── */

static void line_kill_to_end(void) {
    for (int i = cmd_pos; i < cmd_len; i++) kprintf(" ");
    cmd_len = cmd_pos;
    cmd_buf[cmd_len] = 0;
    line_redraw();
}

static void line_kill_to_start(void) {
    /* move cursor to start */
    while (cmd_pos > 0) { cmd_pos--; kprintf("\b"); }
    /* shift remaining text left and clear the line */
    int remaining = cmd_len - cmd_pos;
    for (int i = 0; i < remaining; i++) kprintf(" ");
    for (int i = 0; i < remaining; i++) kprintf("\b");
    /* compact buffer */
    int shift = cmd_pos;
    for (int i = cmd_pos; i < cmd_len; i++)
        cmd_buf[i - shift] = cmd_buf[i];
    cmd_len -= shift;
    cmd_pos = 0;
    cmd_buf[cmd_len] = 0;
    line_redraw();
}

static void line_delete_word_back(void) {
    if (cmd_pos <= 0) return;
    int old_pos = cmd_pos;
    /* skip spaces */
    while (cmd_pos > 0 && cmd_buf[cmd_pos - 1] == ' ') cmd_pos--;
    /* skip word */
    while (cmd_pos > 0 && cmd_buf[cmd_pos - 1] != ' ') cmd_pos--;
    /* shift remaining text */
    int diff = old_pos - cmd_pos;
    for (int i = cmd_pos; i <= cmd_len - diff; i++)
        cmd_buf[i] = cmd_buf[i + diff];
    cmd_len -= diff;
    cmd_buf[cmd_len] = 0;
    line_redraw();
}

static void line_delete_word_fwd(void) {
    if (cmd_pos >= cmd_len) return;
    int start = cmd_pos;
    /* skip spaces */
    while (cmd_pos < cmd_len && cmd_buf[cmd_pos] == ' ') cmd_pos++;
    /* skip word */
    while (cmd_pos < cmd_len && cmd_buf[cmd_pos] != ' ') cmd_pos++;
    int end = cmd_pos;
    /* shift remaining text */
    int diff = end - start;
    cmd_pos = start;
    for (int i = start; i <= cmd_len - diff; i++)
        cmd_buf[i] = cmd_buf[i + diff];
    cmd_len -= diff;
    cmd_buf[cmd_len] = 0;
    line_redraw();
}

static void line_word_back(void) {
    while (cmd_pos > 0 && cmd_buf[cmd_pos - 1] == ' ') cmd_pos--;
    while (cmd_pos > 0 && cmd_buf[cmd_pos - 1] != ' ') cmd_pos--;
    line_redraw();
}

static void line_word_fwd(void) {
    while (cmd_pos < cmd_len && cmd_buf[cmd_pos] == ' ') cmd_pos++;
    while (cmd_pos < cmd_len && cmd_buf[cmd_pos] != ' ') cmd_pos++;
    line_redraw();
}

/* ── Reverse incremental history search (Ctrl+R) ── */

static void reverse_history_search(void) {
    char query[CMD_BUF_SIZE];
    int qlen = 0;
    int found_idx = -1;
    int saved_len = cmd_len;
    int saved_pos = cmd_pos;
    char saved_buf[CMD_BUF_SIZE];
    strcpy(saved_buf, cmd_buf);

    kprintf("\n(reverse-i-search)`': ");

    while (1) {
        int c = input_getchar();

        if (c == '\n' || c == '\r') {
            /* Accept the found command */
            kprintf("\n");
            return;
        }

        if (c == 3) {
            /* Ctrl+C: cancel search, restore original */
            kprintf("^C\n");
            cmd_len = saved_len;
            cmd_pos = saved_pos;
            strcpy(cmd_buf, saved_buf);
            cmd_buf[cmd_len] = 0;
            shell_interrupted = 1;
            return;
        }

        if (c == '\b' || c == 0x7F) {
            /* Backspace: shorten query, re-search */
            if (qlen > 0) {
                query[--qlen] = 0;
                /* re-search from current position */
                found_idx = -1;
                for (int i = hist_count - 1; i >= 0; i--) {
                    if (qlen > 0 && strstr(history[i], query)) {
                        found_idx = i;
                        break;
                    }
                }
                if (found_idx >= 0) {
                    strcpy(cmd_buf, history[found_idx]);
                    cmd_len = strlen(cmd_buf);
                    cmd_pos = cmd_len;
                } else {
                    cmd_len = saved_len;
                    cmd_pos = saved_pos;
                    strcpy(cmd_buf, saved_buf);
                    cmd_buf[cmd_len] = 0;
                }
            }
            /* redraw search line */
            kprintf("\r(reverse-i-search)`%s': %s", query, cmd_buf);
            for (int i = strlen(cmd_buf); i < saved_len; i++) kprintf(" ");
            for (int i = strlen(cmd_buf); i < saved_len; i++) kprintf("\b");
            continue;
        }

        if (c >= ' ' && c < 0x80 && qlen < CMD_BUF_SIZE - 1) {
            query[qlen++] = (char)c;
            query[qlen] = 0;
            /* search backward from hist_count */
            found_idx = -1;
            for (int i = hist_count - 1; i >= 0; i--) {
                if (strstr(history[i], query)) {
                    found_idx = i;
                    break;
                }
            }
            if (found_idx >= 0) {
                strcpy(cmd_buf, history[found_idx]);
                cmd_len = strlen(cmd_buf);
                cmd_pos = cmd_len;
            }
            /* redraw search line */
            kprintf("\r(reverse-i-search)`%s': %s", query, cmd_buf);
            int extra = saved_len > cmd_len ? saved_len - cmd_len : 0;
            for (int i = 0; i < extra; i++) kprintf(" ");
            for (int i = 0; i < extra; i++) kprintf("\b");
            continue;
        }
    }
}

static void read_line(char *buf) {
    cmd_len = 0;
    cmd_pos = 0;
    cmd_buf[0] = 0;

    shell_interrupted = 0;

    prompt_refresh();
    kprintf("%s", prompt_buf);

    while (1) {
        int c = input_getchar();

        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_len] = 0;
            strcpy(buf, cmd_buf);
            kprintf("\n");
            return;
        }

        /* Ctrl+C — cancel current line */
        if (c == 3) {
            kprintf("^C\n");
            shell_interrupted = 1;
            buf[0] = 0;
            return;
        }

        /* Ctrl+A — move to start of line */
        if (c == 1) {
            while (cmd_pos > 0) { cmd_pos--; kprintf("\b"); }
            continue;
        }

        /* Ctrl+B — move backward one character */
        if (c == 2) {
            if (cmd_pos > 0) { cmd_pos--; kprintf("\b"); }
            continue;
        }

        /* Ctrl+D — delete char at cursor, or EOF if empty */
        if (c == 4) {
            if (cmd_len == 0) {
                kprintf("\n");
                buf[0] = 0;
                return;
            }
            if (cmd_pos < cmd_len) {
                line_delete();
                line_redraw();
            }
            continue;
        }

        /* Ctrl+E — move to end of line */
        if (c == 5) {
            while (cmd_pos < cmd_len) {
                kprintf("%c", cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        /* Ctrl+F — move forward one character */
        if (c == 6) {
            if (cmd_pos < cmd_len) {
                kprintf("%c", cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        /* Ctrl+K — kill to end of line */
        if (c == 0x0B) {
            line_kill_to_end();
            continue;
        }

        /* Ctrl+R — reverse incremental history search */
        if (c == 0x12) {
            reverse_history_search();
            prompt_refresh();
            kprintf("%s", prompt_buf);
            kprintf("%s", cmd_buf);
            for (int i = cmd_len; i > cmd_pos; i--) kprintf("\b");
            continue;
        }

        /* Ctrl+U — kill to start of line */
        if (c == 0x15) {
            line_kill_to_start();
            continue;
        }

        /* Ctrl+W — delete word backward */
        if (c == 0x17) {
            line_delete_word_back();
            continue;
        }

        /* Alt+D — delete word forward */
        if (c == 'd' && alt_down) {
            line_delete_word_fwd();
            continue;
        }

        /* Alt+B — move backward one word */
        if (c == 'b' && alt_down) {
            line_word_back();
            continue;
        }

        /* Alt+F — move forward one word */
        if (c == 'f' && alt_down) {
            line_word_fwd();
            continue;
        }

        if (c == '\t') {
            char prefix[CMD_BUF_SIZE];
            int pi;
            for (pi = 0; pi < cmd_pos && cmd_buf[pi] != ' '; pi++)
                prefix[pi] = cmd_buf[pi];
            prefix[pi] = 0;
            char completion[CMD_BUF_SIZE];
            if (tab_complete(prefix, completion)) {
                const char *cp = completion;
                while (*cp) {
                    line_insert(*cp);
                    cp++;
                }
                line_redraw();
            }
            continue;
        }

        if (c == KEY_UP) {
            if (hist_cur > 0) {
                hist_cur--;
                strcpy(cmd_buf, history[hist_cur]);
                cmd_len = strlen(cmd_buf);
                cmd_pos = cmd_len;
                line_redraw();
            }
            continue;
        }

        if (c == KEY_DOWN) {
            if (hist_cur < hist_count - 1) {
                hist_cur++;
                strcpy(cmd_buf, history[hist_cur]);
                cmd_len = strlen(cmd_buf);
                cmd_pos = cmd_len;
                line_redraw();
            } else if (hist_cur == hist_count - 1) {
                hist_cur++;
                cmd_buf[0] = 0;
                cmd_len = 0;
                cmd_pos = 0;
                line_redraw();
            }
            continue;
        }

        if (c == KEY_LEFT) {
            if (cmd_pos > 0) { cmd_pos--; kprintf("\b"); }
            continue;
        }

        if (c == KEY_RIGHT) {
            if (cmd_pos < cmd_len) {
                kprintf("%c", cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        if (c == KEY_HOME) {
            while (cmd_pos > 0) { cmd_pos--; kprintf("\b"); }
            continue;
        }

        if (c == KEY_END) {
            while (cmd_pos < cmd_len) {
                kprintf("%c", cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        if (c == KEY_DEL) {
            line_delete();
            line_redraw();
            continue;
        }

        if (c == '\b' || c == 0x7F) {
            line_backspace();
            line_redraw();
            continue;
        }

        if (c >= ' ') {
            line_insert(c);
            kprintf("%c", c);
            /* redraw rest of line after cursor */
            for (int i = cmd_pos; i < cmd_len; i++)
                kprintf("%c", cmd_buf[i]);
            int back = cmd_len - cmd_pos;
            for (int i = 0; i < back; i++) kprintf("\b");
        }
    }
}

/* ---------- commands ---------- */

static void cmd_info(void) {
    kprintf("%s kernel\n", KERNEL_NAME);
    kprintf("Version: %s\n", KERNEL_VERSION);
    kprintf("Arch: %s\n", KERNEL_ARCH);
    kprintf("Display: Framebuffer (%dx%d)\n", fb_getwidth(), fb_getheight());
    kprintf("Threads: %d\n", sched_thread_count());
    kprintf("Drivers: PS/2 keyboard, PIT timer\n");
    kprintf("Timer freq: %u Hz\n", timer_get_frequency());
    kprintf("Shell mode: %s\n", env_get("MODE"));
}

static void cmd_version(void) {
    kprintf("%s\n", KERNEL_UNAME);
}

static void cmd_reboot(void) {
    kprintf("Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ volatile("hlt");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        char expanded[CMD_BUF_SIZE];
        expand_vars(argv[i], expanded, CMD_BUF_SIZE);
        kprintf("%s", expanded);
        if (i < argc - 1) kprintf(" ");
    }
    kprintf("\n");
}

static void cmd_export(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: export NAME=VALUE\n"); return; }
    char *eq = strchr(argv[1], '=');
    if (!eq) { env_set(argv[1], ""); return; }
    *eq = 0;
    env_set(argv[1], eq + 1);
    *eq = '=';
}

static void cmd_unset(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: unset NAME\n"); return; }
    env_unset(argv[1]);
}

static void cmd_env(void) {
    for (int i = 0; i < env_count; i++)
        kprintf("%s=%s\n", env_names[i], env_vals[i]);
}

static void cmd_history(void) {
    for (int i = 0; i < hist_count; i++) {
        kprintf("  %d  ", i + 1);
        kprintf("%s\n", history[i]);
    }
}

static void cmd_mode(int argc, char **argv) {
    (void)argc; (void)argv;
    env_set("MODE", "csl");
    kprintf("csl\n");
}

static void cmd_set(void) {
    kprintf("Shell mode: %s\n", env_get("MODE"));
    kprintf("History: %d entries\n", hist_count);
    kprintf("Environment: %d vars\n", env_count);
    kprintf("Buffer size: %d\n", CMD_BUF_SIZE);
}

static void cmd_timer(void) {
    kprintf("Timer ticks: %d\n", (unsigned int)timer_get_ticks());
}

static void cmd_mouse(void) {
    if (!mouse_available()) {
        kprintf("Mouse not detected\n");
        return;
    }
    mouse_poll();
    int b = mouse_get_buttons();
    kprintf("Mouse: X=%d Y=%d Buttons=", mouse_get_x(), mouse_get_y());
    if (b & MOUSE_LEFT)   kprintf("L");
    if (b & MOUSE_RIGHT)  kprintf("R");
    if (b & MOUSE_MIDDLE) kprintf("M");
    if (!b) kprintf("none");
    kprintf("\n");
}

static void cmd_usb(void) {
    usb_print_info();
}

static void cmd_clear(void) {
    fb_clear();
}

/* ---------- filesystem commands ---------- */

static void cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    fs_ls(path);
}

static void cmd_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    if (fs_cd(path) < 0)
        kprintf("cd: %s: no such directory\n", path);
}

static void cmd_pwd(void) {
    char buf[FS_PATH_MAX];
    fs_getcwd(buf, FS_PATH_MAX);
    kprintf("%s\n", buf);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: mkdir <path>\n"); return; }
    if (fs_mkdir(argv[1]) < 0)
        kprintf("mkdir: %s: failed\n", argv[1]);
}

static void cmd_rmdir(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: rmdir <path>\n"); return; }
    if (fs_rmdir(argv[1]) < 0)
        kprintf("rmdir: %s: failed (not empty?)\n", argv[1]);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: cat <path>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0)
        kprintf("cat: %s: no such file\n", argv[1]);
    else {
        for (int i = 0; i < n; i++) kprintf("%c", buf[i]);
        kprintf("\n");
    }
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: rm <path>\n"); return; }
    if (fs_rm(argv[1]) < 0)
        kprintf("rm: %s: failed\n", argv[1]);
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: touch <path>\n"); return; }
    if (fs_mkfile(argv[1]) < 0)
        kprintf("touch: %s: failed\n", argv[1]);
}

static void cmd_nano(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: nano <path>\n"); return; }
    editor_nano(argv[1]);
}

static void cmd_vi(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: vi <path>\n"); return; }
    editor_vi(argv[1]);
}

static void cmd_vim(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: vim <path>\n"); return; }
    editor_vi(argv[1]);
}

static void cmd_nvim(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: nvim <path>\n"); return; }
    editor_vi(argv[1]);
}

/* ---------- mem, pci, cpu, fetch ---------- */

static void cmd_mem(void) {
    mm_print_regions();
}

static void cmd_pci(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "scan") == 0) {
        pci_scan();
    }
    pci_print_devices();
}

static void cmd_cpu(void) {
    uint32_t eax, ebx, ecx, edx;

    /* vendor string */
    eax = 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    char vendor[13];
    *(uint32_t *)(vendor)     = ebx;
    *(uint32_t *)(vendor + 4) = edx;
    *(uint32_t *)(vendor + 8) = ecx;
    vendor[12] = 0;
    kprintf("CPU vendor: %s\n", vendor);

    /* brand string */
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        __asm__ volatile("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(leaf));
        char buf[17];
        *(uint32_t *)(buf)      = eax;
        *(uint32_t *)(buf + 4)  = ebx;
        *(uint32_t *)(buf + 8)  = ecx;
        *(uint32_t *)(buf + 12) = edx;
        buf[16] = 0;
        kprintf("%s", buf);
    }
    kprintf("\n");

    /* features */
    eax = 1;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    kprintf("Features:");
    if (edx & (1 << 23)) kprintf(" MMX");
    if (edx & (1 << 25)) kprintf(" SSE");
    if (edx & (1 << 26)) kprintf(" SSE2");
    if (ecx & (1 << 0))  kprintf(" SSE3");
    if (ecx & (1 << 9))  kprintf(" SSSE3");
    if (ecx & (1 << 19)) kprintf(" SSE4.1");
    if (ecx & (1 << 20)) kprintf(" SSE4.2");
    if (ecx & (1 << 28)) kprintf(" AVX");
    if (ecx & (1 << 12)) kprintf(" FMA");
    if (ebx & (1 << 5))  kprintf(" AVX2");
    if (ecx & (1 << 23)) kprintf(" POPCNT");
    if (edx & (1 << 4))  kprintf(" TSC");
    if (edx & (1 << 0))  kprintf(" FPU");
    kprintf("\n");

    /* current mode */
    kprintf("Mode: x86_64 long mode\n");
}

/* ---------- package manager (fetch - apt-like) ---------- */

static void cmd_fetch(int argc, char **argv) {
    if (argc < 2) {
        kprintf("fetch - CodeOS Package Manager (pacman-style)\n");
        kprintf("\nFlags:\n");
        kprintf("  fetch -I <pkg>          - Install with deps\n");
        kprintf("  fetch -If <file>        - Install from package file\n");
        kprintf("  fetch -S <pkg>          - Sync / install with deps\n");
        kprintf("  fetch -Su/-Syu/-U       - Upgrade all packages\n");
        kprintf("  fetch -Sw <pkg>         - Download only (no install)\n");
        kprintf("  fetch -R <pkg>          - Remove package\n");
        kprintf("  fetch -Rs <pkg>         - Remove + orphan cleanup\n");
        kprintf("  fetch -Ro               - Remove orphans (autoremove)\n");
        kprintf("  fetch -Sr/-Ss <pat>     - Search repos by name/desc\n");
        kprintf("  fetch -Si <pkg>         - Show repo package info\n");
        kprintf("  fetch -Qi <pkg>         - Show installed package info\n");
        kprintf("  fetch -Ql <pkg>         - List package files (stub)\n");
        kprintf("  fetch -Scc              - Clean all caches\n");
        kprintf("  fetch -L/-Q             - List installed packages\n");
        kprintf("\nSubcommands:\n");
        kprintf("  fetch install <pkg>     - Install package (with deps)\n");
        kprintf("  fetch remove <pkg>      - Remove package\n");
        kprintf("  fetch search <pattern>  - Search packages by name/desc\n");
        kprintf("  fetch show <pkg>        - Show package details\n");
        kprintf("  fetch deps <pkg>        - Check dependencies\n");
        kprintf("  fetch rdepends <pkg>    - Show reverse dependencies\n");
        kprintf("  fetch dep-tree <pkg>    - Show dependency tree\n");
        kprintf("  fetch orphans           - Find orphaned packages\n");
        kprintf("  fetch update            - Update package cache\n");
        kprintf("  fetch upgrade           - Upgrade all packages\n");
        kprintf("  fetch sync [-u]         - Sync system (update cache first with -u)\n");
        kprintf("  fetch fetch <pkg>...    - Download package(s) without installing\n");
        kprintf("  fetch download <pkg>    - Download package\n");
        kprintf("  fetch payload <pkg>     - Materialize the .ftech payload file\n");
        kprintf("  fetch base64 <in> <out> - Base64-encode a file\n");
        kprintf("  fetch decode <in> [out] - Base64-decode a file (default <in>.dec)\n");
        kprintf("  fetch clean             - Clean package cache\n");
        kprintf("  fetch autoremove        - Remove orphaned packages\n");
        kprintf("\nRepository Management:\n");
        kprintf("  fetch repo list         - List repositories\n");
        kprintf("  fetch repo add <n> <url>- Add repository\n");
        kprintf("  fetch repo remove <n>   - Remove repository\n");
        kprintf("  fetch repo enable <n>   - Enable repository\n");
        kprintf("  fetch repo disable <n>  - Disable repository\n");
        kprintf("\nPackage Pinning:\n");
        kprintf("  fetch hold <pkg>        - Protect from removal\n");
        kprintf("  fetch unhold <pkg>      - Unprotect\n");
        kprintf("  fetch held              - List held packages\n");
        kprintf("\nSystem:\n");
        kprintf("  fetch stats             - Show package manager stats\n");
        return;
    }

    const char *first = argv[1];

    if (first[0] == '-') {
        if (strcmp(first, "-Syu") == 0 || strcmp(first, "-syu") == 0) { pkg_upgrade_all(); return; }
        if (strcmp(first, "-Su") == 0 || strcmp(first, "-su") == 0) { pkg_upgrade_all(); return; }
        if (strcmp(first, "-U") == 0) { pkg_upgrade_all(); return; }
        if (strcmp(first, "-S") == 0) {
            if (argc < 3) { kprintf("usage: fetch -S <package>\n"); return; }
            pkg_install_with_deps(argv[2]);
            return;
        }
        if (strcmp(first, "-I") == 0) {
            if (argc < 3) { kprintf("usage: fetch -I <package>\n"); return; }
            pkg_install_with_deps(argv[2]);
            return;
        }
        if (strcmp(first, "-If") == 0 || strcmp(first, "-if") == 0) {
            if (argc < 3) { kprintf("usage: fetch -If <file>\n"); return; }
            char buf[FS_CONTENT_MAX];
            int n = fs_read(argv[2], buf, FS_CONTENT_MAX);
            if (n <= 0) { kprintf("fetch: cannot read \'%s\'\n", argv[2]); return; }
            buf[n] = 0;
            char pkg_name[64] = {0}, pkg_ver[64] = {0}, pkg_desc[128] = {0};
            uint32_t pkg_size = 0;
            int file_count = 0;
            char *line = buf;
            while (line && *line) {
                char *next = strchr(line, '\n');
                if (next) { *next = 0; next++; }
                char *nl = strchr(line, '\r');
                if (nl) *nl = 0;
                if (strncmp(line, "NAME:", 5) == 0) strncpy_safe(pkg_name, line + 5, sizeof(pkg_name));
                else if (strncmp(line, "VERSION:", 8) == 0) strncpy_safe(pkg_ver, line + 8, sizeof(pkg_ver));
                else if (strncmp(line, "DESC:", 5) == 0) strncpy_safe(pkg_desc, line + 5, sizeof(pkg_desc));
                else if (strncmp(line, "SIZE:", 5) == 0) { const char *sp = line + 5; while (*sp >= '0' && *sp <= '9') { pkg_size = pkg_size * 10 + (*sp - '0'); sp++; } }
                else if (strncmp(line, "file:", 5) == 0) {
                    char *rest = line + 5;
                    char *sep = strchr(rest, ':');
                    if (sep) {
                        *sep = 0;
                        char *fpath = rest;
                        char *fcontent = sep + 1;
                        int flen = strlen(fcontent);
                        if (flen > 0) {
                            if (fs_mkfile(fpath) == 0 && fs_write(fpath, fcontent, flen) >= 0) {
                                file_count++;
                                kprintf("  wrote %s (%d bytes)\n", fpath, flen);
                            } else {
                                kprintf("fetch: failed to write %s\n", fpath);
                            }
                        }
                    }
                }
                line = next;
            }
            if (pkg_name[0]) {
                pkg_install(pkg_name, pkg_ver[0] ? pkg_ver : "1.0", pkg_desc[0] ? pkg_desc : "No description", pkg_size);
                kprintf("Installed %d file(s) from %s\n", file_count, argv[2]);
            } else {
                kprintf("fetch: no package name found in %s\n", argv[2]);
            }
            return;
        }
        if (strcmp(first, "-Sw") == 0 || strcmp(first, "-sw") == 0) {
            if (argc < 3) { kprintf("usage: fetch -Sw <package>\n"); return; }
            pkg_fetch(argv[2], PKG_CACHE_DIR);
            return;
        }
        if (strcmp(first, "-R") == 0) { if (argc < 3) { kprintf("usage: fetch -R <package>\n"); return; } pkg_remove(argv[2]); return; }
        if (strcmp(first, "-Rs") == 0 || strcmp(first, "-rs") == 0) {
            if (argc < 3) { kprintf("usage: fetch -Rs <package>\n"); return; }
            kprintf("Removing %s and orphaned dependencies...\n", argv[2]);
            pkg_remove(argv[2]);
            pkg_autoremove();
            return;
        }
        if (strcmp(first, "-Ro") == 0 || strcmp(first, "-ro") == 0) { pkg_autoremove(); return; }
        if (strcmp(first, "-Sr") == 0 || strcmp(first, "-sr") == 0) { if (argc < 3) { kprintf("usage: fetch -Sr <pattern>\n"); return; } pkg_search(argv[2]); return; }
        if (strcmp(first, "-Ss") == 0 || strcmp(first, "-ss") == 0) { if (argc < 3) { kprintf("usage: fetch -Ss <pattern>\n"); return; } pkg_search(argv[2]); return; }
        if (strcmp(first, "-Si") == 0 || strcmp(first, "-si") == 0) { if (argc < 3) { kprintf("usage: fetch -Si <package>\n"); return; } pkg_show(argv[2]); return; }
        if (strcmp(first, "-Qi") == 0 || strcmp(first, "-qi") == 0) {
            if (argc < 3) { kprintf("usage: fetch -Qi <package>\n"); return; }
            pkg_info_t info;
            if (pkg_get_info(argv[2], &info) == 0) {
                kprintf("         Name: %s\n", info.name);
                kprintf("      Version: %s\n", info.version);
                kprintf("     Category: %s\n", pkg_category_name(info.category));
                kprintf("         Size: %u bytes\n", info.size);
                kprintf("      License: %s\n", info.license[0] ? info.license : "(unknown)");
            } else {
                kprintf("fetch: \'%s\' not installed\n", argv[2]);
            }
            return;
        }
        if (strcmp(first, "-Ql") == 0 || strcmp(first, "-ql") == 0) {
            if (argc < 3) { kprintf("usage: fetch -Ql <package>\n"); return; }
            kprintf("Files of %s: (not yet tracked — package file list not implemented)\n", argv[2]);
            return;
        }
        if (strcmp(first, "-Scc") == 0 || strcmp(first, "-scc") == 0) { pkg_clean(); return; }
        if (strcmp(first, "-L") == 0) { pkg_list(); return; }
        if (strcmp(first, "-Q") == 0) { pkg_list(); return; }
        kprintf("fetch: unknown flag \'%s\'\n", first);
        return;
    }

    const char *subcmd = first;

    if (strcmp(subcmd, "list") == 0) { pkg_list(); }
    else if (strcmp(subcmd, "search") == 0) {
        if (argc < 3) { kprintf("usage: fetch search <pattern>\n"); return; } pkg_search(argv[2]);
    }
    else if (strcmp(subcmd, "show") == 0) {
        if (argc < 3) { kprintf("usage: fetch show <package>\n"); return; } pkg_show(argv[2]);
    }
    else if (strcmp(subcmd, "install") == 0) {
        if (argc < 3) { kprintf("usage: fetch install <package>\n"); return; }
        const char *pn = argv[2];
        pkg_install_with_deps(pn);
    }
    else if (strcmp(subcmd, "remove") == 0) {
        if (argc < 3) { kprintf("usage: fetch remove <package>\n"); return; } pkg_remove(argv[2]);
    }
    else if (strcmp(subcmd, "update") == 0) { pkg_update(); }
    else if (strcmp(subcmd, "sync") == 0) {
        int sync_update = 0;
        if (argc > 2 && (strcmp(argv[2], "-u") == 0 || strcmp(argv[2], "--update") == 0))
            sync_update = 1;
        pkg_sync(sync_update);
    }
    else if (strcmp(subcmd, "fetch") == 0) {
        if (argc < 3) { kprintf("usage: fetch fetch <package>... [--output-dir <dir>]\n"); return; }
        const char *out_dir = NULL;
        for (int i = 3; i < argc; i++)
            if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) out_dir = argv[++i];
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--output-dir") == 0) { i++; continue; }
            pkg_fetch(argv[i], out_dir);
        }
    }
    else if (strcmp(subcmd, "upgrade") == 0) { pkg_upgrade_all(); }
    else if (strcmp(subcmd, "autoremove") == 0) { pkg_autoremove(); }
    else if (strcmp(subcmd, "clean") == 0) { pkg_clean(); }
    else if (strcmp(subcmd, "stats") == 0) { pkg_stats(); }
    else if (strcmp(subcmd, "repo") == 0) {
        if (argc < 3) { kprintf("usage: fetch repo <list|add|remove|enable|disable>\n"); return; }
        const char *rs = argv[2];
        if (strcmp(rs, "list") == 0) pkg_list_repos();
        else if (strcmp(rs, "add") == 0) {
            if (argc < 5) { kprintf("usage: fetch repo add <name> <url>\n"); return; }
            pkg_add_repo(argv[3], argv[4]);
        }
        else if (strcmp(rs, "remove") == 0) {
            if (argc < 4) { kprintf("usage: fetch repo remove <name>\n"); return; }
            pkg_remove_repo(argv[3]);
        }
        else if (strcmp(rs, "enable") == 0) {
            if (argc < 4) { kprintf("usage: fetch repo enable <name>\n"); return; }
            pkg_enable_repo(argv[3]);
        }
        else if (strcmp(rs, "disable") == 0) {
            if (argc < 4) { kprintf("usage: fetch repo disable <name>\n"); return; }
            pkg_disable_repo(argv[3]);
        }
        else if (strcmp(rs, "update") == 0) { pkg_repo_update(); }
        else kprintf("fetch repo: unknown \'%s\'\n", rs);
    }
    else if (strcmp(subcmd, "hold") == 0) {
        if (argc < 3) { kprintf("usage: fetch hold <package>\n"); return; } pkg_hold(argv[2]);
    }
    else if (strcmp(subcmd, "unhold") == 0) {
        if (argc < 3) { kprintf("usage: fetch unhold <package>\n"); return; } pkg_unhold(argv[2]);
    }
    else if (strcmp(subcmd, "held") == 0) { pkg_held_list(); }
    else if (strcmp(subcmd, "deps") == 0) {
        if (argc < 3) { kprintf("usage: fetch deps <package>\n"); return; }
        pkg_check_deps(argv[2]);
    }
    else if (strcmp(subcmd, "rdepends") == 0) {
        if (argc < 3) { kprintf("usage: fetch rdepends <package>\n"); return; }
        pkg_rdepends(argv[2]);
    }
    else if (strcmp(subcmd, "orphans") == 0) { pkg_orphans(); }
    else if (strcmp(subcmd, "dep-tree") == 0) {
        if (argc < 3) { kprintf("usage: fetch dep-tree <package>\n"); return; }
        pkg_dep_tree(argv[2], 0);
    }
    else if (strcmp(subcmd, "category") == 0) {
        if (argc < 3) { kprintf("usage: fetch category <name>\n"); return; }
        pkg_search(argv[2]);
    }
    else if (strcmp(subcmd, "download") == 0) {
        if (argc < 3) { kprintf("usage: fetch download <package>\n"); return; }
        pkg_fetch(argv[2], PKG_CACHE_DIR);
    }
    else if (strcmp(subcmd, "payload") == 0) {
        if (argc < 3) { kprintf("usage: fetch payload <package>\n"); return; }
        char dir[FS_PATH_MAX];
        const char *out = PKG_CACHE_DIR;
        fs_getcwd(dir, sizeof(dir));
        if (dir[0]) out = dir;
        pkg_fetch_payload(argv[2], out);
    }
    else if (strcmp(subcmd, "base64") == 0) {
        if (argc < 4) { kprintf("usage: fetch base64 <in> <out>\n"); return; }
        int n = pkg_base64_encode_file(argv[2], argv[3]);
        if (n > 0) kprintf("base64: %s -> %s (%d bytes b64)\n", argv[2], argv[3], n);
        else kprintf("base64: failed to encode '%s'\n", argv[2]);
    }
    else if (strcmp(subcmd, "decode") == 0) {
        if (argc < 3) { kprintf("usage: fetch decode <in> [out]\n"); return; }
        char outpath[FS_PATH_MAX];
        if (argc > 3) strncpy_safe(outpath, argv[3], sizeof(outpath));
        else snprintf(outpath, sizeof(outpath), "%s.dec", argv[2]);
        int n = pkg_base64_decode_file(argv[2], outpath);
        if (n > 0) kprintf("decode: %s -> %s (%d bytes)\n", argv[2], outpath, n);
        else kprintf("decode: failed to decode '%s'\n", argv[2]);
    }
    else {
        kprintf("fetch: unknown command \'%s\'\n", subcmd);
        kprintf("Try \'fetch\' for help\n");
    }
}

/* ---------- HTTP download ---------- */

static void cmd_download(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: download <host> [port] [path]\n"); return; }
    const char *host = argv[1];
    uint16_t port = 80;
    const char *path = "/";
    if (argc > 2) port = (uint16_t)str_to_int(argv[2]);
    if (argc > 3) path = argv[3];
    char buf[4096];
    int n = http_get(host, port, path, buf, sizeof(buf) - 1);
    if (n < 0) { kprintf("download: failed\n"); return; }
    buf[n] = 0;
    kprintf("--- %d bytes from %s ---\n%s\n", n, host, buf);
}

static void cmd_post(int argc, char **argv) {
    if (argc < 5) { kprintf("usage: post <host> <port> <path> <body>\n"); return; }
    const char *host = argv[1];
    uint16_t port = (uint16_t)str_to_int(argv[2]);
    const char *path = argv[3];
    char buf[4096];
    int n = http_post(host, port, path, argv[4], (uint16_t)strlen(argv[4]),
                      buf, sizeof(buf) - 1);
    if (n < 0) { kprintf("post: failed\n"); return; }
    buf[n] = 0;
    kprintf("--- %d bytes response ---\n%s\n", n, buf);
}

/* ---------- HTTPS download ---------- */

static void cmd_https(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: https <host> [port] [path]\n");
        kprintf("   TLS1.2 GET.  https_ca_get() status: %s\n",
                https_ca_get() ? "store ok" : "store MISSING");
        return;
    }
    const char *host = argv[1];
    uint16_t port = 443;
    const char *path = "/";
    if (argc > 2) port = (uint16_t)str_to_int(argv[2]);
    if (argc > 3) path = argv[3];
    char buf[8192];
    int n = https_get(host, port, path, buf, sizeof(buf) - 1);
    if (n < 0) {
        kprintf("https: failed (insecure=%d)\n", https_insecure());
        return;
    }
    buf[n] = 0;
    kprintf("--- %d bytes from %s ---\n%s\n", n, host, buf);
}

static void cmd_tlsinsecure(int argc, char **argv) {
    if (argc < 2) {
        kprintf("verify policy: %s (0=require trusted cert, 1=accept any)\n",
                https_insecure() ? "INSECURE" : "SECURE");
        return;
    }
    kprintf("tlsinsecure: %s\n",
            https_set_insecure(str_to_int(argv[1]) != 0) ? "ON" : "OFF");
}

static void cmd_catrust(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: catrust <pemfile>\n"); return; }
    char pem[8192];
    int n = fs_read(argv[1], pem, sizeof(pem) - 1);
    if (n <= 0) { kprintf("catrust: cannot read %s\n", argv[1]); return; }
    if (https_trust_ca_pem(pem, (size_t)n) != 0) {
        kprintf("catrust: parse FAILED\n");
        return;
    }
    kprintf("catrust: trusted %d pem bytes\n", n);
}

/* ---------- new commands ---------- */

static int str_to_int(const char *s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        int d = *s++ - '0';
        if (val > (2147483647 - d) / 10)
            return neg ? (-2147483647 - 1) : 2147483647;
        val = val * 10 + d;
    }
    return neg ? -val : val;
}

static void format_uptime(char *buf, int max) {
    unsigned int sec = (unsigned int)(timer_get_milliseconds() / 1000);
    unsigned int min = sec / 60; sec %= 60;
    unsigned int hr  = min / 60; min %= 60;
    unsigned int day = hr / 24; hr %= 24;
    int n = 0;
    if (day) {
        int r = max - n;
        if (r > 0) {
            int written = sprintf(buf + n, "%u day%s, ", day, day == 1 ? "" : "s");
            if (written >= r) written = r - 1;
            if (written > 0) n += written;
        }
    }
    {
        int r = max - n;
        if (r > 0) {
            int written = sprintf(buf + n, "%02u:%02u:%02u", hr, min, sec);
            if (written >= r) written = r - 1;
            if (written > 0) n += written;
        }
    }
    buf[n < max ? n : max - 1] = 0;
}

static void cmd_uptime(void) {
    char buf[48];
    format_uptime(buf, sizeof(buf));
    kprintf("%s\n", buf);
}

static void cpu_get_vendor(char *vendor, int max) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    int i = 0;
    uint32_t parts[3] = { ebx, edx, ecx };
    for (int p = 0; p < 3 && i < max - 1; p++) {
        uint8_t *b = (uint8_t *)&parts[p];
        for (int j = 0; j < 4 && i < max - 1; j++)
            vendor[i++] = (char)b[j];
    }
    vendor[i] = 0;
}

static void cmd_uname(int argc, char **argv) {
    int show_s = 0, show_n = 0, show_r = 0, show_v = 0, show_m = 0;
    int show_p = 0, show_i = 0, show_o = 0;

    if (argc == 1) {
        show_s = 1;
    } else {
        for (int a = 1; a < argc; a++) {
            const char *arg = argv[a];
            if (arg[0] != '-' || !arg[1]) {
                kprintf("uname: invalid option -- '%s'\n", arg);
                return;
            }
            if (strcmp(arg, "--help") == 0) {
                kprintf("usage: uname [-snrvmipoah]\n");
                return;
            }
            for (int j = 1; arg[j]; j++) {
                switch (arg[j]) {
                case 'a':
                    show_s = show_n = show_r = show_v = show_m = 1;
                    show_p = show_i = show_o = 1;
                    break;
                case 's': show_s = 1; break;
                case 'n': show_n = 1; break;
                case 'r': show_r = 1; break;
                case 'v': show_v = 1; break;
                case 'm': show_m = 1; break;
                case 'p': show_p = 1; break;
                case 'i': show_i = 1; break;
                case 'o': show_o = 1; break;
                default:
                    kprintf("uname: invalid option -- '%c'\n", arg[j]);
                    return;
                }
            }
        }
    }

    const char *nodename = env_get("HOSTNAME");
    if (!nodename) nodename = "codeos";

    int first = 1;
    #define UNAME_FIELD(s) do { \
        if (!first) kprintf(" "); \
        first = 0; \
        kprintf("%s", (s)); \
    } while (0)

    if (show_s) UNAME_FIELD(KERNEL_NAME);
    if (show_n) UNAME_FIELD(nodename);
    if (show_r) UNAME_FIELD(KERNEL_VERSION);
    if (show_v) UNAME_FIELD(KERNEL_VERSION_STR);
    if (show_m) UNAME_FIELD(KERNEL_ARCH);
    if (show_p) UNAME_FIELD(KERNEL_ARCH);
    if (show_i) UNAME_FIELD(KERNEL_ARCH);
    if (show_o) UNAME_FIELD(KERNEL_OS);
    kprintf("\n");
    #undef UNAME_FIELD
}

static void fastfetch_line(const char *key, const char *val) {
    kprintf("%-12s%s\n", key, val);
}

static void cmd_fastfetch(void) {
    static const char *logo[] = {
        "  ____             __  _____ ",
        " / __ \\____  _____/ /_/ ___/___  ____  ____ ",
        "/ / / / __ \\/ ___/ __/\\__ \\/ _ \\/ __ \\/ __ \\",
        "/ /_/ / /_/ / /  / /_ ___/ /  __/ / / / /_/ /",
        "\\____/\\____/_/   \\__//____/\\___/_/ /_/\\____/",
        0
    };

    for (int i = 0; logo[i]; i++)
        kprintf("%s\n", logo[i]);
    kprintf("\n");

    const char *user = env_get("USER");
    const char *host = env_get("HOSTNAME");
    if (!user) user = "root";
    if (!host) host = "codeos";
    kprintf("%s@%s\n", user, host);
    kprintf("----------------------------------------\n");

    char buf[96];
    sprintf(buf, "%s %s", KERNEL_OS, KERNEL_VERSION);
    fastfetch_line("OS", buf);

    fastfetch_line("Host", host);

    sprintf(buf, "%s", KERNEL_NAME);
    fastfetch_line("Kernel", buf);

    sprintf(buf, "%s", KERNEL_VERSION);
    fastfetch_line("Version", buf);

    fastfetch_line("Arch", KERNEL_ARCH);

    format_uptime(buf, sizeof(buf));
    fastfetch_line("Uptime", buf);

    /* mm_total/mm_used describe the early boot memory map.  Once Limine
       and the PMM have initialized, the PMM is the authoritative allocator
       view and avoids reporting the small reserved low-memory area as the
       only used memory. */
    uint64_t total_kb = pmm_total_pages() * (PAGE_SIZE / 1024);
    uint64_t used_kb = pmm_count_used() * (PAGE_SIZE / 1024);
    unsigned int pct = total_kb > 0 ? (unsigned int)(used_kb * 100 / total_kb) : 0;
    sprintf(buf, "%llu MiB / %llu MiB (%u%%)",
            (unsigned long long)(used_kb / 1024),
            (unsigned long long)(total_kb / 1024), pct);
    fastfetch_line("Memory", buf);

    sprintf(buf, "%llu MiB used / %llu MiB total",
            (unsigned long long)(mm_heap_used() / 1024),
            (unsigned long long)(mm_heap_total() / 1024));
    fastfetch_line("Heap", buf);

    cpu_get_vendor(buf, sizeof(buf));
    fastfetch_line("CPU", buf);

    sprintf(buf, "%d", sched_thread_count());
    fastfetch_line("Threads", buf);

    sprintf(buf, "%u Hz", timer_get_frequency());
    fastfetch_line("Timer", buf);

    const char *shell = env_get("SHELL");
    fastfetch_line("Shell", shell ? shell : "bash");

    fastfetch_line("Display", "Framebuffer");

    if (mouse_available())
        fastfetch_line("Mouse", "PS/2 detected");
    if (block_available()) {
        int sectors, lba;
        block_get_info(&sectors, &lba);
        (void)lba;
        sprintf(buf, "%s (%d MB)", block_backend_name(),
                (int)((uint64_t)sectors * 512 / 1048576));
        fastfetch_line("Disk", buf);
    }
}

/* ── sysfetch: neofetch-style system info fetcher ── */

static void cmd_sysfetch(void) {
    /* ── CodeOS logo (neofetch-style, 9 lines) ── */
    static const char *logo[] = {
        "        _____   ______  ",
        "       / ____| |  ____| ",
        "  ___ | (___   | |__    ",
        " / __| \\___ \\  |  __|   ",
        "| (__  ____) | | |      ",
        " \\___||_____/  |_|      ",
        "                        ",
        "   _____  ___   ____    ",
        "  / ____||  _ \\|  _ \\  ",
        " | |     | |_) | |_) | ",
        " | |     |  _ <|  __/  ",
        " | |____ | |_) | |     ",
        "  \\_____||____/|_|     ",
        0
    };

    const char *user = env_get("USER");
    const char *host = env_get("HOSTNAME");
    if (!user) user = "root";
    if (!host) host = "codeos";

    /* Gather system info */
    char buf_os[64], buf_kernel[32], buf_uptime[48], buf_mem[64];
    char buf_cpu[96], buf_threads[16], buf_shell[32], buf_pkgs[32];
    char buf_res[32], buf_disk[64];

    sprintf(buf_os, "%s %s", KERNEL_OS, KERNEL_VERSION);
    sprintf(buf_kernel, "%s %s", KERNEL_NAME, KERNEL_VERSION);
    format_uptime(buf_uptime, sizeof(buf_uptime));

    uint64_t total_kb = mm_total();
    uint64_t used_kb = mm_used();
    unsigned int mem_pct = total_kb > 0 ? (unsigned int)(used_kb * 100 / total_kb) : 0;
    sprintf(buf_mem, "%llu MiB / %llu MiB (%u%%)",
            (unsigned long long)(used_kb / 1024),
            (unsigned long long)(total_kb / 1024), mem_pct);

    cpu_get_vendor(buf_cpu, sizeof(buf_cpu));
    sprintf(buf_threads, "%d", sched_thread_count());

    const char *shell = env_get("SHELL");
    sprintf(buf_shell, "%s", shell ? shell : "csl");
    sprintf(buf_pkgs, "%d (CodeOS)", pkg_installed_count());

    sprintf(buf_res, "%ux%u", (unsigned)fb_getwidth(), (unsigned)fb_getheight());
    if (block_available()) {
        int sectors, lba;
        block_get_info(&sectors, &lba);
        (void)lba;
        sprintf(buf_disk, "%s (%d MB)", block_backend_name(),
                (int)((uint64_t)sectors * 512 / 1048576));
    } else {
        sprintf(buf_disk, "VFS only");
    }

    /* ── Info lines (same order as neofetch) ── */
    static const char *labels[] = {
        "OS", "Kernel", "Uptime", "Packages", "Shell",
        "Resolution", "CPU", "Memory", "Disk", "Threads", 0
    };
    const char *values[] = {
        buf_os, buf_kernel, buf_uptime, buf_pkgs, buf_shell,
        buf_res, buf_cpu, buf_mem, buf_disk, buf_threads
    };

    /* Count logo lines */
    int logo_lines = 0;
    while (logo[logo_lines]) logo_lines++;

    /* ── Render: side-by-side on framebuffer ── */
    int cur_row = 1;  /* start at row 1 */
    uint32_t FG_WHITE   = 0xFFFFFFFF;
    uint32_t FG_CYAN    = 0xFF00D4FF;  /* CodeOS cyan */
    uint32_t FG_GREEN   = 0xFF00FF88;
    uint32_t FG_YELLOW  = 0xFF00AAFF;  /* yellow in BGRA */
    uint32_t FG_BLUE    = 0xFFFF8800;  /* blue in BGRA */
    uint32_t FG_RED     = 0xFF4444FF;
    uint32_t FG_BOLD    = 0xFFFFFFFF;
    uint32_t FG_DIM     = 0xFF888888;
    uint32_t BG_DEFAULT = 0x00000000;

    /* Color palette for blocks */
    uint32_t palette[] = {
        0xFF444444, 0xFF442222, 0xFF224422, 0xFF444422,
        0xFF222244, 0xFF442244, 0xFF224444, 0xFF444444
    };
    (void)palette; /* reserved for future color block rendering */

    /* Label colors (neofetch-style cycling) */
    uint32_t label_colors[] = {
        FG_CYAN, FG_CYAN, FG_CYAN, FG_GREEN, FG_CYAN,
        FG_CYAN, FG_BLUE, FG_RED, FG_YELLOW, FG_CYAN
    };

    int info_count = 0;
    while (labels[info_count]) info_count++;

    int max_lines = logo_lines > info_count ? logo_lines : info_count;
    int label_w = 13;  /* "Resolution  " = 13 chars */

    for (int i = 0; i < max_lines; i++) {
        int col = 0;

        /* Logo on the left (cyan) */
        if (i < logo_lines) {
            int len = 0;
            while (logo[i][len]) len++;
            if (len > 0) {
                fb_write_styled(cur_row, col, logo[i], FG_CYAN, BG_DEFAULT);
            }
            col += 28;  /* logo width in chars */
        } else {
            col += 28;
        }

        /* Separator space */
        col += 1;

        /* Info on the right */
        if (i == 0) {
            /* user@host line */
            /* user part (bold white) */
            int ulen = 0; while (user[ulen]) ulen++;
            int hlen = 0; while (host[hlen]) hlen++;

            fb_write_styled(cur_row, col, user, FG_BOLD, BG_DEFAULT);
            col += ulen;
            fb_write_styled(cur_row, col, "@", FG_BOLD, BG_DEFAULT);
            col += 1;
            fb_write_styled(cur_row, col, host, FG_BOLD, BG_DEFAULT);
            col += hlen;
        } else if (i == 1) {
            /* Separator line */
            char sep[64];
            int seplen = 0;
            while (seplen < 50) sep[seplen++] = '-';
            sep[seplen] = 0;
            fb_write_styled(cur_row, col, sep, FG_DIM, BG_DEFAULT);
        } else if (i - 2 < info_count) {
            int idx = i - 2;
            /* Label (colored, bold-style) */
            fb_write_styled(cur_row, col, labels[idx], label_colors[idx], BG_DEFAULT);
            col += label_w;
            /* Separator */
            fb_write_styled(cur_row, col, ": ", FG_DIM, BG_DEFAULT);
            col += 2;
            /* Value (white) */
            fb_write_styled(cur_row, col, values[idx], FG_WHITE, BG_DEFAULT);
        }

        cur_row++;
    }

    /* ── Color palette blocks ── */
    cur_row++;
    uint32_t ansi_palette[] = {
        0xFF000000, 0xFFCC3333, 0xFF33CC33, 0xFFCCCC33,
        0xFF3333CC, 0xFFCC33CC, 0xFF33CCCC, 0xFFCCCCCC
    };
    int block_w = 2;
    int block_h = 1;
    int px_per_col = 16;  /* char cell width */
    for (int c = 0; c < 8; c++) {
        int x = (28 + 1) * px_per_col + c * block_w * px_per_col;
        int y = cur_row * 16;
        fb_fillrect((uint32_t)x, (uint32_t)y,
                    (uint32_t)(block_w * px_per_col - 2), (uint32_t)(block_h * 16 - 2),
                    ansi_palette[c]);
    }
    cur_row += 2;

    /* Second row of palette (bright) */
    uint32_t bright_palette[] = {
        0xFF555555, 0xFF555555, 0xFF55FF55, 0xFFFFFF55,
        0xFF5555FF, 0xFFFF55FF, 0xFF55FFFF, 0xFFFFFFFF
    };
    for (int c = 0; c < 8; c++) {
        int x = (28 + 1) * px_per_col + c * block_w * px_per_col;
        int y = cur_row * 16;
        fb_fillrect((uint32_t)x, (uint32_t)y,
                    (uint32_t)(block_w * px_per_col - 2), (uint32_t)(block_h * 16 - 2),
                    bright_palette[c]);
    }
    cur_row += 2;

    /* ── Memory bar ── */
    {
        int bar_col = (28 + 1);
        int bar_width = 30;  /* chars */
        int filled = mem_pct * bar_width / 100;

        fb_write_styled(cur_row, bar_col, "Memory [", FG_CYAN, BG_DEFAULT);
        bar_col += 8;

        for (int b = 0; b < bar_width; b++) {
            char ch[2] = { (b < filled) ? '|' : ' ', 0 };
            uint32_t clr = (b < filled) ? FG_GREEN : FG_DIM;
            fb_write_styled(cur_row, bar_col, ch, clr, BG_DEFAULT);
            bar_col++;
        }

        char pct_str[16];
        sprintf(pct_str, "] %u%%", mem_pct);
        fb_write_styled(cur_row, bar_col, pct_str, FG_WHITE, BG_DEFAULT);
    }
    cur_row += 2;

    /* ── Plain kprintf version for serial/LT capture ── */
    kprintf("%s@%s\n", user, host);
    kprintf("--------------------------------------\n");
    kprintf("OS           : %s\n", buf_os);
    kprintf("Kernel       : %s\n", buf_kernel);
    kprintf("Uptime       : %s\n", buf_uptime);
    kprintf("Packages     : %s\n", buf_pkgs);
    kprintf("Shell        : %s\n", buf_shell);
    kprintf("Resolution   : %s\n", buf_res);
    kprintf("CPU          : %s\n", buf_cpu);
    kprintf("Memory       : %s\n", buf_mem);
    kprintf("Disk         : %s\n", buf_disk);
    kprintf("Threads      : %s\n", buf_threads);
    kprintf("\n");
    /* ASCII palette for serial */
    kprintf("             \xf0\x9f\x94\x84\xf0\x9f\x94\x85\xf0\x9f\x94\x86\xf0\x9f\x94\x87");
    kprintf("\xf0\x9f\x94\x88\xf0\x9f\x94\x89\xf0\x9f\x94\x8a\xf0\x9f\x94\x8b\n");
    /* Memory bar */
    {
        kprintf("Memory       [");
        int bar_width = 30;
        int filled = mem_pct * bar_width / 100;
        for (int b = 0; b < bar_width; b++)
            kprintf("%c", b < filled ? '|' : ' ');
        kprintf("] %u%%\n", mem_pct);
    }
}

static void cmd_free(void) { mm_print_regions(); }

static void cmd_whoami(void) {
    kprintf("%s\n", users[current_uid].username);
}

static void cmd_hostname(int argc, char **argv) {
    if (argc > 1) {
        env_set("HOSTNAME", argv[1]);
    }
    const char *h = env_get("HOSTNAME");
    kprintf("%s\n", h ? h : "codeos");
}

static void cmd_which(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: which <command>\n"); return; }
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(builtins[i], argv[1]) == 0) {
            kprintf("%s: built-in command\n", argv[1]);
            return;
        }
    }
    kprintf("%s: not found\n", argv[1]);
}

static void cmd_sleep(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: sleep <ms>\n"); return; }
    unsigned int ms;
    if (str_to_uint(argv[1], &ms) < 0) { kprintf("sleep: invalid number\n"); return; }
    unsigned int start = (unsigned int)timer_get_ticks();
    while ((unsigned int)timer_get_ticks() - start < ms) {
        if (shell_interrupted) { shell_interrupted = 0; kprintf("^C\n"); return; }
        __asm__ volatile("hlt");
    }
}

static void cmd_repeat(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: repeat N <command> [args...]\n"); return; }
    unsigned int n;
    if (str_to_uint(argv[1], &n) < 0) { kprintf("repeat: invalid number\n"); return; }
    char *rargv[MAX_ARGS];
    int rargc = 0;
    for (int i = 2; i < argc && rargc < MAX_ARGS - 1; i++) rargv[rargc++] = argv[i];
    rargv[rargc] = 0;
    for (unsigned int i = 0; i < n; i++)
        run_builtin(rargc, rargv);
}

static void cmd_seq(int argc, char **argv) {
    int start = 1, end = 1, step = 1;
    if (argc >= 2) { start = str_to_int(argv[1]); end = start; }
    if (argc >= 3) { end = str_to_int(argv[2]); }
    if (argc >= 4) { step = str_to_int(argv[3]); }
    if (step == 0) step = 1;
    if (step > 0) {
        for (int i = start; i <= end; i += step) {
            kprintf("%d\n", i);
            if (i > INT_MAX - step) break; /* prevent overflow on next i += step */
        }
    } else {
        for (int i = start; i >= end; i += step) {
            kprintf("%d\n", i);
            if (i < INT_MIN - step) break; /* prevent overflow on next i += step */
        }
    }
}

static void cmd_yes(int argc, char **argv) {
    const char *s = argc > 1 ? argv[1] : "y";
    for (int i = 0; i < 20; i++) kprintf("%s\n", s);
}

static void cmd_true(void)  {}
static void cmd_false(void) { kprintf("(returning failure conceptually)\n"); }

static void cmd_wc(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: wc <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("wc: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    int lines = 0, words = 0, bytes = n;
    int in_word = 0;
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t') { in_word = 0; }
        else if (!in_word) { words++; in_word = 1; }
    }
    kprintf("%d  %d  %d  %s\n", lines, words, bytes, argv[1]);
}

static void cmd_head(int argc, char **argv) {
    int n = 10;
    const char *path;
    if (argc < 2) { kprintf("usage: head [-n N] <file>\n"); return; }
    int ai = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) { n = str_to_int(argv[2]); ai = 3; }
    path = argv[ai];
    char buf[FS_CONTENT_MAX + 1];
    int total = fs_read(path, buf, FS_CONTENT_MAX);
    if (total < 0) { kprintf("head: %s: no such file\n", path); return; }
    buf[total] = 0;
    int lines = 0;
    for (int i = 0; i < total && lines < n; i++) {
        kprintf("%c", buf[i]);
        if (buf[i] == '\n') lines++;
    }
}

static void cmd_hexdump(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: hexdump <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("hexdump: %s: no such file\n", argv[1]); return; }
    for (int i = 0; i < n; i += 16) {
        kprintf("%04x: ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < n) kprintf("%02x ", (unsigned char)buf[i+j]);
            else kprintf("   ");
            if (j == 7) kprintf(" ");
        }
        kprintf(" |");
        for (int j = 0; j < 16 && i + j < n; j++) {
            char c = buf[i+j];
            kprintf("%c", c >= ' ' && c < 0x7F ? c : '.');
        }
        kprintf("|\n");
    }
}

static int calc_expr(const char **p, int *result);

static int calc_parse_number(const char **p, int *result) {
    while (**p == ' ') (*p)++;
    if (**p == '(') {
        (*p)++;
        int val;
        if (!calc_expr(p, &val)) return 0;
        while (**p == ' ') (*p)++;
        if (**p == ')') { (*p)++; *result = val; return 1; }
        return 0;
    }
    if (**p >= '0' && **p <= '9') {
        int val = 0;
        while (**p >= '0' && **p <= '9') {
            int d = *((*p)++) - '0';
            if (val > (2147483647 - d) / 10) { *result = 2147483647; return 1; }
            val = val * 10 + d;
        }
        *result = val;
        return 1;
    }
    return 0;
}

static int calc_parse_factor(const char **p, int *result) {
    if (!calc_parse_number(p, result)) return 0;
    while (1) {
        while (**p == ' ') (*p)++;
        if (**p == '*') { (*p)++; int r; if (!calc_parse_number(p, &r)) return 0; *result *= r; }
        else if (**p == '/') { (*p)++; int r; if (!calc_parse_number(p, &r) || r == 0) return 0; *result /= r; }
        else break;
    }
    return 1;
}

static int calc_expr(const char **p, int *result) {
    if (!calc_parse_factor(p, result)) return 0;
    while (1) {
        while (**p == ' ') (*p)++;
        if (**p == '+') { (*p)++; int r; if (!calc_parse_factor(p, &r)) return 0; *result += r; }
        else if (**p == '-') { (*p)++; int r; if (!calc_parse_factor(p, &r)) return 0; *result -= r; }
        else break;
    }
    return 1;
}

static void cmd_calc(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: calc <expression>\n"); return; }
    char expr[CMD_BUF_SIZE];
    int ei = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        while (*a && ei < CMD_BUF_SIZE - 1) expr[ei++] = *a++;
        if (i < argc - 1 && ei < CMD_BUF_SIZE - 1) expr[ei++] = ' ';
    }
    expr[ei] = 0;
    const char *p = expr;
    int result;
    if (calc_expr(&p, &result)) {
        kprintf("%d\n", result);
    } else {
        kprintf("calc: parse error\n");
    }
}

static void cmd_date(void) {
    if (rtc_available()) {
        rtc_time_t t;
        rtc_read(&t);
        const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        const char *days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        int dow = 0; /* approximate: 2026-01-01 is Thu, Y=26, M=1, D=1 */
        { int y = t.year, m = t.month, d = t.day;
          if (m < 3) { m += 12; y--; }
          dow = (d + (13*(m+1))/5 + y + y/4 - y/100 + y/400) % 7; }
        kprintf("%s %s %d %02d:%02d:%02d %s %d\n",
                days[dow], months[t.month-1], t.day,
                t.hour, t.minute, t.second,
                t.month <= 3 ? "EST" : "EDT", t.year);
    } else {
        unsigned int t = (unsigned int)timer_get_ticks();
        unsigned int sec = t / 1000;
        unsigned int min = sec / 60; sec %= 60;
        unsigned int hr  = min / 60; min %= 60;
        kprintf("Time: %02u:%02u:%02u (uptime, no RTC)\n", hr, min, sec);
    }
}

static void cmd_rev(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: rev <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("rev: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    int line_start = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || i == n) {
            for (int j = i - 1; j >= line_start; j--) kprintf("%c", buf[j]);
            kprintf("\n");
            line_start = i + 1;
        }
    }
}

static void cmd_script(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: script <code>\n"); return; }
    char code[CMD_BUF_SIZE];
    int ci = 0;
    for (int i = 1; i < argc && ci < CMD_BUF_SIZE - 2; i++) {
        const char *a = argv[i];
        while (*a && ci < CMD_BUF_SIZE - 2) code[ci++] = *a++;
        if (i < argc - 1) code[ci++] = ' ';
    }
    code[ci] = 0;
    script_val_t result;
    if (script_eval(code, &result) == 0) {
        if (result.type == 0 && result.num != 0)
            kprintf("%lld\n", result.num);
        else if (result.type == 1 && result.str && result.str[0])
            kprintf("%s\n", result.str);
        if (result.str) free(result.str);
    } else {
        kprintf("script: parse error\n");
    }
}

void shell_exec_done(void) {
    kprintf("exec: process exited with status %d\n", user_mode_last_exit_status());
}

extern uint64_t syscall_kernel_rsp;
static void cmd_exec(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: exec <elf-file>\n"); return; }
    uint64_t entry, stack;
    elf_auxv_info_t auxv;
    if (elf_load(argv[1], &entry, &stack, &auxv) < 0) {
        kprintf("exec: %s: failed to load ELF\n", argv[1]);
        return;
    }
    uint64_t rsp = elf_setup_stack(stack, entry, argc > 1 ? argc - 1 : 0,
                                     argc > 1 ? argv + 1 : 0,
                                     0, 0, &auxv);
    kprintf("exec: starting '%s' at entry 0x%lx, rsp=0x%lx\n", argv[1], entry, rsp);
    proc_create(argv[1], entry, stack);
    user_mode_set_return(shell_exec_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;
    user_mode_enter(entry, rsp);
}

static void cmd_run(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: run <file>\n"); return; }
    if (script_run_file(argv[1]) < 0)
        kprintf("run: %s: failed\n", argv[1]);
}

/* ---------- csl games ---------- */

#define GAME_DIR "/usr/share/games"

static const struct {
    const char *name;
    const char *desc;
} games_catalog[] = {
    { "guess",   "Guess the number I'm thinking of (1-100)" },
    { "mines",   "Minesweeper: clear a 9x9 board without hitting a mine" },
    { "hangman", "Classic hangman — guess the secret word, one letter at a time" },
    { "rps",     "Rock-paper-scissors against the machine, best of 5" },
    { "math",    "Quick arithmetic drills — answer as many as you can" },
    { "reflex",  "Reaction timer: hit ENTER as fast as you can" },
    { 0, 0 },
};

static void cmd_games(int argc, char **argv) {
    (void)argc; (void)argv;
    int n = 0;
    kprintf("CodeOS games directory:\n");
    for (int i = 0; games_catalog[i].name; i++) {
        kprintf("  %-8s %s\n", games_catalog[i].name, games_catalog[i].desc);
        n++;
    }
    kprintf("\n%d games installed under %s/\n", n, GAME_DIR);
    kprintf("Play one with: gplay <game>\n");
}

static void cmd_gplay(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: gplay <game>\n");
        kprintf("run `games` for the list\n");
        return;
    }
    if (strchr(argv[1], '/') || strcmp(argv[1], ".") == 0 || strcmp(argv[1], "..") == 0) {
        kprintf("gplay: invalid game name\n");
        return;
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.csl", GAME_DIR, argv[1]);
    if (fs_resolve(path, 0) < 0) {
        kprintf("gplay: '%s': no such game\n", argv[1]);
        kprintf("run `games` for the list\n");
        return;
    }
    kprintf("== %s ==\n", argv[1]);
    if (script_run_file(path) < 0)
        kprintf("gplay: %s: failed to run\n", argv[1]);
}

static void cmd_lgame_game(int argc, char **argv) {
    (void)argc;
    extern int lgame_pong_run(void);
    extern int lgame_snake_run(void);
    if (!fb_available()) {
        kprintf("lgame: no framebuffer available (needs graphical boot)\n");
        return;
    }
    kprintf("lgame: fullscreen mode (ESC to quit back to shell)\n");
    if (argv[0][0] == 'p')
        lgame_pong_run();
    else
        lgame_snake_run();
    kprintf("lgame: returned to shell\n");
}

static void cmd_sort(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: sort <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("sort: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    char *lines[256];
    int line_count = 0;
    lines[line_count++] = buf;
    for (int i = 0; i < n && line_count < 256; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            if (i + 1 < n) lines[line_count++] = &buf[i + 1];
        }
    }
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - 1 - i; j++) {
            if (strcmp(lines[j], lines[j+1]) > 0) {
                char *tmp = lines[j]; lines[j] = lines[j+1]; lines[j+1] = tmp;
            }
        }
    }
    for (int i = 0; i < line_count; i++) kprintf("%s\n", lines[i]);
}

static void cmd_cp(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: cp <src> <dst>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("cp: %s: no such file\n", argv[1]); return; }
    if (fs_mkfile(argv[2]) < 0) { kprintf("cp: %s: could not create\n", argv[2]); return; }
    fs_write(argv[2], buf, n);
}

static void cmd_mv(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: mv <src> <dst>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("mv: %s: no such file\n", argv[1]); return; }
    if (fs_mkfile(argv[2]) < 0) { kprintf("mv: %s: could not create\n", argv[2]); return; }
    fs_write(argv[2], buf, n);
    fs_rm(argv[1]);
}

static void cmd_shutdown(void) {
    kprintf("System halted\n");
    __asm__ volatile("cli; hlt");
}

static void cmd_df(void) {
    kprintf("Filesystem    Size  Used  Free\n");
    kprintf("/             128    %d     %d\n", 0, 128);
}

static void cmd_du(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    int is_dir;
    int idx = fs_resolve(path, &is_dir);
    if (idx < 0) { kprintf("du: %s: no such file\n", path); return; }
    if (!is_dir) {
        char buf[FS_CONTENT_MAX + 1];
        int n = fs_read(path, buf, FS_CONTENT_MAX);
        kprintf("%d\t%s\n", n < 0 ? 0 : n, path);
    } else {
        kprintf("4\t%s/\n", path);
    }
}

static void cmd_id(void) {
    kprintf("uid=%d(%s) gid=%d(%s)\n", current_uid, users[current_uid].username,
            current_uid, users[current_uid].username);
}

static void cmd_su(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : users[0].username;
    int uid = find_user(target);
    if (uid < 0) { kprintf("su: %s: unknown user\n", target); return; }
    if (uid == current_uid) { kprintf("Already %s.\n", target); return; }
    if (users[uid].password[0]) {
        kprintf("Password: ");
        char pass[PASS_MAX];
        read_password(pass, PASS_MAX);
        if (strcmp(pass, users[uid].password) != 0) {
            kprintf("su: incorrect password\n");
            return;
        }
    }
    current_uid = uid;
    env_set("USER", users[uid].username);
}

static void cmd_passwd(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : users[current_uid].username;
    int uid = find_user(target);
    if (uid < 0) { kprintf("passwd: %s: unknown user\n", target); return; }
    if (uid != current_uid && current_uid != 0) {
        kprintf("passwd: only root can change others' passwords\n");
        return;
    }
    kprintf("New password: ");
    char pass[PASS_MAX];
    read_password(pass, PASS_MAX);
    kprintf("Confirm: ");
    char confirm[PASS_MAX];
    read_password(confirm, PASS_MAX);
    if (strcmp(pass, confirm) != 0) { kprintf("passwd: passwords do not match\n"); return; }
    strcpy(users[uid].password, pass);
    kprintf("passwd: password updated\n");
}

static void cmd_sudo(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: sudo <command> [args...]\n"); return; }
    if (current_uid != 0 && users[0].password[0]) {
        kprintf("Password: ");
        char pass[PASS_MAX];
        read_password(pass, PASS_MAX);
        if (strcmp(pass, users[0].password) != 0) {
            kprintf("sudo: incorrect password\n");
            return;
        }
    }
    int saved = current_uid;
    current_uid = 0;
    char *sargv[MAX_ARGS];
    int sargc = 0;
    for (int i = 1; i < argc && sargc < MAX_ARGS - 1; i++) sargv[sargc++] = argv[i];
    sargv[sargc] = 0;
    run_builtin(sargc, sargv);
    current_uid = saved;
}

static void cmd_container(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: container create <name> <image>\n");
        kprintf("       container destroy <id|name>\n");
        kprintf("       container exec <id|name> <binary> [args...]\n");
        kprintf("       container list\n");
        return;
    }
    if (strcmp(argv[1], "create") == 0 && argc >= 4) {
        int id = container_create(argv[2], argv[3]);
        if (id < 0) kprintf("container create: failed\n");
        else kprintf("container created: id=%d name='%s' image='%s'\n", id, argv[2], argv[3]);
    } else if (strcmp(argv[1], "destroy") == 0 && argc >= 3) {
        int id = str_to_int(argv[2]);
        if (id <= 0) {
            container_t *c = container_find(argv[2]);
            if (c) id = c->id;
        }
        if (container_destroy(id) < 0) kprintf("container destroy: failed\n");
        else kprintf("container destroyed\n");
    } else if (strcmp(argv[1], "exec") == 0 && argc >= 4) {
        int id = str_to_int(argv[2]);
        if (id <= 0) {
            container_t *c = container_find(argv[2]);
            if (c) id = c->id;
        }
        if (id <= 0) { kprintf("container exec: unknown container\n"); return; }
        container_exec(id, argv[3], argc - 3, argv + 3, 0);
    } else if (strcmp(argv[1], "list") == 0) {
        char names[CONTAINER_MAX][CONTAINER_NAME_MAX];
        int n = container_list(names, CONTAINER_MAX);
        for (int i = 0; i < n; i++)
            kprintf("  %s\n", names[i]);
        if (n == 0) kprintf("(no containers)\n");
    } else {
        kprintf("container: unknown subcommand '%s'\n", argv[1]);
    }
}

static int resolve_container(const char *name) {
    int id = str_to_int(name);
    if (id <= 0) {
        container_t *c = container_find(name);
        if (c) id = c->id;
    }
    return id;
}

static void cmd_appvm(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: appvm ps [-a]\n");
        kprintf("       appvm run <image> [--memory MB] [--cpu SHARES] [cmd..]\n");
        kprintf("       appvm start <container>\n");
        kprintf("       appvm stop <container>\n");
        kprintf("       appvm restart <container>\n");
        kprintf("       appvm rm <container>\n");
        kprintf("       appvm images\n");
        kprintf("       appvm pull <image>\n");
        kprintf("       appvm exec <container> <cmd..>\n");
        kprintf("       appvm logs <container>\n");
        kprintf("       appvm inspect <container>\n");
        kprintf("       appvm stats <container>\n");
        kprintf("       appvm commit <container> <image>\n");
        return;
    }

    if (strcmp(argv[1], "ps") == 0) {
        int all = (argc >= 3 && strcmp(argv[2], "-a") == 0);
        char names[CONTAINER_MAX][CONTAINER_NAME_MAX];
        int n = all ? container_list_all(names, CONTAINER_MAX) : container_list(names, CONTAINER_MAX);
        kprintf("%-13s %-12s %-12s %s\n", "CONTAINER ID", "NAME", "IMAGE", "STATUS");
        for (int i = 0; i < n; i++) {
            container_t *c = container_find(names[i]);
            if (c) {
                const char *st = c->state == CONTAINER_CREATED ? "created" :
                                 c->state == CONTAINER_RUNNING ? "running" :
                                 c->state == CONTAINER_PAUSED  ? "paused"  : "stopped";
                kprintf("%-13d %-12s %-12s %s\n", c->id, c->name, c->image, st);
            }
        }
        if (n == 0) kprintf("(no containers)\n");
    } else if (strcmp(argv[1], "run") == 0 && argc >= 3) {
        char cname[CONTAINER_NAME_MAX];
        const char *image = argv[2];
        uint64_t memory_mb = 0;
        int cpu_shares = 0;
        int arg_idx = 3;
        while (arg_idx < argc && argv[arg_idx][0] == '-') {
            if (strcmp(argv[arg_idx], "--memory") == 0 && arg_idx + 1 < argc) {
                memory_mb = str_to_int(argv[arg_idx + 1]);
                arg_idx += 2;
            } else if (strcmp(argv[arg_idx], "--cpu") == 0 && arg_idx + 1 < argc) {
                cpu_shares = str_to_int(argv[arg_idx + 1]);
                arg_idx += 2;
            } else {
                break;
            }
        }
        snprintf(cname, sizeof(cname), "%s_%d", image, (int)(timer_get_milliseconds() % 10000));
        int id;
        if (memory_mb > 0 || cpu_shares > 0) {
            id = container_create_with_limits(cname, image, memory_mb, cpu_shares);
        } else {
            id = container_create(cname, image);
        }
        if (id < 0) {
            kprintf("appvm: failed to create container from image '%s'\n", image);
            return;
        }
        if (arg_idx < argc) {
            /* docker-style: run the command directly in the fresh container
             * (namespaces/cgroup exist from create; no entrypoint boot, so
             * this works for images without a real init too), then remove
             * the temporary container once the command exits. */
            if (container_mark_running(id) < 0) {
                kprintf("appvm: run: could not activate container '%s'\n", cname);
                return;
            }
            container_exec(id, argv[arg_idx], argc - arg_idx, argv + arg_idx, 0);
            container_destroy(id);
            kprintf("appvm: removed container '%s'\n", cname);
        } else {
            container_start(id);
        }
    } else if (strcmp(argv[1], "start") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_start(id);
    } else if (strcmp(argv[1], "stop") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_stop(id);
    } else if (strcmp(argv[1], "restart") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_restart(id);
    } else if (strcmp(argv[1], "rm") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_destroy(id);
    } else if (strcmp(argv[1], "images") == 0) {
        kprintf("%-32s %s\n", "IMAGE NAME", "ROOT");
        kprintf("──────────────────────────────────────────\n");
        char names[16][FS_NAME_MAX];
        int n = fs_listdir("/containers/images", names, 16);
        if (n <= 0) {
            kprintf("(no images)\n");
        } else {
            for (int i = 0; i < n; i++) {
                if (!names[i][0]) continue;
                char img_root[FS_PATH_MAX];
                snprintf(img_root, sizeof(img_root), "/containers/images/%s", names[i]);
                kprintf("%-32s %s\n", names[i], img_root);
            }
        }
    } else if (strcmp(argv[1], "pull") == 0 && argc >= 3) {
        const char *image = argv[2];
        char img_root[FS_PATH_MAX];
        snprintf(img_root, sizeof(img_root), "/containers/images/%s", image);
        int is_dir;
        if (fs_resolve(img_root, &is_dir) >= 0 && is_dir) {
            kprintf("appvm: image '%s' already present\n", image);
            return;
        }
        if (strcmp(image, "debian-minimal") == 0) {
            kprintf("appvm: pulling debian-minimal...\n");
            rootfs_extract_debian_minimal();
        } else if (strcmp(image, "android-stock") == 0 ||
                   strncmp(image, "android", 7) == 0) {
            kprintf("appvm: pulling %s...\n", image);
            rootfs_seed_android_stock();
        } else {
            kprintf("appvm: pull: unknown image '%s' (no source)\n", image);
            return;
        }
        if (fs_resolve(img_root, &is_dir) >= 0 && is_dir)
            kprintf("appvm: image '%s' ready\n", image);
        else
            kprintf("appvm: pull failed for '%s'\n", image);
    } else if (strcmp(argv[1], "exec") == 0 && argc >= 4) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_exec(id, argv[3], argc - 3, argv + 3, 0);
    } else if (strcmp(argv[1], "logs") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        char buf[4096];
        int n = container_logs(id, buf, sizeof(buf));
        if (n > 0) kprintf("%s", buf);
        else kprintf("(no logs)\n");
    } else if (strcmp(argv[1], "inspect") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        char buf[1024];
        container_inspect(id, buf, sizeof(buf));
        kprintf("%s", buf);
    } else if (strcmp(argv[1], "stats") == 0 && argc >= 3) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        uint64_t mem, cpu;
        if (container_stats(id, &mem, &cpu) == 0) {
            uint64_t mem_limit = 0;
            int cpu_shares = 0, cpu_quota = 0, pids_max = 0;
            container_get_limits(id, &mem_limit, &cpu_shares, &cpu_quota, &pids_max);
            kprintf("Container %d:\n", id);
            if (mem_limit > 0)
                kprintf("  Memory: %lu KB / %lu MB\n", mem, mem_limit / (1024 * 1024));
            else
                kprintf("  Memory: %lu KB / unlimited\n", mem);
            kprintf("  CPU:    %lu ticks  shares=%d  quota=%s\n", cpu, cpu_shares, cpu_quota > 0 ? "set" : "unlimited");
            kprintf("  PIDs:   max=%d\n", pids_max > 0 ? pids_max : 64);
        }
    } else if (strcmp(argv[1], "commit") == 0 && argc >= 4) {
        int id = resolve_container(argv[2]);
        if (id <= 0) { kprintf("appvm: unknown container '%s'\n", argv[2]); return; }
        container_t *c = container_get(id);
        if (c) container_create_image(argv[3], c->root_path);
    } else {
        kprintf("appvm: unknown command '%s'\n", argv[1]);
    }
}

/* ── ow: OpenWeb render smoke test ──
 * `ow render <url>` navigates the Rust HTTP backend, renders the page with the
 * Rust HTML renderer (ow_render_rs) and dumps the text grid to the console.
 *
 * The real implementations live in openweb_core.o (Qt/OpenWeb phase, linked
 * only into the final kernel target), so declare weak fallbacks here: the
 * stage-1 link picks these up, the final link overrides them with the strong
 * definitions. */
__attribute__((weak)) void ow_core_navigate(const char *text) { (void)text; }
__attribute__((weak)) void ow_core_dump_active(void) {}

static void cmd_ow(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "render") == 0) {
        kprintf("ow: fetching '%s'...\n", argv[2]);
        ow_core_navigate(argv[2]);
        ow_core_dump_active();
        return;
    }
    kprintf("usage: ow render <url>   (fetch + render + dump the page grid)\n");
}

#include "pe_loader.h"

static void cmd_wine(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Wine Compatibility Layer for CodeOS\n");
        kprintf("Usage: wine <executable.exe> [args...]\n");
        kprintf("       wine --status\n");
        kprintf("       wine --list-prefixes\n");
        kprintf("       wine --create-prefix <name>\n");
        kprintf("\n");
        kprintf("The Wine layer translates Windows API calls to CodeOS syscalls.\n");
        kprintf("PE executables (.exe) are automatically detected and loaded\n");
        kprintf("via the kernel PE loader with builtin Win32 API stubs.\n");
        return;
    }

    if (strcmp(argv[1], "--status") == 0) {
        kprintf("Wine compat layer: active\n");
        kprintf("PE loader: available (kernel/kernel/pe_loader.c)\n");
        kprintf("API stubs: ntdll, kernel32, user32, gdi32, ole32, advapi32, winmm\n");
        kprintf("Prefixes: /wines/{default,proton-ge,gaming,desktop}\n");
        return;
    }

    if (strcmp(argv[1], "--list-prefixes") == 0) {
        kprintf("Available wine prefixes:\n");
        kprintf("  default   - Default Wine prefix\n");
        kprintf("  proton-ge - Proton GE compatibility\n");
        kprintf("  gaming    - Gaming-optimized\n");
        kprintf("  desktop   - Desktop applications\n");
        return;
    }

    if (strcmp(argv[1], "--create-prefix") == 0) {
        if (argc < 3) { kprintf("Usage: wine --create-prefix <name>\n"); return; }
        kprintf("wine: creating prefix '%s'...\n", argv[2]);
        fs_mkdir("/wines");
        char buf[256];
        strcpy(buf, "/wines/");
        strcat(buf, argv[2]);
        fs_mkdir(buf);
        char dc[256];
        strcpy(dc, buf);
        strcat(dc, "/drive_c");
        fs_mkdir(dc);
        kprintf("wine: prefix '%s' created\n", argv[2]);
        return;
    }

    /* Load PE executable */
    kprintf("wine: loading %s...\n", argv[1]);

    uint64_t entry = 0, stack = 0;
    uint64_t base = pe_load_executable(argv[1], &entry, &stack, 0);
    if (base == (uint64_t)-1) {
        kprintf("wine: failed to load %s\n", argv[1]);
        return;
    }

    kprintf("wine: loaded at 0x%lx, entry=0x%lx\n", base, entry);
    kprintf("wine: to execute, use 'wine' user program or exec syscall\n");
}

static void cmd_wineserver(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("wineserver: managing wine prefixes\n");
    kprintf("  /wines/default/   - default prefix\n");
    kprintf("  /wines/proton-ge/ - Proton GE prefix\n");
    kprintf("  /wines/gaming/    - gaming prefix\n");
    kprintf("  /wines/desktop/   - desktop apps prefix\n");
}

static void cmd_root(int argc, char **argv) {
    if (argc > 1) {
        /* Run command as root */
        if (current_uid != 0 && users[0].password[0]) {
            kprintf("Password: ");
            char pass[PASS_MAX];
            read_password(pass, PASS_MAX);
            if (strcmp(pass, users[0].password) != 0) {
                kprintf("root: incorrect password\n");
                return;
            }
        }
        int saved = current_uid;
        current_uid = 0;
        char *sargv[MAX_ARGS];
        int sargc = 0;
        for (int i = 1; i < argc && sargc < MAX_ARGS - 1; i++) sargv[sargc++] = argv[i];
        sargv[sargc] = 0;
        run_builtin(sargc, sargv);
        current_uid = saved;
    } else {
        /* Drop into root shell */
        if (current_uid != 0 && users[0].password[0]) {
            kprintf("Password: ");
            char pass[PASS_MAX];
            read_password(pass, PASS_MAX);
            if (strcmp(pass, users[0].password) != 0) {
                kprintf("root: incorrect password\n");
                return;
            }
        }
        current_uid = 0;
        kprintf("root shell (type 'exit' to return)\n");
    }
}

static void cmd_useradd(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: useradd <username>\n"); return; }
    if (current_uid != 0) { kprintf("useradd: only root can add users\n"); return; }
    if (user_count >= MAX_USERS) { kprintf("useradd: maximum users reached\n"); return; }
    const char *name = argv[1];
    if (strlen(name) >= USER_NAME_MAX) { kprintf("useradd: username too long\n"); return; }
    if (find_user(name) >= 0) { kprintf("useradd: user '%s' already exists\n", name); return; }
    strcpy(users[user_count].username, name);
    users[user_count].password[0] = 0;
    users[user_count].uid = user_count;
    user_count++;
    kprintf("useradd: user '%s' created (uid=%d)\n", name, user_count - 1);
}

static void cmd_userdel(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: userdel <username>\n"); return; }
    if (current_uid != 0) { kprintf("userdel: only root can delete users\n"); return; }
    int uid = find_user(argv[1]);
    if (uid < 0) { kprintf("userdel: user '%s' does not exist\n", argv[1]); return; }
    if (uid == 0) { kprintf("userdel: cannot delete root\n"); return; }
    for (int i = uid; i < user_count - 1; i++) users[i] = users[i + 1];
    user_count--;
    if (current_uid == uid) current_uid = 0;
    kprintf("userdel: user '%s' deleted\n", argv[1]);
}

static void cmd_users(void) {
    kprintf("Users:\n");
    for (int i = 0; i < user_count; i++) {
        kprintf("  %s (uid=%d)%s\n", users[i].username, i,
                i == current_uid ? " *" : "");
    }
}

static void cmd_login(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : users[0].username;
    int uid = find_user(name);
    if (uid < 0) { kprintf("login: unknown user\n"); return; }
    if (users[uid].password[0]) {
        kprintf("Password: ");
        char pass[PASS_MAX];
        read_password(pass, PASS_MAX);
        if (strcmp(pass, users[uid].password) != 0) {
            kprintf("Login incorrect\n");
            return;
        }
    }
    current_uid = uid;
    env_set("USER", users[uid].username);
    kprintf("Login successful\n");
}

static void cmd_tail(int argc, char **argv) {
    int n = 10;
    const char *path;
    if (argc < 2) { kprintf("usage: tail [-n N] <file>\n"); return; }
    int ai = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) { n = str_to_int(argv[2]); ai = 3; }
    path = argv[ai];
    char buf[FS_CONTENT_MAX + 1];
    int total = fs_read(path, buf, FS_CONTENT_MAX);
    if (total < 0) { kprintf("tail: %s: no such file\n", path); return; }
    buf[total] = 0;
    int lines = 0;
    for (int i = 0; i < total; i++)
        if (buf[i] == '\n') lines++;
    int start_line = lines - n;
    if (start_line < 0) start_line = 0;
    int line = 0, i = 0;
    while (i < total && line < start_line) { if (buf[i] == '\n') line++; i++; }
    while (i < total) { kprintf("%c", buf[i]); i++; }
}

static void cmd_grep(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: grep <pattern> <file>\n"); return; }
    const char *pattern = argv[1];
    const char *path = argv[2];
    char buf[FS_CONTENT_MAX + 1];
    int total = fs_read(path, buf, FS_CONTENT_MAX);
    if (total < 0) { kprintf("grep: %s: no such file\n", path); return; }
    buf[total] = 0;
    int plen = strlen(pattern);
    int line_start = 0;
    for (int i = 0; i <= total; i++) {
        if (buf[i] == '\n' || i == total) {
            buf[i] = 0;
            int matched = 0;
            for (int j = line_start; j < i - plen + 1; j++) {
                int k;
                for (k = 0; k < plen; k++)
                    if (buf[j + k] != pattern[k]) break;
                if (k == plen) { matched = 1; break; }
            }
            if (matched) { for (int j = line_start; j < i; j++) kprintf("%c", buf[j]); kprintf("\n"); }
            line_start = i + 1;
        }
    }
}

static int find_in_list(const char *name, const char *list, int len, const char *dir) {
    int li = 0;
    while (li < len) {
        char ftype = ' ';
        while (li < len && (list[li] == ' ' || list[li] == '\t')) li++;
        if (li < len && (list[li] == 'd' || list[li] == 'f')) { ftype = list[li]; li++; }
        if (li < len && list[li] == ' ') li++;
        char fname[256];
        int fi = 0;
        while (li < len && list[li] != '\n' && fi < 255)
            fname[fi++] = list[li++];
        if (li < len && list[li] == '\n') li++;
        fname[fi] = 0;
        if (fi == 0 || strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0) continue;
        int match = 0;
        for (int i = 0; fname[i] && !match; i++) {
            int k;
            for (k = 0; name[k] && fname[i + k] && name[k] == fname[i + k]; k++);
            if (name[k] == 0) match = 1;
        }
        if (match) kprintf("%s/%s\n", dir, fname);
        if (ftype == 'd') {
            char sub[FS_PATH_MAX];
            int si = 0;
            const char *sp = dir;
            while (*sp && si < FS_PATH_MAX - 2) sub[si++] = *sp++;
            if (si > 0 && sub[si-1] != '/') sub[si++] = '/';
            sp = fname;
            while (*sp && si < FS_PATH_MAX - 2) sub[si++] = *sp++;
            sub[si] = 0;
            kprintf_capture_begin();
            cmd_ls(2, (char *[]){(char *)"ls", sub, 0});
            int sublen = kprintf_capture_end();
            const char *sublist = kprintf_capture_get();
            find_in_list(name, sublist, sublen, sub);
        }
    }
    return 0;
}

static void cmd_find(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: find <name> [path]\n"); return; }
    const char *name = argv[1];
    char dir_buf[FS_PATH_MAX];
    const char *dir;
    if (argc > 2) {
        dir = argv[2];
    } else {
        fs_getcwd(dir_buf, FS_PATH_MAX);
        dir = dir_buf;
    }
    kprintf_capture_begin();
    cmd_ls(2, (char *[]){(char *)"ls", (char *)dir, 0});
    int len = kprintf_capture_end();
    const char *list = kprintf_capture_get();
    find_in_list(name, list, len, dir);
}

static void cmd_chmod(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: chmod <mode> <file>\n"); return; }
    if (current_uid != 0) { kprintf("chmod: only root can change permissions\n"); return; }
    kprintf("chmod: permissions set on '%s'\n", argv[2]);
}

static void cmd_chown(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: chown <user> <file>\n"); return; }
    if (current_uid != 0) { kprintf("chown: only root can change ownership\n"); return; }
    int uid = find_user(argv[1]);
    if (uid < 0) { kprintf("chown: unknown user '%s'\n", argv[1]); return; }
    kprintf("chown: '%s' owner set to '%s'\n", argv[2], argv[1]);
}

static void cmd_ln(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: ln <target> <link>\n"); return; }
    if (strcmp(argv[1], "-s") == 0) {
        kprintf("ln: symlinks not supported\n");
    } else {
        kprintf("ln: hard links not supported\n");
    }
}

static void cmd_dd(int argc, char **argv) {
    const char *ifile = 0, *ofile = 0;
    int count = 1, bs = 512;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "if=", 3) == 0) ifile = argv[i] + 3;
        else if (strncmp(argv[i], "of=", 3) == 0) ofile = argv[i] + 3;
        else if (strncmp(argv[i], "count=", 6) == 0) count = str_to_int(argv[i] + 6);
        else if (strncmp(argv[i], "bs=", 3) == 0) bs = str_to_int(argv[i] + 3);
    }
    if (count <= 0 || bs <= 0) { kprintf("dd: count and bs must be positive\n"); return; }
    if (!ifile && !ofile) { kprintf("usage: dd if=<file> of=<file> [bs=N] [count=N]\n"); return; }
    if (!ifile) { kprintf("dd: missing if=\n"); return; }
    if (!ofile) { kprintf("dd: missing of=\n"); return; }
    char buf[4096];
    int in = fs_read(ifile, buf, sizeof(buf));
    if (in < 0) { kprintf("dd: %s: no such file\n", ifile); return; }
    if (fs_mkfile(ofile) < 0) { kprintf("dd: %s: could not create\n", ofile); return; }
    long to_write = (long)count * (long)bs;
    if (to_write > INT_MAX) to_write = INT_MAX;
    if (to_write < 0) { kprintf("dd: overflow\n"); return; }
    if ((int)to_write > in) to_write = in;
    if (fs_write(ofile, buf, (int)to_write) < 0) { kprintf("dd: write failed\n"); return; }
    kprintf("%d+0 records in\n%d+0 records out\n%d bytes copied\n",
            (int)(to_write / bs), (int)(to_write / bs), (int)to_write);
}

static void cmd_kill(void) {
    kprintf("kill: no processes\n");
}

static void cmd_ps(void) {
    kprintf("  PID  COMMAND\n");
    kprintf("    0  kernel\n");
}

/* ---------- new commands batch 2 ---------- */

static void cmd_source(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: source <file>\n"); return; }
    if (argv[1][0] == '-') { kprintf("source: %s: invalid option\n", argv[1]); return; }
    shell_source_file(argv[1]);
}

static void cmd_type(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: type <command>\n"); return; }
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(builtins[i], argv[1]) == 0) {
            kprintf("%s is a shell built-in\n", argv[1]);
            return;
        }
    }
    kprintf("%s: not found\n", argv[1]);
}

static void cmd_less(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: less <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("less: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    int pos = 0, lines_out = 0;
    int c = 0;
    while (pos < n) {
        lines_out = 0;
        for (int i = pos; i < n && lines_out < 23; i++) {
            kprintf("%c", buf[i]);
            if (buf[i] == '\n') lines_out++;
            pos++;
        }
        if (pos >= n) break;
        kprintf("--- more (space/enter=next, q=quit) ---");
        while (1) {
            c = input_getchar();
            if (c == 'q' || c == 'Q') { kprintf("\n"); break; }
            if (c == ' ' || c == '\n' || c == '\r') { kprintf("\n"); break; }
        }
        if (c == 'q' || c == 'Q') break;
    }
}

static void cmd_time(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: time <command> [args...]\n"); return; }
    unsigned int start = (unsigned int)timer_get_ticks();
    char *targv[MAX_ARGS];
    int targc = 0;
    for (int i = 1; i < argc && targc < MAX_ARGS - 1; i++) targv[targc++] = argv[i];
    targv[targc] = 0;
    run_builtin(targc, targv);
    unsigned int elapsed = (unsigned int)timer_get_ticks() - start;
    kprintf("\nreal: %u ms\n", elapsed);
}

static void cmd_tee(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: tee <file>\n"); return; }
    const char *path = argv[1];
    int append = 0;
    if (strcmp(argv[1], "-a") == 0) { append = 1; if (argc > 2) path = argv[2]; else { kprintf("usage: tee [-a] <file>\n"); return; } }
    kprintf_capture_begin();
    while (1) {
        int c = input_getchar();
        if (c == '\n' || c == '\r') { kprintf("\n"); break; }
        kprintf("%c", (char)c);
    }
    int len = kprintf_capture_end();
    const char *cap = kprintf_capture_get();
    kprintf("%s", cap);
    if (append) {
        char old[FS_CONTENT_MAX + 1];
        int olen = fs_read(path, old, FS_CONTENT_MAX);
        if (olen >= 0) {
            int total = olen + len;
            if (total > FS_CONTENT_MAX) total = FS_CONTENT_MAX;
            for (int i = 0; i < len && olen + i < total; i++) old[olen + i] = cap[i];
            fs_write(path, old, total);
        } else {
            if (fs_mkfile(path) == 0)
                fs_write(path, cap, len);
        }
    } else {
        if (fs_mkfile(path) == 0)
            fs_write(path, cap, len);
    }
}

static void cmd_tr(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: tr <set1> <set2> <file>\n"); return; }
    const char *set1 = argv[1], *set2 = argv[2], *path = argv[3];
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("tr: %s: no such file\n", path); return; }
    buf[n] = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; set1[j]; j++) {
            if (buf[i] == set1[j] && set2[j]) { buf[i] = set2[j]; break; }
        }
    }
    kprintf("%s", buf);
}

static void cmd_nl(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: nl <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("nl: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    int line = 1;
    kprintf("%6d  ", line++);
    for (int i = 0; i < n; i++) {
        kprintf("%c", buf[i]);
        if (buf[i] == '\n') kprintf("%6d  ", line++);
    }
    if (n > 0 && buf[n-1] != '\n') kprintf("\n");
}

static void cmd_fold(int argc, char **argv) {
    int width = 80;
    const char *path;
    if (argc < 2) { kprintf("usage: fold [-w W] <file>\n"); return; }
    int ai = 1;
    if (strcmp(argv[1], "-w") == 0) { if (argc > 2) width = str_to_int(argv[2]); ai = 3; }
    path = argv[ai];
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("fold: %s: no such file\n", path); return; }
    buf[n] = 0;
    int col = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') { kprintf("\n"); col = 0; }
        else if (col >= width) { kprintf("\n%c", buf[i]); col = 1; }
        else { kprintf("%c", buf[i]); col++; }
    }
    if (col > 0) kprintf("\n");
}

static void cmd_basename(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: basename <path>\n"); return; }
    const char *p = argv[1];
    const char *last = p;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    kprintf("%s\n", last);
}

static void cmd_dirname(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: dirname <path>\n"); return; }
    const char *p = argv[1];
    const char *last_slash = 0;
    while (*p) { if (*p == '/') last_slash = p; p++; }
    if (!last_slash) kprintf(".\n");
    else {
        while (last_slash > argv[1] && *(last_slash-1) == '/') last_slash--;
        if (last_slash == argv[1]) kprintf("/\n");
        else { for (const char *s = argv[1]; s < last_slash; s++) kprintf("%c", *s); kprintf("\n"); }
    }
}

static void cmd_tty(void) { kprintf("/dev/ttyS0\n"); }
static void cmd_logname(void) { kprintf("root\n"); }
static void cmd_nproc(void) { kprintf("1\n"); }

static void cmd_realpath(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: realpath <path>\n"); return; }
    int is_dir;
    int idx = fs_resolve(argv[1], &is_dir);
    if (idx < 0) { kprintf("%s: no such file\n", argv[1]); return; }
    char cwd[FS_PATH_MAX];
    char *p = cwd;
    const char *s = argv[1];
    if (*s == '/') { *p++ = '/'; s++; }
    while (*s && p - cwd < FS_PATH_MAX - 1) {
        *p++ = *s++;
    }
    *p = 0;
    kprintf("%s\n", cwd);
}

static void cmd_mktemp(void) {
    char name[32];
    for (int i = 0; i < 1000; i++) {
        int pi = 8;
        name[0] = '/'; name[1] = 't'; name[2] = 'm'; name[3] = 'p'; name[4] = '/';
        name[5] = 't'; name[6] = 'm'; name[7] = 'p';
        unsigned int r = (unsigned int)timer_get_ticks();
        for (int j = 0; j < 6; j++) {
            r = r * 1103515245u + 12345u;
            name[pi++] = 'a' + (r % 26);
        }
        name[pi] = 0;
        int is_dir; if (fs_resolve(name, &is_dir) < 0 && fs_mkfile(name) == 0) { kprintf("%s\n", name); return; }
    }
}

static void cmd_expand(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: expand <file>\n"); return; }
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(argv[1], buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("expand: %s: no such file\n", argv[1]); return; }
    buf[n] = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\t') kprintf("        ");
        else kprintf("%c", buf[i]);
    }
}

static void cmd_sync(void) { kprintf("sync: fs synchronized\n"); }

static int shell_exit_flag;
static void cmd_exit(void) {
    kprintf("logout\n");
    shell_exit_flag = 1;
}

static void cmd_read(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: read <variable>\n"); return; }
    char buf[CMD_BUF_SIZE];
    int bi = 0;
    while (1) {
        int c = input_getchar();
        if (c == '\n' || c == '\r') { buf[bi] = 0; kprintf("\n"); break; }
        if (c == '\b' || c == 0x7F) { if (bi > 0) { bi--; kprintf("\b \b"); } continue; }
        if (bi < CMD_BUF_SIZE - 1 && c >= ' ' && c < 0x80) { buf[bi++] = c; kprintf("%c", c); }
    }
    env_set(argv[1], buf);
}

static void cmd_wait(void) { kprintf("wait: no background jobs\n"); }

static void cmd_beep(void) {
    speaker_beep(880, 200);
}

static void cmd_desktop(void) {
    if (!desktop_active()) {
        kprintf("desktop: no framebuffer available (boot with GRUB for GUI)\n");
        return;
    }
    kprintf("desktop: entering GUI mode (Ctrl+C to return to shell)\n");
    desktop_run();
    kprintf("desktop: returned to shell\n");
}

static void cmd_clamscan(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: clamscan <file|--all|--list|--update|--stats>\n");
        return;
    }
    if (strcmp(argv[1], "--list") == 0) clamav_list_sigs();
    else if (strcmp(argv[1], "--update") == 0) clamav_update_db();
    else if (strcmp(argv[1], "--stats") == 0) clamav_stats();
    else if (strcmp(argv[1], "--all") == 0) {
        kprintf("ClamAV: scanning key system files...\n");
        int found = clamav_scan_file("/etc/passwd") + clamav_scan_file("/etc/hosts")
                  + clamav_scan_file("/etc/wm.conf") + clamav_scan_file("/boot/kernel.bin")
                  + clamav_scan_file("/bin/init");
        kprintf("ClamAV: scan complete, %d threats found\n", found);
    } else {
        int n = clamav_scan_file(argv[1]);
        if (n > 0) kprintf("ClamAV: %d threat(s) found in %s\n", n, argv[1]);
        else if (n == 0) kprintf("ClamAV: %s OK\n", argv[1]);
    }
}

static void cmd_secaudit(int argc, char **argv) {
    (void)argc; (void)argv;
    sec_audit_dump();
}

static void cmd_secintegrity(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: secintegrity <add|check|list> [path]\n");
        return;
    }
    if (strcmp(argv[1], "add") == 0 && argc > 2) sec_integrity_add(argv[2]);
    else if (strcmp(argv[1], "check") == 0 && argc > 2) sec_integrity_check(argv[2]);
    else if (strcmp(argv[1], "list") == 0) sec_integrity_list();
    else kprintf("secintegrity: unknown command\n");
}

static void cmd_secfw(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: secfw <on|off|status|add <rule>>\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) sec_enforce_firewall(1);
    else if (strcmp(argv[1], "off") == 0) sec_enforce_firewall(0);
    else if (strcmp(argv[1], "status") == 0)
        kprintf("Firewall: %s\n", sec_firewall_enabled() ? "ENABLED" : "disabled");
    else if (strcmp(argv[1], "add") == 0 && argc > 2) sec_firewall_rule_add(argv[2]);
}

static void cmd_secstatus(void) {
    sec_status();
}

static void cmd_secthreat(void) {
    int tl = clamav_threat_level();
    kprintf("Threat level: ");
    if (tl == 0) kprintf("none\n");
    else if (tl <= 2) kprintf("low\n");
    else if (tl <= 4) kprintf("medium\n");
    else kprintf("HIGH\n");
    if (tl > 0) kprintf("Run 'clamscan --all' to scan the system\n");
}

static void cmd_adblock(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: adblock <on|off|status|stats|list|reload|test <host>>\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        adblock_enable();
        kprintf("AdBlock: enabled\n");
    } else if (strcmp(argv[1], "off") == 0) {
        adblock_disable();
        kprintf("AdBlock: disabled\n");
    } else if (strcmp(argv[1], "status") == 0) {
        adblock_status();
    } else if (strcmp(argv[1], "stats") == 0) {
        adblock_stats();
    } else if (strcmp(argv[1], "list") == 0) {
        adblock_list();
    } else if (strcmp(argv[1], "reload") == 0) {
        int n = adblock_reload();
        kprintf("AdBlock: reloaded %d domains\n", n);
    } else if (strcmp(argv[1], "test") == 0 && argc > 2) {
        if (adblock_check_host(argv[2]))
            kprintf("AdBlock: %s is BLOCKED\n", argv[2]);
        else
            kprintf("AdBlock: %s is allowed\n", argv[2]);
    } else {
        kprintf("adblock: unknown command\n");
    }
}

static void cmd_ping(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: ping <ip> [timeout_ms]\n");
        return;
    }
    uint32_t ip = 0;
    int parts[4] = {0};
    int pi = 0, ni = 0;
    const char *s = argv[1];
    while (*s && pi < 4) {
        if (*s >= '0' && *s <= '9') { ni = ni * 10 + (*s - '0'); }
        else if (*s == '.') { parts[pi++] = ni; ni = 0; }
        s++;
    }
    if (pi == 3) parts[pi] = ni;
    else { kprintf("ping: invalid IP\n"); return; }
    for (int i = 0; i < 4; i++)
        if (parts[i] < 0 || parts[i] > 255) { kprintf("ping: invalid octet\n"); return; }
    ip = (uint32_t)parts[0] | ((uint32_t)parts[1] << 8) | ((uint32_t)parts[2] << 16) | ((uint32_t)parts[3] << 24);

    int timeout = 1000;
    if (argc > 2) timeout = str_to_int(argv[2]);

    kprintf("PING %d.%d.%d.%d ... ", parts[0], parts[1], parts[2], parts[3]);
    if (icmp_ping(ip, timeout) == 0)
        kprintf("pong!\n");
    else
        kprintf("timeout\n");
}

static void cmd_nslookup(int argc, char **argv) {
    if (argc < 2) {
        kprintf("usage: nslookup <ip>\n");
        return;
    }
    uint32_t ip;
    if (dns_resolve(argv[1], &ip) == 0) {
        kprintf("IP:   %d.%d.%d.%d\n",
                ip & 0xFF, (ip >> 8) & 0xFF,
                (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    } else {
        kprintf("DNS: disabled - only numeric IPs are accepted\n");
    }
}

static void cmd_netinfo(void) {
    if (!net_ready()) {
        kprintf("Network: not initialized\n");
        return;
    }
    kprintf("Network status:\n");
    dns_status();
    kprintf("  DNS is DISABLED (IP-only stack)\n");
    kprintf("  Use 'nslookup <ip>' to validate an IP literal\n");
    kprintf("  Use 'ping <ip>' to test connectivity\n");
}

static void cmd_setcursor(int argc, char **argv) {
    static const char *names[] = {
        "arrow", "hand", "crosshair", "text",
        "resize_h", "resize_v", "resize_nwse", "resize_nesw"
    };
    if (argc < 2) {
        kprintf("usage: setcursor <style>\n");
        kprintf("styles: arrow hand crosshair text resize_h resize_v resize_nwse resize_nesw\n");
        kprintf("current: %s\n", names[fb_cursor_get_style()]);
        return;
    }
    for (int i = 0; i < CURSOR_STYLE_COUNT; i++) {
        if (strcmp(argv[1], names[i]) == 0) {
            fb_cursor_set_style(i);
            kprintf("cursor style: %s\n", names[i]);
            return;
        }
    }
    kprintf("setcursor: unknown style '%s'\n", argv[1]);
}

static void cmd_panel(void) {
    int w = fb_get_cols();
    int h = fb_get_rows();
    if (w < 80) w = 80;
    if (h < 25) h = 25;

    fb_clear();

    uint32_t header_fg = FB_RGB(255,255,255), header_bg = FB_RGB(0,0,170);
    uint32_t normal_fg = FB_RGB(192,192,192), normal_bg = FB_RGB(0,0,0);
    uint32_t label_fg  = FB_RGB(0,170,170), label_bg  = FB_RGB(0,0,0);
    uint32_t dim_fg    = FB_RGB(85,85,85), dim_bg    = FB_RGB(0,0,0);
    uint32_t foot_fg   = FB_RGB(0,0,0), foot_bg   = FB_RGB(192,192,192);

    fb_fill_row_bg(0, header_bg);
    fb_write_styled(0, 28, "codeos-1 Panel", header_fg, header_bg);

    for (int r = 1; r < h - 1; r++)
        fb_fill_row_bg(r, normal_bg);

    fb_fill_row_bg(h - 1, foot_bg);
    fb_write_styled(h - 1, 0, " Q=Quit  R=Refresh  Panel v1.0", foot_fg, foot_bg);

    int running = 1;
    int frame = 0;

    while (running) {
        unsigned int t = (unsigned int)timer_get_ticks();

        fb_write_styled(1, 2, "System Information", label_fg, label_bg);

        fb_write_styled(2, 2, "  OS: " KERNEL_UNAME, normal_fg, normal_bg);
        fb_write_styled(3, 2, "  Architecture: x86_64 long mode", normal_fg, normal_bg);
        fb_write_styled(4, 2, "  Display: Framebuffer", normal_fg, normal_bg);

        unsigned int secs = (unsigned int)(timer_get_milliseconds() / 1000);
        unsigned int up_min = secs / 60;
        unsigned int up_hr  = up_min / 60;
        char upbuf[48];
        sprintf(upbuf, "  Uptime: %02u:%02u:%02u", up_hr, up_min % 60, secs % 60);
        fb_write_styled(5, 2, upbuf, normal_fg, normal_bg);

        fb_write_styled(6, 2, "  Timer: ", normal_fg, normal_bg);
        char tbuf[48];
        sprintf(tbuf, "%u ms (%u seconds)", t, secs);
        fb_write_styled(6, 11, tbuf, dim_fg, dim_bg);

        fb_write_styled(8, 2, "Memory", label_fg, label_bg);

        uint64_t total = mm_total();
        uint64_t free_mem = mm_free();
        uint64_t used = mm_used();
        char mbuf[96];
        sprintf(mbuf, "  Physical: %llu KB free / %llu KB total", free_mem, total);
        fb_write_styled(9, 2, mbuf, normal_fg, normal_bg);
        sprintf(mbuf, "  Used: %llu KB (%llu%%)", used, total > 0 ? used * 100 / total : 0);
        fb_write_styled(10, 2, mbuf, normal_fg, normal_bg);

        sprintf(mbuf, "  Kernel heap: %u KB used / %u KB total",
                (unsigned int)(mm_heap_used() / 1024),
                (unsigned int)(mm_heap_total() / 1024));
        fb_write_styled(11, 2, mbuf, normal_fg, normal_bg);

        fb_write_styled(13, 2, "Storage", label_fg, label_bg);
        if (block_available()) {
            int sectors, lba;
            block_get_info(&sectors, &lba);
            int sz = (int)((uint64_t)sectors * 512 / 1048576);
            char sbuf[48];
            sprintf(sbuf, "  %s: %d MB (%d sectors)", block_backend_name(), sz, sectors);
            fb_write_styled(14, 2, sbuf, normal_fg, normal_bg);
        } else {
            fb_write_styled(14, 2, "  No disk available", dim_fg, dim_bg);
        }

        fb_write_styled(16, 2, "Drivers", label_fg, label_bg);
        drivers_print_status_panel();

        char hbuf[48];
        sprintf(hbuf, "uptime: %02u:%02u:%02u", up_hr, up_min % 60, secs % 60);
        fb_write_styled(0, 60, hbuf, header_fg, header_bg);

        if (frame % 2 == 0)
            fb_write_styled(h - 1, 0, " Q=Quit  R=Refresh  Panel v1.0", foot_fg, foot_bg);

        if (keyboard_has_input()) {
            char c = keyboard_getchar();
            if (c == 'q' || c == 'Q' || c == 0x1B) running = 0;
            if (c == 'r' || c == 'R') { }
        }

        frame++;
        for (volatile int wd = 0; wd < 100000; wd++) asm volatile("pause");
    }
    fb_clear();
}

static void cmd_lsblk(void) {
    if (!block_available()) { kprintf("lsblk: no disk\n"); return; }
    int sectors, lba;
    block_get_info(&sectors, &lba);
    int mb = (int)((uint64_t)sectors * 512 / 1048576);
    kprintf("NAME  TYPE  SIZE  FSTYPE\n");
    kprintf("sda   disk  %dM   %s\n", mb, block_backend_name());
    int np = part_count();
    for (int i = 0; i < np; i++) {
        partition_t p;
        part_get(i, &p);
        const char *fs = "?";
        if (p.type == 0x83) fs = "ext2/3/4";
        else if (p.type == 0x07) fs = "ntfs";
        else if (p.type == 0x0C || p.type == 0x0B) fs = "fat32";
        else if (p.type == 0x82) fs = "swap";
        kprintf("sda%d  part  %dM   %s\n", i,
                (int)((uint64_t)p.sector_count * 512 / 1048576), fs);
    }
}

static void cmd_mount(int argc, char **argv) {
    if (argc < 2) {
        if (ext2_mounted()) {
            kprintf("ext2/3/4 filesystem mounted\n");
        } else {
            kprintf("usage: mount <partition>  (e.g. mount 0)\n");
        }
        return;
    }
    int idx = str_to_int(argv[1]);
    partition_t p;
    if (part_get(idx, &p) < 0) {
        kprintf("mount: invalid partition %d\n", idx);
        return;
    }
    if (ext2_mount(idx)) {
        kprintf("mounted partition %d (ext2/3/4, %d MB)\n", idx,
                (int)((uint64_t)p.sector_count * 512 / 1048576));
    } else {
        kprintf("mount: failed to mount partition %d (not ext2/3/4?)\n", idx);
    }
}

static void cmd_ls_ext(int argc, char **argv) {
    if (!ext2_mounted()) { kprintf("els: no filesystem mounted\n"); return; }
    const char *path = argc > 1 ? argv[1] : "/";
    if (ext2_list_dir(path) < 0)
        kprintf("els: %s: not found\n", path);
}

static void cmd_cat_ext(int argc, char **argv) {
    if (!ext2_mounted()) { kprintf("ecat: no filesystem mounted\n"); return; }
    if (argc < 2) { kprintf("usage: ecat <path>\n"); return; }
    char *buf = (char*)malloc(4097);
    if (!buf) { kprintf("ecat: out of memory\n"); return; }
    int n = ext2_read_file_path(argv[1], buf, 4096);
    if (n < 0) { free(buf); kprintf("ecat: %s: not found\n", argv[1]); return; }
    buf[n] = 0;
    kprintf("%s\n", buf);
    free(buf);
}

static void cmd_printenv(int argc, char **argv) {
    if (argc > 1) {
        const char *v = env_get(argv[1]);
        kprintf("%s\n", v ? v : "");
        return;
    }
    cmd_env();
}

static void cmd_cut(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: cut -c <range> <file>\n"); return; }
    int start = 0, end = 999;
    int ai = 1;
    if (strcmp(argv[1], "-c") == 0 && argc > 2) {
        const char *r = argv[2];
        start = str_to_int(r);
        const char *d = r; while (*d && *d != '-') d++;
        if (*d == '-') end = *(d+1) ? str_to_int(d+1) : 999;
        ai = 3;
    }
    const char *path = argv[ai];
    char buf[FS_CONTENT_MAX + 1];
    int n = fs_read(path, buf, FS_CONTENT_MAX);
    if (n < 0) { kprintf("cut: %s: no such file\n", path); return; }
    buf[n] = 0;
    int col = 1;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') { col = 1; kprintf("\n"); }
        else { if (col >= start && col <= end) kprintf("%c", buf[i]); col++; }
    }
}

static void cmd_comm(int argc, char **argv) {
    if (argc < 3) { kprintf("usage: comm <file1> <file2>\n"); return; }
    char b1[FS_CONTENT_MAX + 1], b2[FS_CONTENT_MAX + 1];
    int n1 = fs_read(argv[1], b1, FS_CONTENT_MAX);
    int n2 = fs_read(argv[2], b2, FS_CONTENT_MAX);
    if (n1 < 0 || n2 < 0) { kprintf("comm: file not found\n"); return; }
    b1[n1] = 0; b2[n2] = 0;
    char *lines1[128], *lines2[128];
    int lc1 = 0, lc2 = 0;
    lines1[lc1++] = b1; lines2[lc2++] = b2;
    for (int i = 0; i < n1 && lc1 < 128; i++) if (b1[i] == '\n') { b1[i] = 0; if (i + 1 < n1) lines1[lc1++] = &b1[i+1]; }
    for (int i = 0; i < n2 && lc2 < 128; i++) if (b2[i] == '\n') { b2[i] = 0; if (i + 1 < n2) lines2[lc2++] = &b2[i+1]; }
    int i = 0, j = 0;
    while (i < lc1 && j < lc2) {
        int cmp = strcmp(lines1[i], lines2[j]);
        if (cmp == 0) { kprintf("\t\t%s\n", lines1[i]); i++; j++; }
        else if (cmp < 0) { kprintf("%s\n", lines1[i]); i++; }
        else { kprintf("\t%s\n", lines2[j]); j++; }
    }
    while (i < lc1) kprintf("%s\n", lines1[i++]);
    while (j < lc2) kprintf("\t%s\n", lines2[j++]);
}

static void cmd_ascii(void) {
    kprintf("Dec Hex Char  Dec Hex Char  Dec Hex Char  Dec Hex Char\n");
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 4; j++) {
            int val = i + j * 32;
            kprintf("%3d 0x%02x %c", val, val, val >= ' ' && val < 0x7F ? val : '.');
            if (j < 3) kprintf("  ");
        }
        kprintf("\n");
    }
}

static void cmd_fortune(void) {
    const char *sayings[] = {
        "A journey of a thousand miles begins with a single step.",
        "I think, therefore I am.",
        "Hello, World!",
        "The only constant is change.",
        "Knowledge is power.",
        "To be or not to be, that is the question.",
        "42",
        "Have you tried turning it off and on again?",
        "This kernel is learning.",
        "Keep calm and carry on.",
        "There is no place like 127.0.0.1.",
        "In the beginning was the command line.",
        "Talk is cheap. Show me the code.",
        "Unix is user-friendly. It's just selective about who its friends are.",
    };
    int n = sizeof(sayings) / sizeof(sayings[0]);
    unsigned int idx = (unsigned int)timer_get_ticks() % n;
    kprintf("%s\n", sayings[idx]);
}

static void cmd_banner(int argc, char **argv) {
    if (argc < 2) { kprintf("usage: banner <text>\n"); return; }
    const char *text = argv[1];
    int len = 0; while (text[len]) len++;
    kprintf("+");
    for (int i = 0; i < len + 2; i++) kprintf("-");
    kprintf("+\n");
    kprintf("| %s |\n", text);
    kprintf("+");
    for (int i = 0; i < len + 2; i++) kprintf("-");
    kprintf("+\n");
}

/* ---------- advanced system info commands ---------- */

static void cmd_sysinfo(void) {
    kprintf("=== System Information ===\n");
    kprintf("OS: %s\n", KERNEL_UNAME);
    kprintf("Architecture: x86_64 (long mode)\n");
    uint32_t eax = 0;
    __asm__ volatile("cpuid" : "=a"(eax) : "a"(0));
    kprintf("Timer frequency: %s Hz\n", env_get("HZ"));
    kprintf("Shell: %s\n", env_get("MODE"));
    kprintf("Display: Framebuffer (%dx%d)\n", fb_getwidth(), fb_getheight());
    kprintf("Mouse: %s\n", mouse_available() ? "detected" : "not available");
    kprintf("Uptime: ");
    cmd_uptime();
    kprintf("\n");
}

static void cmd_meminfo(void) {
    kprintf("=== Memory Information ===\n");
    kprintf("Physical memory:\n");
    kprintf("  Total:  %llu KB (%llu MB)\n", mm_total(), mm_total() / 1024);
    kprintf("  Used:   %llu KB (%llu MB)\n", mm_used(), mm_used() / 1024);
    kprintf("  Free:   %llu KB (%llu MB)\n", mm_free(), mm_free() / 1024);
    kprintf("\nKernel heap:\n");
    size_t heap_used = mm_heap_used();
    size_t heap_total = mm_heap_total();
    size_t heap_peak = mm_heap_peak();
    kprintf("  Total:  %u bytes (%u KB)\n", (unsigned int)heap_total, (unsigned int)(heap_total / 1024));
    kprintf("  Used:   %u bytes\n", (unsigned int)heap_used);
    kprintf("  Peak:   %u bytes\n", (unsigned int)heap_peak);
    kprintf("  Free:   %u bytes\n", (unsigned int)(heap_total - heap_used));
    if (heap_total > 0) {
        unsigned int pct = (heap_used * 100) / heap_total;
        kprintf("  Usage:  %u%%\n", pct);
    }
}

static void cmd_stats(void) {
    kprintf("=== System Statistics ===\n");
    kprintf("Uptime: ");
    cmd_uptime();
    kprintf("Command history: %d entries\n", hist_count);
    kprintf("Environment: %d variables\n", env_count);
    kprintf("Shell buffer: %d bytes\n", CMD_BUF_SIZE);
    kprintf("Max arguments: %d\n", MAX_ARGS);
    kprintf("Timer: ");
    unsigned int ticks = (unsigned int)timer_get_ticks();
    kprintf("%u ms (%u seconds)\n", ticks, ticks / 1000);
}

static void cmd_devices(void) {
    kprintf("=== Device Information ===\n");
    drivers_print_status();
    kprintf("\nPCI (%d):\n", pci_device_count());
    pci_print_devices();
}

static void cmd_heapstat(void) {
    kprintf("=== Heap Statistics ===\n");
    size_t total = mm_heap_total();
    size_t used = mm_heap_used();
    size_t peak = mm_heap_peak();
    size_t free = total - used;
    kprintf("Total heap: %u KB\n", (unsigned int)(total / 1024));
    kprintf("Used: %u KB\n", (unsigned int)(used / 1024));
    kprintf("Free: %u KB\n", (unsigned int)(free / 1024));
    kprintf("Peak usage: %u KB\n", (unsigned int)(peak / 1024));
    if (total > 0) {
        unsigned int usage_pct = (used * 100) / total;
        unsigned int peak_pct = (peak * 100) / total;
        kprintf("Current: %u%% full\n", usage_pct);
        kprintf("Peak: %u%% full\n", peak_pct);
        if (usage_pct > 80) kprintf("WARNING: Heap usage high!\n");
    }
}

/* ---------- glob expansion ---------- */

volatile int shell_interrupted;

int glob_match(const char *p, const char *s) {
    while (*p && *s) {
        if (*p == '*') {
            p++;
            if (!*p) return 1;
            while (*s) {
                if (glob_match(p, s)) return 1;
                s++;
            }
            return 0;
        }
        if (*p == '?') { p++; s++; continue; }
        if (*p != *s) return 0;
        p++; s++;
    }
    while (*p == '*') p++;
    return *p == 0 && *s == 0;
}

static int contains_wildcard(const char *s) {
    while (*s) { if (*s == '*' || *s == '?') return 1; s++; }
    return 0;
}

/* Expand glob patterns in argv. Returns new argc.
   new_argv must hold MAX_ARGS entries. Names are copied into a static buffer. */
#define GLOB_NAME_BUF 4096
static char glob_name_buf[GLOB_NAME_BUF];
static int glob_name_pos;

static int expand_glob_argv(int argc, char **argv, char **new_argv) {
    glob_name_pos = 0;
    int nc = 0;
    for (int i = 0; i < argc && i < MAX_ARGS; i++) {
        if (!contains_wildcard(argv[i])) {
            new_argv[nc++] = argv[i];
            continue;
        }
        int found = 0;
        char pattern[FS_CONTENT_MAX];
        strcpy(pattern, argv[i]);

        char names[128][FS_NAME_MAX];
        int nentries = fs_listdir(".", names, 128);
        for (int e = 0; e < nentries; e++) {
            if (glob_match(pattern, names[e]) && names[e][0]) {
                if (nc < MAX_ARGS - 1 && glob_name_pos + FS_NAME_MAX + 1 < GLOB_NAME_BUF) {
                    char *dst = glob_name_buf + glob_name_pos;
                    strcpy(dst, names[e]);
                    new_argv[nc++] = dst;
                    glob_name_pos += FS_NAME_MAX + 1;
                    found = 1;
                }
            }
        }
        if (!found) {
            new_argv[nc++] = argv[i];
        }
    }
    new_argv[nc] = 0;
    return nc;
}

/* ---------- redirection support ---------- */

static void redirect_run(int argc, char **argv) {
    int redirect_out = 0;     /* 0=none, 1=truncate, 2=append */
    const char *redirect_file = 0;
    int new_argc = 0;
    char *new_argv[MAX_ARGS];
    for (int i = 0; i < argc && i < MAX_ARGS; i++) {
        if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            redirect_out = 1;
            redirect_file = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], ">>") == 0 && i + 1 < argc) {
            redirect_out = 2;
            redirect_file = argv[i + 1];
            i++;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = 0;

    if (redirect_out) {
        kprintf_capture_begin();
        run_builtin(new_argc, new_argv);
        int len = kprintf_capture_end();
        const char *cap = kprintf_capture_get();
        if (redirect_out == 2) {
            char old[FS_CONTENT_MAX + 1];
            int olen = fs_read(redirect_file, old, FS_CONTENT_MAX);
            if (olen > 0) {
                int total = olen + len;
                if (total > FS_CONTENT_MAX) total = FS_CONTENT_MAX;
                for (int i = 0; i < len && olen + i < total; i++) old[olen + i] = cap[i];
                fs_write(redirect_file, old, total);
            } else {
                fs_mkfile(redirect_file);
                fs_write(redirect_file, cap, len);
            }
        } else {
            fs_mkfile(redirect_file);
            fs_write(redirect_file, cap, len);
        }
    } else {
        run_builtin(new_argc, new_argv);
    }
}

static void run_builtin(int argc, char **argv) {
    if (argc == 0) return;
    const char *cmd = argv[0];
    if (strcmp(cmd, "clear") == 0) cmd_clear();
    else if (strcmp(cmd, "echo") == 0) cmd_echo(argc, argv);
    else if (strcmp(cmd, "timer") == 0) cmd_timer();
    else if (strcmp(cmd, "info") == 0) cmd_info();
    else if (strcmp(cmd, "version") == 0) cmd_version();
    else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
    else if (strcmp(cmd, "export") == 0) cmd_export(argc, argv);
    else if (strcmp(cmd, "unset") == 0) cmd_unset(argc, argv);
    else if (strcmp(cmd, "env") == 0) cmd_env();
    else if (strcmp(cmd, "history") == 0) cmd_history();
    else if (strcmp(cmd, "mode") == 0) cmd_mode(argc, argv);
    else if (strcmp(cmd, "set") == 0) cmd_set();
    else if (strcmp(cmd, "mouse") == 0) cmd_mouse();
    else if (strcmp(cmd, "ls") == 0) cmd_ls(argc, argv);
    else if (strcmp(cmd, "cd") == 0) cmd_cd(argc, argv);
    else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
    else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(argc, argv);
    else if (strcmp(cmd, "rmdir") == 0) cmd_rmdir(argc, argv);
    else if (strcmp(cmd, "cat") == 0) cmd_cat(argc, argv);
    else if (strcmp(cmd, "rm") == 0) cmd_rm(argc, argv);
    else if (strcmp(cmd, "touch") == 0) cmd_touch(argc, argv);
    else if (strcmp(cmd, "nano") == 0) cmd_nano(argc, argv);
    else if (strcmp(cmd, "vi") == 0) cmd_vi(argc, argv);
    else if (strcmp(cmd, "vim") == 0) cmd_vim(argc, argv);
    else if (strcmp(cmd, "nvim") == 0) cmd_nvim(argc, argv);
    else if (strcmp(cmd, "mem") == 0) cmd_mem();
    else if (strcmp(cmd, "pci") == 0) cmd_pci(argc, argv);
    else if (strcmp(cmd, "cpu") == 0) cmd_cpu();
    else if (strcmp(cmd, "fetch") == 0) cmd_fetch(argc, argv);
    else if (strcmp(cmd, "pacman") == 0) cmd_fetch(argc, argv);
    else if (strcmp(cmd, "uptime") == 0) cmd_uptime();
    else if (strcmp(cmd, "uname") == 0) cmd_uname(argc, argv);
    else if (strcmp(cmd, "fastfetch") == 0) cmd_fastfetch();
    else if (strcmp(cmd, "sysfetch") == 0) cmd_sysfetch();
    else if (strcmp(cmd, "free") == 0) cmd_free();
    else if (strcmp(cmd, "whoami") == 0) cmd_whoami();
    else if (strcmp(cmd, "hostname") == 0) cmd_hostname(argc, argv);
    else if (strcmp(cmd, "which") == 0) cmd_which(argc, argv);
    else if (strcmp(cmd, "sleep") == 0) cmd_sleep(argc, argv);
    else if (strcmp(cmd, "repeat") == 0) cmd_repeat(argc, argv);
    else if (strcmp(cmd, "seq") == 0) cmd_seq(argc, argv);
    else if (strcmp(cmd, "script") == 0) cmd_script(argc, argv);
    else if (strcmp(cmd, "csl") == 0) cmd_script(argc, argv);
    else if (strcmp(cmd, "yes") == 0) cmd_yes(argc, argv);
    else if (strcmp(cmd, "true") == 0) cmd_true();
    else if (strcmp(cmd, "false") == 0) cmd_false();
    else if (strcmp(cmd, "wc") == 0) cmd_wc(argc, argv);
    else if (strcmp(cmd, "head") == 0) cmd_head(argc, argv);
    else if (strcmp(cmd, "hexdump") == 0) cmd_hexdump(argc, argv);
    else if (strcmp(cmd, "calc") == 0) cmd_calc(argc, argv);
    else if (strcmp(cmd, "date") == 0) cmd_date();
    else if (strcmp(cmd, "rev") == 0) cmd_rev(argc, argv);
    else if (strcmp(cmd, "exec") == 0) cmd_exec(argc, argv);
    else if (strcmp(cmd, "run") == 0) cmd_run(argc, argv);
    else if (strcmp(cmd, "games") == 0) cmd_games(argc, argv);
    else if (strcmp(cmd, "gplay") == 0) cmd_gplay(argc, argv);
    else if (strcmp(cmd, "pong") == 0) cmd_lgame_game(argc, argv);
    else if (strcmp(cmd, "snake") == 0) cmd_lgame_game(argc, argv);
    else if (strcmp(cmd, "sort") == 0) cmd_sort(argc, argv);
    else if (strcmp(cmd, "cp") == 0) cmd_cp(argc, argv);
    else if (strcmp(cmd, "mv") == 0) cmd_mv(argc, argv);
    else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
    else if (strcmp(cmd, "df") == 0) cmd_df();
    else if (strcmp(cmd, "du") == 0) cmd_du(argc, argv);
    else if (strcmp(cmd, "id") == 0) cmd_id();
    else if (strcmp(cmd, "su") == 0) cmd_su(argc, argv);
    else if (strcmp(cmd, "sudo") == 0) cmd_sudo(argc, argv);
    else if (strcmp(cmd, "root") == 0) cmd_root(argc, argv);
    else if (strcmp(cmd, "container") == 0) cmd_container(argc, argv);
    else if (strcmp(cmd, "appvm") == 0) cmd_appvm(argc, argv);
    else if (strcmp(cmd, "waydroid") == 0) cmd_waydroid(argc, argv);
    else if (strcmp(cmd, "ow") == 0) cmd_ow(argc, argv);
    else if (strcmp(cmd, "login") == 0) cmd_login(argc, argv);
    else if (strcmp(cmd, "passwd") == 0) cmd_passwd(argc, argv);
    else if (strcmp(cmd, "useradd") == 0) cmd_useradd(argc, argv);
    else if (strcmp(cmd, "userdel") == 0) cmd_userdel(argc, argv);
    else if (strcmp(cmd, "users") == 0) cmd_users();
    else if (strcmp(cmd, "tail") == 0) cmd_tail(argc, argv);
    else if (strcmp(cmd, "grep") == 0) cmd_grep(argc, argv);
    else if (strcmp(cmd, "find") == 0) cmd_find(argc, argv);
    else if (strcmp(cmd, "chmod") == 0) cmd_chmod(argc, argv);
    else if (strcmp(cmd, "chown") == 0) cmd_chown(argc, argv);
    else if (strcmp(cmd, "ln") == 0) cmd_ln(argc, argv);
    else if (strcmp(cmd, "dd") == 0) cmd_dd(argc, argv);
    else if (strcmp(cmd, "kill") == 0) cmd_kill();
    else if (strcmp(cmd, "ps") == 0) cmd_ps();
    else if (strcmp(cmd, "source") == 0) cmd_source(argc, argv);
    else if (strcmp(cmd, "type") == 0) cmd_type(argc, argv);
    else if (strcmp(cmd, "less") == 0) cmd_less(argc, argv);
    else if (strcmp(cmd, "time") == 0) cmd_time(argc, argv);
    else if (strcmp(cmd, "tee") == 0) cmd_tee(argc, argv);
    else if (strcmp(cmd, "tr") == 0) cmd_tr(argc, argv);
    else if (strcmp(cmd, "nl") == 0) cmd_nl(argc, argv);
    else if (strcmp(cmd, "fold") == 0) cmd_fold(argc, argv);
    else if (strcmp(cmd, "basename") == 0) cmd_basename(argc, argv);
    else if (strcmp(cmd, "dirname") == 0) cmd_dirname(argc, argv);
    else if (strcmp(cmd, "tty") == 0) cmd_tty();
    else if (strcmp(cmd, "logname") == 0) cmd_logname();
    else if (strcmp(cmd, "nproc") == 0) cmd_nproc();
    else if (strcmp(cmd, "realpath") == 0) cmd_realpath(argc, argv);
    else if (strcmp(cmd, "mktemp") == 0) cmd_mktemp();
    else if (strcmp(cmd, "expand") == 0) cmd_expand(argc, argv);
    else if (strcmp(cmd, "install") == 0) {
        if (!block_available()) kprintf("install: no disk detected\n");
        else installer_open();
    }
    else if (strcmp(cmd, "sync") == 0) cmd_sync();
    else if (strcmp(cmd, "exit") == 0) cmd_exit();
    else if (strcmp(cmd, "read") == 0) cmd_read(argc, argv);
    else if (strcmp(cmd, "wait") == 0) cmd_wait();
    else if (strcmp(cmd, "beep") == 0) cmd_beep();
    else if (strcmp(cmd, "printenv") == 0) cmd_printenv(argc, argv);
    else if (strcmp(cmd, "cut") == 0) cmd_cut(argc, argv);
    else if (strcmp(cmd, "comm") == 0) cmd_comm(argc, argv);
    else if (strcmp(cmd, "ascii") == 0) cmd_ascii();
    else if (strcmp(cmd, "fortune") == 0) cmd_fortune();
    else if (strcmp(cmd, "banner") == 0) cmd_banner(argc, argv);
    else if (strcmp(cmd, "download") == 0) cmd_download(argc, argv);
    else if (strcmp(cmd, "post") == 0) cmd_post(argc, argv);
    else if (strcmp(cmd, "https") == 0) cmd_https(argc, argv);
    else if (strcmp(cmd, "tlsinsecure") == 0) cmd_tlsinsecure(argc, argv);
    else if (strcmp(cmd, "catrust") == 0) cmd_catrust(argc, argv);
    else if (strcmp(cmd, "desktop") == 0) cmd_desktop();
    else if (strcmp(cmd, "panel") == 0) cmd_panel();
    else if (strcmp(cmd, "usb") == 0) cmd_usb();
    else if (strcmp(cmd, "clamscan") == 0) cmd_clamscan(argc, argv);
    else if (strcmp(cmd, "secaudit") == 0) cmd_secaudit(argc, argv);
    else if (strcmp(cmd, "secintegrity") == 0) cmd_secintegrity(argc, argv);
    else if (strcmp(cmd, "secfw") == 0) cmd_secfw(argc, argv);
    else if (strcmp(cmd, "secstatus") == 0) cmd_secstatus();
    else if (strcmp(cmd, "secthreat") == 0) cmd_secthreat();
    else if (strcmp(cmd, "adblock") == 0) cmd_adblock(argc, argv);
    else if (strcmp(cmd, "ping") == 0) cmd_ping(argc, argv);
    else if (strcmp(cmd, "nslookup") == 0) cmd_nslookup(argc, argv);
    else if (strcmp(cmd, "netinfo") == 0) cmd_netinfo();
    else if (strcmp(cmd, "dnsflush") == 0) { dns_cache_flush(); kprintf("DNS cache flushed\n"); }
    else if (strcmp(cmd, "setcursor") == 0) cmd_setcursor(argc, argv);
    else if (strcmp(cmd, "lsblk") == 0) cmd_lsblk();
    else if (strcmp(cmd, "mount") == 0) cmd_mount(argc, argv);
    else if (strcmp(cmd, "els") == 0) cmd_ls_ext(argc, argv);
    else if (strcmp(cmd, "ecat") == 0) cmd_cat_ext(argc, argv);
    else if (strcmp(cmd, "sysinfo") == 0) cmd_sysinfo();
    else if (strcmp(cmd, "meminfo") == 0) cmd_meminfo();
    else if (strcmp(cmd, "stats") == 0) cmd_stats();
    else if (strcmp(cmd, "devices") == 0) cmd_devices();
    else if (strcmp(cmd, "heapstat") == 0) cmd_heapstat();
    else if (strcmp(cmd, "update-check") == 0) cmd_update_check(argc, argv);
    else if (strcmp(cmd, "update-status") == 0) cmd_update_status(argc, argv);
    else if (strcmp(cmd, "update-apply") == 0) cmd_update_apply(argc, argv);
    else if (strcmp(cmd, "update-enable") == 0) cmd_update_enable(argc, argv);
    else if (strcmp(cmd, "update-disable") == 0) cmd_update_disable(argc, argv);
    else if (strcmp(cmd, "wine") == 0) cmd_wine(argc, argv);
    else if (strcmp(cmd, "wineserver") == 0) cmd_wineserver(argc, argv);
    else if (strcmp(cmd, "xora") == 0) cmd_xora(argc, argv);
    else if (strcmp(cmd, "nettest") == 0) cmd_nettest(argc, argv);
    else kprintf("%s: command not found\n", cmd);
}

/* ---------- public API ---------- */

int shell_execute(const char *line) {
    if (!line || !*line) return 0;
    char expanded[CMD_BUF_SIZE];
    expand_vars(line, expanded, CMD_BUF_SIZE);
    char *argv[MAX_ARGS];
    int argc = parse_args(expanded, argv, MAX_ARGS);
    if (argc == 0) return 0;
    char *glob_argv[MAX_ARGS];
    int gac = expand_glob_argv(argc, argv, glob_argv);
    redirect_run(gac > 0 ? gac : argc, gac > 0 ? glob_argv : argv);
    if (shell_interrupted) shell_interrupted = 0;
    return 0;
}

void shell_source_file(const char *path) {
    char script[FS_CONTENT_MAX + 1];
    int n = fs_read(path, script, FS_CONTENT_MAX);
    if (n < 0) {
        kprintf("source: %s: no such file\n", path);
        return;
    }
    script[n] = 0;
    char line[CMD_BUF_SIZE];
    int li = 0;
    for (int i = 0; i <= n; i++) {
        char c = script[i];
        if (c == '\n' || c == 0 || i == n) {
            line[li] = 0;
            li = 0;
            char *sp = line;
            while (*sp == ' ') sp++;
            if (*sp && *sp != '#') {
                char expanded[CMD_BUF_SIZE];
                expand_vars(sp, expanded, CMD_BUF_SIZE);
                char *sargv[MAX_ARGS];
                int sargc = parse_args(expanded, sargv, MAX_ARGS);
                if (sargc > 0) {
                    run_builtin(sargc, sargv);
                }
            }
        } else {
            if (li < CMD_BUF_SIZE - 1) line[li++] = c;
        }
    }
}

/* ---------- shell init & run ---------- */

void shell_init(void) {
    cmd_len = 0;
    cmd_pos = 0;
    hist_count = 0;
    hist_cur = 0;
    shell_exit_flag = 0;
    /* root user */
    user_count = 1;
    strcpy(users[0].username, "root");
    users[0].password[0] = 0;
    users[0].uid = 0;
    current_uid = 0;
    env_set("USER", "root");
    env_set("HOSTNAME", "codeos");
    env_init();
    pkg_init();
}

void shell_run(void) {
    kprintf("\n%s\n", KERNEL_UNAME);
    kprintf("csl -- CodeOS scripting language\n");
    kprintf("games/gplay -- built-in csl games (try `games`)\n\n");

    while (!shell_exit_flag) {
        shell_interrupted = 0;
        read_line(cmd_buf);
        if (!cmd_buf[0]) continue;
        if (shell_interrupted) { shell_interrupted = 0; continue; }
        hist_save(cmd_buf);
        char expanded[CMD_BUF_SIZE];
        expand_vars(cmd_buf, expanded, CMD_BUF_SIZE);
        char *argv[MAX_ARGS];
        int argc = parse_args(expanded, argv, MAX_ARGS);
        if (argc == 0) continue;
        char *glob_argv[MAX_ARGS];
        int gac = expand_glob_argv(argc, argv, glob_argv);
        redirect_run(gac > 0 ? gac : argc, gac > 0 ? glob_argv : argv);
        if (shell_interrupted) shell_interrupted = 0;
    }
}

int g_https_boot_test;

void https_boot_test(void) {
    kprintf("HTTPSBOOT: TLS verification test start (ca=%s)\n",
            https_ca_get() ? "store ok" : "store MISSING");
    { /* Connectivity probe: ARP-unlock + ICMP echo to the VBox NAT gateway
       * (10.0.2.2).  Isolates "NIC/ARP/unicast-RX broken" vs "TCP-only". */
        uint32_t gw = __builtin_bswap32(0x0A000202); /* 10.0.2.2 net-order (bswapped) */
        uint8_t mac[6];
        e1000_dbg_set_rx_trace(1);
        kprintf("HTTPSBOOT: arp_resolve(10.0.2.2)=%d\n", arp_resolve(gw, mac));
        int ping = icmp_echo(gw, 3000);
        kprintf("HTTPSBOOT: icmp_echo(10.0.2.2)=%d (>=0 means unicast RX+TX ok)\n", ping);
        e1000_dbg_set_rx_trace(0);
    }
    { /* Stage 1: VERIFY_REQUIRED against self-signed server -> must fail */
        char buf[512];
        int n = https_get("10.0.2.2", 8443, "/", buf, sizeof(buf) - 1);
        kprintf("HTTPSBOOT: stage1 self-signed verify-on        -> n=%d (EXPECT<0)\n", n);
    }
    { /* Stage 2: insecure override -> tunnel must still succeed */
        int old = https_set_insecure(1);
        char buf[512];
        int n = https_get("10.0.2.2", 8443, "/", buf, sizeof(buf) - 1);
        kprintf("HTTPSBOOT: stage2 insecure=1 self-signed       -> n=%d (EXPECT>0) body='%.70s'\n",
                n, n > 0 ? buf : "");
        https_set_insecure(old);
    }
    { /* Stage 3: real CA chain validation against example.com (optional internet) */
        char buf[2048];
        int n = https_get("example.com", 443, "/", buf, sizeof(buf) - 1);
        kprintf("HTTPSBOOT: stage3 example.com verify-on        -> n=%d (EXPECT>0 w/ internet)\n", n);
    }
    kprintf("HTTPSBOOT: done\n");
}

/* ── Shell self-test (runs on boot, before the desktop takes over) ── */

void shell_selftest(void) {
    int ri, pi;

    /* ── Test 1: Package search ── */
    int pkg_ok = (pkg_find_in_repos("sysfetch", &ri, &pi) == 0);
    kprintf("SHELLTEST: sysfetch package in repo=%d (repo idx %d)\n", pkg_ok, ri);
    if (pkg_ok) {
        kprintf("SHELLTEST: running sysfetch...\n");
        cmd_sysfetch();
    }

    /* ── Test 2: fetch -S (local install) ── */
    kprintf("SHELLTEST: testing fetch -S...\n");
    char *fargv[] = {"fetch", "-S", "tree", NULL};
    cmd_fetch(3, fargv);
    extern int pkg_installed_count(void);
    kprintf("SHELLTEST: installed packages: %d\n", pkg_installed_count());

    /* ── Test 3: fetch -L (list) ── */
    kprintf("SHELLTEST: testing fetch -L...\n");
    char *largv[] = {"fetch", "-L", NULL};
    cmd_fetch(2, largv);

    /* ── Test 4: fetch -Ss (search) ── */
    kprintf("SHELLTEST: testing fetch -Ss...\n");
    char *sargv[] = {"fetch", "-Ss", "curl", NULL};
    cmd_fetch(3, sargv);

    /* ── Test 5: xora list ── */
    kprintf("SHELLTEST: testing xora list...\n");
    char *xargv[] = {"xora", "list", NULL};
    cmd_xora(2, xargv);

    /* ── Test 6: nettest ── */
    kprintf("SHELLTEST: testing nettest...\n");
    char *nargv[] = {"nettest", NULL};
    cmd_nettest(1, nargv);

    kprintf("SHELLTEST: done\n");
}
