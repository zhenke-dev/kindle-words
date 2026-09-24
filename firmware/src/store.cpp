#include "store.h"
#include <LittleFS.h>

static const char *kMetaPath   = "/meta.bin";
static const char *kParamsPath = "/params.bin";
static const char *kLogPath    = "/reviews.csv";
static const uint32_t kSegMagic    = 0x4B575347;  // "KWSG"
static const uint32_t kMetaMagic   = 0x4B574D54;  // "KWMT"
static const uint32_t kParamsMagic = 0x4B575052;  // "KWPR"

/* 卡片状态按 512 张一段分文件存储（/s00../sNN），写回时只写脏段，
   单次 Flash 写入从 ~131KB 降到 ~10KB，消除评分时的整表写阻塞 */
static const uint16_t kSegCards = 512;

static CardState *g_states = nullptr;
static uint16_t   g_count = 0;
static uint16_t   g_seg_count = 0;
static uint32_t   g_dirty_segs = 0;    // 段脏位图（段数 ≤ 32）
static Meta       g_meta;
static Params     g_params;
static bool       g_meta_dirty = false;
static uint32_t   g_reviews_since_flush = 0;
static uint32_t   g_last_flush_ms = 0;

static const uint32_t kFlushEveryReviews = 10;
static const uint32_t kFlushIntervalMs   = 30000;

static void seg_path(uint16_t seg, char *out, size_t cap) {
    snprintf(out, cap, "/s%02d", seg);
}

static void write_seg(uint16_t seg) {
    char path[8];
    seg_path(seg, path, sizeof(path));
    File f = LittleFS.open(path, "w");
    if (!f) return;
    uint32_t magic = kSegMagic;
    uint16_t base = seg * kSegCards;
    uint16_t cnt = g_count - base;
    if (cnt > kSegCards) cnt = kSegCards;
    f.write((uint8_t *)&magic, 4);
    f.write((uint8_t *)&base, 2);
    f.write((uint8_t *)&cnt, 2);
    f.write((uint8_t *)(g_states + base), cnt * sizeof(CardState));
    f.close();
}

static void write_meta() {
    File f = LittleFS.open(kMetaPath, "w");
    if (!f) return;
    uint32_t magic = kMetaMagic;
    f.write((uint8_t *)&magic, 4);
    f.write((uint8_t *)&g_meta, sizeof(g_meta));
    f.close();
    g_meta_dirty = false;
}

static void write_params() {
    File f = LittleFS.open(kParamsPath, "w");
    if (!f) return;
    uint32_t magic = kParamsMagic;
    f.write((uint8_t *)&magic, 4);
    f.write((uint8_t *)&g_params, sizeof(g_params));
    f.close();
}

static void params_defaults() {
    const float *def = fsrs_default_parameters();
    memcpy(g_params.w, def, sizeof(g_params.w));
    g_params.dr = 0.9f;
    g_params.new_per_day = 20;
    g_params.random_order = 0;
}

bool store_begin(uint16_t card_count) {
    g_count = card_count;
    g_seg_count = (g_count + kSegCards - 1) / kSegCards;
    g_states = (CardState *)heap_caps_malloc(g_count * sizeof(CardState),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_states) g_states = (CardState *)malloc(g_count * sizeof(CardState));
    if (!g_states) return false;
    memset(g_states, 0, g_count * sizeof(CardState));
    memset(&g_meta, 0, sizeof(g_meta));
    params_defaults();

    LittleFS.remove("/states.bin");   // 旧版整表文件（开发期迁移）

    // 分段卡片状态
    for (uint16_t seg = 0; seg < g_seg_count; seg++) {
        char path[8];
        seg_path(seg, path, sizeof(path));
        File f = LittleFS.open(path, "r");
        if (!f) continue;
        uint32_t magic = 0; uint16_t base = 0, cnt = 0;
        if (f.read((uint8_t *)&magic, 4) == 4 && magic == kSegMagic &&
            f.read((uint8_t *)&base, 2) == 2 && base == seg * kSegCards &&
            f.read((uint8_t *)&cnt, 2) == 2 && base + cnt <= g_count) {
            f.read((uint8_t *)(g_states + base), cnt * sizeof(CardState));
        }
        f.close();
    }
    // 元数据（旧版文件可能更短，短读时新增字段保持 0）
    File f = LittleFS.open(kMetaPath, "r");
    if (f) {
        uint32_t magic = 0;
        if (f.read((uint8_t *)&magic, 4) == 4 && magic == kMetaMagic) {
            f.read((uint8_t *)&g_meta, sizeof(g_meta));
        }
        f.close();
    }
    // 旧数据没有 learned_count 字段：顺序学习历史下与 next_new_id 等价
    if (g_meta.learned_count == 0 && g_meta.next_new_id > 0) {
        g_meta.learned_count = g_meta.next_new_id;
    }
    // 参数（旧版文件可能缺少 random_order 字段，短读时继承默认值）
    f = LittleFS.open(kParamsPath, "r");
    if (f) {
        uint32_t magic = 0;
        if (f.read((uint8_t *)&magic, 4) == 4 && magic == kParamsMagic) {
            Params p = g_params;
            size_t got = f.read((uint8_t *)&p, sizeof(p));
            if (got >= 92) {   // w[21] + dr + new_per_day
                bool ok = true;
                for (int i = 0; i < FSRS_PARAMETER_COUNT; i++) ok &= isfinite(p.w[i]);
                if (ok && p.dr > 0.f && p.dr < 1.f) g_params = p;
            }
        }
        f.close();
    }
    // 日志文件不存在则写表头
    if (!LittleFS.exists(kLogPath)) {
        f = LittleFS.open(kLogPath, "w");
        if (f) {
            f.println("ts,day,word_id,word,rating,elapsed_days,s_before,d_before,s_after,d_after,interval");
            f.close();
        }
    }
    Serial.printf("[store] segs=%u next_new=%u total_reviews=%u params_v=%u\n",
                  g_seg_count, g_meta.next_new_id, g_meta.total_reviews,
                  g_meta.params_version);
    return true;
}

const Meta   &store_meta()    { return g_meta; }
const Params &store_params()  { return g_params; }
CardState    *store_states()  { return g_states; }

void store_set_day(uint32_t day) {
    if (day > g_meta.cur_day) {
        g_meta.cur_day = day;
        g_meta.new_done_today = 0;
        g_meta.reviews_today = 0;
        g_meta.old_words_today = 0;
        g_meta_dirty = true;
    }
}

void store_meta_dirty() { g_meta_dirty = true; g_reviews_since_flush++; }

void store_states_dirty(uint16_t id) {
    uint16_t seg = id / kSegCards;
    if (seg < 32) g_dirty_segs |= (1u << seg);
}

void store_set_params(const float *w, float dr) {
    memcpy(g_params.w, w, sizeof(g_params.w));
    g_params.dr = dr;
    g_meta.params_version++;
    write_params();
    write_meta();
}

void store_set_settings(uint32_t new_per_day, float dr, uint32_t random_order) {
    if (new_per_day > 0 && new_per_day <= 200) g_params.new_per_day = new_per_day;
    if (dr > 0.7f && dr < 0.99f) g_params.dr = dr;
    g_params.random_order = random_order ? 1 : 0;
    write_params();
}

void store_flush_if_needed() {
    bool due_reviews = g_reviews_since_flush >= kFlushEveryReviews;
    bool due_time = g_dirty_segs && (millis() - g_last_flush_ms > kFlushIntervalMs);
    if (due_reviews || due_time) store_flush();
}

void store_flush() {
    uint32_t segs = g_dirty_segs;
    if (segs) {
        for (uint16_t seg = 0; seg < g_seg_count; seg++) {
            if (segs & (1u << seg)) write_seg(seg);
        }
        g_dirty_segs = 0;
    }
    if (g_meta_dirty) write_meta();
    g_reviews_since_flush = 0;
    g_last_flush_ms = millis();
}

static void write_log_header() {
    LittleFS.remove(kLogPath);
    File f = LittleFS.open(kLogPath, "w");
    if (f) {
        f.println("ts,day,word_id,word,rating,elapsed_days,s_before,d_before,s_after,d_after,interval");
        f.close();
    }
}

void store_reset_data() {
    store_flush();
    memset(g_states, 0, g_count * sizeof(CardState));
    uint32_t pv = g_meta.params_version;
    memset(&g_meta, 0, sizeof(g_meta));
    g_meta.params_version = pv;          // 参数代数保留
    for (uint16_t seg = 0; seg < g_seg_count; seg++) {
        char path[8];
        seg_path(seg, path, sizeof(path));
        LittleFS.remove(path);
    }
    LittleFS.remove(kMetaPath);
    write_log_header();
    g_dirty_segs = 0;
    g_meta_dirty = false;
    g_reviews_since_flush = 0;
    Serial.println("[store] learning data reset");
}

void store_reset_params() {
    params_defaults();
    g_meta.params_version = 0;      // 恢复默认参数后代数归零
    write_params();
    write_meta();
    Serial.println("[store] params reset to default");
}

void store_log_review(uint64_t ts, uint32_t day, uint16_t id, const char *word,
                      uint8_t rating, uint32_t elapsed,
                      const CardState &before, const CardState &after) {
    File f = LittleFS.open(kLogPath, "a");
    if (!f) return;
    char line[160];
    snprintf(line, sizeof(line), "%llu,%u,%u,%s,%u,%u,%.4f,%.4f,%.4f,%.4f,%u\n",
             (unsigned long long)ts, day, id, word, rating, elapsed,
             (double)before.stability, (double)before.difficulty,
             (double)after.stability, (double)after.difficulty, after.interval);
    f.print(line);
    f.close();
}
