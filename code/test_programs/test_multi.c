// test_multi.c
// 多维数组测试程序 — 含多维越界漏洞
//
// 从文件读取两个整数作为行列索引，验证：
// 1. LLVM Pass 对多维数组的维度感知
// 2. 多维数组状态追踪的正确性

#include <stdio.h>
#include <stdlib.h>

// 二维数组越界测试
static void test_multidimensional_array(int row, int col) {
    char matrix[3][4];
    matrix[row][col] = 'X';   // 可能越界
    printf("  [test_multidimensional] row=%d col=%d written\n", row, col);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <row> <col>\n", argv[0]);
        return 1;
    }

    int row = atoi(argv[1]);
    int col = atoi(argv[2]);

    printf("=== Multi-Dimensional Array Test ===\n");
    printf("Input: row=%d col=%d\n", row, col);

    test_multidimensional_array(row, col);

    printf("=== Test Complete ===\n");
    return 0;
}
