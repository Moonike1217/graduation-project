// array_example.c
// 一个简单的 C 程序，包含多种数组访问场景。
// 配合 afl_array_state.c 编译后，可以观察 __afl_report_array 的行为。
//
// 编译方式（独立测试模式，不依赖 AFL++）：
//   clang array_example.c ../src/afl_array_state.c -o array_example -lm
//
// 运行方式：
//   ./array_example 5     # 正常访问
//   ./array_example 11    # 越界访问
//   ./array_example -1    # 负索引越界
//
// 如果配合 LLVM Pass 插桩：
//   这一步由编译器自动完成，不需要手动调用 __afl_report_array。

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// 手动模拟 LLVM Pass 生成的 ArrayID
// MyArrayPass.cpp 中用 FNV-1a 哈希 function_name::var_name 生成：
//   hash("main::buffer")  ≈ 0x0A1B2C3D  (示意值，实际取决于函数名和变量名)
//   hash("main::matrix#dim1") ≈ 0x11223344
#define ARRAYID_BUFFER   0x0A1B2C3D
#define ARRAYID_MATRIX   0x11223344

// ---------------------------------------------------------------
// 插桩调用点模拟
// 实际情况下，这些调用由 LLVM Pass 在编译时自动插入。
// 这里手动插入以演示调用时机和参数。
// ---------------------------------------------------------------

// 声明运行时库提供的函数
void __afl_report_array(uint32_t array_id, int64_t index,
                        uint32_t access_type, uint32_t line);

static void simulate_compute_sum(float *buffer, int idx, int line) {
    // 模拟第1个插桩点：读 buffer[idx]
    // 参数说明：
    //   array_id    = ARRAYID_BUFFER   — 数组的唯一ID（编译时确定）
    //   index       = idx              — 本次访问的索引值（运行时确定）
    //   access_type = 1                — bit0=1 表示读操作
    //   line        = line             — 源代码行号
    __afl_report_array(ARRAYID_BUFFER, idx, 1, line);

    // 真实的数组读取操作
    float val = buffer[idx];
    printf("  buffer[%d] = %.2f\n", idx, val);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <索引值>\n", argv[0]);
        return 1;
    }

    int idx = atoi(argv[1]);

    // ---------- 场景1: 一维数组访问 ----------
    printf("===== 场景1: 一维 float 数组 =====\n");
    float buffer[10] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    printf("输入索引: %d\n", idx);
    printf("数组长度: 10\n");

    // 手动模拟 LLVM Pass 在此处的插桩
    //
    // 步骤1: 识别 GEP 指令
    //   源代码: buffer[idx]  →  LLVM 生成:
    //     %ptr = getelementptr [10 x float], ptr %buffer, i64 0, i64 %idx
    //   其中最后一个操作数 %idx 就是要传递给 __afl_report_array 的索引值
    //
    // 步骤2: 插入插桩调用
    //   MyArrayPass.cpp 第151-156行:
    //     Builder.CreateCall(ReportFn, {
    //         Builder.getInt32(ArrayID),      // 编译时确定的哈希值
    //         Idx64,                           // 运行时索引值 (ZExt/Trunc 到 i64)
    //         Builder.getInt32(AccessType),    // 编译时确定的访问类型
    //         Builder.getInt32(LineNum)        // 编译时确定的行号
    //     });
    //
    // 步骤3: 实际生成的调用 ≡
    //   __afl_report_array(ARRAYID_BUFFER, idx, 3, 62);
    //   其中 access_type=3 因为对 buffer[idx] 的赋值既读又写(先读后写)

    // === 插桩点1: buffer[idx] = ... (写操作, access_type=2) ===
    // 对应论文 4.2.2 节："在GEP指令之后、内存访问指令之前插入"
    __afl_report_array(ARRAYID_BUFFER, idx, 2, 56);
    buffer[idx] = 999.0f;

    // === 插桩点2: = buffer[idx] (读操作, access_type=1) ===
    // 通过函数调用来模拟对 buffer[idx] 的读取
    simulate_compute_sum(buffer, idx, 60);

    // ---------- 场景2: 越界检测 ----------
    printf("\n===== 场景2: 越界检测 =====\n");
    if (idx < 0 || idx >= 10) {
        printf("检测到! 索引 %d 越界 (数组范围 0~9)\n", idx);
        printf("越界访问可能读取到相邻内存: %.2f\n", buffer[idx]);
    } else {
        printf("索引 %d 在合法范围内\n", idx);
    }

    return 0;
}
