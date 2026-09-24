/*
 * schedule.c — 对应 fsrs-rs 的 examples/schedule.rs
 *
 * 演示：新卡首复习 -> 取 Good 分支 -> 若干天后再次调度 -> 历史回放。
 * 编译：make example
 */
#include "fsrs.h"

#include <stdio.h>

static const char *rating_name(int r)
{
    switch (r) {
    case FSRS_RATING_AGAIN: return "Again";
    case FSRS_RATING_HARD:  return "Hard";
    case FSRS_RATING_GOOD:  return "Good";
    case FSRS_RATING_EASY:  return "Easy";
    default:                return "?";
    }
}

static void dump(const char *title, const struct fsrs_next_states *ns)
{
    int ratings[4] = { FSRS_RATING_AGAIN, FSRS_RATING_HARD,
                       FSRS_RATING_GOOD, FSRS_RATING_EASY };
    int i;

    printf("%s\n", title);
    printf("  %-6s %-12s %-12s %-10s\n", "按钮", "S(天)", "D", "间隔(天)");
    for (i = 0; i < 4; i++) {
        const struct fsrs_memory_state *m = fsrs_next_states_pick(ns, ratings[i]);
        uint32_t interval = fsrs_next_states_interval(ns, ratings[i]);
        printf("  %-6s %-12.4f %-12.4f %-10u\n", rating_name(ratings[i]),
               m->stability, m->difficulty, interval);
    }
    printf("\n");
}

int main(void)
{
    float desired_retention = 0.9f;
    struct fsrs_scheduler *fsrs;
    struct fsrs_next_states ns;
    struct fsrs_memory_state state;
    struct fsrs_review history[5];
    int err = FSRS_OK;
    int i;

    /* 默认参数即 FSRS-6 的 21 个权重；new 会复制参数，handle 独占所有权 */
    fsrs = fsrs_scheduler_new(NULL, 0, desired_retention, &err);
    if (fsrs == NULL) {
        printf("创建失败: %s\n", fsrs_error_string(err));
        return 1;
    }

    printf("FSRS-%d  C 实现（参数个数 %zu, DR=%.2f）\n\n",
           FSRS_VERSION_MAJOR, fsrs_parameter_count(), (double)desired_retention);

    /* 1) 新卡：current 传 NULL 表示还没有记忆状态 */
    if (fsrs_scheduler_next_states(fsrs, NULL, 0, &ns) != FSRS_OK) {
        printf("调度失败\n");
        fsrs_scheduler_free(fsrs);
        return 1;
    }
    dump("1) 新卡，首次复习：", &ns);

    /* 2) 用户按了 Good，保存其记忆状态；2 天后再次调度 */
    state = *fsrs_next_states_pick(&ns, FSRS_RATING_GOOD);
    if (fsrs_scheduler_next_states(fsrs, &state, 2, &ns) != FSRS_OK) {
        printf("调度失败\n");
        fsrs_scheduler_free(fsrs);
        return 1;
    }
    printf("2) 上次 Good（S=%.4f, D=%.4f），2 天后：\n",
           (double)state.stability, (double)state.difficulty);
    dump("", &ns);
    printf("   此时的可提取性 R = %.4f\n\n",
           (double)fsrs_scheduler_retrievability(fsrs, &state, 2));

    /* 3) 复习历史回放：得到当前的记忆状态 */
    history[0].rating = FSRS_RATING_GOOD;  history[0].delta_days = 0;
    history[1].rating = FSRS_RATING_GOOD;  history[1].delta_days = 1;
    history[2].rating = FSRS_RATING_HARD;  history[2].delta_days = 3;
    history[3].rating = FSRS_RATING_AGAIN; history[3].delta_days = 6;
    history[4].rating = FSRS_RATING_GOOD;  history[4].delta_days = 1;

    if (fsrs_scheduler_memory_state(fsrs, history, 5, &state) != FSRS_OK) {
        printf("回放失败\n");
        fsrs_scheduler_free(fsrs);
        return 1;
    }
    printf("3) 5 条历史回放后：S=%.4f, D=%.4f\n",
           (double)state.stability, (double)state.difficulty);
    printf("   按 DR=0.9 的下一间隔 = %u 天\n\n",
           fsrs_scheduler_next_interval(fsrs, state.stability));

    /* 4) 期望保留率对间隔的影响 */
    printf("4) 同一稳定性下，期望保留率 -> 间隔：\n");
    for (i = 90; i >= 70; i -= 5) {
        float dr = (float)i / 100.0f;
        fsrs_scheduler_set_desired_retention(fsrs, dr);
        printf("   DR=%.2f -> %u 天\n", (double)dr,
               fsrs_scheduler_next_interval(fsrs, state.stability));
    }

    fsrs_scheduler_free(fsrs); /* 与 new 配对，且只调用一次 */
    printf("\nhandle 已释放\n");
    return 0;
}
