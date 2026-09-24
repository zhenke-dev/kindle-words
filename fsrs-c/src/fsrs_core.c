/*
 * fsrs_core.c — FSRS-6 纯算法内核
 *
 * 公式基线（三源交叉验证一致）：
 *   [1] fsrs-rs  src/inference_v6.rs      FSRS6_DEFAULT_PARAMETERS（21 参数）
 *   [2] srs-benchmark models/fsrs_v6.py   遗忘曲线 / 短期稳定性 / 失败稳定性
 *   [3] ts-fsrs  packages/fsrs/src/algorithm.ts
 *                                         初始难度 / 难度更新 / 成功稳定性
 *   另由 fsrs-rs 的 memory_state_from_sm2 反解，交叉印证成功稳定性公式：
 *       SInc - 1 = e^{w8} · (11 - D) · S^{-w9} · (e^{w10(1-R)} - 1)
 *
 * 与 FSRS-5 的差异集中在两点：
 *   - 遗忘曲线可学习：decay = -w20（v5 固定 -0.5，factor 固定 19/81）
 *   - 短期稳定性加入幂律：sinc 乘 S^{-w19}（v5 无此项）
 */
#include "fsrs_internal.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------ 默认参数表 */

static const float kDefaultParameters[FSRS_PARAMETER_COUNT] = {
    /* w0..w3  首次复习后的初始稳定性（Again / Hard / Good / Easy） */
    0.212f, 1.2931f, 2.3065f, 8.2956f,
    /* w4..w7  难度相关：基础项 / 评分指数 / 更新速率 / 均值回归权重 */
    6.4133f, 0.8334f, 3.0194f, 0.001f,
    /* w8..w10 成功路径：增益底数 / 饱和指数 / 可提取性影响 */
    1.8722f, 0.1666f, 0.796f,
    /* w11..w14 失败路径：难度因子 / 难度幂 / 增长幂 / 可提取性影响 */
    1.4835f, 0.0614f, 0.2629f, 1.6483f,
    /* w15..w16 评分乘子：Hard 惩罚 / Easy 奖励 */
    0.6014f, 1.8729f,
    /* w17..w19 短期稳定性：评分指数 / 评分偏移 / 幂律指数 */
    0.5425f, 0.0912f, 0.0658f,
    /* w20     遗忘曲线衰减（正值；decay 取其负） */
    0.1542f
};

const float *fsrs_default_parameters(void)
{
    return kDefaultParameters;
}

size_t fsrs_parameter_count(void)
{
    return (size_t)FSRS_PARAMETER_COUNT;
}

const char *fsrs_error_string(int err)
{
    switch (err) {
    case FSRS_OK:              return "ok";
    case FSRS_ERR_NULL:        return "null pointer";
    case FSRS_ERR_PARAM_COUNT: return "parameter count must be 0 or 21";
    case FSRS_ERR_RATING:      return "rating out of range 1..4";
    case FSRS_ERR_RETENTION:   return "desired retention out of range (0,1)";
    case FSRS_ERR_NUMERIC:     return "non-finite or invalid numeric state";
    case FSRS_ERR_ALLOC:       return "allocation failure";
    default:                   return "unknown error";
    }
}

const float *fsrs_internal_pick_parameters(const float *w)
{
    return (w != NULL) ? w : kDefaultParameters;
}

int fsrs_internal_parameters_valid(const float *w)
{
    int i;

    if (w == NULL) {
        return 0;
    }
    for (i = 0; i < FSRS_PARAMETER_COUNT; i++) {
        if (!isfinite((double)w[i])) {
            return 0;
        }
    }
    return 1;
}

/* --------------------------------------------------------------- 数值工具 */

double fsrs_internal_clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * factor = 0.9^(1/decay) - 1，decay = -w20（负数）。
 * 指数在对数空间夹紧到 60，避免 |1/decay| 很大时 exp() 上溢；
 * 与 srs-benchmark 的 (ln0.9/decay).clamp(max=60).exp() - 1 等价。
 */
double fsrs_internal_factor(const float *w)
{
    double decay = -(double)w[20];
    double e;

    if (decay == 0.0) {
        return 0.0; /* 退化保护：不会发生，避免除零 */
    }
    e = FSRS_LN_09 / decay;
    if (e > FSRS_FACTOR_EXP_MAX) {
        e = FSRS_FACTOR_EXP_MAX;
    }
    return exp(e) - 1.0;
}

/* --------------------------------------------------------------- 遗忘曲线 */

double fsrs_internal_retrievability(const float *w, double stability, double elapsed_days)
{
    double decay = -(double)w[20];
    double factor = fsrs_internal_factor(w);
    double s = (stability > (double)FSRS_S_MIN) ? stability : (double)FSRS_S_MIN;
    double t = (elapsed_days > 0.0) ? elapsed_days : 0.0; /* 负数/NaN 归 0，与 Rust f32::max 一致 */
    double base, r;

    /* 曲线按整天计算：与 fsrs-rs 的 t.max(0.0).round() 一致
       （其测试 forgetting_curve_rounds_elapsed_days 显式断言了这一点） */
    t = round(t);
    base = 1.0 + factor * (t / s);
    if (!(base > 0.0)) { /* 含 NaN 保护 */
        return 0.0;
    }
    r = pow(base, decay);
    return fsrs_internal_clamp(r, 0.0, 1.0);
}

float fsrs_retrievability(const float *w, float stability, float elapsed_days)
{
    w = fsrs_internal_pick_parameters(w);
    return (float)fsrs_internal_retrievability(w, (double)stability, (double)elapsed_days);
}

/* ------------------------------------------------------------------- 间隔 */

double fsrs_internal_next_interval(const float *w, double stability, double desired_retention)
{
    double decay = -(double)w[20];
    double factor = fsrs_internal_factor(w);
    double s = (stability > (double)FSRS_S_MIN) ? stability : (double)FSRS_S_MIN;
    double interval;

    if (factor == 0.0) {
        return 1.0;
    }
    interval = (s / factor) * (pow((double)desired_retention, 1.0 / decay) - 1.0);
    interval = round(interval);
    if (!isfinite(interval)) {
        /* 极端参数下 pow() 可上溢到 inf：按最长间隔处理，
           避免随后的 uint32 转换成为未定义行为 */
        interval = (double)FSRS_S_MAX;
    }
    /* 夹到 [1, S_MAX]：上界与 fsrs-rs 的 next_interval_scalar 的
       clamp(0.0, S_MAX) 对齐，同时保证结果落在 uint32 可表示范围内 */
    return fsrs_internal_clamp(interval, 1.0, (double)FSRS_S_MAX);
}

uint32_t fsrs_next_interval(const float *w, float stability, float desired_retention)
{
    w = fsrs_internal_pick_parameters(w);
    if (!(desired_retention > 0.0f && desired_retention < 1.0f)) {
        return 0; /* 非法保留率：无法给出有意义间隔 */
    }
    return (uint32_t)fsrs_internal_next_interval(w, (double)stability,
                                                 (double)desired_retention);
}

/* ------------------------------------------------------- 首次复习的初始值 */

float fsrs_init_stability(const float *w, int rating)
{
    w = fsrs_internal_pick_parameters(w);
    if (rating < FSRS_RATING_AGAIN || rating > FSRS_RATING_EASY) {
        return 0.0f;
    }
    return (float)fsrs_internal_clamp((double)w[rating - 1],
                                      (double)FSRS_S_MIN, (double)FSRS_S_MAX);
}

double fsrs_internal_init_difficulty_raw(const float *w, int rating)
{
    /* D0(G) = w4 - e^{w5(G-1)} + 1；G=1 时恰为 w4。此处不夹紧，供均值回归取用。 */
    return (double)w[4] - exp((double)w[5] * ((double)rating - 1.0)) + 1.0;
}

float fsrs_init_difficulty(const float *w, int rating)
{
    w = fsrs_internal_pick_parameters(w);
    if (rating < FSRS_RATING_AGAIN || rating > FSRS_RATING_EASY) {
        return 0.0f;
    }
    return (float)fsrs_internal_clamp(fsrs_internal_init_difficulty_raw(w, rating),
                                      (double)FSRS_D_MIN, (double)FSRS_D_MAX);
}

/* --------------------------------------------------------------- 难度更新 */

double fsrs_internal_next_difficulty(const float *w, double difficulty, int rating)
{
    double delta_d = -(double)w[6] * ((double)rating - 3.0);
    double damped  = difficulty + delta_d * ((10.0 - difficulty) / 9.0); /* 线性阻尼 */
    double d0_easy = fsrs_internal_clamp(fsrs_internal_init_difficulty_raw(w, FSRS_RATING_EASY),
                                         (double)FSRS_D_MIN, (double)FSRS_D_MAX);
    double reverted = (double)w[7] * d0_easy + (1.0 - (double)w[7]) * damped; /* 均值回归 */

    return fsrs_internal_clamp(reverted, (double)FSRS_D_MIN, (double)FSRS_D_MAX);
}

float fsrs_next_difficulty(const float *w, float difficulty, int rating)
{
    w = fsrs_internal_pick_parameters(w);
    if (rating < FSRS_RATING_AGAIN || rating > FSRS_RATING_EASY) {
        return difficulty;
    }
    return (float)fsrs_internal_next_difficulty(w, (double)difficulty, rating);
}

/* ----------------------------------------------------------- 稳定性：成功 */

double fsrs_internal_stability_after_success(const float *w, double d, double s,
                                             double r, int rating)
{
    double hard_penalty = (rating == FSRS_RATING_HARD) ? (double)w[15] : 1.0;
    double easy_bonus   = (rating == FSRS_RATING_EASY) ? (double)w[16] : 1.0;
    double alpha;

    alpha = 1.0
          + exp((double)w[8])
          * (11.0 - d)
          * pow(s, -(double)w[9])
          * (exp((1.0 - r) * (double)w[10]) - 1.0)
          * hard_penalty
          * easy_bonus;

    return fsrs_internal_clamp(s * alpha, (double)FSRS_S_MIN, (double)FSRS_S_MAX);
}

/* ----------------------------------------------------------- 稳定性：失败 */

double fsrs_internal_stability_after_failure(const float *w, double d, double s, double r)
{
    double s_after_fail = (double)w[11]
                        * pow(d, -(double)w[12])
                        * (pow(s + 1.0, (double)w[13]) - 1.0)
                        * exp((1.0 - r) * (double)w[14]);
    /* 失败后的地板：不能低于上一稳定性按短期因子收缩后的值 */
    double s_floor = s / exp((double)w[17] * (double)w[18]);
    double next = (s_after_fail < s_floor) ? s_after_fail : s_floor;

    if (!(next > (double)FSRS_S_MIN)) { /* 含 NaN 保护 */
        next = (double)FSRS_S_MIN;
    } else if (next > (double)FSRS_S_MAX) {
        /* 上界：与 fsrs-rs 的 stability_after_failure_scalar 的
           clamp(S_MIN, S_MAX) 对齐，兑现“每次推进后统一 clamp”的承诺 */
        next = (double)FSRS_S_MAX;
    }
    return next;
}

/* ----------------------------------------------------------- 稳定性：短期 */

double fsrs_internal_stability_short_term(const float *w, double s, int rating)
{
    double sinc = pow(s, -(double)w[19])
                * exp((double)w[17] * ((double)rating - 3.0 + (double)w[18]));
    /* Hard/Good/Easy 保证不下降（sinc 至少为 1）；Again 允许下降 */
    double masked = (rating >= FSRS_RATING_HARD && sinc < 1.0) ? 1.0 : sinc;

    return fsrs_internal_clamp(s * masked, (double)FSRS_S_MIN, (double)FSRS_S_MAX);
}

/* --------------------------------------------------------------- 一步推进 */

/**
 * 单个评分的状态推进。
 * current 为 NULL 或 stability <= 0 时按新卡（首次复习）处理：
 * 直接取 S0 / D0，不经过遗忘曲线（首次的 Again 不是 lapse）。
 * 在历史回放的起点上与 fsrs-rs 等价（其 step 在 nth==0 且 stability==0 时
 * 同样覆盖为初始值）；差别只在 next_states：上游对 Some(state) 且 stability==0
 * 的退化输入不播种、夹到 S_MIN 照常计算，本库统一按新卡播种——两种选择都只
 * 影响该退化输入，见 README §7.8。
 */
static int fsrs_step(const float *w,
                     const struct fsrs_memory_state *current,
                     uint32_t elapsed_days,
                     int rating,
                     struct fsrs_memory_state *out)
{
    double s, d, next_s, next_d;
    int is_new = (current == NULL) || !(current->stability > 0.0f);

    if (is_new) {
        /* 新卡：首次复习直接播种 */
        next_s = fsrs_internal_clamp((double)w[rating - 1],
                                     (double)FSRS_S_MIN, (double)FSRS_S_MAX);
        next_d = fsrs_internal_clamp(fsrs_internal_init_difficulty_raw(w, rating),
                                     (double)FSRS_D_MIN, (double)FSRS_D_MAX);
    } else {
        s = (double)current->stability;
        d = (double)current->difficulty;
        if (!isfinite(s) || !isfinite(d) || d < (double)FSRS_D_MIN - 1e-6
            || d > (double)FSRS_D_MAX + 1e-6) {
            return FSRS_ERR_NUMERIC;
        }
        /* 输入稳定性先夹到合法区间（与 fsrs-rs 的 step 入口一致），
           保证随后任何分支的输出都不会越过 S_MAX */
        s = fsrs_internal_clamp(s, (double)FSRS_S_MIN, (double)FSRS_S_MAX);

        /* 难度：无论成败都更新 */
        next_d = fsrs_internal_next_difficulty(w, d, rating);

        if (elapsed_days == 0) {
            /* 同日复习：走短期（same-day）模型 */
            next_s = fsrs_internal_stability_short_term(w, s, rating);
        } else {
            double r = fsrs_internal_retrievability(w, s, (double)elapsed_days);
            if (rating == FSRS_RATING_AGAIN) {
                next_s = fsrs_internal_stability_after_failure(w, d, s, r);
            } else {
                next_s = fsrs_internal_stability_after_success(w, d, s, r, rating);
            }
        }
    }

    /* FSRS-6 下 stability_fast 恒等于 stability（与 fsrs-rs 的 v6 路径一致） */
    out->stability      = (float)next_s;
    out->stability_fast = (float)next_s;
    out->difficulty     = (float)next_d;

    /* 所有分支（含新卡播种）统一验收，确保 FSRS_OK 不会伴随 NaN/Inf 出库 */
    if (!isfinite(out->stability) || !isfinite(out->difficulty)
        || !isfinite(out->stability_fast)) {
        return FSRS_ERR_NUMERIC;
    }
    return FSRS_OK;
}

/* --------------------------------------------------------- next_states API */

int fsrs_next_states(const float *w,
                     float desired_retention,
                     const struct fsrs_memory_state *current,
                     uint32_t elapsed_days,
                     struct fsrs_next_states *out)
{
    static const int kRatings[4] = {
        FSRS_RATING_AGAIN, FSRS_RATING_HARD, FSRS_RATING_GOOD, FSRS_RATING_EASY
    };
    struct fsrs_next_states tmp; /* 全部算完再一次性提交给 *out */
    int i;

    if (out == NULL) {
        return FSRS_ERR_NULL;
    }
    if (!(desired_retention > 0.0f && desired_retention < 1.0f)) {
        return FSRS_ERR_RETENTION;
    }
    if (current != NULL
        && (!isfinite(current->stability) || !isfinite(current->difficulty))) {
        return FSRS_ERR_NUMERIC;
    }

    w = fsrs_internal_pick_parameters(w);
    if (!fsrs_internal_parameters_valid(w)) {
        return FSRS_ERR_NUMERIC;
    }

    for (i = 0; i < 4; i++) {
        struct fsrs_memory_state *slot;
        uint32_t *interval_slot;
        int rc;

        switch (kRatings[i]) {
        case FSRS_RATING_AGAIN: slot = &tmp.again; interval_slot = &tmp.interval_again; break;
        case FSRS_RATING_HARD:  slot = &tmp.hard;  interval_slot = &tmp.interval_hard;  break;
        case FSRS_RATING_GOOD:  slot = &tmp.good;  interval_slot = &tmp.interval_good;  break;
        default:                slot = &tmp.easy;  interval_slot = &tmp.interval_easy;  break;
        }

        rc = fsrs_step(w, current, elapsed_days, kRatings[i], slot);
        if (rc != FSRS_OK) {
            return rc;
        }
        *interval_slot = (uint32_t)fsrs_internal_next_interval(w, (double)slot->stability,
                                                               (double)desired_retention);
    }

    *out = tmp; /* 成功才提交：所有错误路径都在此之前返回，*out 保持不变 */
    return FSRS_OK;
}

const struct fsrs_memory_state *fsrs_next_states_pick(const struct fsrs_next_states *states,
                                                      int rating)
{
    if (states == NULL) {
        return NULL;
    }
    switch (rating) {
    case FSRS_RATING_AGAIN: return &states->again;
    case FSRS_RATING_HARD:  return &states->hard;
    case FSRS_RATING_GOOD:  return &states->good;
    case FSRS_RATING_EASY:  return &states->easy;
    default:                return NULL;
    }
}

uint32_t fsrs_next_states_interval(const struct fsrs_next_states *states, int rating)
{
    if (states == NULL) {
        return 0;
    }
    switch (rating) {
    case FSRS_RATING_AGAIN: return states->interval_again;
    case FSRS_RATING_HARD:  return states->interval_hard;
    case FSRS_RATING_GOOD:  return states->interval_good;
    case FSRS_RATING_EASY:  return states->interval_easy;
    default:                return 0;
    }
}

/* ---------------------------------------------------------- 历史回放 API */

/**
 * 回放整段复习历史，得到最终记忆状态。
 * starting 非 NULL 时从该起始态出发（对应 fsrs-rs 的
 * memory_state(item, Some(state))），为 NULL 时从新卡出发。
 *
 * 仅在返回 FSRS_OK 时写入 *out；返回错误时 *out 保持不变。
 */
int fsrs_memory_state_from(const float *w,
                           const struct fsrs_memory_state *starting,
                           const struct fsrs_review *reviews,
                           size_t count,
                           struct fsrs_memory_state *out)
{
    struct fsrs_memory_state state;
    int have_state = 0;
    size_t i;

    if (out == NULL) {
        return FSRS_ERR_NULL;
    }
    if (count > 0 && reviews == NULL) {
        return FSRS_ERR_NULL;
    }
    if (starting != NULL) {
        /* 与上游 validate_state 一致：只要求有限；S<=0 由 fsrs_step 按新卡播种，
           D 越界在首次真正推进时由 fsrs_step 拒绝 */
        if (!isfinite(starting->stability) || !isfinite(starting->difficulty)) {
            return FSRS_ERR_NUMERIC;
        }
        state = *starting;
        have_state = 1;
    }

    if (count == 0) {
        if (have_state) {
            *out = state; /* 无历史但有起始态：原样透传 */
        } else {
            memset(out, 0, sizeof(*out)); /* 无历史且无起始态：仍是新卡 */
        }
        return FSRS_OK;
    }

    w = fsrs_internal_pick_parameters(w);

    for (i = 0; i < count; i++) {
        struct fsrs_next_states step;
        const struct fsrs_memory_state *picked;
        int rc;

        if (reviews[i].rating < FSRS_RATING_AGAIN || reviews[i].rating > FSRS_RATING_EASY) {
            return FSRS_ERR_RATING;
        }
        rc = fsrs_next_states(w, 0.9f,
                              have_state ? &state : NULL,
                              reviews[i].delta_days, &step);
        if (rc != FSRS_OK) {
            return rc;
        }
        picked = fsrs_next_states_pick(&step, (int)reviews[i].rating);
        if (picked == NULL) {
            return FSRS_ERR_RATING;
        }
        state = *picked;
        have_state = 1;
    }

    *out = state;
    return FSRS_OK;
}

int fsrs_memory_state(const float *w,
                      const struct fsrs_review *reviews,
                      size_t count,
                      struct fsrs_memory_state *out)
{
    return fsrs_memory_state_from(w, NULL, reviews, count, out);
}
