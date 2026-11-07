#include "unistd.h"
#include "stdio.h"
#include "string.h"

#define IDE_MAX_LINES 128
#define IDE_LINE_SIZE 256
#define IDE_MAX_INPUT 512

static char source[IDE_MAX_LINES][IDE_LINE_SIZE];
static char file_path[256];
static int source_lines;
static int modified;

static int read_line(char *out, int capacity) {
    int length = 0;
    while (length < capacity - 1) {
        char ch;
        int count = sys_read(0, &ch, 1);
        if (count <= 0) return count;
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (length > 0) {
                length--;
                sys_write("\b \b", 3);
            }
            continue;
        }
        if (ch >= ' ' && ch < 127) {
            out[length++] = ch;
            putchar(ch);
        }
    }
    out[length] = '\0';
    return length;
}

static int is_swift_source(void) {
    int length = (int)strlen(file_path);
    return length >= 6 && strcmp(file_path + length - 6, ".swift") == 0;
}

static int is_keyword(const char *word, int length) {
    static const char *c_keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "else", "enum", "extern", "for", "if", "int", "long",
        "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while"
    };
    static const char *swift_keywords[] = {
        "as", "class", "defer", "enum", "extension", "false", "func", "guard",
        "if", "import", "in", "let", "nil", "private", "protocol", "public",
        "return", "self", "static", "struct", "switch", "true", "var", "while"
    };
    const char **keywords = is_swift_source() ? swift_keywords : c_keywords;
    int count = is_swift_source()
        ? (int)(sizeof(swift_keywords) / sizeof(swift_keywords[0]))
        : (int)(sizeof(c_keywords) / sizeof(c_keywords[0]));
    for (int i = 0; i < count; i++) {
        if ((int)strlen(keywords[i]) == length &&
            strncmp(word, keywords[i], (size_t)length) == 0)
            return 1;
    }
    return 0;
}

static void show_source_line(const char *line) {
    int i = 0;
    while (line[i]) {
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            int start = i++;
            while (line[i]) {
                if (line[i] == '\\' && line[i + 1]) {
                    i += 2;
                    continue;
                }
                if (line[i++] == quote) break;
            }
            printf("\033[33m%.*s\033[0m", i - start, line + start);
            continue;
        }
        if (line[i] == '/' && line[i + 1] == '/') {
            printf("\033[32m%s\033[0m", line + i);
            return;
        }
        if ((line[i] >= 'a' && line[i] <= 'z') ||
            (line[i] >= 'A' && line[i] <= 'Z') || line[i] == '_') {
            int start = i++;
            while ((line[i] >= 'a' && line[i] <= 'z') ||
                   (line[i] >= 'A' && line[i] <= 'Z') ||
                   (line[i] >= '0' && line[i] <= '9') || line[i] == '_')
                i++;
            if (is_keyword(line + start, i - start))
                printf("\033[36m%.*s\033[0m", i - start, line + start);
            else
                printf("%.*s", i - start, line + start);
            continue;
        }
        putchar(line[i++]);
    }
}

static void show_source(void) {
    printf("\033[1m%s\033[0m (%d lines)%s\n", file_path, source_lines,
           modified ? " [modified]" : "");
    for (int i = 0; i < source_lines; i++) {
        printf("\033[90m%3d |\033[0m ", i + 1);
        show_source_line(source[i]);
        putchar('\n');
    }
}

static int load_source(void) {
    int fd = sys_open(file_path, O_RDONLY);
    if (fd < 0) return 0;

    char chunk[256];
    int count;
    int column = 0;
    source_lines = 0;
    while ((count = sys_read(fd, chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < count; i++) {
            char ch = chunk[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                if (source_lines == IDE_MAX_LINES) {
                    sys_close(fd);
                    return -1;
                }
                source[source_lines++][column] = '\0';
                column = 0;
            } else {
                if (source_lines >= IDE_MAX_LINES || column >= IDE_LINE_SIZE - 1) {
                    sys_close(fd);
                    return -1;
                }
                source[source_lines][column++] = ch;
            }
        }
    }
    sys_close(fd);
    if (count < 0) return -1;
    if (column > 0 || source_lines == 0) {
        if (source_lines == IDE_MAX_LINES) return -1;
        source[source_lines++][column] = '\0';
    }
    return 1;
}

static int save_source(void) {
    int fd = sys_open(file_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        puts("Save failed: cannot create or truncate the file.");
        return -1;
    }
    for (int i = 0; i < source_lines; i++) {
        int length = (int)strlen(source[i]);
        if (length && sys_pwrite(fd, source[i], length) != length) {
            sys_close(fd);
            puts("Save failed while writing source text.");
            return -1;
        }
        if (i + 1 < source_lines && sys_pwrite(fd, "\n", 1) != 1) {
            sys_close(fd);
            puts("Save failed while writing a line break.");
            return -1;
        }
    }
    if (sys_close(fd) < 0) {
        puts("Save failed while closing the file.");
        return -1;
    }
    modified = 0;
    printf("Saved %s\n", file_path);
    return 0;
}

static int set_template(const char *kind) {
    if (source_lines != 0) {
        puts("Template refused: the current buffer is not empty.");
        return -1;
    }
    if (strcmp(kind, "c") == 0) {
        strcpy(source[0], "#include <stdio.h>");
        strcpy(source[1], "");
        strcpy(source[2], "int main(void) {");
        strcpy(source[3], "    puts(\"Hello from CodeOS\");");
        strcpy(source[4], "    return 0;");
        strcpy(source[5], "}");
        source_lines = 6;
    } else if (strcmp(kind, "swift") == 0) {
        strcpy(source[0], "import Foundation");
        strcpy(source[1], "");
        strcpy(source[2], "print(\"Hello from Swift\")");
        source_lines = 3;
    } else {
        puts("Template must be 'c' or 'swift'.");
        return -1;
    }
    modified = 1;
    return 0;
}

static int parse_line_number(const char *text, int *number) {
    if (!text || !*text || !number) return -1;
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        int digit = *text++ - '0';
        if (value > (IDE_MAX_LINES - digit) / 10) return -1;
        value = value * 10 + digit;
    }
    if (*text) return -1;
    *number = value;
    return 0;
}

static int run_program(const char *path, char **arguments, int count) {
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        printf("Not available: %s\n", path);
        return -1;
    }
    sys_close(fd);

    int pid = sys_fork();
    if (pid < 0) {
        puts("Could not start a child process.");
        return -1;
    }
    if (pid == 0) {
        if (sys_execve(path, arguments, count) < 0) {
            printf("Could not execute %s\n", path);
            sys_exit(127);
        }
        sys_exit(127);
    }
    int status = 0;
    if (sys_wait(pid, &status) < 0) {
        puts("Could not wait for the child process.");
        return -1;
    }
    return status;
}

static int build_source(char *output, int output_size) {
    if (save_source() < 0) return -1;
    if (snprintf(output, (size_t)output_size, "%s.out", file_path) >= output_size) {
        puts("Build output path is too long.");
        return -1;
    }
    const char *compiler_paths[] = {
        "/bin/cc", "/usr/bin/cc", "/bin/clang", "/usr/bin/clang",
        "/bin/swiftc", "/usr/bin/swiftc"
    };
    int swift = is_swift_source();
    for (int i = 0; i < (int)(sizeof(compiler_paths) / sizeof(compiler_paths[0])); i++) {
        int is_swift_compiler = strstr(compiler_paths[i], "swiftc") != NULL;
        if (is_swift_compiler != swift) continue;
        char *arguments[] = {
            (char *)compiler_paths[i], file_path, "-o", output, NULL
        };
        int result = run_program(compiler_paths[i], arguments, 4);
        if (result == 0) {
            printf("Build complete: %s\n", output);
            return 0;
        }
        if (result != -1) {
            printf("Build failed with exit status %d.\n", result);
            return result;
        }
    }
    puts(swift
        ? "Swift compiler unavailable. CodeOS does not yet include a native Swift toolchain."
        : "C compiler unavailable. Install a local CodeOS-compatible compiler first.");
    return -1;
}

static int edit_command(char *input, char *output, int output_size) {
    char *command = input;
    if (*command == ':') command++;
    while (*command == ' ' || *command == '\t') command++;
    char *arguments = command;
    while (*arguments && *arguments != ' ' && *arguments != '\t') arguments++;
    if (*arguments) *arguments++ = '\0';
    while (*arguments == ' ' || *arguments == '\t') arguments++;

    if (strcmp(command, "help") == 0) {
        puts(":show | :append TEXT | :set N TEXT | :insert N TEXT | :delete N");
        puts(":template c|swift | :save [PATH] | :build | :run | :quit[!]");
        return 0;
    }
    if (strcmp(command, "show") == 0) {
        show_source();
        return 0;
    }
    if (strcmp(command, "append") == 0) {
        if (source_lines >= IDE_MAX_LINES ||
            strlen(arguments) >= IDE_LINE_SIZE) {
            puts("Append failed: source buffer is full or line is too long.");
            return -1;
        }
        strcpy(source[source_lines++], arguments);
        modified = 1;
        return 0;
    }
    if (strcmp(command, "set") == 0 || strcmp(command, "insert") == 0 ||
        strcmp(command, "delete") == 0) {
        char *number_text = arguments;
        while (*arguments && *arguments != ' ' && *arguments != '\t') arguments++;
        if (*arguments) *arguments++ = '\0';
        while (*arguments == ' ' || *arguments == '\t') arguments++;
        int line_number;
        if (parse_line_number(number_text, &line_number) < 0) {
            puts("Expected a valid line number.");
            return -1;
        }
        if (strcmp(command, "set") == 0) {
            if (line_number < 1 || line_number > source_lines ||
                strlen(arguments) >= IDE_LINE_SIZE) {
                puts("Set failed: line number or text is out of range.");
                return -1;
            }
            strcpy(source[line_number - 1], arguments);
        } else if (strcmp(command, "insert") == 0) {
            if (line_number < 1 || line_number > source_lines + 1 ||
                source_lines >= IDE_MAX_LINES ||
                strlen(arguments) >= IDE_LINE_SIZE) {
                puts("Insert failed: line number or text is out of range.");
                return -1;
            }
            for (int i = source_lines; i >= line_number; i--)
                strcpy(source[i], source[i - 1]);
            strcpy(source[line_number - 1], arguments);
            source_lines++;
        } else {
            if (line_number < 1 || line_number > source_lines) {
                puts("Delete failed: line number is out of range.");
                return -1;
            }
            for (int i = line_number; i < source_lines; i++)
                strcpy(source[i - 1], source[i]);
            source[--source_lines][0] = '\0';
        }
        modified = 1;
        return 0;
    }
    if (strcmp(command, "template") == 0) return set_template(arguments);
    if (strcmp(command, "save") == 0 || strcmp(command, "write") == 0) {
        if (*arguments) {
            if (strlen(arguments) >= sizeof(file_path)) {
                puts("Path is too long.");
                return -1;
            }
            strcpy(file_path, arguments);
            if (snprintf(output, (size_t)output_size, "%s.out", file_path) >= output_size) {
                puts("Output path is too long.");
                return -1;
            }
        }
        return save_source();
    }
    if (strcmp(command, "build") == 0) return build_source(output, output_size);
    if (strcmp(command, "run") == 0) {
        if (modified) {
            puts("Save and build the modified source before running it.");
            return -1;
        }
        char *run_arguments[] = { output, NULL };
        return run_program(output, run_arguments, 1);
    }
    if (strcmp(command, "quit") == 0) {
        if (modified) {
            puts("Unsaved changes. Use :save or :quit! to discard them.");
            return 0;
        }
        return 1;
    }
    if (strcmp(command, "quit!") == 0) return 1;
    puts("Unknown command. Type :help.");
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 2 || strlen(argv[1]) >= sizeof(file_path)) {
        puts("Usage: offline-ide <source-file>");
        return 2;
    }
    strcpy(file_path, argv[1]);
    int loaded = load_source();
    if (loaded < 0) {
        puts("Could not load the file: it exceeds the editor's size limits or cannot be read.");
        return 1;
    }
    if (!loaded) printf("New file: %s\n", file_path);

    char output[300];
    if (snprintf(output, sizeof(output), "%s.out", file_path) >= (int)sizeof(output)) {
        puts("Output path is too long.");
        return 1;
    }
    puts("CodeOS Offline IDE — all editing is local. Type :help for commands.");
    show_source();
    for (;;) {
        char input[IDE_MAX_INPUT];
        printf("ide> ");
        int count = read_line(input, sizeof(input));
        if (count <= 0) return count < 0 ? 1 : 0;
        if (edit_command(input, output, sizeof(output)) == 1) return 0;
    }
}
