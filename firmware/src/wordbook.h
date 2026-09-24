#pragma once
#include <Arduino.h>

/* words.bin 只读访问（格式见 tools/build_words.py） */
bool     wordbook_begin();
uint16_t wordbook_count();
bool     wordbook_get(uint16_t id, char *word, size_t wcap, char *meaning, size_t mcap);
