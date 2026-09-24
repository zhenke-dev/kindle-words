#pragma once
#include <Arduino.h>
#include "fsrs.h"
#include "store.h"

bool     scheduler_begin();
void     scheduler_reset();                      // 清空今日重学队列（配合数据重置）
void     scheduler_set_day(uint32_t day);          // 每个请求携带的客户端逻辑日
uint32_t scheduler_day();

/* 下一张待学习卡片 id；-1 表示今日队列完成 */
int      scheduler_next();
/* 计算某卡片四个评分的后继状态与间隔（elapsed 按当前逻辑日） */
bool     scheduler_preview(uint16_t id, fsrs_next_states_t &ns);
/* 对卡片评分，更新状态、写日志；err 为空表示成功 */
bool     scheduler_rate(uint16_t id, uint8_t rating, uint64_t ts, const char *word);

uint32_t scheduler_due_count();        // 今日到期复习卡数量
uint32_t scheduler_learn_count();      // 今日重学队列中待重现的卡数
uint32_t scheduler_new_remaining();    // 今日剩余新卡额度
