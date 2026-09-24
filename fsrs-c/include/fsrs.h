/*
 * fsrs.h — FSRS-6 记忆算法的 C 语言实现（公共 ABI）
 *
 * 算法核心对齐 fsrs-rs 的 FSRS-6（21 参数）推理路径；
 * ABI 风格参照 rs-fsrs-c：
 *   struct 名  : fsrs_ + Rust 中的结构名
 *   enum   名  : fsrs_ + Rust 中的枚举名
 *   方法名     : fsrs_ + 结构名 + _ + 方法名
 * 复杂对象以 opaque pointer + create/destroy 暴露，跨语言边界安全。
 *
 * 全部接口为纯 C（C11），不依赖 Rust 运行时，可静态链接或编译为共享库。
 */
#ifndef FSRS_H
#define FSRS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 版本常量 */

#define FSRS_VERSION_MAJOR 6
#define FSRS_VERSION_MINOR 0
#define FSRS_VERSION_PATCH 0

/** FSRS-6 参数个数（w[0..20]） */
#define FSRS_PARAMETER_COUNT 21

/** 稳定性下界，与 fsrs-rs 的 S_MIN 一致 */
#define FSRS_S_MIN 0.01f
/** 稳定性上界，与 fsrs-rs 的 S_MAX 一致 */
#define FSRS_S_MAX 36500.0f
/** 难度取值区间 */
#define FSRS_D_MIN 1.0f
#define FSRS_D_MAX 10.0f

/* ------------------------------------------------------------------- 枚举 */

/** 复习评分。取值与 py-fsrs / rs-fsrs-nodejs 一致（1-based）。 */
enum fsrs_rating {
    FSRS_RATING_AGAIN = 1, /**< 想不起来（lapse） */
    FSRS_RATING_HARD  = 2, /**< 想起来了，但很吃力（属于成功） */
    FSRS_RATING_GOOD  = 3, /**< 想起来了，有犹豫 */
    FSRS_RATING_EASY  = 4  /**< 完全不费力 */
};
typedef enum fsrs_rating fsrs_rating_t;

/** 错误码：一律用非负整数返回，避免把错误混进 NaN / 空指针。 */
enum fsrs_error {
    FSRS_OK              = 0,
    FSRS_ERR_NULL        = 1, /**< 必需指针为空 */
    FSRS_ERR_PARAM_COUNT = 2, /**< 参数数组长度不是 0 / 21 */
    FSRS_ERR_RATING      = 3, /**< 评分不在 1..4 */
    FSRS_ERR_RETENTION   = 4, /**< 期望保留率不在 (0,1) */
    FSRS_ERR_NUMERIC     = 5, /**< 出现非有限数（NaN / Inf）或非法状态 */
    FSRS_ERR_ALLOC       = 6  /**< 内存分配失败 */
};
typedef enum fsrs_error fsrs_error_t;

/* -------------------------------------------------------- #[repr(C)] 结构 */

/** 记忆状态。字段顺序与 fsrs-rs 的 MemoryState 一致，可直接对应到 FFI。 */
struct fsrs_memory_state {
    float stability;      /**< S：R 从 100% 衰减到 90% 所需天数 */
    float difficulty;     /**< D：难度，恒在 [1,10] */
    float stability_fast; /**< S_fast：同日（短期）模型用的稳定性 */
};
typedef struct fsrs_memory_state fsrs_memory_state_t;

/** 四个评分各自的后继状态与间隔。对应 fsrs-rs 的 NextStates。 */
struct fsrs_next_states {
    struct fsrs_memory_state again;
    struct fsrs_memory_state hard;
    struct fsrs_memory_state good;
    struct fsrs_memory_state easy;
    uint32_t interval_again;
    uint32_t interval_hard;
    uint32_t interval_good;
    uint32_t interval_easy;
};
typedef struct fsrs_next_states fsrs_next_states_t;

/** 一条复习记录：评分为主，delta_days 为距上次复习的天数。 */
struct fsrs_review {
    uint32_t rating;     /**< 取值见 enum fsrs_rating */
    uint32_t delta_days; /**< 首次复习填 0 */
};
typedef struct fsrs_review fsrs_review_t;

/** 调度器：opaque handle，对应 rs-fsrs-c 的 fsrs_FSRS 不透明指针。 */
struct fsrs_scheduler;
typedef struct fsrs_scheduler fsrs_scheduler_t;

/* ------------------------------------------------------- 参数与元信息查询 */

/** 返回 FSRS-6 默认参数数组（21 个 float，静态存储，勿释放）。 */
const float *fsrs_default_parameters(void);

/** 返回参数个数（21）。 */
size_t fsrs_parameter_count(void);

/** 错误码转可读字符串（永不返回 NULL）。 */
const char *fsrs_error_string(int err);

/* --------------------------------------------------------------- 无状态内核
 *
 * 以下函数不分配内存、不持有全局状态，可并发调用；参数数组 w 可为 NULL，
 * 为 NULL 时使用默认参数；非 NULL 时必须指向 FSRS_PARAMETER_COUNT 个有限值，
 * 返回 int 的接口会以 FSRS_ERR_NUMERIC 拒绝含非有限值的 w。
 * 适合嵌入式或已被其他语言管理生命周期的场景。
 */

/** 可提取性 R：给定稳定性与已流逝天数。 */
float fsrs_retrievability(const float *w, float stability, float elapsed_days);

/** 下一间隔（天）：由稳定性与期望保留率反解遗忘曲线。
 *  DR 非法返回 0；结果夹在 [1, FSRS_S_MAX]。 */
uint32_t fsrs_next_interval(const float *w, float stability, float desired_retention);

/** 首次复习后的初始稳定性 S0 = clamp(w[rating-1], S_MIN, S_MAX)。 */
float fsrs_init_stability(const float *w, int rating);

/** 首次复习后的初始难度 D0 = clamp(w4 - e^{w5(G-1)} + 1, 1, 10)。 */
float fsrs_init_difficulty(const float *w, int rating);

/** 难度更新：线性阻尼 + 向 D0(Easy) 的均值回归。 */
float fsrs_next_difficulty(const float *w, float difficulty, int rating);

/**
 * 一次复习后的四个后继状态与对应间隔。
 * current 为 NULL（或 stability <= 0）时按新卡处理；
 * current->stability 会先夹到 [FSRS_S_MIN, FSRS_S_MAX]，
 * difficulty 越出 [1,10] 视为非法状态。
 * 成功时返回 FSRS_OK；w 含非有限值或状态非法返回 FSRS_ERR_NUMERIC，
 * DR 非法返回 FSRS_ERR_RETENTION，out 为空返回 FSRS_ERR_NULL。
 * 仅在返回 FSRS_OK 时写入 *out；返回错误时 *out 保持不变。
 */
int fsrs_next_states(const float *w,
                     float desired_retention,
                     const struct fsrs_memory_state *current,
                     uint32_t elapsed_days,
                     struct fsrs_next_states *out);

/** 按评分从 next_states 中取出对应分支（调用方仍需自己取 interval）。 */
const struct fsrs_memory_state *fsrs_next_states_pick(const struct fsrs_next_states *states,
                                                      int rating);

/** 取 next_states 中某评分对应的间隔（天）。 */
uint32_t fsrs_next_states_interval(const struct fsrs_next_states *states, int rating);

/** 回放整段复习历史，得到最终记忆状态（对应 fsrs-rs 的 memory_state(item, None)）。
 *  count==0 时 *out 清零（仍是新卡）。仅在返回 FSRS_OK 时写入 *out。 */
int fsrs_memory_state(const float *w,
                      const struct fsrs_review *reviews,
                      size_t count,
                      struct fsrs_memory_state *out);

/**
 * 从给定起始记忆态回放历史（对应 fsrs-rs 的 memory_state(item, Some(state))），
 * 用于历史被截断、起始态由 SM-2 等方式估算的卡片。
 * starting 为 NULL 时与 fsrs_memory_state 完全等价；starting 的 stability/difficulty
 * 必须有限（否则返回 FSRS_ERR_NUMERIC），stability <= 0 时首条复习按新卡播种，
 * count == 0 时把 starting 原样写入 *out。
 * 仅在返回 FSRS_OK 时写入 *out；返回错误时 *out 保持不变。
 */
int fsrs_memory_state_from(const float *w,
                           const struct fsrs_memory_state *starting,
                           const struct fsrs_review *reviews,
                           size_t count,
                           struct fsrs_memory_state *out);

/* ------------------------------------------------- FFI 层：opaque scheduler
 *
 * 命名遵循 rs-fsrs-c：fsrs_<struct>_<method>。
 * handle 由 Rust 堆（此处为 C 堆）分配，调用方必须配对 destroy/free。
 */

/**
 * 创建调度器。w 为 NULL 或 n 为 0 时使用默认参数；n 为 21 时复制传入参数，
 * 且 21 个值必须全部有限（否则返回 NULL 并给出 FSRS_ERR_NUMERIC）；
 * 其余长度返回 NULL 并通过 err 给出 FSRS_ERR_PARAM_COUNT。err 可为 NULL。
 */
struct fsrs_scheduler *fsrs_scheduler_new(const float *w, size_t n,
                                          float desired_retention, int *err);

/** 释放调度器；传入 NULL 为空操作。释放后调用方应把指针置空。 */
void fsrs_scheduler_free(struct fsrs_scheduler *scheduler);

/** 取内部参数数组（只读，21 个），随 handle 生命周期有效。 */
const float *fsrs_scheduler_parameters(const struct fsrs_scheduler *scheduler);

/** 取期望保留率。 */
float fsrs_scheduler_desired_retention(const struct fsrs_scheduler *scheduler);

/** 设置期望保留率，合法区间 (0,1)。 */
int fsrs_scheduler_set_desired_retention(struct fsrs_scheduler *scheduler,
                                         float desired_retention);

/** 四个评分的后继状态与间隔。 */
int fsrs_scheduler_next_states(const struct fsrs_scheduler *scheduler,
                               const struct fsrs_memory_state *current,
                               uint32_t elapsed_days,
                               struct fsrs_next_states *out);

/** 复习历史回放。 */
int fsrs_scheduler_memory_state(const struct fsrs_scheduler *scheduler,
                                const struct fsrs_review *reviews,
                                size_t count,
                                struct fsrs_memory_state *out);

/** 从起始记忆态回放历史（截断历史用；starting 为 NULL 时等同上一个函数）。 */
int fsrs_scheduler_memory_state_from(const struct fsrs_scheduler *scheduler,
                                     const struct fsrs_memory_state *starting,
                                     const struct fsrs_review *reviews,
                                     size_t count,
                                     struct fsrs_memory_state *out);

/** 当前可提取性。 */
float fsrs_scheduler_retrievability(const struct fsrs_scheduler *scheduler,
                                    const struct fsrs_memory_state *state,
                                    uint32_t elapsed_days);

/** 给定稳定性求下一间隔。 */
uint32_t fsrs_scheduler_next_interval(const struct fsrs_scheduler *scheduler,
                                      float stability);

#ifdef __cplusplus
}
#endif

#endif /* FSRS_H */
