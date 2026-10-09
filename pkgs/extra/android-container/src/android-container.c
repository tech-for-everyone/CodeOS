#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "android.h"

#define MAX_PROPERTIES 32

static struct {
    char key[64];
    char val[128];
} properties[MAX_PROPERTIES];
static int prop_count;

static void prop_set(const char *key, const char *val) {
    for (int i = 0; i < prop_count; i++) {
        if (strcmp(properties[i].key, key) == 0) {
            strncpy(properties[i].val, val, 127);
            return;
        }
    }
    if (prop_count < MAX_PROPERTIES) {
        strncpy(properties[prop_count].key, key, 63);
        strncpy(properties[prop_count].val, val, 127);
        prop_count++;
    }
}

static void init_properties(void) {
    prop_set("ro.product.name", "codeos");
    prop_set("ro.product.model", "CodeOS Android Compat");
    prop_set("ro.product.device", "codeos_x86_64");
    prop_set("ro.build.version.sdk", "29");
    prop_set("ro.build.version.release", "10");
    prop_set("dalvik.vm.heapsize", "64m");
    prop_set("persist.sys.timezone", "UTC");
    prop_set("ro.product.cpu.abi", "x86_64");
    prop_set("ro.product.cpu.abilist", "x86_64,x86,arm64-v8a,armeabi-v7a");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    puts("CodeOS Android Container v1.0");
    puts("==============================\n");

    /* Initialize system properties */
    init_properties();

    puts("System Properties:");
    for (int i = 0; i < prop_count; i++) {
        printf("  [%s]: [%s]\n", properties[i].key, properties[i].val);
    }

    /* Register as binder context manager */
    int handle = binder_set_context_mgr();
    if (handle >= 0) {
        printf("\nBinder context manager registered (handle=%d)\n", handle);
    } else {
        puts("\nBinder not available - running without IPC");
    }

    /* Create ashmem regions for shared system buffers */
    int shm = ashmem_create("system_heap", 65536);
    if (shm >= 0) {
        printf("Ashmem: system_heap created (id=%d, size=%d)\n",
               shm, ashmem_get_size(shm));
    }

    puts("\nAndroid container ready.");
    puts("Available commands: props, binder, shm, help, exit");
    sys_write("> ", 2);

    char buf[256];
    int pos = 0;
    while (1) {
        char c;
        int n = sys_read(0, &c, 1);
        if (n <= 0) { sys_yield(); continue; }
        if (c == '\r' || c == '\n') {
            buf[pos] = 0;
            sys_write("\n", 1);

            if (strcmp(buf, "exit") == 0) break;
            else if (strcmp(buf, "props") == 0) {
                for (int i = 0; i < prop_count; i++)
                    printf("  %s=%s\n", properties[i].key, properties[i].val);
            } else if (strcmp(buf, "binder") == 0) {
                int ref = binder_get_ref();
                if (ref >= 0)
                    printf("Binder ref obtained: handle=%d\n", ref);
                else
                    puts("Binder ref failed");
            } else if (strcmp(buf, "shm") == 0) {
                printf("Ashmem regions:\n");
                printf("  system_heap: id=%d size=%d\n", shm, shm >= 0 ? ashmem_get_size(shm) : 0);
            } else if (strcmp(buf, "help") == 0) {
                puts("Commands:");
                puts("  props    - list system properties");
                puts("  binder   - test binder IPC");
                puts("  shm      - show ashmem status");
                puts("  help     - this help");
                puts("  exit     - shutdown container");
            } else if (buf[0]) {
                printf("android: unknown command '%s'\n", buf);
            }

            pos = 0;
            sys_write("> ", 2);
        } else if (c == '\b' || c == 127) {
            if (pos > 0) { pos--; sys_write("\b \b", 3); }
        } else if (c >= ' ' && c < 127 && pos < 255) {
            buf[pos++] = c;
            sys_write(&c, 1);
        }
    }

    puts("Android container shutting down.");
    return 0;
}
