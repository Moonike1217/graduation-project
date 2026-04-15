#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <input>\n", argv[0]);
        return 1;
    }

    char buffer[100];
    strncpy(buffer, argv[1], sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    if (strcmp(buffer, "fuzz") == 0) {
        printf("You found the secret!\n");
    } else if (strcmp(buffer, "crash") == 0) {
        printf("Triggering crash!\n");
        int *ptr = NULL;
        *ptr = 42; // 对空指针解引用 -> Segmentation fault
    } else {
        printf("Try again!\n");
    }

    return 0;
}

// 编译 ： afl-clang-fast -o test test.c
// 测试：  afl-fuzz -i in -o output ./test @@