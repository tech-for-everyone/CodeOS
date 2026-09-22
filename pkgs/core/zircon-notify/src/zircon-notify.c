#include "unistd.h"
#include "stdio.h"
#include "string.h"

typedef struct {
    int type;
    int x, y;
    char text[128];
} zircon_ipc_msg_t;

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    puts("Zircon Notification Viewer v1.0");
    puts("Monitoring kernel notifications...");
    puts("");

    int count = 0;
    while (1) {
        zircon_ipc_msg_t msg;
        int r = sys_zircon_ipc(1, &msg);
        if (r > 0) {
            count++;
            printf("[%d] Type:%d  %s\n", count, msg.type, msg.text);
        }
        sys_yield();
    }
}
