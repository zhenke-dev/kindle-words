#include "wordbook.h"
#include <LittleFS.h>

static const char *kPath = "/words.bin";
static uint32_t *g_offsets = nullptr;   // 每条记录的绝对偏移
static uint16_t  g_count = 0;

bool wordbook_begin() {
    File f = LittleFS.open(kPath, "r");
    if (!f) {
        Serial.println("[wordbook] open words.bin failed");
        return false;
    }
    char magic[4];
    if (f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "KWDB", 4) != 0) {
        Serial.println("[wordbook] bad magic");
        return false;
    }
    uint8_t hdr[2];
    if (f.read(hdr, 2) != 2) return false;
    g_count = hdr[0] | (hdr[1] << 8);
    g_offsets = (uint32_t *)heap_caps_malloc(g_count * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_offsets) g_offsets = (uint32_t *)malloc(g_count * 4);
    if (!g_offsets) return false;
    if (f.read((uint8_t *)g_offsets, g_count * 4) != g_count * 4) return false;
    f.close();
    Serial.printf("[wordbook] %u words\n", g_count);
    return true;
}

uint16_t wordbook_count() { return g_count; }

bool wordbook_get(uint16_t id, char *word, size_t wcap, char *meaning, size_t mcap) {
    if (id >= g_count) return false;
    File f = LittleFS.open(kPath, "r");
    if (!f) return false;
    if (!f.seek(g_offsets[id])) { f.close(); return false; }

    uint8_t wl = 0;
    if (f.read(&wl, 1) != 1) { f.close(); return false; }
    if (wl >= wcap) wl = wcap - 1;
    if (f.read((uint8_t *)word, wl) != wl) { f.close(); return false; }
    word[wl] = 0;

    uint8_t ml2[2];
    if (f.read(ml2, 2) != 2) { f.close(); return false; }
    uint16_t ml = ml2[0] | (ml2[1] << 8);
    uint16_t want = ml;
    if (want >= mcap) want = mcap - 1;
    if (f.read((uint8_t *)meaning, want) != want) { f.close(); return false; }
    meaning[want] = 0;
    if (want < ml) f.seek(g_offsets[id] + 1 + wl + 2 + ml);  // 丢弃剩余
    f.close();
    return true;
}
