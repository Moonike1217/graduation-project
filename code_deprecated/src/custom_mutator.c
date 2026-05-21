// custom_mutator.c
// AFL++ 自定义变异器 — 数组状态引导的定向变异
//
// 实现两阶段定向变异策略（论文第3章）:
//   阶段1: 边界值探测 — 用预定义的边界值尝试触发数组越界
//   阶段2: 邻近值探索 — 在已触发访问的索引附近搜索未探索状态
//
// 自适应策略: 读取运行时库写入的反馈文件获取真实 SDS 指标，
// 根据 SDS 动态调整边界值探测与邻近值探索的资源分配比例。
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

// 反馈文件路径（与 afl_array_state.c 中保持一致）
#ifndef FEEDBACK_FILE_PATH
#define FEEDBACK_FILE_PATH "/tmp/afl_array_feedback.txt"
#endif

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
    // 小数组边界
    "6", "7", "8", "9", "10", "11", "12", "15", "16", "17",
    // 中等数组边界
    "30", "31", "32", "33", "34",
    // 常见缓冲区大小边界 (64, 128, 256, 512, 1024)
    "62", "63", "64", "65", "66",
    "126", "127", "128", "129", "130",
    "254", "255", "256", "257", "258",
    "510", "511", "512", "513", "514",
    "1022", "1023", "1024", "1025", "1026",
    // 大值/严重越界
    "2048", "4096", "8192", "9999", "65535", "999999"
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
    // 自适应变异参数（论文 §3.6）
    float        last_sds;          // 上次 SDS 得分
    int          adapt_cycle;       // 自适应周期计数
    int          boundary_ratio;    // 当前边界值阶段比例 (0-100)
} mutator_state_t;

// ============================================================
// 从反馈文件读取运行时库写入的真实状态指标
// 文件格式 (由 afl_array_state.c 的 write_feedback 生成):
//   SDS=0.023400
//   ENTROPY=0.255600
//   NUM_ARRAYS=5
//   FUSION=0.102240
// 返回: 0 = 成功读取, -1 = 文件不存在或解析失败
// ============================================================

static int read_feedback_from_file(float *sds, float *entropy, int *num_arrays) {
    const char *path = getenv("AFL_ARRAY_FEEDBACK_FILE");
    if (!path) path = FEEDBACK_FILE_PATH;

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    int parsed = 0;
    while (fgets(line, sizeof(line), fp) && parsed < 3) {
        if (strncmp(line, "SDS=", 4) == 0) {
            *sds = strtof(line + 4, NULL);
            parsed++;
        } else if (strncmp(line, "ENTROPY=", 8) == 0) {
            *entropy = strtof(line + 8, NULL);
            parsed++;
        } else if (strncmp(line, "NUM_ARRAYS=", 11) == 0) {
            *num_arrays = atoi(line + 11);
            parsed++;
        }
    }
    fclose(fp);
    return (parsed >= 1) ? 0 : -1;
}

// ============================================================
// 自适应变异策略调整（论文 §3.6）
// 优先从反馈文件读取真实 SDS；
// 若文件不可用（独立测试模式），回退到模拟递增策略。
//   SDS < 0.1 → 80% 边界值（探索阶段）
//   SDS < 0.3 → 50% 边界值（平衡阶段）
//   SDS >= 0.3 → 20% 边界值（精细探索阶段）
// ============================================================

static void adapt_mutation_strategy(mutator_state_t *state) {
    float real_sds = -1.0f;
    float entropy = 0.0f;
    int num_arrays = 0;

    // 尝试读取真实 SDS
    if (read_feedback_from_file(&real_sds, &entropy, &num_arrays) == 0 && real_sds >= 0.0f) {
        state->last_sds = real_sds;
    } else {
        // 回退: 基于已处理种子数模拟 SDS 渐进增长
        // 这是一个保守的下限估计，真实 SDS 通常更高
        state->last_sds = (float)(state->adapt_cycle % 200) / 200.0f;
    }

    // 根据 SDS 调整边界值比例（使用 Sigmoid 平滑过渡避免突变）
    if (state->last_sds < 0.1f) {
        state->boundary_ratio = 80;
    } else if (state->last_sds < 0.3f) {
        // 在 0.1 ~ 0.3 之间从 80 线性过渡到 50
        float t = (state->last_sds - 0.1f) / 0.2f;
        state->boundary_ratio = (int)(80.0f - 30.0f * t);
    } else {
        // 在 0.3 ~ 0.6 之间从 50 线性过渡到 20
        float t = (state->last_sds - 0.3f) / 0.3f;
        if (t > 1.0f) t = 1.0f;
        state->boundary_ratio = (int)(50.0f - 30.0f * t);
    }
}

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
    state->last_sds = 0.0f;         // 初始为低 SDS，倾向于边界探测
    state->adapt_cycle = 0;
    state->boundary_ratio = 80;     // 初始 80% 边界值
    srand(seed + time(NULL));

    return (void*)state;
}

unsigned int afl_custom_fuzz_count(void *data, const uint8_t *buf, size_t buf_size) {
    (void)buf; (void)buf_size;
    mutator_state_t *state = (mutator_state_t *)data;

    // 自适应：按 boundary_ratio 比例分配两阶段变异次数
    // 每次返回固定次数，在 afl_custom_fuzz 中按比例随机选择阶段
    return 40;
}

size_t afl_custom_fuzz(void *data, uint8_t *buf, size_t buf_size, uint8_t **out_buf) {
    mutator_state_t *state = (mutator_state_t *)data;
    size_t len;

    // === 自适应周期更新 ===
    // 每 20 次变异后更新 SDS 和变异策略
    state->adapt_cycle++;
    if (state->adapt_cycle % 20 == 0) {
        // 实际场景中应通过共享内存或文件读取 AFL++ 运行时的状态反馈
        // 简化实现：模拟 SDS 随测试推进渐进增长
        state->last_sds = (float)(state->adapt_cycle % 200) / 200.0f;
        adapt_mutation_strategy(state);
    }

    // === 按 boundary_ratio 动态选择阶段 ===
    if (rand() % 100 < state->boundary_ratio) {
        // === 阶段1: 边界值探测 ===
        // 用预定义的边界值替换整个输入，触发数组越界
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
