#include "unistd.h"
#include "stdio.h"
#include "string.h"

static const char *splash[] = {
    "",
    "   _____ ___  ____   ____ ___  ______ ____  _____ ",
    "  |  ___/ _ \\|  _ \\ / __ \\\\ \\/ / ___|  _ \\| ____|",
    "  | |_ | | | | |_) | |  | |\\  / |   | |_) |  _|  ",
    "  |  _|| |_| |  _ <| |__| |/  \\ |___|  __/| |___ ",
    "  |_|   \\___/|_| \\_\\\\____//_/\\_\\____|_|   |_____|",
    "   ~ CodeOS AI Assistant ~",
    "",
    "Hello! I'm FreeCode, your kernel-integrated AI!",
    "",
};

static char inbuf[512];
static int inpos;

static int readline(void) {
    inpos = 0;
    while (1) {
        int n = sys_read(0, inbuf + inpos, 1);
        if (n <= 0) { sys_sleep(50); continue; }
        char c = inbuf[inpos];
        if (c == '\n' || c == '\r') {
            inbuf[inpos] = 0;
            puts("");
            return inpos;
        }
        if (c == '\b' || c == 127) {
            if (inpos > 0) { inpos--; puts("\b \b"); }
        } else if (inpos < 511) {
            inpos++;
            putchar(c);
        }
    }
}

static void handle_query(const char *input) {
    if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
        puts("  FreeCode: Goodbye! Come back anytime. :)");
        sys_sleep(1000);
        sys_exit(0);
    }
    if (strcmp(input, "clear") == 0) {
        puts("");
        for (int i = 0; i < (int)(sizeof(splash)/sizeof(splash[0])); i++)
            puts(splash[i]);
        puts("  -------------------------------------------------");
        puts("");
        return;
    }

    char resp[2048];
    int len = sys_ai_query(input, resp, sizeof(resp));
    if (len > 0) {
        resp[len] = 0;
        char *line = resp;
        while (*line) {
            char *nl = line;
            while (*nl && *nl != '\n') nl++;
            char saved = *nl;
            *nl = 0;
            printf("  FreeCode: %s\n", line);
            if (saved == 0) break;
            *nl = saved;
            line = nl + 1;
        }
    } else {
        printf("  FreeCode: ");
        const char *fallbacks[] = {
            "Hmm, let me think about that...",
            "I'm not sure I understand. Try asking about CodeOS!",
            "Interesting question! Ask me about the kernel, GUI, or networking.",
            "I know a lot about CodeOS internals. What would you like to know?",
        };
        int idx = (len + (int)(*input)) & 3;
        if (idx < 0) idx = 0;
        puts(fallbacks[idx]);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    for (int i = 0; i < (int)(sizeof(splash)/sizeof(splash[0])); i++)
        puts(splash[i]);
    puts("  -------------------------------------------------");
    puts("");
    while (1) {
        printf("  You: ");
        if (readline() > 0) {
            handle_query(inbuf);
            puts("");
        }
    }
}
