#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define MAX_CMD 512
#define MAX_ARGS 32
#define MAX_HISTORY 64

static char *history[MAX_HISTORY];
static int history_count = 0;
static int history_pos = -1;

static char cwd[256];

static void update_cwd(void) {
    sys_getcwd(cwd, sizeof(cwd));
}

static void add_history(const char *cmd) {
    if (history_count < MAX_HISTORY) {
        history[history_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; i++)
            history[i-1] = history[i];
        history[MAX_HISTORY-1] = strdup(cmd);
    }
    history_pos = history_count;
}

static void print_prompt(void) {
    printf("\033[1;32m%s\033[0m$ ", cwd);
}

static int run_builtin(char **argv, int argc) {
    if (strcmp(argv[0], "exit") == 0) {
        return -1;
    }
    else if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) putchar(' ');
            printf("%s", argv[i]);
        }
        putchar('\n');
        return 0;
    }
    else if (strcmp(argv[0], "clear") == 0 || strcmp(argv[0], "cls") == 0) {
        printf("\033[2J\033[H");
        return 0;
    }
    else if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        puts("Built-in commands:");
        puts("  echo <text>     - Print text");
        puts("  clear, cls      - Clear screen");
        puts("  help, ?         - Show this help");
        puts("  exit            - Exit shell");
        puts("  cd [dir]        - Change directory");
        puts("  pwd             - Print working directory");
        puts("  ls [dir]        - List directory");
        puts("  cat <file>      - Display file");
        puts("  mkdir <dir>     - Create directory");
        puts("  rmdir <dir>     - Remove directory");
        puts("  rm <file>       - Remove file");
        puts("  mv <src> <dst>  - Move/rename");
        puts("  cp <src> <dst>  - Copy file");
        puts("  touch <file>    - Create empty file");
        puts("  stat <file>     - File info");
        puts("  history         - Show command history");
        puts("  env             - Show environment");
        puts("  export VAR=val  - Set environment variable");
        return 0;
    }
    else if (strcmp(argv[0], "cd") == 0) {
        const char *path = argc > 1 ? argv[1] : "/";
        if (sys_chdir(path) < 0) {
            fprintf(stderr, "cd: %s: No such file or directory\n", path);
        }
        update_cwd();
        return 0;
    }
    else if (strcmp(argv[0], "pwd") == 0) {
        puts(cwd);
        return 0;
    }
    else if (strcmp(argv[0], "ls") == 0) {
        const char *path = argc > 1 ? argv[1] : ".";
        char names[128][32];
        int count = sys_readdir(path, (char *)names, 128);
        if (count < 0) {
            fprintf(stderr, "ls: %s: No such file or directory\n", path);
            return 0;
        }
        for (int i = 0; i < count; i++) {
            if (names[i][0]) {
                stat_t st;
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", path, names[i]);
                if (sys_stat(full, &st) == 0) {
                    if (st.is_dir)
                        printf("\033[1;34m%s/\033[0m  ", names[i]);
                    else
                        printf("%s  ", names[i]);
                } else {
                    printf("%s  ", names[i]);
                }
            }
        }
        putchar('\n');
        return 0;
    }
    else if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) {
            fprintf(stderr, "cat: missing file operand\n");
            return 0;
        }
        int fd = sys_open(argv[1], 0);
        if (fd < 0) {
            fprintf(stderr, "cat: %s: No such file or directory\n", argv[1]);
            return 0;
        }
        char buf[1024];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = 0;
            printf("%s", buf);
        }
        sys_close(fd);
        return 0;
    }
    else if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2) {
            fprintf(stderr, "mkdir: missing operand\n");
            return 0;
        }
        if (sys_mkdir(argv[1]) < 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s'\n", argv[1]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "rmdir") == 0) {
        if (argc < 2) {
            fprintf(stderr, "rmdir: missing operand\n");
            return 0;
        }
        if (sys_rmdir(argv[1]) < 0) {
            fprintf(stderr, "rmdir: failed to remove '%s'\n", argv[1]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "rm") == 0) {
        if (argc < 2) {
            fprintf(stderr, "rm: missing operand\n");
            return 0;
        }
        if (sys_unlink(argv[1]) < 0) {
            fprintf(stderr, "rm: cannot remove '%s'\n", argv[1]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "mv") == 0) {
        if (argc < 3) {
            fprintf(stderr, "mv: missing operand\n");
            return 0;
        }
        if (sys_rename(argv[1], argv[2]) < 0) {
            fprintf(stderr, "mv: cannot move '%s' to '%s'\n", argv[1], argv[2]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "cp") == 0) {
        if (argc < 3) {
            fprintf(stderr, "cp: missing operand\n");
            return 0;
        }
        int src_fd = sys_open(argv[1], 0);
        if (src_fd < 0) {
            fprintf(stderr, "cp: cannot open '%s'\n", argv[1]);
            return 0;
        }
        int dst_fd = sys_open(argv[2], 0);
        if (dst_fd >= 0) {
            sys_close(dst_fd);
            if (sys_unlink(argv[2]) < 0) {
                fprintf(stderr, "cp: cannot overwrite '%s'\n", argv[2]);
                sys_close(src_fd);
                return 0;
            }
        }
        int new_fd = sys_mkfile(argv[2]);
        if (new_fd < 0) {
            fprintf(stderr, "cp: cannot create '%s'\n", argv[2]);
            sys_close(src_fd);
            return 0;
        }
        char buf[1024];
        int n;
        while ((n = sys_read(src_fd, buf, sizeof(buf))) > 0) {
            sys_write(new_fd, buf, n);
        }
        sys_close(src_fd);
        sys_close(new_fd);
        return 0;
    }
    else if (strcmp(argv[0], "touch") == 0) {
        if (argc < 2) {
            fprintf(stderr, "touch: missing operand\n");
            return 0;
        }
        int fd = sys_open(argv[1], 0);
        if (fd < 0) {
            fd = sys_mkfile(argv[1]);
            if (fd < 0) {
                fprintf(stderr, "touch: cannot create '%s'\n", argv[1]);
                return 0;
            }
        }
        sys_close(fd);
        return 0;
    }
    else if (strcmp(argv[0], "stat") == 0) {
        if (argc < 2) {
            fprintf(stderr, "stat: missing operand\n");
            return 0;
        }
        stat_t st;
        if (sys_stat(argv[1], &st) < 0) {
            fprintf(stderr, "stat: cannot stat '%s'\n", argv[1]);
            return 0;
        }
        printf("File: %s\n", argv[1]);
        printf("Size: %d bytes\n", st.size);
        printf("Type: %s\n", st.is_dir ? "directory" : "regular file");
        return 0;
    }
    else if (strcmp(argv[0], "history") == 0) {
        for (int i = 0; i < history_count; i++) {
            printf("%d  %s\n", i + 1, history[i]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "env") == 0) {
        extern char **environ;
        if (environ) {
            for (int i = 0; environ[i]; i++)
                puts(environ[i]);
        }
        return 0;
    }
    else if (strcmp(argv[0], "export") == 0) {
        if (argc < 2) {
            fprintf(stderr, "export: missing operand\n");
            return 0;
        }
        char *eq = strchr(argv[1], '=');
        if (!eq) {
            fprintf(stderr, "export: invalid format, use VAR=value\n");
            return 0;
        }
        *eq = '\0';
        setenv(argv[1], eq + 1, 1);
        return 0;
    }
    return 1;
}

static int run_external(char **argv, int argc) {
    char path[256];
    snprintf(path, sizeof(path), "/bin/%s", argv[0]);
    int fd = sys_open(path, 0);
    if (fd < 0) {
        snprintf(path, sizeof(path), "/usr/bin/%s", argv[0]);
        fd = sys_open(path, 0);
    }
    if (fd < 0) {
        return 1;
    }
    sys_close(fd);

    char *envp[] = { NULL };
    if (sys_execve(path, argv, argc, envp) < 0) {
        fprintf(stderr, "%s: exec failed\n", argv[0]);
        return 1;
    }
    return 0;
}

static void run_cmd(char *line) {
    char *argv[MAX_ARGS];
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= MAX_ARGS) break;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return;

    add_history(line);

    if (run_builtin(argv, argc) != 0) {
        run_external(argv, argc);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[MAX_CMD];
    int pos = 0;
    update_cwd();
    print_prompt();

    while (1) {
        char c;
        int n = sys_read(0, &c, 1);
        if (n <= 0) continue;
        if (c == '\r' || c == '\n') {
            buf[pos] = 0;
            putchar('\n');
            run_cmd(buf);
            pos = 0;
            print_prompt();
        }
        else if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                sys_write("\b \b", 3);
            }
        }
        else if (c == 27) {
            char seq[2];
            if (sys_read(0, &seq[0], 1) > 0 && sys_read(0, &seq[1], 1) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'A') {
                        if (history_pos > 0) {
                            history_pos--;
                            while (pos > 0) { sys_write("\b \b", 3); pos--; }
                            strncpy(buf, history[history_pos], MAX_CMD - 1);
                            buf[MAX_CMD - 1] = 0;
                            pos = strlen(buf);
                            sys_write(buf, pos);
                        }
                    }
                    else if (seq[1] == 'B') {
                        if (history_pos < history_count - 1) {
                            history_pos++;
                            while (pos > 0) { sys_write("\b \b", 3); pos--; }
                            strncpy(buf, history[history_pos], MAX_CMD - 1);
                            buf[MAX_CMD - 1] = 0;
                            pos = strlen(buf);
                            sys_write(buf, pos);
                        }
                        else if (history_pos == history_count - 1) {
                            history_pos = history_count;
                            while (pos > 0) { sys_write("\b \b", 3); pos--; }
                            buf[0] = 0;
                            pos = 0;
                        }
                    }
                    else if (seq[1] == 'C') {
                    }
                    else if (seq[1] == 'D') {
                    }
                }
            }
        }
        else if (c >= ' ' && c < 127 && pos < MAX_CMD - 1) {
            buf[pos++] = c;
            sys_write(&c, 1);
        }
    }
}