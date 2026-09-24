/*
 * fsrs_ffi.c — 面向跨语言调用的 opaque handle 层
 *
 * 对应 rs-fsrs-c 的做法：Rust 侧的 FSRS 结构在此成为不透明指针，
 * 由 create/destroy 显式管理生命周期；结构体方法按
 * `fsrs_<struct>_<method>` 命名导出。区别在于：本实现不链接 Rust，
 * handle 由 C 堆分配，因此 `fsrs_scheduler_free` 就是普通 free。
 *
 * 所有权约定（跨语言边界必须遵守）：
 *   - new 返回的 handle 归调用方所有，只能 free 一次；
 *   - free(NULL) 是空操作；
 *   - parameters 返回的数组所有权仍在 handle，禁止外部释放；
 *   - handle 本身只读共享是线程安全的（内部无可变状态），
 *     set_desired_retention 需要外部串行化。
 */
#include "fsrs_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct fsrs_scheduler {
    float parameters[FSRS_PARAMETER_COUNT];
    float desired_retention;
};

struct fsrs_scheduler *fsrs_scheduler_new(const float *w, size_t n,
                                          float desired_retention, int *err)
{
    struct fsrs_scheduler *scheduler;

    if (err != NULL) {
        *err = FSRS_OK;
    }
    if (!(desired_retention > 0.0f && desired_retention < 1.0f)) {
        if (err != NULL) {
            *err = FSRS_ERR_RETENTION;
        }
        return NULL;
    }
    if (w != NULL && n != 0 && n != (size_t)FSRS_PARAMETER_COUNT) {
        if (err != NULL) {
            *err = FSRS_ERR_PARAM_COUNT;
        }
        return NULL;
    }

    scheduler = (struct fsrs_scheduler *)malloc(sizeof(*scheduler));
    if (scheduler == NULL) {
        if (err != NULL) {
            *err = FSRS_ERR_ALLOC;
        }
        return NULL;
    }

    if (w != NULL && n == (size_t)FSRS_PARAMETER_COUNT) {
        memcpy(scheduler->parameters, w, sizeof(scheduler->parameters));
    } else {
        memcpy(scheduler->parameters, fsrs_default_parameters(),
               sizeof(scheduler->parameters));
    }
    if (!fsrs_internal_parameters_valid(scheduler->parameters)) {
        free(scheduler);
        if (err != NULL) {
            *err = FSRS_ERR_NUMERIC;
        }
        return NULL;
    }
    scheduler->desired_retention = desired_retention;
    return scheduler;
}

void fsrs_scheduler_free(struct fsrs_scheduler *scheduler)
{
    free(scheduler); /* free(NULL) 由标准保证为空操作 */
}

const float *fsrs_scheduler_parameters(const struct fsrs_scheduler *scheduler)
{
    return (scheduler != NULL) ? scheduler->parameters : NULL;
}

float fsrs_scheduler_desired_retention(const struct fsrs_scheduler *scheduler)
{
    return (scheduler != NULL) ? scheduler->desired_retention : 0.0f;
}

int fsrs_scheduler_set_desired_retention(struct fsrs_scheduler *scheduler,
                                         float desired_retention)
{
    if (scheduler == NULL) {
        return FSRS_ERR_NULL;
    }
    if (!(desired_retention > 0.0f && desired_retention < 1.0f)) {
        return FSRS_ERR_RETENTION;
    }
    scheduler->desired_retention = desired_retention;
    return FSRS_OK;
}

int fsrs_scheduler_next_states(const struct fsrs_scheduler *scheduler,
                               const struct fsrs_memory_state *current,
                               uint32_t elapsed_days,
                               struct fsrs_next_states *out)
{
    if (scheduler == NULL) {
        return FSRS_ERR_NULL;
    }
    return fsrs_next_states(scheduler->parameters, scheduler->desired_retention,
                            current, elapsed_days, out);
}

int fsrs_scheduler_memory_state(const struct fsrs_scheduler *scheduler,
                                const struct fsrs_review *reviews,
                                size_t count,
                                struct fsrs_memory_state *out)
{
    if (scheduler == NULL) {
        return FSRS_ERR_NULL;
    }
    return fsrs_memory_state(scheduler->parameters, reviews, count, out);
}

int fsrs_scheduler_memory_state_from(const struct fsrs_scheduler *scheduler,
                                     const struct fsrs_memory_state *starting,
                                     const struct fsrs_review *reviews,
                                     size_t count,
                                     struct fsrs_memory_state *out)
{
    if (scheduler == NULL) {
        return FSRS_ERR_NULL;
    }
    return fsrs_memory_state_from(scheduler->parameters, starting, reviews, count, out);
}

float fsrs_scheduler_retrievability(const struct fsrs_scheduler *scheduler,
                                    const struct fsrs_memory_state *state,
                                    uint32_t elapsed_days)
{
    if (scheduler == NULL || state == NULL) {
        return 0.0f;
    }
    return fsrs_retrievability(scheduler->parameters, state->stability,
                               (float)elapsed_days);
}

uint32_t fsrs_scheduler_next_interval(const struct fsrs_scheduler *scheduler,
                                      float stability)
{
    if (scheduler == NULL) {
        return 0;
    }
    return fsrs_next_interval(scheduler->parameters, stability,
                              scheduler->desired_retention);
}
