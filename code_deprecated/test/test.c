// test.c — 数组状态引导模糊测试的测试程序
//
// 包含多种数组使用场景：
//   1. 一维局部数组
//   2. 二维数组
//   3. 结构体数组成员
//   4. 全局数组
//
// 编译（独立 LLVM Pass 模式）:
//   clang-19 -S -emit-llvm -g -O0 test.c -o test.ll
//   opt-19 -load-pass-plugin=MyArrayPass.so -passes="my-array-pass" test.ll -S -o test_instr.ll
//   clang-19 test_instr.ll -L. -lafl_array_state -o test_app
//
// 编译（AFL++ 集成模式）:
//   afl-clang-fast -O0 -g test.c src/afl_array_state.c -o test_afl
//
// AFL++ 运行:
//   mkdir -p seeds && echo "0" > seeds/seed
//   afl-fuzz -i seeds -o out ./test_afl @@

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ============================================================
// 数组状态报告函数声明（由 afl_array_state.c 提供）
// ============================================================
void __afl_array_state_print_report(void);

// ============================================================
// 全局数组
// ============================================================
int global_scores[5] = {10, 20, 30, 40, 50};

// ============================================================
// 测试用例1：一维局部数组
// target_array: local_data[10]，索引范围 0-9
// 越界条件: idx < 0 或 idx >= 10
// ============================================================
void test_array_operations(int idx) {
    float local_data[10] = {0.0f};
    // 允许真正的越界发生，以触发 Crash
    local_data[idx] = 1.5f;
}

// ============================================================
// 测试用例2：二维数组
// target_array: matrix[3][4]，有效索引 r∈[0,2], c∈[0,3]
// 越界条件: r < 0 || r >= 3 || c < 0 || c >= 4
// ============================================================
void test_multidimensional_array(int r, int c) {
    char matrix[3][4] = {0};
    // 允许二维数组越界
    matrix[r][c] = 'A';
}

// ============================================================
// 测试用例3：结构体中的数组
// target_array: packet.payload[32]，有效索引 0-31
// 越界条件: idx < 0 || idx >= 32
// ============================================================
struct DataPacket {
    int id;
    char payload[32];
};

void test_struct_array(int idx) {
    struct DataPacket packet = {0};
    // 允许结构体数组成员越界
    packet.payload[idx] = 'H';
}

// ============================================================
// 测试用例4：全局数组访问
// target_array: global_scores[5]，有效索引 0-4
// ============================================================
void test_global_array(int idx) {
    if (idx >= 0 && idx < 5) {
        global_scores[idx] = idx * 100;
    }
}

// ============================================================
// 主函数
// ============================================================
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    // 从文件读取输入（AFL++ 标准方式）
    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;

    char buf[64] = {0};
    if (fgets(buf, sizeof(buf) - 1, f) == NULL) {
        fclose(f);
        return 1;
    }
    fclose(f);

    // 去除换行符
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

    // 将输入转换为整数
    int idx = atoi(buf);
    printf("--- Testing with index: %d ---\n", idx);

    // 执行各类数组操作
    test_array_operations(idx);
    test_multidimensional_array(idx, idx + 1);
    test_struct_array(idx);
    test_global_array(idx);

    // 打印数组状态报告
    __afl_array_state_print_report();

    return 0;
}
