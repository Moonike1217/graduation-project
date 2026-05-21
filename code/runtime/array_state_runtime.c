// array_state_runtime.c
// 数组状态运行时追踪库 — 实现文件
//
// 功能：
//   1. 接收 LLVM Pass 插桩的 __afl_report_array_state 调用
//   2. 追踪各数组的索引访问状态（位图 + 频次统计）
//   3. 计算多样性指标：香农熵、海明距离、欧氏距离
//   4. 提供综合评估得分供 AFLGO 种子调度使用

#include "array_state_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// AFLGo 的共享内存位图指针（由 aflgo-runtime.o 定义）
// 用于将数组状态信息直接写入 AFL 的覆盖位图，使 AFL 将不同的数组状态
// 视为"新覆盖"，从而自然地引导探索更多变量状态组合。
extern unsigned char *__afl_area_ptr;

// ==================== 数据结构 ====================

// 每个数组的状态信息
typedef struct {
    uint32_t    array_id;                  // 数组唯一 ID
    char        name[64];                  // 数组名
    uint32_t    access_count;              // 总访问次数
    int64_t     min_index;                 // 访问过的最小索引
    int64_t     max_index;                 // 访问过的最大索引
    int         has_oob;                   // 是否越界
    uint8_t     visited_slots[MAX_INDEX_SLOTS]; // 索引访问位图
    uint32_t    unique_slots;              // 不同索引槽位数
    uint32_t    slot_access_count[MAX_INDEX_SLOTS]; // 各槽位访问频次
    int64_t     last_index;                // 最新索引值
} array_state_entry;

// 全局追踪器
typedef struct {
    array_state_entry arrays[MAX_TRACKED_ARRAYS];
    int    num_arrays;
} array_tracker_t;

static array_tracker_t tracker = {0};

// 查找或创建数组状态条目
static array_state_entry* find_or_create_array(uint32_t array_id) {
    // 查找已存在的
    for (int i = 0; i < tracker.num_arrays; i++) {
        if (tracker.arrays[i].array_id == array_id) {
            return &tracker.arrays[i];
        }
    }
    // 创建新条目
    if (tracker.num_arrays < MAX_TRACKED_ARRAYS) {
        array_state_entry *arr = &tracker.arrays[tracker.num_arrays++];
        memset(arr, 0, sizeof(array_state_entry));
        arr->array_id = array_id;
        arr->min_index = INT64_MAX;
        arr->max_index = INT64_MIN;
        arr->last_index = 0;
        snprintf(arr->name, sizeof(arr->name), "arr_%u", array_id);
        return arr;
    }
    return NULL;
}

// ==================== 核心 API 实现 ====================

void __afl_report_array_state(uint32_t array_id, int64_t index,
                               uint32_t access_type, uint32_t line) {
    (void)access_type;
    (void)line;

    // 更新状态追踪
    array_state_entry *arr = find_or_create_array(array_id);
    if (!arr) return;

    arr->access_count++;
    arr->last_index = index;

    if (index < arr->min_index) arr->min_index = index;
    if (index > arr->max_index) arr->max_index = index;

    // 更新索引槽位（取模映射）
    uint32_t slot = (uint64_t)index % MAX_INDEX_SLOTS;
    if (!arr->visited_slots[slot]) {
        arr->visited_slots[slot] = 1;
        arr->unique_slots++;
    }
    arr->slot_access_count[slot]++;

    // 越界检测
    if (index < 0) arr->has_oob = 1;
}

// ==================== 状态查询 API ====================

int __afl_get_num_arrays(void) {
    return tracker.num_arrays;
}

uint32_t __afl_get_unique_slots(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
    return arr ? arr->unique_slots : 0;
}

uint64_t __afl_get_total_accesses(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
    return arr ? arr->access_count : 0;
}

int64_t __afl_get_min_index(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
    return arr ? arr->min_index : 0;
}

int64_t __afl_get_max_index(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
    return arr ? arr->max_index : 0;
}

int __afl_has_oob(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
    return arr ? arr->has_oob : 0;
}

// ==================== 多样性度量 API ====================

// 香农熵: H = -sum(p_i * log2(p_i))
float __afl_get_entropy(uint32_t array_id) {
    array_state_entry *arr = find_or_create_array(array_id);
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

// 归一化信息熵得分
float __afl_get_entropy_score(void) {
    if (tracker.num_arrays == 0) return 0.0f;
    float total = 0.0f;
    float max_entropy = log2f((float)MAX_INDEX_SLOTS);

    for (int i = 0; i < tracker.num_arrays; i++) {
        array_state_entry *arr = &tracker.arrays[i];
        if (arr->access_count == 0) continue;
        float ent = __afl_get_entropy(arr->array_id);
        total += ent / max_entropy;
    }
    return total / (float)tracker.num_arrays;
}

// 状态多样性得分 SDS
float __afl_get_diversity_score(void) {
    if (tracker.num_arrays == 0) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < tracker.num_arrays; i++) {
        total += (float)tracker.arrays[i].unique_slots / (float)MAX_INDEX_SLOTS;
    }
    return total / (float)tracker.num_arrays;
}


// ==================== 状态管理 API ====================

void __afl_reset_state(void) {
    tracker.num_arrays = 0;
}

void __afl_print_state_report(void) {
    fprintf(stderr, "\n===== Array State Report =====\n");
    fprintf(stderr, "Tracked arrays: %d\n", tracker.num_arrays);
    fprintf(stderr, "Diversity Score (SDS):     %.4f\n", __afl_get_diversity_score());
    fprintf(stderr, "Entropy Score:             %.4f\n", __afl_get_entropy_score());

    for (int i = 0; i < tracker.num_arrays; i++) {
        array_state_entry *arr = &tracker.arrays[i];
        fprintf(stderr, "  [%d] ID=0x%08x name=%s accesses=%u "
                "range=[%lld, %lld] unique=%u entropy=%.4f oob=%d\n",
            i, arr->array_id, arr->name,
            arr->access_count,
            (long long)arr->min_index, (long long)arr->max_index,
            arr->unique_slots, __afl_get_entropy(arr->array_id),
            arr->has_oob);
    }
    fprintf(stderr, "===============================\n");
}

void __afl_write_feedback(const char *path) {
    if (!path) path = "/tmp/array_feedback.txt";
    FILE *fp = fopen(path, "w");
    if (!fp) return;

    fprintf(fp, "SDS=%.6f\n", __afl_get_diversity_score());
    fprintf(fp, "ENTROPY=%.6f\n", __afl_get_entropy_score());
    fprintf(fp, "NUM_ARRAYS=%d\n", tracker.num_arrays);

    fclose(fp);
}

// 写入单个聚合哈希到位图，表示本次执行的数组状态整体特征
// 不同测试用例的数组访问模式不同 → 不同哈希值 → AFL 视为新覆盖
// 每个测试用例只写一次，避免淹没位图
static void __afl_write_array_bitmap(void) {
    if (!__afl_area_ptr) return;
    if (tracker.num_arrays == 0) return;

    uint64_t state_hash = 0xdeadbeef;
    for (int i = 0; i < tracker.num_arrays; i++) {
        array_state_entry *arr = &tracker.arrays[i];
        state_hash ^= (uint64_t)arr->array_id;
        state_hash ^= (uint64_t)arr->unique_slots * 0x9e3779b97f4a7c15ULL;
        state_hash ^= (uint64_t)arr->has_oob * 0xbf58476d1ce4e5b9ULL;
        state_hash ^= (uint64_t)(arr->max_index - arr->min_index) * 0x85ebca6b;
        state_hash = (state_hash ^ (state_hash >> 31)) * 0x85ebca6b;
        state_hash = (state_hash ^ (state_hash >> 27)) * 0xc4ceb9fe;
    }
    state_hash ^= (uint64_t)tracker.num_arrays * 0x9e3779b9;
    // 注释掉位图写入，让 paths_total 只反映代码边覆盖率
    // 数组状态追踪仍然保留，供自定义变异器通过 /tmp/array_feedback.txt 使用
    // __afl_area_ptr[state_hash & 0xFFFF]++;
}

// atexit 回调：写出反馈文件 + 写聚合哈希到位图
static void __afl_write_feedback_atexit(void) {
    // 写入聚合哈希
    __afl_write_array_bitmap();

    // 写出反馈文件（可选）
    const char *path = getenv("AFL_ARRAY_FEEDBACK_FILE");
    if (path) {
        __afl_write_feedback(path);
    } else if (getenv("AFL_ARRAY_DIVERSITY")) {
        __afl_write_feedback("/tmp/array_feedback.txt");
    }
}

// 构造函数：注册 atexit 写出反馈
__attribute__((constructor))
static void runtime_init(void) {
    // 只要 AFL_ARRAY_FEEDBACK_FILE 或 AFL_ARRAY_DIVERSITY 设置了，就自动写出反馈
    if (getenv("AFL_ARRAY_FEEDBACK_FILE") || getenv("AFL_ARRAY_DIVERSITY")) {
        atexit(__afl_write_feedback_atexit);
    }
}
