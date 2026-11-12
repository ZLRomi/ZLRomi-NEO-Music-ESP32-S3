#include "cover.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "jpeg.h"
#include "netease.h"
#include "theme.h"

static const char *TAG = "cover";

#define CACHE_N      8
#define BODY_MAX     (32 * 1024)

typedef struct {
    int64_t  id;
    bool     used;
    uint32_t slot;
    cover_t  img;
} cache_item_t;

static cache_item_t s_cache[CACHE_N];
static uint32_t     s_serial;

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    bool     overflow;
} body_t;

static esp_err_t on_event(esp_http_client_event_t *e)
{
    body_t *b = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b) {
        if (b->len + e->data_len + 1 > b->cap) {
            size_t want = b->cap + e->data_len + 2048;
            if (want > BODY_MAX) { b->overflow = true; return ESP_FAIL; }
            uint8_t *p = realloc(b->buf, want);
            if (!p) { b->overflow = true; return ESP_FAIL; }
            b->buf = p;
            b->cap = want;
        }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
    }
    return ESP_OK;
}

/** 无网络 / 解码失败时的封面：暖色底 + 唱片同心圆，不是空白占位 */
static void fallback_cover(cover_t *out)
{
    uint16_t base = g_theme.surface_hi;
    uint16_t ring = g_theme.accent_dim;
    uint16_t dot  = g_theme.accent;
    int cx = COVER_W / 2, cy = COVER_H / 2;
    for (int y = 0; y < COVER_H; y++) {
        for (int x = 0; x < COVER_W; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            uint16_t c = base;
            if (d2 < 20 * 20) c = base;
            if (d2 < 18 * 18 && d2 > 16 * 16) c = ring;
            if (d2 < 11 * 11 && d2 > 9 * 9) c = ring;
            if (d2 < 4 * 4) c = dot;
            out->px[y * COVER_W + x] = c;
        }
    }
}

static void cache_put(int64_t id, const cover_t *img)
{
    int victim = 0;
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < CACHE_N; i++) {
        if (!s_cache[i].used) { victim = i; break; }
        if (s_cache[i].slot < oldest) {
            oldest = s_cache[i].slot;
            victim = i;
        }
    }
    s_cache[victim].id = id;
    s_cache[victim].used = true;
    s_cache[victim].slot = ++s_serial;
    memcpy(&s_cache[victim].img, img, sizeof(cover_t));
}

static bool cache_get(int64_t id, cover_t *out)
{
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].used && s_cache[i].id == id) {
            s_cache[i].slot = ++s_serial;
            memcpy(out, &s_cache[i].img, sizeof(cover_t));
            return true;
        }
    }
    return false;
}

bool cover_get(int64_t song_id, const char *url, cover_t *out)
{
    if (!out) return false;
    if (cache_get(song_id, out)) return true;

    if (!url || !*url) {
        fallback_cover(out);
        return false;
    }

    /* 网易云封面支持 param 缩略图，减小下载与解码量 */
    char small_url[SONG_COVER_LEN + 24];
    snprintf(small_url, sizeof(small_url), "%s?param=120y120", url);

    body_t b = { .buf = malloc(4096), .len = 0, .cap = 4096, .overflow = false };
    if (!b.buf) {
        fallback_cover(out);
        return false;
    }

    esp_http_client_config_t cfg = {
        .url = small_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_event,
        .user_data = &b,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    bool ok = false;
    if (cli) {
        esp_err_t err = esp_http_client_perform(cli);
        int status = esp_http_client_get_status_code(cli);
        esp_http_client_cleanup(cli);
        if (err == ESP_OK && status >= 200 && status < 300 &&
            !b.overflow && b.len > 128) {
            ok = jpeg_decode_rgb565(b.buf, b.len, out->px, COVER_W, COVER_H);
        }
    }
    free(b.buf);

    if (!ok) {
        ESP_LOGW(TAG, "封面取不到（id=%lld），用唱片图代替", (long long)song_id);
        fallback_cover(out);
        return false;
    }
    cache_put(song_id, out);
    return true;
}
