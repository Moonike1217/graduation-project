// test_string.c
// 字符串处理测试程序 — 含缓冲区溢出漏洞
//
// 模拟真实程序中的字符串操作场景：
// 1. 字符串拷贝越界
// 2. 字符串拼接溢出
// 3. 格式化写入溢出

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 基于位置的字符串拷贝越界
// src 的前 20 字节被无条件复制到 16 字节的 dest 中
static void test_string_copy(void) {
    char dest[16];
    const char *src = "AAAAAAAAAAAAAAAAAAAA";  // 20 bytes
    strcpy(dest, src);  // 缓冲区溢出
    printf("  [test_string_copy] copied %zu bytes to 16-byte buffer\n", strlen(src));
}

// 字符串拼接溢出
static void test_string_concat(int extra_len) {
    char buffer[20];
    strcpy(buffer, "Hello");
    for (int i = 0; i < extra_len && i < 100; i++) {
        strcat(buffer, "X");  // extra_len > 10 时溢出
    }
    printf("  [test_string_concat] extra_len=%d result_len=%zu\n",
           extra_len, strlen(buffer));
}

// 格式化写入溢出
static void test_format_write(int val) {
    char buf[8];
    int written = snprintf(buf, sizeof(buf), "%d", val);
    buf[written] = '\0';  // 当 val 很大时，written >= 8，越界写入
    printf("  [test_format_write] val=%d written=%d\n", val, written);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <value>\n", argv[0]);
        return 1;
    }

    int val = atoi(argv[1]);

    printf("=== String Processing Test ===\n");
    printf("Input value: %d\n", val);

    test_string_copy();
    test_string_concat(val);
    test_format_write(val);

    printf("=== Test Complete ===\n");
    return 0;
}
