# 4.3 数组状态多样性评估模块 — 演示步骤

## 演示目标

展示运行时数组状态追踪模块能够：(a) 在程序执行过程中实时记录每个数组的索引访问模式；(b) 计算多维度的多样性指标（SDS 状态多样性得分、香农熵、欧氏距离）；(c) 通过反馈文件区分不同输入触发的数组状态空间差异。

## 测试程序

`test_simple.c` — 含越界漏洞的简单数组测试程序（约 67 行），包含 3 个不同作用域的数组：

| 数组 | 类型 | 长度 | 作用域 | 越界条件 |
|------|------|------|--------|---------|
| `local_data[10]` | float[] | 10 | 局部（栈） | `idx < 0` 或 `idx >= 10` |
| `global_scores[5]` | int[] | 5 | 全局 | 无检查，直接越界 |
| `pkt.payload[32]` | char[] | 32 | 结构体成员（栈） | `idx < 0` 或 `idx >= 32` |

程序接受一个整数参数作为数组索引，对三个数组分别执行读写操作。

## 前提

Docker 容器已启动，Pass 和运行时库已编译：

```bash
# 确认容器在运行
docker ps --filter "name=aflgo-test"

# 进入容器
docker exec -it aflgo-test bash
```

## 演示步骤

### 步骤 0：编译 Pass 和运行时库

如果尚未编译或修改了源码，需要重新构建：

```bash
cd /workspace/passes/build && make

cd /workspace/runtime
clang-11 -c -O2 -fPIC array_state_runtime.c -o array_state_runtime.o
ar rcs libarray_state.a array_state_runtime.o
```

验证产物：

```bash
ls -la /workspace/passes/build/libarray_instrument_pass.so
ls -la /workspace/runtime/libarray_state.a
```

> **讲解要点**：4.3 模块由两部分组成——编译时的 LLVM 插桩 Pass（`libarray_instrument_pass.so`，在 GEP 指令后注入 `__afl_report_array_state` 调用）和链接时的运行时库（`libarray_state.a`，实现位图追踪、指标计算和反馈输出）。

### 步骤 1：编译并插桩测试程序

按 5 步流水线构建（C 源码 → IR → 插桩 → 编译 → 链接）：

```bash
cd /workspace/test_programs

# 1a: 生成 LLVM IR
clang-11 -S -emit-llvm -g -O0 -fno-discard-value-names \
    test_simple.c -o test_simple.ll

# 1b: 运行插桩 Pass
opt-11 -load /workspace/passes/build/libarray_instrument_pass.so \
    -array-instrument test_simple.ll -S -o test_simple_instr.ll

# 1c: 编译插桩后的 IR
clang-11 -c -O0 test_simple_instr.ll -o test_simple_instr.o

# 1d: 创建 __afl_area_ptr 桩文件（仅独立测试需要）
cat > /tmp/stub.c << 'EOF'
#include <stddef.h>
unsigned char __afl_area_initial[65536] = {0};
unsigned char *__afl_area_ptr = NULL;
EOF
clang-11 -c /tmp/stub.c -o /tmp/stub.o

# 1e: 链接运行时库
clang-11 test_simple_instr.o /workspace/runtime/libarray_state.a \
    /tmp/stub.o -o test_simple_instr -lm
```

### 步骤 2：正常输入 — 观察状态报告

用正常索引 `3` 运行程序，设置 `AFL_ARRAY_DIVERSITY=1` 启用反馈输出：

```bash
AFL_ARRAY_DIVERSITY=1 ./test_simple_instr 3
cat /tmp/array_feedback.txt
```

观察输出：

```
=== Array Test Program ===
Input index: 3
  [test_array_operations] idx=3 written
  [test_global_array] idx=3 written to global
  [test_struct_array] idx=3 written
=== Test Complete ===
```

反馈文件：
```
SDS=0.003906
ENTROPY=0.000000
EUCLIDEAN=0.095922
NUM_ARRAYS=3
```

> **讲解要点**：
> - idx=3 在三个数组的合法范围内。每个数组只访问了 1 个槽位（`3 % 256 = slot 3`），SDS = `(1/256 + 1/256 + 1/256) / 3 ≈ 0.004`。
> - 熵为 0 是因为每个数组只有一个槽位有访问记录（`p=1.0, log₂(1)=0`），访问分布完全集中在一个槽位上。
> - 欧氏距离非零是因为之前已有其他测试运行的残留状态向量（`/tmp/.array_state_vector.bin`）。首次运行时通常会为 0。

### 步骤 3：边界与越界输入 — 对比状态差异

分别用越界索引运行，与正常输入对比：

```bash
# 先清空历史状态，确保对比的起点一致
rm -f /tmp/.array_state_vector.bin

# 负索引越界（触发三个数组全部 OOB）
AFL_ARRAY_DIVERSITY=1 AFL_ARRAY_FEEDBACK_FILE=/tmp/feedback_neg1.txt \
    ./test_simple_instr -1

# 索引 8 — 部分越界（仅 global_scores[8] OOB，因为长度只有 5）
AFL_ARRAY_DIVERSITY=1 AFL_ARRAY_FEEDBACK_FILE=/tmp/feedback_8.txt \
    ./test_simple_instr 8

# 对比反馈文件
echo ""; echo "=== idx=-1 ==="; cat /tmp/feedback_neg1.txt
echo ""; echo "=== idx=8 ==="; cat /tmp/feedback_8.txt
```

> **讲解要点**：
> - `idx=-1` 触发三个数组全部越界。负索引经 `mod 256` 映射到 **slot 255**。
> - `idx=8` 对 `local_data[10]` 和 `pkt.payload[32]` 合法，但 `global_scores[8]` 越界（长度只有 5）。映射到 **slot 8**。
> - 两次运行的 SDS 值相同（都是 3 个槽位各命中 1 次），但**欧氏距离不同**——因为状态向量 `[-1, -1, -1]` 与 `[8, 8, 8]` 在多维空间中距离很大。
> - **核心原理**：不同输入 → 不同 slot 访问模式 + 不同状态向量 → 不同的 SDS/熵/欧氏距离。AFLGo 根据这些差异调整种子能量分配。

### 步骤 4：欧氏距离 — 度量执行间的状态变化

欧氏距离比较两次执行的状态向量差异。状态向量由每个数组的最新索引值构成：

```bash
# 清空历史状态，从干净起点开始
rm -f /tmp/.array_state_vector.bin

# 第一次运行：idx=5（无历史状态，欧氏距离=0）
AFL_ARRAY_DIVERSITY=1 ./test_simple_instr 5 2>/dev/null
echo "Run 1 (idx=5):"
cat /tmp/array_feedback.txt | grep EUCLIDEAN

# 第二次运行：idx=999（状态向量从 [5,5,5] 跳到 [999,999,999]，距离 > 0）
AFL_ARRAY_DIVERSITY=1 ./test_simple_instr 999 2>/dev/null
echo "Run 2 (idx=999):"
cat /tmp/array_feedback.txt | grep EUCLIDEAN
```

预期：
```
Run 1 (idx=5):
EUCLIDEAN=0.000000
Run 2 (idx=999):
EUCLIDEAN=1.000000
```

> **讲解要点**：
> - 每次运行退出时将当前状态向量（`[last_index_of_arr1, last_index_of_arr2, last_index_of_arr3]`）写入 `/tmp/.array_state_vector.bin`，下次运行时自动加载作为对比基线。
> - idx=5 → idx=999，向量从 `[5, 5, 5]` 跳到 `[999, 999, 999]`，3 个维度各自差 994，欧氏距离达到归一化上限 1.0。
> - 这一步证明了模块能够**跨进程**度量两个测试用例之间的数组状态差异——这正是 4.4 模块中种子间比较的基础。

### 步骤 5：打印完整状态报告

运行时库提供了 `__afl_print_state_report()` 函数，可在程序中调用或通过 GDB 触发。为方便演示，也可以直接查看反馈文件中汇总的指标：

```bash
# 查看每个数组的详细状态
echo ""
echo "=== 最终反馈汇总 ==="
cat /tmp/array_feedback.txt

# 查看状态向量持久化文件（跨进程欧氏距离用）
echo ""
echo "=== 状态向量持久化 ==="
cat /tmp/.array_state_vector.bin 2>/dev/null || echo "(首次运行后生成)"
```

## 核心指标解读

| 指标 | 公式 | 含义 | 范围 |
|------|------|------|------|
| **SDS** | `avg(unique_slots[i] / 256)` | 各数组已探索索引槽位的平均比例 | `[0, 1]` |
| **Entropy** | `avg(H_i / H_max)`，其中 `H_i = -Σ(p_j·log₂(p_j))` | 各数组索引访问分布的均匀程度 | `[0, 1]` |
| **Euclidean** | `√(Σ(idx_i - idx_i_prev)²)` 归一化 | 与上次执行的状态向量差异 | `[0, 1]` |
| **NUM_ARRAYS** | — | 本次执行实际触发的数组数量 | 整数 |

## 预期结果

- [ ] 插桩后的 `test_simple_instr` 能正常运行，接受整数参数
- [ ] 正常输入（idx=3）产生 `SDS≈0.004`、`ENTROPY≈0`、`NUM_ARRAYS=3`
- [ ] 越界输入（idx=-1, 8）产生不同的 EUCLIDEAN 值（状态向量不同）
- [ ] 清除历史状态后，首次运行 `EUCLIDEAN=0.000000`，大幅改变输入后 `EUCLIDEAN > 0`
- [ ] 反馈文件 `/tmp/array_feedback.txt` 包含全部 4 个字段（SDS / ENTROPY / EUCLIDEAN / NUM_ARRAYS）
- [ ] `/tmp/.array_state_vector.bin` 在首次运行后生成，内容为各数组的最新索引值
