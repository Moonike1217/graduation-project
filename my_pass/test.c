#include <stdio.h>
#include <stdint.h>

void __afl_report_array(uint32_t id, int64_t index, int32_t type, int32_t line) {
    const char* mode = (type == 1) ? "READ" : (type == 2 ? "WRITE" : "UNK");
    printf("[Runtime Report] ID: 0x%08x | Line: %-3d | Mode: %-5s | Index: %ld\n", 
            id, line, mode, index);
}

// 1. 全局数组
int global_scores[5] = {10, 20, 30, 40, 50};

void test_array_operations(int idx) {
    float local_data[10] = {0.0f};
    local_data[idx % 10] = 1.5f; // 使用变量索引
}

void test_multidimensional_array(int r, int c) {
    // 3. 多维数组
    char matrix[3][4] = {0};
    matrix[r % 3][c % 4] = 'A'; // 强制产生多维 GEP
    printf("Matrix val: %c\n", matrix[r % 3][c % 4]);
}

struct DataPacket {
    int id;
    char payload[32];
};

void test_struct_array(int idx) {
    // 4. 结构体字段数组
    struct DataPacket packet = {0};
    packet.payload[idx % 32] = 'H';
    printf("Packet payload: %c\n", packet.payload[idx % 32]);
}

int main() {
    printf("--- 开始全量识别测试 (-O0 模式) ---\n");
    test_array_operations(2);
    test_multidimensional_array(1, 2);
    test_struct_array(0);
    printf("--- 测试结束 ---\n");
    return 0;
}