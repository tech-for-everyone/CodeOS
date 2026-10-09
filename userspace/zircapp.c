/* zircapp - Run Zircon apps in CodeOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: zircapp <app> [args...]\n");
        printf("  Runs Zircon-native apps directly in CodeOS\n");
        return 1;
    }
    printf("ZircApp: launching %s\n", argv[1]);
    return 0;
}
