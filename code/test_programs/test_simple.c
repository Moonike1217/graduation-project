// test_simple.c
// 简单数组测试程序 — 含越界漏洞
//
// 从文件读取整数作为数组索引进行访问，用于验证：
// 1. LLVM Pass 是否能正确识别数组变量
// 2. 运行时库是否能正确记录数组状态
// 3. AFLGO 能否通过状态引导触发越界崩溃

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 一维数组测试：通过索引直接写入
// 漏洞: 当 idx < 0 或 idx >= 10 时越界
static void test_array_operations(int idx) {
    float local_data[10];
    if (idx >= 0 && idx < 10) {
        local_data[idx] = (float)idx;  // 正常访问
    } else {
        local_data[idx] = -1.0f;       // 越界写入（漏洞点）
    }
    printf("  [test_array_operations] idx=%d written\n", idx);
}

// 全局数组越界测试
static int global_scores[5];

static void test_global_array(int idx) {
    global_scores[idx] = idx * 10;     // 无条件写入（越界漏洞）
    printf("  [test_global_array] idx=%d written to global\n", idx);
}

// 结构体数组成员越界测试
typedef struct {
    int id;
    char payload[32];
} DataPacket;

static void test_struct_array(int idx) {
    DataPacket pkt;
    pkt.id = 1;
    if (idx >= 0 && idx < 32) {
        pkt.payload[idx] = 'A';        // 正常访问
    } else {
        pkt.payload[idx] = 'X';        // 越界写入（漏洞点）
    }
    printf("  [test_struct_array] idx=%d written\n", idx);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <index>\n", argv[0]);
        return 1;
    }

    int idx = atoi(argv[1]);

    printf("=== Array Test Program ===\n");
    printf("Input index: %d\n", idx);

    test_array_operations(idx);
    test_global_array(idx);
    test_struct_array(idx);

    printf("=== Test Complete ===\n");
    return 0;
}
