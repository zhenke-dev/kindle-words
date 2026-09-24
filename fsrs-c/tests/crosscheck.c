/*
 * crosscheck.c — 输出一批状态转移结果（CSV），供 scripts/crosscheck.py 与
 * 独立 Python 参考实现逐项比对。用于验证 C 内核与公式基线无转写偏差。
 *
 * 用法：make crosscheck
 */
#include "fsrs.h"

#include <stdio.h>

static const double kStability[] = { 0.01, 0.1, 1.0, 5.0, 10.0, 100.0, 1000.0, 36500.0 };
static const double kDifficulty[] = { 1.0, 2.5, 5.0, 7.5, 10.0 };
static const uint32_t kDays[] = { 0, 1, 3, 10, 30, 365, 3650 };
static const int kRatings[] = { FSRS_RATING_AGAIN, FSRS_RATING_HARD,
                                FSRS_RATING_GOOD, FSRS_RATING_EASY };

int main(void)
{
    size_t i, j, k, m;
    float desired_retention = 0.9f;

    printf("rating,stability_in,difficulty_in,elapsed_days,"
           "stability_out,difficulty_out,stability_fast_out,interval\n");

    /* 新卡（无历史状态） */
    for (m = 0; m < 4; m++) {
        struct fsrs_next_states ns;
        const struct fsrs_memory_state *st;
        if (fsrs_next_states(NULL, desired_retention, NULL, 0, &ns) != FSRS_OK) {
            return 1;
        }
        st = fsrs_next_states_pick(&ns, kRatings[m]);
        printf("%d,%.17g,%.17g,%u,%.17g,%.17g,%.17g,%u\n",
               kRatings[m], 0.0, 0.0, 0u,
               (double)st->stability, (double)st->difficulty,
               (double)st->stability_fast,
               fsrs_next_states_interval(&ns, kRatings[m]));
    }

    for (i = 0; i < sizeof(kStability) / sizeof(kStability[0]); i++) {
        for (j = 0; j < sizeof(kDifficulty) / sizeof(kDifficulty[0]); j++) {
            for (k = 0; k < sizeof(kDays) / sizeof(kDays[0]); k++) {
                struct fsrs_memory_state cur;
                struct fsrs_next_states ns;
                cur.stability = (float)kStability[i];
                cur.difficulty = (float)kDifficulty[j];
                cur.stability_fast = (float)kStability[i];

                if (fsrs_next_states(NULL, desired_retention, &cur, kDays[k], &ns) != FSRS_OK) {
                    printf("ERROR at S=%g D=%g t=%u\n", kStability[i], kDifficulty[j], kDays[k]);
                    return 1;
                }
                for (m = 0; m < 4; m++) {
                    const struct fsrs_memory_state *st = fsrs_next_states_pick(&ns, kRatings[m]);
                    printf("%d,%.17g,%.17g,%u,%.17g,%.17g,%.17g,%u\n",
                           kRatings[m], kStability[i], kDifficulty[j], kDays[k],
                           (double)st->stability, (double)st->difficulty,
                           (double)st->stability_fast,
                           fsrs_next_states_interval(&ns, kRatings[m]));
                }
            }
        }
    }
    return 0;
}
