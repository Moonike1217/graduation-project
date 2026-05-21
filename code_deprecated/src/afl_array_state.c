// afl_array_state.c
// 数组类型变量状态引导模糊测试 — 运行时状态采集库
//
// 本文件实现：
// 1. __afl_report_array() — 被 LLVM Pass 插桩调用，记录数组访问到 AFL++ bitmap
// 2. 数组状态追踪 — 记录哪些 (array_id, index) 组合已被访问
// 3. 状态多样性度量 — 提供接口查询已探索/未探索状态
// 4. 反馈文件写入 — 每次执行结束时将状态指标写入文件，供 custom mutator 读取
//
// 可以通过定义 AFL_RUNTIME=1 来启用 AFL++ bitmap 写入；
// 否则（独立/测试模式）仅做状态追踪并输出到 stderr。

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// 反馈文件路径（可通过环境变量 AFL_ARRAY_FEEDBACK_FILE 覆盖）
#ifndef FEEDBACK_FILE_PATH
#define FEEDBACK_FILE_PATH "/tmp/afl_array_feedback.txt"
#endif

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
    uint32_t    slot_access_count[MAX_INDEX_SLOTS]; // 每个槽位的访问次数（用于熵计算）
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

        // 记录索引访问（用取模定位到槽位）
        uint32_t slot = ((uint64_t)index) % MAX_INDEX_SLOTS;
        if (!arr->visited_slots[slot]) {
            arr->visited_slots[slot] = 1;
            arr->unique_slots++;
        }
        arr->slot_access_count[slot]++; // 为信息熵度量统计频率

        // 检测越界访问：负索引或过大正索引标记为越界
        if (index < 0 || (uint64_t)index > MAX_INDEX_SLOTS * 16) {
            arr->has_oob = 1;
        }
    }

    // === 3. 独立测试模式：输出到 stderr ===
#if !AFL_RUNTIME
    const char *type_str = "unknown";
    if ((access_type & 3) == 3)      type_str = "read+write";
    else if (access_type & 1)        type_str = "read";
    else if (access_type & 2)        type_str = "write";
    fprintf(stderr, "  [ArrayAccess] array_id=0x%08x index=%lld type=%s line=%u\n",
        array_id, (long long)index, type_str, line);
    fprintf(stderr, "                idx=%lld\n", (long long)index);
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

// ============================================================
// 信息熵度量 API（论文 §3.4.2）
// ============================================================

// 计算指定数组的访问熵 H = -sum(p_i * log2(p_i))
float __afl_array_state_get_entropy(uint32_t array_id) {
    array_state_t *arr = find_or_create_array(array_id, NULL);
    if (!arr || arr->access_count == 0) return 0.0f;

    float entropy = 0.0f;
    for (int i = 0; i < MAX_INDEX_SLOTS; i++) {
        if (arr->slot_access_count[i] > 0) {
            float p = (float)arr->slot_access_count[i] / (float)arr->access_count;
            entropy -= p * log2f(p);
        }
    }
    return entropy;
}

// 计算全体数组的归一化信息熵得分 E ∈ [0, 1]
// 对每个数组: H_normalized = H / log2(MAX_INDEX_SLOTS)
// 整体得分: 所有数组归一化熵的平均值
float __afl_array_state_get_entropy_score(void) {
    if (array_tracker.num_arrays == 0) return 0.0f;
    float total = 0.0f;
    float max_entropy = log2f((float)MAX_INDEX_SLOTS); // log2(256) = 8.0

    for (int i = 0; i < array_tracker.num_arrays; i++) {
        array_state_t *arr = &array_tracker.arrays[i];
        if (arr->access_count == 0) continue;

        float arr_entropy = 0.0f;
        for (int j = 0; j < MAX_INDEX_SLOTS; j++) {
            if (arr->slot_access_count[j] > 0) {
                float p = (float)arr->slot_access_count[j] / (float)arr->access_count;
                arr_entropy -= p * log2f(p);
            }
        }
        total += arr_entropy / max_entropy;
    }
    return total / (float)array_tracker.num_arrays;
}

// ============================================================
// 融合评估 API（论文 §3.5）
// Q = alpha * coverage_ratio + (1 - alpha) * entropy_score
// ============================================================

float __afl_array_state_get_fusion_score(float coverage_ratio) {
    float entropy_score = __afl_array_state_get_entropy_score();
    float alpha = 0.6f;
    return alpha * coverage_ratio + (1.0f - alpha) * entropy_score;
}

// 重置状态追踪
void __afl_array_state_reset(void) {
    array_tracker.num_arrays = 0;
}

// ============================================================
// 反馈文件写入（供 custom mutator 读取实时状态指标）
// ============================================================

// 获取反馈文件路径（支持环境变量覆盖）
static const char* get_feedback_path(void) {
    const char *env = getenv("AFL_ARRAY_FEEDBACK_FILE");
    return env ? env : FEEDBACK_FILE_PATH;
}

// 将当前状态指标写入反馈文件
// 格式: "SDS=<float> ENTROPY=<float> NUM_ARRAYS=<int> FUSION=<float>"
// custom_mutator 通过读取此文件获取真实的状态探索进度
void __afl_array_state_write_feedback(void) {
    const char *path = get_feedback_path();
    FILE *fp = fopen(path, "w");
    if (!fp) return;

    float sds = __afl_array_state_get_diversity_score();
    float entropy = __afl_array_state_get_entropy_score();
    int num = __afl_array_state_get_num_arrays();
    // 融合得分中覆盖率部分由调用方提供，此处用熵得分作为近似
    float fusion = __afl_array_state_get_fusion_score(0.0f);

    fprintf(fp, "SDS=%.6f\n", sds);
    fprintf(fp, "ENTROPY=%.6f\n", entropy);
    fprintf(fp, "NUM_ARRAYS=%d\n", num);
    fprintf(fp, "FUSION=%.6f\n", fusion);

    fclose(fp);
}

// 打印状态报告（同时写入反馈文件）
void __afl_array_state_print_report(void) {
    float entropy_score = __afl_array_state_get_entropy_score();
    float fusion_score = __afl_array_state_get_fusion_score(0.5f); // 测试模式取默认覆盖率 0.5
    fprintf(stderr, "\n===== Array State Report =====\n");
    fprintf(stderr, "Tracked arrays: %d\n", array_tracker.num_arrays);
    fprintf(stderr, "Overall diversity score (SDS): %.4f\n", __afl_array_state_get_diversity_score());
    fprintf(stderr, "Overall entropy score: %.4f  (normalized, max=1.0)\n", entropy_score);
    fprintf(stderr, "Fusion score (alpha=0.6, cov_ratio=0.5): %.4f\n", fusion_score);
    for (int i = 0; i < array_tracker.num_arrays; i++) {
        array_state_t *arr = &array_tracker.arrays[i];
        float arr_entropy = __afl_array_state_get_entropy(arr->array_id);
        fprintf(stderr, "  [%d] ArrayID=0x%08x name=%s accesses=%u "
                "range=[%llu, %llu] unique_slots=%u entropy=%.4f oob=%d\n",
            i, arr->array_id, arr->name,
            arr->access_count,
            (unsigned long long)arr->min_index,
            (unsigned long long)arr->max_index,
            arr->unique_slots, arr_entropy, arr->has_oob);
    }
    fprintf(stderr, "===============================\n");

    // 同时将状态指标写入反馈文件，供 custom mutator 读取
    __afl_array_state_write_feedback();
}
