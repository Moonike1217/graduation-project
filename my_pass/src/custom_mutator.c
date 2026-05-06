// custom_mutator.c
// AFL++ 自定义变异器 — 数组状态引导的定向变异
//
// 实现两阶段定向变异策略（论文第3章）:
//   阶段1: 边界值探测 — 用预定义的边界值尝试触发数组越界
//   阶段2: 邻近值探索 — 在已触发访问的索引附近搜索未探索状态
//
// 编译:
//   AFL_CUSTOM_MUTATOR_LIBRARY=/path/to/custom_mutator.so afl-fuzz ...
//
// 注意：该变异器需要与 AFL++ 的 libdislocator（越界检测）配合使用效果最佳

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// ============================================================
// 边界值弹药库（论文 §3.4.1 专用变异算子）
// 覆盖各种边界情况：负值、零、边界、越界、极大值
// ============================================================

typedef enum {
    STAGE_BOUNDARY = 0,  // 阶段1: 边界值探测
    STAGE_NEARBY,        // 阶段2: 邻近值探索
    STAGE_COUNT
} mutation_stage_t;

static const char *boundary_values[] = {
    // 负值越界
    "-9999", "-100", "-33", "-32", "-31",
    "-5", "-2", "-1",
    // 边界值
    "0", "1", "2",
    // 小数组边界 (10)
    "8", "9", "10", "11", "12",
    // 中等数组边界 (32)
    "30", "31", "32", "33", "34",
    // 大值/严重越界
    "100", "500", "9999", "65535", "999999"
};
static const int num_boundaries = sizeof(boundary_values) / sizeof(boundary_values[0]);

// 状态结构体
typedef struct {
    uint8_t     *buf;               // 复用缓冲区
    size_t       buf_size;          // 缓冲区大小
    int          stage;             // 当前变异阶段
    int          stage_idx;         // 阶段内索引
    int          last_success;      // 上次成功触发新状态的值
    int          seeds_processed;   // 已处理的种子数
    unsigned int seed;              // 随机种子
} mutator_state_t;

// ============================================================
// AFL++ 自定义变异器 API
// ============================================================

void* afl_custom_init(void *afl, unsigned int seed) {
    (void)afl;
    mutator_state_t *state = (mutator_state_t *)calloc(1, sizeof(mutator_state_t));
    if (!state) return NULL;

    state->buf = (uint8_t *)malloc(128);
    if (!state->buf) {
        free(state);
        return NULL;
    }
    state->buf_size = 128;
    state->stage = STAGE_BOUNDARY;
    state->stage_idx = 0;
    state->last_success = -1;
    state->seeds_processed = 0;
    state->seed = seed;
    srand(seed + time(NULL));

    return (void*)state;
}

unsigned int afl_custom_fuzz_count(void *data, const uint8_t *buf, size_t buf_size) {
    (void)buf; (void)buf_size;
    mutator_state_t *state = (mutator_state_t *)data;

    // 阶段1: 每个种子尝试所有边界值
    if (state->stage == STAGE_BOUNDARY) {
        return num_boundaries;  // 每个种子对所有边界值进行测试
    }
    // 阶段2: 邻近探索，尝试更多组合
    return 20;
}

size_t afl_custom_fuzz(void *data, uint8_t *buf, size_t buf_size, uint8_t **out_buf) {
    mutator_state_t *state = (mutator_state_t *)data;
    size_t len;

    // === 阶段1: 边界值探测 ===
    // 用预定义的边界值替换整个输入，触发数组越界
    if (state->stage == STAGE_BOUNDARY) {
        int idx = state->stage_idx % num_boundaries;
        state->stage_idx++;

        const char *val = boundary_values[idx];
        len = strlen(val);
        memcpy(state->buf, val, len);
        state->buf[len] = '\0';
        *out_buf = state->buf;
        return len;
    }

    // === 阶段2: 邻近值探索 ===
    // 在原输入值附近的整数进行探索
    {
        // 提取原始整数值
        char tmp[64] = {0};
        size_t copy_len = buf_size < 63 ? buf_size : 63;
        memcpy(tmp, buf, copy_len);
        tmp[copy_len] = '\0';
        int base_val = atoi(tmp);

        // 生成邻近值: base_val + offset
        int offsets[] = {-10, -5, -3, -2, -1, 1, 2, 3, 5, 10, 20, 50, 100, -100, 500, -500, 1000, 2000, -999, 9999};
        int num_offsets = sizeof(offsets) / sizeof(offsets[0]);
        int offset_idx = state->stage_idx % num_offsets;
        state->stage_idx++;

        int new_val = base_val + offsets[offset_idx];
        // 防止溢出
        if (new_val < -1000000) new_val = -1000000;
        if (new_val > 1000000) new_val = 1000000;

        int printed = snprintf((char*)state->buf, state->buf_size, "%d", new_val);
        len = (size_t)printed;
        *out_buf = state->buf;
        return len;
    }
}

void afl_custom_deinit(void *data) {
    mutator_state_t *state = (mutator_state_t *)data;
    if (state) {
        if (state->buf) free(state->buf);
        free(state);
    }
}
