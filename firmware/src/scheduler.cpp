#include "scheduler.h"
#include "wordbook.h"

static uint32_t g_day = 0;

/* 今日重学队列（仅 RAM）：评分为 重来/困难 的卡，间隔若干张后在本 session 重现，
   再次评分时 elapsed_days = 0，走 FSRS-6 的同日短期模型 */
#define RQ_MAX 32
#define RQ_MIN_GAP 2          // 入队后至少间隔 2 张其他卡才可重现
static uint16_t g_rq[RQ_MAX];
static uint8_t  g_rq_age[RQ_MAX];
static uint8_t  g_rq_head = 0, g_rq_count = 0;

static void rq_push(uint16_t id) {
    if (g_rq_count == RQ_MAX) {            // 满则丢最旧
        g_rq_head = (g_rq_head + 1) % RQ_MAX;
        g_rq_count--;
    }
    uint8_t tail = (g_rq_head + g_rq_count) % RQ_MAX;
    g_rq[tail] = id;
    g_rq_age[tail] = 0;
    g_rq_count++;
}

static int rq_pop() {
    if (!g_rq_count) return -1;
    int id = g_rq[g_rq_head];
    g_rq_head = (g_rq_head + 1) % RQ_MAX;
    g_rq_count--;
    return id;
}

static void rq_age_all() {
    for (uint8_t i = 0; i < RQ_MAX; i++) {
        if (g_rq_age[i] < 250) g_rq_age[i]++;
    }
}

bool scheduler_begin() { return true; }

void scheduler_reset() { g_rq_head = g_rq_count = 0; }

void scheduler_set_day(uint32_t day) {
    if (day == 0) return;
    if (day > g_day) {           // 跨天则清空重学队列
        g_rq_head = g_rq_count = 0;
    }
    store_set_day(day);
    if (day > g_day) g_day = day;
}

uint32_t scheduler_day() { return g_day; }

static int find_due(uint32_t *due_day) {
    CardState *st = store_states();
    uint16_t total = wordbook_count();
    int best = -1;
    uint32_t best_due = 0xFFFFFFFF;
    for (uint32_t i = 0; i < total; i++) {
        if (st[i].status == 1) {
            uint32_t due = st[i].last_day + st[i].interval;
            if (due <= g_day && due < best_due) { best_due = due; best = (int)i; }
        }
    }
    if (due_day) *due_day = best_due;
    return best;
}

uint32_t scheduler_due_count() {
    CardState *st = store_states();
    uint16_t total = wordbook_count();
    uint32_t n = 0;
    for (uint32_t i = 0; i < total; i++) {
        if (st[i].status == 1 && st[i].last_day + st[i].interval <= g_day) n++;
    }
    return n;
}

uint32_t scheduler_learn_count() { return g_rq_count; }

uint32_t scheduler_new_remaining() {
    const Meta &m = store_meta();
    const Params &p = store_params();
    uint32_t quota = p.new_per_day > m.new_done_today ? p.new_per_day - m.new_done_today : 0;
    uint32_t left = wordbook_count() > m.learned_count ? wordbook_count() - m.learned_count : 0;
    return quota < left ? quota : left;
}

/* 选取下一张新卡：顺序模式按下一张未学 id，随机模式在未学卡中均匀抽取 */
static int pick_new() {
    const Meta &m = store_meta();
    const Params &p = store_params();
    CardState *st = store_states();
    uint16_t total = wordbook_count();

    if (!p.random_order) {
        return m.next_new_id < total ? (int)m.next_new_id : -1;
    }
    uint32_t unlearned = total > m.learned_count ? total - m.learned_count : 0;
    if (unlearned == 0) return -1;
    uint32_t k = esp_random() % unlearned;
    for (uint32_t i = 0; i < total; i++) {
        if (st[i].status == 0 && k-- == 0) return (int)i;
    }
    return -1;
}

int scheduler_next() {
    int due = find_due(nullptr);
    bool has_new = scheduler_new_remaining() > 0;

    // 1. 已间隔开的重学卡
    if (g_rq_count && g_rq_age[g_rq_head] >= RQ_MIN_GAP) return rq_pop();
    // 2. 到期复习卡
    if (due >= 0) return due;
    // 3. 新卡
    if (has_new) {
        int id = pick_new();
        if (id >= 0) return id;
    }
    // 4. 兜底：队列里只剩重学卡时也照常发
    if (g_rq_count) return rq_pop();
    return -1;
}

bool scheduler_preview(uint16_t id, fsrs_next_states_t &ns) {
    CardState *st = store_states();
    const Params &p = store_params();
    fsrs_memory_state_t cur;
    const fsrs_memory_state_t *pcur = nullptr;
    uint32_t elapsed = 0;
    if (st[id].status == 1 && st[id].stability > 0) {
        cur.stability = st[id].stability;
        cur.difficulty = st[id].difficulty;
        cur.stability_fast = st[id].stability;
        pcur = &cur;
        elapsed = g_day > st[id].last_day ? g_day - st[id].last_day : 0;
    }
    return fsrs_next_states(p.w, p.dr, pcur, elapsed, &ns) == FSRS_OK;
}

bool scheduler_rate(uint16_t id, uint8_t rating, uint64_t ts, const char *word) {
    if (rating < FSRS_RATING_AGAIN || rating > FSRS_RATING_EASY) return false;
    if (id >= wordbook_count()) return false;

    rq_age_all();

    CardState *st = store_states();
    Meta &m = *(Meta *)&store_meta();
    CardState before = st[id];
    uint32_t elapsed = 0;
    if (before.status == 1 && before.stability > 0) {
        elapsed = g_day > before.last_day ? g_day - before.last_day : 0;
    }

    fsrs_next_states_t ns;
    if (!scheduler_preview(id, ns)) return false;

    const fsrs_memory_state_t *next = fsrs_next_states_pick(&ns, rating);
    uint32_t ivl = fsrs_next_states_interval(&ns, rating);
    if (!next) return false;

    bool was_new = (before.status == 0);
    CardState &c = st[id];
    c.stability = next->stability;
    c.difficulty = next->difficulty;
    c.last_day = g_day;
    c.interval = ivl;
    c.reps++;
    c.status = 1;
    c.last_rating = rating;
    store_states_dirty(id);

    if (was_new) {
        m.new_done_today++;
        m.learned_count++;
        if (id == m.next_new_id) m.next_new_id++;
    } else if (before.last_day != g_day) {
        m.old_words_today++;   // 该旧词今天第一次复习
    }
    m.total_reviews++;
    m.reviews_today++;
    store_meta_dirty();

    // 重来/困难 → 本 session 稍后再学一次
    if (rating <= FSRS_RATING_HARD) rq_push(id);

    store_log_review(ts, g_day, id, word ? word : "", rating, elapsed, before, c);
    // 不在请求路径上写 Flash，由 loop() 里的 store_flush_if_needed 节流处理
    return true;
}
