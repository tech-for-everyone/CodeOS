#include "unistd.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("Zircon Clock v1.0");
    puts("");

    int ticks = 0;
    while (1) {
        printf("\rUptime: %d:%02d:%02d  ",
            ticks / 3600, (ticks / 60) % 60, ticks % 60);

        /* Scroll a simple ASCII clock hand */
        const char *hand = "-->";
        int pos = ticks % 20;
        printf("\n[");
        for (int i = 0; i < 20; i++) {
            if (i == pos) printf("%s", hand);
            else printf(".");
        }
        printf("]");

        sys_sleep(1000);
        ticks++;

        /* Move cursor up 2 lines */
        printf("\033[2A");
    }
}
