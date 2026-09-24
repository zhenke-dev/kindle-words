#pragma once
#include <Arduino.h>
#include "fsrs.h"

/* 卡片记忆状态（20 字节定长记录，整块持久化到 /states.bin） */
struct CardState {
    float    stability;
    float    difficulty;
    uint32_t last_day;     // 最近一次复习的逻辑日（客户端提供的 unix day）
    uint32_t interval;     // 距下次复习的天数
    uint16_t reps;
    uint8_t  status;       // 0 = 未学习, 1 = 已学习
    uint8_t  last_rating;
};
static_assert(sizeof(CardState) == 20, "CardState must be 20 bytes");

struct Meta {
    uint32_t cur_day;         // 当前逻辑日
    uint32_t new_done_today;  // 今日已学新卡数
    uint32_t next_new_id;     // 下一张新卡 id（顺序发新卡时使用）
    uint32_t total_reviews;
    uint32_t reviews_today;
    uint32_t old_words_today; // 今日复习过的旧词数（同词重学不重复计数）
    uint32_t params_version;  // 参数代数，每次更新 +1
    uint32_t learned_count;   // 已学习卡片总数（随机顺序时不等于 next_new_id）
};

struct Params {
    float    w[FSRS_PARAMETER_COUNT];
    float    dr;
    uint32_t new_per_day;
    uint32_t random_order;    // 新词顺序：0 = 按词书顺序, 1 = 随机
};

bool           store_begin(uint16_t card_count);
const Meta   & store_meta();
const Params & store_params();
CardState    * store_states();          // 整个卡片状态数组（RAM 镜像）
void           store_set_day(uint32_t day);
void           store_meta_dirty();
void           store_states_dirty(uint16_t id);   // 标记某卡所属段为脏
void           store_set_params(const float *w, float dr);
void           store_set_settings(uint32_t new_per_day, float dr, uint32_t random_order);
void           store_flush_if_needed(); // 节流写回（每若干次复习/定时）
void           store_flush();
void           store_reset_data();      // 清除全部学习数据（卡片状态/元数据/日志），保留 FSRS 参数
void           store_reset_params();    // FSRS 参数恢复默认值
void           store_log_review(uint64_t ts, uint32_t day, uint16_t id, const char *word,
                                uint8_t rating, uint32_t elapsed,
                                const CardState &before, const CardState &after);
