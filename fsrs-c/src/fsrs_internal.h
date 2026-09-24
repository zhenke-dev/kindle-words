/*
 * fsrs_internal.h — 内核内部声明（不对外暴露）
 *
 * 约定：所有中间运算使用 double，仅在写入结构体时降为 float。
 * 这与 fsrs-rs 的做法一致（其遗忘曲线即以 f64 计算后再转 f32），
 * 可显著降低 float 累乘带来的漂移。
 */
#ifndef FSRS_INTERNAL_H
#define FSRS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "fsrs.h"

/* ln(0.9)，用于遗忘曲线的 factor。写成字面量避免不同 libm 的宏差异。 */
#define FSRS_LN_09 (-0.10536051565782628)

/* factor 计算中对指数的上界保护，与 srs-benchmark 的 clamp(max=60) 一致 */
#define FSRS_FACTOR_EXP_MAX 60.0

double fsrs_internal_clamp(double v, double lo, double hi);
double fsrs_internal_factor(const float *w);
double fsrs_internal_retrievability(const float *w, double stability, double elapsed_days);
double fsrs_internal_next_interval(const float *w, double stability, double desired_retention);
double fsrs_internal_init_difficulty_raw(const float *w, int rating);
double fsrs_internal_next_difficulty(const float *w, double difficulty, int rating);
double fsrs_internal_stability_after_success(const float *w, double d, double s,
                                             double r, int rating);
double fsrs_internal_stability_after_failure(const float *w, double d, double s, double r);
double fsrs_internal_stability_short_term(const float *w, double s, int rating);

/** 校验参数数组：w 为 NULL 时用默认参数；返回值非 NULL 表示可用。 */
const float *fsrs_internal_pick_parameters(const float *w);

/** 21 个参数是否全部有限（isfinite）。w 不可为 NULL。 */
int fsrs_internal_parameters_valid(const float *w);

#endif /* FSRS_INTERNAL_H */
