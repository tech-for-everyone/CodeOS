#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define MAX_LINE 4096
#define MAX_ARGS 64
#define HIST_MAX 32

static int root_mode = 0;
static char history[HIST_MAX][MAX_LINE];
static int history_count = 0;

static char *trim_whitespace(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == '\0') return str;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return str;
}

static const char *expand_home(const char *path, char *out, int out_size) {
    if (!path || !out || out_size < 2) return path;
    if (path[0] != '~') return path;
    /* CodeOS currently boots the single root account. Its home is a normal
     * directory in the visible tree, so `~` follows the same path as `ed`. */
    const char *home = "/root/home/root";
    int n = 0;
    while (*home && n < out_size - 1) out[n++] = *home++;
    path++;
    while (*path && n < out_size - 1) out[n++] = *path++;
    out[n] = 0;
    return out;
}

static int split_command(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = 0;
    return argc;
}

static void hist_add(const char *line) {
    if (!line || !*line) return;
    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return;
    if (history_count == HIST_MAX) {
        memmove(history[0], history[1], (HIST_MAX - 1) * MAX_LINE);
        history_count = HIST_MAX - 1;
    }
    strncpy(history[history_count], line, MAX_LINE - 1);
    history[history_count][MAX_LINE - 1] = '\0';
    history_count++;
}

/* Replace !! and !n with a saved line. Returns -1 when the event is missing. */
static int hist_expand(char *line) {
    if (line[0] != '!') return 0;
    int idx = -1;
    if (line[1] == '!' && line[2] == '\0') {
        idx = history_count - 1;
    } else if (line[1] >= '1' && line[1] <= '9') {
        int n = 0;
        const char *p = line + 1;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (*p == '\0' && n >= 1 && n <= history_count)
            idx = n - 1;
    }
    if (idx < 0) {
        printf("csh: %s: event not found\n", line);
        return -1;
    }
    strncpy(line, history[idx], MAX_LINE - 1);
    line[MAX_LINE - 1] = '\0';
    printf("%s\n", line);
    return 0;
}

static void print_prompt(void) {
    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) strcpy(cwd, "/");
    printf("csh:%s%s ", cwd, root_mode ? "#" : "$");
}

static int builtin_cd(char **argv, int argc) {
    char path[512];
    const char *requested = argc > 1 ? argv[1] : "~";
    requested = expand_home(requested, path, sizeof(path));
    if (sys_chdir(requested) < 0) {
        printf("%s: %s: directory not found\n", argv[0], requested);
        return 1;
    }
    return 0;
}

static int builtin_pwd(void) {
    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) return 1;
    printf("%s\n", cwd);
    return 0;
}

static int builtin_echo(char **argv, int argc) {
    int i = 1;
    int newline = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        i = 2;
    }
    for (; i < argc; i++) {
        if (i > (newline ? 1 : 2)) putchar(' ');
        printf("%s", argv[i]);
    }
    if (newline) printf("\n");
    return 0;
}

static int builtin_ls(char **argv, int argc) {
    int fd = sys_open(argc > 1 ? argv[1] : ".", 0);
    if (fd < 0) {
        printf("ls: cannot open\n");
        return 1;
    }
    char buf[256];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) putchar(buf[i]);
    }
    sys_close(fd);
    return 0;
}

static int builtin_clear(void) {
    printf("\033[2J\033[H");
    return 0;
}

static int builtin_history(void) {
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i]);
    return 0;
}

static int builtin_help(void) {
    puts("CodeOS csh:");
    puts("  echo [-n] [text]  Print text");
    puts("  pwd               Print working directory");
    puts("  cd [path]         Change directory (~ is home; ed is an alias)");
    puts("  ls [dir]          List directory");
    puts("  clear             Clear screen");
    puts("  mkdir [dir]       Create directory");
    puts("  rmdir [dir]       Remove an empty directory");
    puts("  touch [file]      Create file");
    puts("  rm [path]         Remove file");
    puts("  cat [file]        Print file");
    puts("  history           Show recent commands");
    puts("  !!  !n            Repeat the previous command, or history entry n");
    puts("  offline-ide [file] Open the offline code editor");
    puts("  root              Toggle root mode");
    puts("  exec [file]       Execute a file (other names are searched too)");
    puts("  exit              Quit");
    return 0;
}

static int builtin_mkdir(char **argv, int argc) {
    if (argc < 2) { printf("mkdir: missing operand\n"); return 1; }
    if (sys_mkdir(argv[1]) < 0) {
        printf("mkdir: cannot create '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int builtin_rmdir(char **argv, int argc) {
    if (argc < 2) { printf("rmdir: missing operand\n"); return 1; }
    if (sys_rmdir(argv[1]) < 0) {
        printf("rmdir: cannot remove '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int builtin_touch(char **argv, int argc) {
    if (argc < 2) { printf("touch: missing operand\n"); return 1; }
    int fd = sys_open(argv[1], O_WRONLY | O_CREAT);
    if (fd >= 0) { sys_close(fd); return 0; }
    printf("touch: cannot create\n");
    return 1;
}

static int builtin_rm(char **argv, int argc) {
    if (argc < 2) { printf("rm: missing operand\n"); return 1; }
    if (sys_unlink(argv[1]) < 0) {
        printf("rm: cannot remove '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int builtin_cat(char **argv, int argc) {
    if (argc < 2) { printf("cat: missing operand\n"); return 1; }
    int fd = sys_open(argv[1], 0);
    if (fd < 0) { printf("cat: cannot open %s\n", argv[1]); return 1; }
    char buf[1024];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(buf, n);
    sys_close(fd);
    return 0;
}

static int builtin_root(void) {
    root_mode = !root_mode;
    printf("Root mode %s\n", root_mode ? "enabled" : "disabled");
    return 0;
}

static int spawn(const char *path, char **argv, int argc) {
    int pid = sys_fork();
    if (pid < 0) { printf("csh: fork failed\n"); return 1; }
    if (pid == 0) {
        sys_execve(path, argv, argc);
        printf("csh: cannot execute %s\n", path);
        sys_exit(127);
    }
    int status = 0;
    sys_wait(pid, &status);
    return 0;
}

static int run_external(char **argv, int argc) {
    char path[256];
    if (strchr(argv[0], '/'))
        return spawn(argv[0], argv, argc);

    static const char *dirs[] = { "/bin/", "/usr/bin/" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s%s", dirs[i], argv[0]);
        int fd = sys_open(path, 0);
        if (fd >= 0) {
            sys_close(fd);
            return spawn(path, argv, argc);
        }
    }
    printf("%s: command not found\n", argv[0]);
    return 1;
}

static int builtin_exec(char **argv, int argc) {
    if (argc < 2) { printf("exec: missing operand\n"); return 1; }
    if (root_mode) printf("Executing %s with root privileges\n", argv[1]);
    return run_external(argv + 1, argc - 1);
}

static int run_builtin(char **argv, int argc) {
    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) return -1;
    if (strcmp(argv[0], "help") == 0) builtin_help();
    else if (strcmp(argv[0], "echo") == 0) builtin_echo(argv, argc);
    else if (strcmp(argv[0], "pwd") == 0) builtin_pwd();
    else if (strcmp(argv[0], "ed") == 0 || strcmp(argv[0], "cd") == 0) builtin_cd(argv, argc);
    else if (strcmp(argv[0], "ls") == 0) builtin_ls(argv, argc);
    else if (strcmp(argv[0], "clear") == 0) builtin_clear();
    else if (strcmp(argv[0], "mkdir") == 0) builtin_mkdir(argv, argc);
    else if (strcmp(argv[0], "rmdir") == 0) builtin_rmdir(argv, argc);
    else if (strcmp(argv[0], "touch") == 0) builtin_touch(argv, argc);
    else if (strcmp(argv[0], "rm") == 0) builtin_rm(argv, argc);
    else if (strcmp(argv[0], "cat") == 0) builtin_cat(argv, argc);
    else if (strcmp(argv[0], "history") == 0) builtin_history();
    else if (strcmp(argv[0], "root") == 0) builtin_root();
    else if (strcmp(argv[0], "exec") == 0) builtin_exec(argv, argc);
    else return 1;
    return 0;
}

int main(void) {
    char line[MAX_LINE];
    puts("CodeOS csh");
    puts("Type 'help' for commands.");

    while (1) {
        print_prompt();
        int pos = 0;
        while (1) {
            char c;
            int n = sys_read(0, &c, 1);
            if (n <= 0) continue;
            if (c == '\r' || c == '\n') {
                line[pos] = '\0';
                printf("\n");
                break;
            } else if (c == '\b' || c == 127) {
                if (pos > 0) { pos--; printf("\b \b"); }
            } else if (c >= ' ' && c < 127 && pos < MAX_LINE - 1) {
                line[pos++] = c;
                putchar(c);
            }
        }

        char *input = trim_whitespace(line);
        if (*input == '\0') continue;
        if (hist_expand(input) < 0) continue;

        char saved[MAX_LINE];
        strncpy(saved, input, MAX_LINE - 1);
        saved[MAX_LINE - 1] = '\0';

        char *argv[MAX_ARGS];
        int argc = split_command(input, argv, MAX_ARGS);
        if (argc == 0) continue;

        int rc = run_builtin(argv, argc);
        if (rc < 0) break;
        if (rc > 0) run_external(argv, argc);
        hist_add(saved);
    }
    return 0;
}
