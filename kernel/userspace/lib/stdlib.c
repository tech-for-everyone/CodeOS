#include "unistd.h"
#include "stdarg.h"
#include "string.h"

void exit(int status) {
    sys_exit(status);
    while (1);
}

int atoi(const char *s) {
    int val = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

static char *heap_cur;
static char *heap_end;

void *malloc(int size) {
    if (size <= 0) size = 1;
    if (size < 16) size = 16;
    size = (size + 15) & ~15;

    if (!heap_cur) {
        heap_cur = (char *)sys_brk(0);
        if (!heap_cur || heap_cur == (void *)-1) return 0;
        heap_end = heap_cur;
    }

    if (heap_cur + size > heap_end) {
        char *new_brk = (char *)sys_brk(heap_end + (size > 4096 ? size : 4096));
        if (!new_brk || new_brk == (void *)-1) return 0;
        heap_end = new_brk;
    }

    void *ptr = heap_cur;
    heap_cur += size;
    return ptr;
}

void free(void *p) {
    (void)p;
}

char **environ = NULL;

char *getenv(const char *name) {
    if (!environ) return NULL;
    size_t len = strlen(name);
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=')
            return environ[i] + len + 1;
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) return -1;
    char *existing = getenv(name);
    if (existing && !overwrite) return 0;

    size_t name_len = strlen(name);
    size_t val_len = value ? strlen(value) : 0;
    char *new_entry = malloc(name_len + 1 + val_len + 1);
    if (!new_entry) return -1;
    memcpy(new_entry, name, name_len);
    new_entry[name_len] = '=';
    if (value) memcpy(new_entry + name_len + 1, value, val_len);
    new_entry[name_len + 1 + val_len] = 0;

    if (existing) {
        for (int i = 0; environ[i]; i++) {
            if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
                free(environ[i]);
                environ[i] = new_entry;
                return 0;
            }
        }
    }

    int count = 0;
    while (environ && environ[count]) count++;
    char **new_environ = malloc((count + 2) * sizeof(char *));
    if (!new_environ) {
        free(new_entry);
        return -1;
    }
    if (environ) {
        memcpy(new_environ, environ, count * sizeof(char *));
        free(environ);
    }
    new_environ[count] = new_entry;
    new_environ[count + 1] = NULL;
    environ = new_environ;
    return 0;
}

int unsetenv(const char *name) {
    if (!environ || !name) return -1;
    size_t len = strlen(name);
    for (int i = 0; environ[i]; i++) {
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
            free(environ[i]);
            for (int j = i; environ[j]; j++)
                environ[j] = environ[j + 1];
            return 0;
        }
    }
    return 0;
}

