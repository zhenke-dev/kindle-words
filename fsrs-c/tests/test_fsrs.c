/*
 * test_fsrs.c — 自测：参考向量 + 不变量 + 数值边界 + FFI 契约
 *
 * 参考向量由 scripts/reference.py（独立实现的 Python 版本）生成，
 * 两边公式同源但代码独立，交叉一致可排除转写错误。
 */
#include "fsrs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failed = 0;
static int g_total = 0;

static void check_true(const char *name, int cond)
{
    g_total++;
    if (!cond) {
        g_failed++;
        printf("  FAIL  %s\n", name);
    }
}

static void check_close(const char *name, double got, double expect, double tol)
{
    double diff = fabs(got - expect);
    g_total++;
    if (!(diff <= tol)) {
        g_failed++;
        printf("  FAIL  %s: got %.10g, expect %.10g (diff %.3g > tol %.3g)\n",
               name, got, expect, diff, tol);
    }
}

static void check_u32(const char *name, uint32_t got, uint32_t expect)
{
    g_total++;
    if (got != expect) {
        g_failed++;
        printf("  FAIL  %s: got %u, expect %u\n", name, got, expect);
    }
}

/* 参考向量（来自 scripts/reference.py，FSRS-6 默认参数） */
#define REF_FACTOR        0.9803464944134799
#define REF_R_AT_S        0.9
#define REF_R_3_10        0.9610242690473834
#define REF_INIT_D_AGAIN  6.4133
#define REF_INIT_D_HARD   5.112170705601055
#define REF_INIT_D_GOOD   2.118103970459015
#define REF_INIT_D_EASY   1.0
#define REF_NEW_GOOD_S    2.3065
#define REF_NEW_EASY_S    8.2956
#define REF_GOOD_S        32.02672948198673
#define REF_GOOD_D        4.996
#define REF_HARD_S        23.246875110466817
#define REF_HARD_D        6.671767
#define REF_EASY_S        51.25386164681294
#define REF_EASY_D        3.320233
#define REF_AGAIN_S       1.3919869729546932
#define REF_AGAIN_D       8.347534
#define REF_SAMEDAY_EASY_S 15.5343079471667
#define REF_SAMEDAY_AGAIN_S 3.0512489355716377
#define REF_REPLAY_S      92.7348589306402
#define REF_REPLAY_D      2.1136382587297033

static void test_constants(void)
{
    printf("[constants]\n");
    check_u32("parameter count", (uint32_t)fsrs_parameter_count(), 21);
    check_close("w[20] decay", fsrs_default_parameters()[20], 0.1542, 1e-6);
    check_close("w[0]", fsrs_default_parameters()[0], 0.212, 1e-6);
    check_true("error string non-null", fsrs_error_string(FSRS_OK) != NULL);
    check_true("error string unknown", fsrs_error_string(999) != NULL);
}

static void test_forgetting_curve(void)
{
    printf("[forgetting curve]\n");
    /* 定义性检查：t == S 时 R 必须恰好为 0.9 */
    check_close("R(t=S=10) == 0.9", fsrs_retrievability(NULL, 10.0f, 10.0f),
                REF_R_AT_S, 1e-6);
    check_close("R(t=S=1) == 0.9", fsrs_retrievability(NULL, 1.0f, 1.0f),
                REF_R_AT_S, 1e-6);
    check_close("R(t=S=365) == 0.9", fsrs_retrievability(NULL, 365.0f, 365.0f),
                REF_R_AT_S, 1e-6);
    check_close("R(t=3,S=10)", fsrs_retrievability(NULL, 10.0f, 3.0f), REF_R_3_10, 1e-6);
    check_true("R(t=0) == 1", fabs(fsrs_retrievability(NULL, 10.0f, 0.0f) - 1.0) < 1e-6);
    check_true("R bounded [0,1]",
               fsrs_retrievability(NULL, 1.0f, 100000.0f) >= 0.0f
               && fsrs_retrievability(NULL, 1.0f, 100000.0f) <= 1.0f);
    check_true("R monotone decreasing",
               fsrs_retrievability(NULL, 10.0f, 1.0f) > fsrs_retrievability(NULL, 10.0f, 20.0f));
}

static void test_interval(void)
{
    printf("[interval]\n");
    /* DR=0.9 时间隔应等于稳定性（四舍五入后） */
    check_u32("I(S=10, DR=0.9) == 10", fsrs_next_interval(NULL, 10.0f, 0.9f), 10);
    check_u32("I(S=100, DR=0.9) == 100", fsrs_next_interval(NULL, 100.0f, 0.9f), 100);
    check_u32("I(S=10, DR=0.85)", fsrs_next_interval(NULL, 10.0f, 0.85f), 19);
    check_true("I >= 1", fsrs_next_interval(NULL, 0.01f, 0.9f) >= 1);
    check_u32("I invalid retention -> 0", fsrs_next_interval(NULL, 10.0f, 1.5f), 0);
    /* 更高保留率 => 更短间隔 */
    check_true("higher DR => shorter interval",
               fsrs_next_interval(NULL, 50.0f, 0.95f) < fsrs_next_interval(NULL, 50.0f, 0.80f));
}

static void test_initial_state(void)
{
    printf("[initial state]\n");
    check_close("S0(Again)", fsrs_init_stability(NULL, FSRS_RATING_AGAIN), 0.212, 1e-6);
    check_close("S0(Good)", fsrs_init_stability(NULL, FSRS_RATING_GOOD), REF_NEW_GOOD_S, 1e-6);
    check_close("S0(Easy)", fsrs_init_stability(NULL, FSRS_RATING_EASY), REF_NEW_EASY_S, 1e-6);
    check_close("D0(Again)", fsrs_init_difficulty(NULL, FSRS_RATING_AGAIN), REF_INIT_D_AGAIN, 1e-5);
    check_close("D0(Hard)", fsrs_init_difficulty(NULL, FSRS_RATING_HARD), REF_INIT_D_HARD, 1e-5);
    check_close("D0(Good)", fsrs_init_difficulty(NULL, FSRS_RATING_GOOD), REF_INIT_D_GOOD, 1e-5);
    check_close("D0(Easy)", fsrs_init_difficulty(NULL, FSRS_RATING_EASY), REF_INIT_D_EASY, 1e-5);
    /* D0(Again) 恰为 w4——这是公式在 G=1 时的解析性质 */
    check_close("D0(Again) == w4", fsrs_init_difficulty(NULL, FSRS_RATING_AGAIN),
                fsrs_default_parameters()[4], 1e-6);
    /* 单调性：评分越高，初始难度越低 */
    check_true("D0 monotone decreasing in rating",
               fsrs_init_difficulty(NULL, FSRS_RATING_AGAIN)
               > fsrs_init_difficulty(NULL, FSRS_RATING_HARD)
               && fsrs_init_difficulty(NULL, FSRS_RATING_HARD)
               > fsrs_init_difficulty(NULL, FSRS_RATING_GOOD));
    check_close("bad rating -> 0", fsrs_init_stability(NULL, 0), 0.0, 1e-9);
}

static void test_review_step(void)
{
    struct fsrs_memory_state cur = { 10.0f, 5.0f, 10.0f };
    struct fsrs_next_states ns;
    int rc;

    printf("[review step]\n");
    rc = fsrs_next_states(NULL, 0.9f, &cur, 10, &ns);
    check_true("next_states ok", rc == FSRS_OK);

    check_close("Good S", ns.good.stability, REF_GOOD_S, 1e-4);
    check_close("Good D", ns.good.difficulty, REF_GOOD_D, 1e-4);
    check_close("Hard S", ns.hard.stability, REF_HARD_S, 1e-4);
    check_close("Hard D", ns.hard.difficulty, REF_HARD_D, 1e-4);
    check_close("Easy S", ns.easy.stability, REF_EASY_S, 1e-4);
    check_close("Easy D", ns.easy.difficulty, REF_EASY_D, 1e-4);
    check_close("Again S", ns.again.stability, REF_AGAIN_S, 1e-4);
    check_close("Again D", ns.again.difficulty, REF_AGAIN_D, 1e-4);

    check_u32("interval good", ns.interval_good, 32);
    check_u32("interval hard", ns.interval_hard, 23);
    check_u32("interval easy", ns.interval_easy, 51);
    check_u32("interval again", ns.interval_again, 1);

    /* 排序不变量：Easy > Good > Hard > Again */
    check_true("S ordering",
               ns.easy.stability > ns.good.stability
               && ns.good.stability > ns.hard.stability
               && ns.hard.stability > ns.again.stability);
    check_true("D ordering",
               ns.again.difficulty > ns.hard.difficulty
               && ns.hard.difficulty > ns.good.difficulty
               && ns.good.difficulty > ns.easy.difficulty);
    check_true("success never decreases S", ns.hard.stability > cur.stability);
    check_true("lapse decreases S", ns.again.stability < cur.stability);
    check_true("pick good", fsrs_next_states_pick(&ns, FSRS_RATING_GOOD) == &ns.good);
    check_true("pick invalid -> NULL", fsrs_next_states_pick(&ns, 0) == NULL);
    check_u32("interval via helper", fsrs_next_states_interval(&ns, FSRS_RATING_EASY),
              ns.interval_easy);
}

static void test_short_term(void)
{
    struct fsrs_memory_state cur = { 10.0f, 5.0f, 10.0f };
    struct fsrs_next_states ns;

    printf("[short term / same-day]\n");
    check_true("same-day ok", fsrs_next_states(NULL, 0.9f, &cur, 0, &ns) == FSRS_OK);
    check_close("same-day Easy S", ns.easy.stability, REF_SAMEDAY_EASY_S, 1e-4);
    check_close("same-day Again S", ns.again.stability, REF_SAMEDAY_AGAIN_S, 1e-4);
    /* Hard/Good 的 sinc 被 clamp 到 1，稳定性不下降 */
    check_true("same-day Good S >= S", ns.good.stability >= cur.stability - 1e-5);
    check_true("same-day Hard S >= S", ns.hard.stability >= cur.stability - 1e-5);
    check_true("same-day Again S < S", ns.again.stability < cur.stability);
    check_true("same-day fast == stability",
               fabs(ns.good.stability_fast - ns.good.stability) < 1e-6);
}

static void test_new_card(void)
{
    struct fsrs_next_states ns;

    printf("[new card]\n");
    check_true("new card ok", fsrs_next_states(NULL, 0.9f, NULL, 0, &ns) == FSRS_OK);
    check_close("new Good S", ns.good.stability, REF_NEW_GOOD_S, 1e-6);
    check_close("new Good D", ns.good.difficulty, REF_INIT_D_GOOD, 1e-5);
    check_close("new Again S", ns.again.stability, 0.212, 1e-6);
    check_u32("new Good interval", ns.interval_good, 2);
    check_u32("new Easy interval", ns.interval_easy, 8);
    check_true("new fast == stability",
               fabs(ns.good.stability_fast - ns.good.stability) < 1e-6);
    /* 首次 Again 走初始播种而非 lapse 公式 */
    check_true("first Again uses seeding, not lapse", ns.again.stability > 0.2f);
}

static void test_replay(void)
{
    struct fsrs_review reviews[5];
    struct fsrs_memory_state state;

    printf("[replay]\n");
    reviews[0].rating = FSRS_RATING_GOOD; reviews[0].delta_days = 0;
    reviews[1].rating = FSRS_RATING_GOOD; reviews[1].delta_days = 1;
    reviews[2].rating = FSRS_RATING_GOOD; reviews[2].delta_days = 3;
    reviews[3].rating = FSRS_RATING_GOOD; reviews[3].delta_days = 7;
    reviews[4].rating = FSRS_RATING_GOOD; reviews[4].delta_days = 15;

    check_true("replay ok", fsrs_memory_state(NULL, reviews, 5, &state) == FSRS_OK);
    check_close("replay S", state.stability, REF_REPLAY_S, 1e-3);
    check_close("replay D", state.difficulty, REF_REPLAY_D, 1e-4);

    /* 空历史仍为新卡 */
    check_true("empty replay ok", fsrs_memory_state(NULL, NULL, 0, &state) == FSRS_OK);
    check_close("empty replay S == 0", state.stability, 0.0, 1e-9);

    /* 非法评分 */
    reviews[0].rating = 9;
    check_true("invalid rating rejected",
               fsrs_memory_state(NULL, reviews, 1, &state) == FSRS_ERR_RATING);
}

static void test_invariants(void)
{
    struct fsrs_memory_state state;
    struct fsrs_next_states ns;
    struct fsrs_review review;
    int i;
    int ratings[4] = { FSRS_RATING_AGAIN, FSRS_RATING_HARD,
                       FSRS_RATING_GOOD, FSRS_RATING_EASY };

    printf("[invariants over 400 randomized-ish reviews]\n");
    state.stability = 0.0f;
    state.difficulty = 0.0f;
    state.stability_fast = 0.0f;

    for (i = 0; i < 400; i++) {
        int r = ratings[(i * 7 + i / 3) % 4];
        uint32_t days = (uint32_t)((i * 13) % 40);
        int rc = fsrs_next_states(NULL, 0.9f, &state, days, &ns);
        if (rc != FSRS_OK) {
            check_true("invariant loop ok", 0);
            return;
        }
        state = *fsrs_next_states_pick(&ns, r);
        if (!(state.stability >= FSRS_S_MIN)) {
            check_true("S >= S_MIN", 0);
            return;
        }
        if (!(state.difficulty >= FSRS_D_MIN - 1e-6 && state.difficulty <= FSRS_D_MAX + 1e-6)) {
            check_true("D in [1,10]", 0);
            return;
        }
        if (!isfinite(state.stability) || !isfinite(state.difficulty)) {
            check_true("finite", 0);
            return;
        }
    }
    check_true("S >= S_MIN held", 1);
    check_true("D in [1,10] held", 1);
    check_true("finite held", 1);

    /* 极限压力：超长间隔 + 极小稳定性 */
    state.stability = FSRS_S_MIN;
    state.difficulty = 10.0f;
    state.stability_fast = FSRS_S_MIN;
    check_true("extreme state ok",
               fsrs_next_states(NULL, 0.9f, &state, 36500, &ns) == FSRS_OK);
    check_true("extreme S >= S_MIN", ns.again.stability >= FSRS_S_MIN);
    check_true("extreme D bounded",
               ns.again.difficulty <= FSRS_D_MAX && ns.again.difficulty >= FSRS_D_MIN);

    review.rating = FSRS_RATING_AGAIN;
    review.delta_days = 0;
    check_true("single again ok", fsrs_memory_state(NULL, &review, 1, &state) == FSRS_OK);
}

static void test_interval_bounds(void)
{
    struct fsrs_memory_state cur = { FSRS_S_MAX, 5.0f, FSRS_S_MAX };
    struct fsrs_next_states ns;
    float w[FSRS_PARAMETER_COUNT];
    uint32_t iv;
    int rc;

    printf("[interval upper bound]\n");
    /* 修复前：DR=0.10 且 S=S_MAX 时 I≈1.1e11，转 uint32 属于未定义行为 */
    check_u32("I(S=36500, DR=0.10) -> S_MAX",
              fsrs_next_interval(NULL, 36500.0f, 0.10f), (uint32_t)FSRS_S_MAX);
    check_u32("I(S=36500, DR=0.15) -> S_MAX",
              fsrs_next_interval(NULL, 36500.0f, 0.15f), (uint32_t)FSRS_S_MAX);

    rc = fsrs_next_states(NULL, 0.15f, &cur, 30, &ns);
    check_true("low-DR next_states ok", rc == FSRS_OK);
    check_u32("low-DR interval_good -> S_MAX",
              ns.interval_good, (uint32_t)FSRS_S_MAX);
    check_true("all intervals in [1, S_MAX]",
               ns.interval_again >= 1 && ns.interval_again <= (uint32_t)FSRS_S_MAX
               && ns.interval_hard >= 1 && ns.interval_hard <= (uint32_t)FSRS_S_MAX
               && ns.interval_good >= 1 && ns.interval_good <= (uint32_t)FSRS_S_MAX
               && ns.interval_easy >= 1 && ns.interval_easy <= (uint32_t)FSRS_S_MAX);

    /* 退化但有限的衰减参数：pow() 上溢到 inf，
       修复前 (uint32_t)inf 同属未定义行为，实测返回 0 天 */
    memcpy(w, fsrs_default_parameters(), sizeof(w));
    w[20] = 1e-6f;
    iv = fsrs_next_interval(w, 10.0f, 0.9f);
    check_true("degenerate decay interval in [1, S_MAX]",
               iv >= 1 && iv <= (uint32_t)FSRS_S_MAX);
    check_true("degenerate decay next_states ok",
               fsrs_next_states(w, 0.9f, NULL, 0, &ns) == FSRS_OK);
    check_true("degenerate decay intervals bounded",
               ns.interval_good >= 1 && ns.interval_good <= (uint32_t)FSRS_S_MAX);
}

static void test_parameter_validation(void)
{
    float w[FSRS_PARAMETER_COUNT];
    struct fsrs_next_states ns;
    struct fsrs_memory_state out;
    struct fsrs_review review;
    struct fsrs_scheduler *s;
    int err;

    printf("[parameter validation]\n");
    /* 修复前 fsrs_scheduler_new 只校验 w[0] 与 w[20]，
       中间的 NaN 会穿透并产出 FSRS_OK + NaN 状态 */
    memcpy(w, fsrs_default_parameters(), sizeof(w));
    w[2] = NAN;
    err = FSRS_OK;
    s = fsrs_scheduler_new(w, FSRS_PARAMETER_COUNT, 0.9f, &err);
    check_true("scheduler_new rejects NaN param",
               s == NULL && err == FSRS_ERR_NUMERIC);
    check_true("stateless next_states rejects NaN param",
               fsrs_next_states(w, 0.9f, NULL, 0, &ns) == FSRS_ERR_NUMERIC);

    memcpy(w, fsrs_default_parameters(), sizeof(w));
    w[10] = INFINITY;
    err = FSRS_OK;
    s = fsrs_scheduler_new(w, FSRS_PARAMETER_COUNT, 0.9f, &err);
    check_true("scheduler_new rejects Inf param",
               s == NULL && err == FSRS_ERR_NUMERIC);
    check_true("stateless next_states rejects Inf param",
               fsrs_next_states(w, 0.9f, NULL, 0, &ns) == FSRS_ERR_NUMERIC);

    review.rating = FSRS_RATING_GOOD;
    review.delta_days = 1;
    check_true("history replay rejects NaN param",
               fsrs_memory_state(w, &review, 1, &out) == FSRS_ERR_NUMERIC);

    /* 默认参数路径不受影响（回归） */
    check_true("default params still ok",
               fsrs_next_states(NULL, 0.9f, NULL, 0, &ns) == FSRS_OK);
}

static void test_state_range(void)
{
    struct fsrs_memory_state cur = { 1.0e30f, 5.0f, 1.0e30f };
    struct fsrs_next_states ns;
    int rc;

    printf("[input state range]\n");
    /* 修复前：失败路径缺 S_MAX 上界且入口不夹输入，
       S_in=1e30 会产出 1.04e8 的“稳定性”，违反统一 clamp 的承诺 */
    rc = fsrs_next_states(NULL, 0.9f, &cur, 10, &ns);
    check_true("huge S accepted (clamped at entry)", rc == FSRS_OK);
    check_true("failure S <= S_MAX", ns.again.stability <= FSRS_S_MAX);
    check_true("success S <= S_MAX",
               ns.hard.stability <= FSRS_S_MAX
               && ns.good.stability <= FSRS_S_MAX
               && ns.easy.stability <= FSRS_S_MAX);
    check_true("all S >= S_MIN",
               ns.again.stability >= FSRS_S_MIN && ns.hard.stability >= FSRS_S_MIN
               && ns.good.stability >= FSRS_S_MIN && ns.easy.stability >= FSRS_S_MIN);
    check_true("all outputs finite",
               isfinite(ns.again.stability) && isfinite(ns.again.difficulty)
               && isfinite(ns.good.stability) && isfinite(ns.easy.difficulty));
    check_true("intervals bounded with huge input",
               ns.interval_good >= 1
               && ns.interval_good <= (uint32_t)FSRS_S_MAX);

    /* difficulty 越界视为非法状态（比上游的静默 clamp 更严格，见 README §7） */
    cur.stability = 10.0f;
    cur.difficulty = 50.0f;
    cur.stability_fast = 10.0f;
    check_true("D out of range rejected",
               fsrs_next_states(NULL, 0.9f, &cur, 10, &ns) == FSRS_ERR_NUMERIC);
}

/* ------------------------------------------ 可提取性：整天取整（对齐 fsrs-rs） */

static void test_retrievability_rounding(void)
{
    printf("[retrievability day rounding]\n");
    check_close("R(t=2.4) == R(t=2)",
                fsrs_retrievability(NULL, 10.0f, 2.4f),
                fsrs_retrievability(NULL, 10.0f, 2.0f), 1e-9);
    check_close("R(t=2.6) == R(t=3)",
                fsrs_retrievability(NULL, 10.0f, 2.6f),
                fsrs_retrievability(NULL, 10.0f, 3.0f), 1e-9);
    /* .5 远离零取整，与 Rust 的 f32::round 一致（Python 的 round 是银行家舍入） */
    check_close("R(t=2.5) == R(t=3)",
                fsrs_retrievability(NULL, 10.0f, 2.5f),
                fsrs_retrievability(NULL, 10.0f, 3.0f), 1e-9);
    check_close("R(t=-5) == 1", fsrs_retrievability(NULL, 10.0f, -5.0f), 1.0, 1e-9);
}

/* -------------------------------------------- 从起始态回放（截断历史） */

static void test_memory_state_from(void)
{
    struct fsrs_review full[5];
    struct fsrs_review part[4];
    struct fsrs_memory_state start, a, b;
    struct fsrs_memory_state zero = { 0.0f, 0.0f, 0.0f };
    struct fsrs_memory_state nan_start = { NAN, 5.0f, 0.0f };
    struct fsrs_memory_state bad_d = { 10.0f, 50.0f, 10.0f };

    printf("[memory_state_from / truncated history]\n");
    full[0].rating = FSRS_RATING_GOOD;  full[0].delta_days = 0;
    full[1].rating = FSRS_RATING_GOOD;  full[1].delta_days = 1;
    full[2].rating = FSRS_RATING_GOOD;  full[2].delta_days = 3;
    full[3].rating = FSRS_RATING_GOOD;  full[3].delta_days = 7;
    full[4].rating = FSRS_RATING_GOOD;  full[4].delta_days = 15;
    part[0] = full[1];
    part[1] = full[2];
    part[2] = full[3];
    part[3] = full[4];

    check_true("full replay ok", fsrs_memory_state(NULL, full, 5, &a) == FSRS_OK);
    check_close("full replay S", a.stability, REF_REPLAY_S, 1e-3);

    /* starting=NULL 时与旧接口逐位一致（兼容包装） */
    check_true("NULL starting == fsrs_memory_state",
               fsrs_memory_state_from(NULL, NULL, full, 5, &b) == FSRS_OK
               && b.stability == a.stability && b.difficulty == a.difficulty);

    /* 截断：前缀状态作为起点续放，应与完整回放一致 */
    check_true("prefix state ok", fsrs_memory_state(NULL, full, 1, &start) == FSRS_OK);
    check_true("resume from prefix ok",
               fsrs_memory_state_from(NULL, &start, part, 4, &b) == FSRS_OK);
    check_close("split replay S == full replay S", b.stability, a.stability, 1e-6);
    check_close("split replay D == full replay D", b.difficulty, a.difficulty, 1e-6);

    /* 全零起点：首条复习播种，与全新回放等价（与 fsrs-rs 的 nth==0 语义一致） */
    check_true("zero starting seeds",
               fsrs_memory_state_from(NULL, &zero, full, 5, &b) == FSRS_OK);
    check_close("zero-start replay == full replay", b.stability, a.stability, 1e-6);

    /* count==0：起始态原样透传 */
    start.stability = 12.5f;
    start.difficulty = 3.5f;
    start.stability_fast = 12.5f;
    check_true("empty history keeps starting",
               fsrs_memory_state_from(NULL, &start, NULL, 0, &b) == FSRS_OK
               && b.stability == 12.5f && b.difficulty == 3.5f);

    /* 非法起点 */
    check_true("NaN starting rejected",
               fsrs_memory_state_from(NULL, &nan_start, full, 5, &b) == FSRS_ERR_NUMERIC);
    check_true("out-of-range D starting rejected",
               fsrs_memory_state_from(NULL, &bad_d, full, 5, &b) == FSRS_ERR_NUMERIC);
    check_true("NULL reviews + count>0 rejected",
               fsrs_memory_state(NULL, NULL, 3, &b) == FSRS_ERR_NULL);
}

/* ------------------------------------------- 出错时 *out 保持不变 */

static int all_bytes(const void *p, size_t n, unsigned char v)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t i;

    for (i = 0; i < n; i++) {
        if (b[i] != v) {
            return 0;
        }
    }
    return 1;
}

static void test_out_untouched(void)
{
    struct fsrs_next_states ns;
    struct fsrs_memory_state out;
    struct fsrs_memory_state bad_state = { NAN, 5.0f, 0.0f };
    struct fsrs_review bad_review;

    printf("[out untouched on error]\n");
    memset(&ns, 0xAB, sizeof(ns));
    check_true("NaN state error code",
               fsrs_next_states(NULL, 0.9f, &bad_state, 10, &ns) == FSRS_ERR_NUMERIC);
    check_true("next_states out untouched (NaN state)",
               all_bytes(&ns, sizeof(ns), 0xAB));

    memset(&ns, 0xAB, sizeof(ns));
    check_true("bad DR error code",
               fsrs_next_states(NULL, 1.5f, NULL, 0, &ns) == FSRS_ERR_RETENTION);
    check_true("next_states out untouched (bad DR)",
               all_bytes(&ns, sizeof(ns), 0xAB));

    memset(&out, 0xCD, sizeof(out));
    bad_review.rating = 9;
    bad_review.delta_days = 1;
    check_true("bad rating error code",
               fsrs_memory_state(NULL, &bad_review, 1, &out) == FSRS_ERR_RATING);
    check_true("memory_state out untouched (bad rating)",
               all_bytes(&out, sizeof(out), 0xCD));

    memset(&out, 0xCD, sizeof(out));
    check_true("NULL-reviews replay untouched",
               fsrs_memory_state(NULL, NULL, 2, &out) == FSRS_ERR_NULL
               && all_bytes(&out, sizeof(out), 0xCD));
}

static void test_ffi(void)
{
    struct fsrs_scheduler *s;
    struct fsrs_next_states ns;
    struct fsrs_memory_state cur = { 10.0f, 5.0f, 10.0f };
    struct fsrs_memory_state out;
    struct fsrs_review reviews[2];
    int err = FSRS_OK;
    float bad_params[5] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };

    printf("[ffi layer]\n");
    s = fsrs_scheduler_new(NULL, 0, 0.9f, &err);
    check_true("new with default params", s != NULL && err == FSRS_OK);
    if (s == NULL) {
        return;
    }
    check_true("parameters exposed", fsrs_scheduler_parameters(s) != NULL);
    check_close("desired retention", fsrs_scheduler_desired_retention(s), 0.9, 1e-6);
    check_true("next_states via handle",
               fsrs_scheduler_next_states(s, &cur, 10, &ns) == FSRS_OK);
    check_close("handle good S", ns.good.stability, REF_GOOD_S, 1e-4);
    check_close("handle retrievability",
                fsrs_scheduler_retrievability(s, &cur, 10), REF_R_AT_S, 1e-6);
    check_u32("handle interval", fsrs_scheduler_next_interval(s, 10.0f), 10);

    check_true("set retention ok", fsrs_scheduler_set_desired_retention(s, 0.85f) == FSRS_OK);
    check_close("retention updated", fsrs_scheduler_desired_retention(s), 0.85, 1e-6);
    check_true("set retention invalid",
               fsrs_scheduler_set_desired_retention(s, 0.0f) == FSRS_ERR_RETENTION);
    check_true("set retention null", fsrs_scheduler_set_desired_retention(NULL, 0.9f)
               == FSRS_ERR_NULL);

    reviews[0].rating = FSRS_RATING_GOOD; reviews[0].delta_days = 0;
    reviews[1].rating = FSRS_RATING_GOOD; reviews[1].delta_days = 1;
    check_true("handle memory_state",
               fsrs_scheduler_memory_state(s, reviews, 2, &out) == FSRS_OK);
    check_true("handle memory_state_from (NULL starting)",
               fsrs_scheduler_memory_state_from(s, NULL, reviews, 2, &out) == FSRS_OK);
    check_true("handle null review array",
               fsrs_scheduler_memory_state(s, NULL, 0, &out) == FSRS_OK);
    fsrs_scheduler_free(s);

    /* 自定义参数 */
    s = fsrs_scheduler_new(fsrs_default_parameters(), 21, 0.9f, &err);
    check_true("new with explicit params", s != NULL && err == FSRS_OK);
    check_close("explicit params copied",
                fsrs_scheduler_parameters(s)[20], 0.1542, 1e-6);
    fsrs_scheduler_free(s);

    /* 错误路径 */
    s = fsrs_scheduler_new(bad_params, 5, 0.9f, &err);
    check_true("bad param count rejected", s == NULL && err == FSRS_ERR_PARAM_COUNT);
    s = fsrs_scheduler_new(NULL, 0, 1.5f, &err);
    check_true("bad retention rejected", s == NULL && err == FSRS_ERR_RETENTION);
    s = fsrs_scheduler_new(NULL, 0, 0.9f, NULL);
    check_true("null err pointer tolerated", s != NULL);
    fsrs_scheduler_free(s);

    /* free(NULL) 必须安全，且不可重复释放 */
    fsrs_scheduler_free(NULL);
    check_true("free(NULL) safe", 1);

    /* 内核层的入参校验 */
    check_true("null out rejected",
               fsrs_next_states(NULL, 0.9f, &cur, 10, NULL) == FSRS_ERR_NULL);
    check_true("bad retention rejected",
               fsrs_next_states(NULL, 0.0f, &cur, 10, &ns) == FSRS_ERR_RETENTION);
    cur.stability = NAN;
    check_true("NaN state rejected",
               fsrs_next_states(NULL, 0.9f, &cur, 10, &ns) == FSRS_ERR_NUMERIC);
}

int main(void)
{
    printf("FSRS-6 C implementation — self test\n\n");
    test_constants();
    test_forgetting_curve();
    test_interval();
    test_initial_state();
    test_review_step();
    test_short_term();
    test_new_card();
    test_replay();
    test_invariants();
    test_interval_bounds();
    test_parameter_validation();
    test_state_range();
    test_retrievability_rounding();
    test_memory_state_from();
    test_out_untouched();
    test_ffi();

    printf("\n%d checks, %d failed\n", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
