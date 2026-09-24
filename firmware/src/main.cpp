#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "wordbook.h"
#include "store.h"
#include "scheduler.h"
#include "index_html.h"

/* ------------------------------------------------------------------ 配置 */
static const char *kApSsid = "KindleWords";
static const char *kApPass = "kindle1234";
static const byte  kDnsPort = 53;

static AsyncWebServer server(80);
static DNSServer dns;   // 捕获式 DNS：让 Kindle 的联网检测通过，WiFi 才保持连接

/* POST body 累积缓冲（单客户端，请求体都很小） */
static String g_body;

/* --------------------------------------------------------------- 小工具 */

/* 在 JSON body 中找 "key" 后的数值 */
static double json_num(const char *body, const char *key, double def) {
    if (!body) return def;
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return def;
    p = strchr(p + strlen(pat), ':');
    if (!p) return def;
    return strtod(p + 1, nullptr);
}

/* JSON 字符串转义（只处理引号、反斜杠与控制字符，UTF-8 原样透传） */
static size_t json_escape(char *out, size_t cap, const char *in) {
    size_t n = 0;
    for (const char *p = in; *p && n + 7 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { out[n++] = '\\'; out[n++] = c; }
        else if (c < 0x20) { n += snprintf(out + n, cap - n, "\\u%04x", c); }
        else out[n++] = c;
    }
    out[n] = 0;
    return n;
}

static void send_json(AsyncWebServerRequest *r, int code, const char *json) {
    AsyncWebServerResponse *resp = r->beginResponse(code, "application/json", json);
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
}

static void send_index(AsyncWebServerRequest *r) {
    // 页面内嵌于固件（见 tools/embed_html.py），更新 UI 无需擦除 LittleFS
    AsyncWebServerResponse *resp =
        r->beginResponse_P(200, "text/html", INDEX_HTML);
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
}

static void send_admin(AsyncWebServerRequest *r) {
    AsyncWebServerResponse *resp =
        r->beginResponse_P(200, "text/html", ADMIN_HTML);
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
}

/* 组装"下一张卡片"响应（/api/next 与 /api/rate 共用） */
static void send_next_card(AsyncWebServerRequest *r) {
    static char buf[2048];
    uint32_t due = scheduler_due_count() + scheduler_learn_count();
    uint32_t newr = scheduler_new_remaining();
    const Meta &m = store_meta();

    int id = scheduler_next();
    if (id < 0) {
        snprintf(buf, sizeof(buf),
                 "{\"done\":1,\"due\":%u,\"new\":%u,\"learned\":%u,\"today\":%u}",
                 due, newr, m.learned_count, m.reviews_today);
        send_json(r, 200, buf);
        return;
    }

    char word[24], meaning[256], esc[400];
    if (!wordbook_get((uint16_t)id, word, sizeof(word), meaning, sizeof(meaning))) {
        send_json(r, 500, "{\"error\":\"wordbook\"}");
        return;
    }
    json_escape(esc, sizeof(esc), meaning);

    fsrs_next_states_t ns;
    uint32_t ia = 1, ih = 1, ig = 1, ie = 1;
    if (scheduler_preview((uint16_t)id, ns)) {
        ia = ns.interval_again; ih = ns.interval_hard;
        ig = ns.interval_good; ie = ns.interval_easy;
    }
    snprintf(buf, sizeof(buf),
             "{\"done\":0,\"id\":%d,\"word\":\"%s\",\"meaning\":\"%s\","
             "\"ivl\":[%u,%u,%u,%u],\"due\":%u,\"new\":%u}",
             id, word, esc, ia, ih, ig, ie, due, newr);
    send_json(r, 200, buf);
}

/* --------------------------------------------------------------- 路由 */

static void handle_body(AsyncWebServerRequest *r, uint8_t *data, size_t len,
                        size_t index, size_t total) {
    if (index == 0) g_body = "";
    for (size_t i = 0; i < len; i++) g_body += (char)data[i];
}

static void setup_routes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { send_index(r); });
    server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *r) { send_admin(r); });

    server.on("/api/next", HTTP_GET, [](AsyncWebServerRequest *r) {
        if (r->hasParam("day")) {
            scheduler_set_day((uint32_t)r->getParam("day")->value().toInt());
        }
        send_next_card(r);
    });

    server.on("/api/rate", HTTP_POST,
        [](AsyncWebServerRequest *r) {
            const char *b = g_body.c_str();
            scheduler_set_day((uint32_t)json_num(b, "day", 0));
            long id = (long)json_num(b, "id", -1);
            long rating = (long)json_num(b, "rating", 0);
            uint64_t ts = (uint64_t)json_num(b, "ts", 0);

            char word[24] = "";
            if (id >= 0) {
                char meaning[256];
                wordbook_get((uint16_t)id, word, sizeof(word), meaning, sizeof(meaning));
            }
            if (id < 0 || !scheduler_rate((uint16_t)id, (uint8_t)rating, ts, word)) {
                send_json(r, 400, "{\"error\":\"rate\"}");
                return;
            }
            send_next_card(r);
        },
        nullptr, handle_body);

    server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *r) {
        static char buf[512];
        const Meta &m = store_meta();
        const Params &p = store_params();
        snprintf(buf, sizeof(buf),
                 "{\"total\":%u,\"learned\":%u,\"due\":%u,\"new_done\":%u,"
                 "\"old_today\":%u,\"new_remaining\":%u,\"new_per_day\":%u,"
                 "\"reviews_today\":%u,\"total_reviews\":%u,\"dr\":%.2f,"
                 "\"params_version\":%u,\"order\":%u}",
                 wordbook_count(), m.learned_count,
                 scheduler_due_count() + scheduler_learn_count(),
                 m.new_done_today, m.old_words_today, scheduler_new_remaining(),
                 p.new_per_day, m.reviews_today, m.total_reviews,
                 (double)p.dr, m.params_version, p.random_order);
        send_json(r, 200, buf);
    });

    server.on("/api/export.csv", HTTP_GET, [](AsyncWebServerRequest *r) {
        store_flush();
        AsyncWebServerResponse *resp =
            r->beginResponse(LittleFS, "/reviews.csv", "text/csv", true);
        resp->addHeader("Content-Disposition",
                        "attachment; filename=\"reviews.csv\"");
        r->send(resp);
    });

    server.on("/api/params", HTTP_GET, [](AsyncWebServerRequest *r) {
        static char buf[640];
        const Params &p = store_params();
        int n = snprintf(buf, sizeof(buf), "{\"w\":[");
        for (int i = 0; i < FSRS_PARAMETER_COUNT; i++) {
            n += snprintf(buf + n, sizeof(buf) - n, "%s%.6g", i ? "," : "", (double)p.w[i]);
        }
        snprintf(buf + n, sizeof(buf) - n, "],\"dr\":%.4f,\"params_version\":%u}",
                 (double)p.dr, store_meta().params_version);
        send_json(r, 200, buf);
    });

    server.on("/api/params", HTTP_POST,
        [](AsyncWebServerRequest *r) {
            const char *p = strstr(g_body.c_str(), "\"w\"");
            if (!p) { send_json(r, 400, "{\"error\":\"no w\"}"); return; }
            p = strchr(p, '[');
            if (!p) { send_json(r, 400, "{\"error\":\"bad w\"}"); return; }
            float w[FSRS_PARAMETER_COUNT];
            const char *cur = p + 1;
            for (int i = 0; i < FSRS_PARAMETER_COUNT; i++) {
                char *end = nullptr;
                w[i] = strtof(cur, &end);
                if (end == cur) { send_json(r, 400, "{\"error\":\"w count\"}"); return; }
                cur = end;
                while (*cur == ' ' || *cur == ',') cur++;
            }
            for (int i = 0; i < FSRS_PARAMETER_COUNT; i++) {
                if (!isfinite(w[i])) { send_json(r, 400, "{\"error\":\"w finite\"}"); return; }
            }
            float dr = (float)json_num(g_body.c_str(), "dr", store_params().dr);
            if (dr <= 0.f || dr >= 1.f) dr = store_params().dr;
            store_set_params(w, dr);
            send_json(r, 200, "{\"ok\":1}");
        },
        nullptr, handle_body);

    server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest *r) {
            double npd = json_num(g_body.c_str(), "new_per_day", store_params().new_per_day);
            double dr = json_num(g_body.c_str(), "dr", store_params().dr);
            double order = json_num(g_body.c_str(), "order", store_params().random_order);
            store_set_settings((uint32_t)npd, (float)dr, (uint32_t)order);
            send_json(r, 200, "{\"ok\":1}");
        },
        nullptr, handle_body);

    server.on("/api/reset_data", HTTP_POST, [](AsyncWebServerRequest *r) {
        store_reset_data();
        scheduler_reset();
        send_json(r, 200, "{\"ok\":1}");
    });

    server.on("/api/reset_params", HTTP_POST, [](AsyncWebServerRequest *r) {
        store_reset_params();
        send_json(r, 200, "{\"ok\":1}");
    });

    // 各平台联网检测地址：直接回 204，让设备认定"已联网"
    const char *probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                            "/library/test/success.html", "/ncsi.txt",
                            "/connectivity-check", "/kindle-wifi/wifistub.html"};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        server.on(probes[i], HTTP_GET, [](AsyncWebServerRequest *r) { r->send(204); });
    }

    server.onNotFound([](AsyncWebServerRequest *r) {
        // 只有浏览器导航请求才回首页；其余（后台探测、图标等）快速 404
        if (r->method() == HTTP_GET && r->hasHeader("Accept") &&
            r->header("Accept").indexOf("text/html") >= 0) {
            send_index(r);
            return;
        }
        r->send(404, "text/plain", "");
    });
}

/* --------------------------------------------------------------- 入口 */

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[boot] KindleWords starting");

    if (!LittleFS.begin(true)) {
        Serial.println("[boot] LittleFS mount failed");
    }
    if (!wordbook_begin()) {
        Serial.println("[boot] wordbook init failed");
    }
    if (!store_begin(wordbook_count())) {
        Serial.println("[boot] store init failed");
    }
    scheduler_begin();

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);            // 关闭省电，消除 AP 响应延迟抖动
    WiFi.softAP(kApSsid, kApPass);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[wifi] AP %s  ip=%s\n", kApSsid, ip.toString().c_str());

    dns.start(kDnsPort, "*", ip);   // 所有域名都解析到本机

    setup_routes();
    server.begin();
    Serial.println("[http] async server started on :80");
}

void loop() {
    dns.processNextRequest();
    store_flush_if_needed();
    delay(2);
}
