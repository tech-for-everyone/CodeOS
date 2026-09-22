#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define MAX_LINE 4096
#define MAX_ARGS 64

static int root_mode = 0;

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

static void print_prompt(void) {
    printf("csl%s> ", root_mode ? "#" : "$");
}

static int builtin_cd(char **argv, int argc) {
    (void)argv; (void)argc;
    printf("cd: not supported\n");
    return 1;
}

static int builtin_pwd(void) {
    printf("/\n");
    return 0;
}

static int builtin_echo(char **argv, int argc) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        printf("%s", argv[i]);
    }
    printf("\n");
    return 0;
}

static int builtin_ls(char **argv, int argc) {
    (void)argv; (void)argc;
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

static int builtin_help(void) {
    puts("CSL commands:");
    puts("  echo [text]     Print text");
    puts("  pwd             Print working directory");
    puts("  cd [dir]        Change directory");
    puts("  ls [dir]        List directory");
    puts("  clear           Clear screen");
    puts("  mkdir [dir]     Create directory");
    puts("  touch [file]    Create file");
    puts("  rm [path]       Remove file");
    puts("  cat [file]      Print file");
    puts("  root            Toggle root mode");
    puts("  exec [file]     Execute file");
    puts("  exit            Quit");
    return 0;
}

static int builtin_mkdir(char **argv, int argc) {
    if (argc < 2) { printf("mkdir: missing operand\n"); return 1; }
    int fd = sys_open(argv[1], 0);
    if (fd >= 0) { sys_close(fd); printf("mkdir: already exists\n"); return 1; }
    char cmd[512];
    int len = snprintf(cmd, sizeof(cmd), "mkdir %s", argv[1]);
    sys_write(cmd, len);
    printf("mkdir: not supported\n");
    return 1;
}

static int builtin_touch(char **argv, int argc) {
    if (argc < 2) { printf("touch: missing operand\n"); return 1; }
    int fd = sys_open(argv[1], 0);
    if (fd >= 0) { sys_close(fd); return 0; }
    fd = sys_open(argv[1], 1);
    if (fd >= 0) { sys_close(fd); return 0; }
    printf("touch: cannot create\n");
    return 1;
}

static int builtin_rm(char **argv, int argc) {
    (void)argv;
    if (argc < 2) { printf("rm: missing operand\n"); return 1; }
    printf("rm: not supported\n");
    return 1;
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

static int builtin_exec(char **argv, int argc) {
    if (argc < 2) { printf("exec: missing operand\n"); return 1; }
    if (root_mode) printf("Executing %s with root privileges\n", argv[1]);
    int pid = sys_fork();
    if (pid < 0) { printf("exec: fork failed\n"); return 1; }
    if (pid == 0) {
        sys_execve(argv[1], argv + 1, argc - 1);
        printf("exec: cannot execute %s\n", argv[1]);
        sys_exit(127);
    }
    int status = 0;
    sys_wait(pid, &status);
    return 0;
}

int main(void) {
    char line[MAX_LINE];
    puts("CSL shell — CodeOS Scripting Language");
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

        char *argv[MAX_ARGS];
        int argc = split_command(input, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) break;
        else if (strcmp(argv[0], "help") == 0) builtin_help();
        else if (strcmp(argv[0], "echo") == 0) builtin_echo(argv, argc);
        else if (strcmp(argv[0], "pwd") == 0) builtin_pwd();
        else if (strcmp(argv[0], "cd") == 0) builtin_cd(argv, argc);
        else if (strcmp(argv[0], "ls") == 0) builtin_ls(argv, argc);
        else if (strcmp(argv[0], "clear") == 0) builtin_clear();
        else if (strcmp(argv[0], "mkdir") == 0) builtin_mkdir(argv, argc);
        else if (strcmp(argv[0], "touch") == 0) builtin_touch(argv, argc);
        else if (strcmp(argv[0], "rm") == 0) builtin_rm(argv, argc);
        else if (strcmp(argv[0], "cat") == 0) builtin_cat(argv, argc);
        else if (strcmp(argv[0], "root") == 0) builtin_root();
        else if (strcmp(argv[0], "exec") == 0) builtin_exec(argv, argc);
        else printf("%s: command not found\n", argv[0]);
    }
    return 0;
}
