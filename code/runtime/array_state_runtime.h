// array_state_runtime.h
// 数组状态运行时追踪库 — 头文件

#ifndef ARRAY_STATE_RUNTIME_H
#define ARRAY_STATE_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 配置常量 ====================
#define MAX_TRACKED_ARRAYS  64     // 最多追踪的数组数
#define MAX_INDEX_SLOTS     256    // 索引槽位数

// ==================== 核心 API ====================

// 被 LLVM Pass 插桩调用，记录数组访问
void __afl_report_array_state(uint32_t array_id, int64_t index,
                              uint32_t access_type, uint32_t line);

// ==================== 状态查询 API ====================

// 获取已追踪的数组数量
int  __afl_get_num_arrays(void);

// 获取指定数组的唯一索引访问数（状态探索率）
uint32_t __afl_get_unique_slots(uint32_t array_id);

// 获取指定数组的总访问次数
uint64_t __afl_get_total_accesses(uint32_t array_id);

// 获取指定数组的最小/最大索引
int64_t  __afl_get_min_index(uint32_t array_id);
int64_t  __afl_get_max_index(uint32_t array_id);

// 检查是否发生过越界
int      __afl_has_oob(uint32_t array_id);

// ==================== 多样性度量 API ====================

// 计算香农熵: H = -sum(p_i * log2(p_i))
float __afl_get_entropy(uint32_t array_id);

// 计算归一化信息熵得分 E ∈ [0, 1]
float __afl_get_entropy_score(void);

// 计算状态多样性得分 SDS = avg(unique_slots / MAX_INDEX_SLOTS)
float __afl_get_diversity_score(void);

// ==================== 状态管理 API ====================

// 重置状态追踪
void __afl_reset_state(void);

// 输出状态报告到 stderr
void __afl_print_state_report(void);

// 将状态指标写入文件（供 AFLGO 调度使用）
void __afl_write_feedback(const char *path);

#ifdef __cplusplus
}
#endif

#endif // ARRAY_STATE_RUNTIME_H
