// afl_array_state.c
// 数组类型变量状态引导模糊测试 — 运行时状态采集库
//
// 本文件实现：
// 1. __afl_report_array() — 被 LLVM Pass 插桩调用，记录数组访问到 AFL++ bitmap
// 2. 数组状态追踪 — 记录哪些 (array_id, index) 组合已被访问
// 3. 状态多样性度量 — 提供接口查询已探索/未探索状态
//
// 可以通过定义 AFL_RUNTIME=1 来启用 AFL++ bitmap 写入；
// 否则（独立/测试模式）仅做状态追踪并输出到 stderr。

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// 配置常量
// ============================================================

#define MAX_TRACKED_ARRAYS 64        // 最多追踪的数组数量
#define MAX_INDEX_SLOTS 256          // 每个数组的索引槽位数

// ============================================================
// AFL++ 接口（仅在 AFL++ 环境下可用）
// ============================================================

#if AFL_RUNTIME
// AFL++ 提供的全局共享内存指针（每个子进程自动映射）
extern uint8_t *__afl_area_ptr;
#endif

// ============================================================
// 状态追踪数据结构
// ============================================================

// 每个数组的状态信息
typedef struct {
    uint32_t    array_id;           // 数组唯一 ID
    char        name[64];           // 数组名（用于调试输出）
    uint32_t    access_count;       // 总访问次数
    uint64_t    min_index;          // 访问过的最小索引
    uint64_t    max_index;          // 访问过的最大索引
    int         has_oob;            // 是否发生过越界访问
    uint8_t     visited_slots[MAX_INDEX_SLOTS]; // 访问过的索引位图
    uint32_t    unique_slots;       // 不同索引位置的数量
} array_state_t;

// 全局状态追踪器
static struct {
    array_state_t arrays[MAX_TRACKED_ARRAYS];
    int num_arrays;
} array_tracker = {0};

// ============================================================
// 内部工具函数
// ============================================================

// 查找或注册数组状态条目
static array_state_t* find_or_create_array(uint32_t array_id, const char *dbg_name) {
    // 查找已存在的
    for (int i = 0; i < array_tracker.num_arrays; i++) {
        if (array_tracker.arrays[i].array_id == array_id) {
            return &array_tracker.arrays[i];
        }
    }
    // 创建新的
    if (array_tracker.num_arrays < MAX_TRACKED_ARRAYS) {
        array_state_t *arr = &array_tracker.arrays[array_tracker.num_arrays++];
        memset(arr, 0, sizeof(array_state_t));
        arr->array_id = array_id;
        arr->min_index = UINT64_MAX;
        arr->max_index = 0;
        if (dbg_name) {
            snprintf(arr->name, sizeof(arr->name), "%s", dbg_name);
        } else {
            snprintf(arr->name, sizeof(arr->name), "array_%u", array_id);
        }
        return arr;
    }
    return NULL;
}

// ============================================================
// 核心 API：被 LLVM Pass 插桩调用
// ============================================================

void __afl_report_array(uint32_t array_id, int64_t index, uint32_t access_type, uint32_t line) {
    // === 1. 写入 AFL++ bitmap（仅在 AFL++ 环境下）===
#if AFL_RUNTIME
    if (__afl_area_ptr != NULL) {
        // 简化哈希：16位输出，用于索引 trace_bits
        uint64_t x = (uint64_t)array_id ^ ((uint64_t)index << 5);
        x = (x ^ (x >> 16)) * 0x85ebca6b;
        x = x ^ (x >> 13);
        x = x ^ (x << 16);
        uint16_t hash = (uint16_t)(x & 0xFFFF);
        __afl_area_ptr[hash & 0xFFFF]++;
    }
#endif

    // === 2. 更新状态追踪（所有模式下均工作）===
    array_state_t *arr = find_or_create_array(array_id, NULL);
    if (arr) {
        arr->access_count++;
        if ((uint64_t)index < arr->min_index) arr->min_index = (uint64_t)index;
        if ((uint64_t)index > arr->max_index) arr->max_index = (uint64_t)index;

        // 记录索引访问（简化版：用取模定位到槽位）
        uint32_t slot = ((uint64_t)index) % MAX_INDEX_SLOTS;
        if (!arr->visited_slots[slot]) {
            arr->visited_slots[slot] = 1;
            arr->unique_slots++;
        }
    }

    // === 3. 独立测试模式：输出到 stderr ===
#if !AFL_RUNTIME
    fprintf(stderr, "  [ArrayAccess] array_id=0x%08x index=%lld type=%s line=%u\n",
        array_id, (long long)index,
        (access_type & 1) ? "read" : "",
        line);
    if (access_type & 2) {
        fprintf(stderr, "                write idx=%lld\n", (long long)index);
    }
#endif
}

// ============================================================
// 状态查询 API（供 custom mutator 使用）
// ============================================================

// 获取所有已追踪数组的数量
int __afl_array_state_get_num_arrays(void) {
    return array_tracker.num_arrays;
}

// 获取指定数组的 ID
uint32_t __afl_array_state_get_array_id(int idx) {
    if (idx < 0 || idx >= array_tracker.num_arrays) return 0;
    return array_tracker.arrays[idx].array_id;
}

// 获取指定数组的唯一索引访问数（状态多样性度量）
uint32_t __afl_array_state_get_unique_slots(uint32_t array_id) {
    array_state_t *arr = find_or_create_array(array_id, NULL);
    return arr ? arr->unique_slots : 0;
}

// 获取总访问次数
uint64_t __afl_array_state_get_total_accesses(uint32_t array_id) {
    array_state_t *arr = find_or_create_array(array_id, NULL);
    return arr ? arr->access_count : 0;
}

// ============================================================
// 状态多样性度量 API
// ============================================================

// 计算所有数组的总状态多样性得分（论文第3章的状态多样性度量）
// 公式: diversity = sum(unique_slots_i / MAX_INDEX_SLOTS) / num_arrays
float __afl_array_state_get_diversity_score(void) {
    if (array_tracker.num_arrays == 0) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < array_tracker.num_arrays; i++) {
        total += (float)array_tracker.arrays[i].unique_slots / (float)MAX_INDEX_SLOTS;
    }
    return total / (float)array_tracker.num_arrays;
}

// 重置状态追踪
void __afl_array_state_reset(void) {
    array_tracker.num_arrays = 0;
}

// 打印状态报告
void __afl_array_state_print_report(void) {
    fprintf(stderr, "\n===== Array State Report =====\n");
    fprintf(stderr, "Tracked arrays: %d\n", array_tracker.num_arrays);
    fprintf(stderr, "Overall diversity score: %.4f\n", __afl_array_state_get_diversity_score());
    for (int i = 0; i < array_tracker.num_arrays; i++) {
        array_state_t *arr = &array_tracker.arrays[i];
        fprintf(stderr, "  [%d] ArrayID=0x%08x name=%s accesses=%u "
                "range=[%llu, %llu] unique_slots=%u oob=%d\n",
            i, arr->array_id, arr->name,
            arr->access_count,
            (unsigned long long)arr->min_index,
            (unsigned long long)arr->max_index,
            arr->unique_slots, arr->has_oob);
    }
    fprintf(stderr, "===============================\n");
}
