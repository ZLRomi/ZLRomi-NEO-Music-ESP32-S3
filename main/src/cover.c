/*
 * cover.c —— 封面异步下载 + JPEG 解码 + 内存缓存
 *
 * 架构：
 *   UI 线程  cover_peek()  → 查缓存失败就把 id/url 投进队列，立即返回 false
 *   后台任务  cover_task() → 从队列取任务，下载 JPEG（带浏览器 UA）、解码成 96x96
 *                            RGB565，写入缓存；永不阻塞 UI
 *
 * 缓存用最近最少使用淘汰（LRU），容量 CACHE_N 张。封面 URL 一并缓存，
 * 避免同一张图因为 URL 变体被重复下载。
 */
#include "cover.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg.h"
#include "netease.h"

/* 只要 ui_redraw 一个符号，单独前向声明，避免拉进整个 ui.h 造成循环依赖 */
void ui_redraw(void);

static const char *TAG = "cover";

#define CACHE_N     24          /* 24 张 × 18KB ≈ 432KB，全在 PSRAM（8MB 里占 5%） */
#define QUEUE_N     32
#define BODY_MAX    (48 * 1024)
/* 12KB：jpeg_t 结构体本身在栈上约 2.3KB，再加 HTTP/TLS 开销。
   ⚠️ 栈在内部 RAM：PSRAM 栈上跑 TLS（内部要走 flash 读证书）会触发
      esp_task_stack_is_sane_cache_disabled() 断言崩溃。 */
#define TASK_STACK  (12 * 1024)

/* 这些 CDN 走 HTTPS 需要正常 UA，个别节点对空 UA 直接 403 */
static const char *UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120 Safari/537.36";

typedef struct {
    int64_t id;
    char    url[SONG_COVER_LEN];
} req_t;

typedef struct {
    int64_t  id;
    bool     used;
    uint32_t stamp;
    cover_t  img;
    char     url[SONG_COVER_LEN];
} cache_item_t;

/* 缓存与队列都放 PSRAM：12 张 × 6KB ≈ 72KB，内部 RAM 吃不消 */
static EXT_RAM_BSS_ATTR cache_item_t s_cache[CACHE_N];
static uint32_t s_stamp;

static QueueHandle_t     s_q;
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;
static volatile bool     s_ready;

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    bool     overflow;
} body_t;

/**
 * 分配下载缓冲。
 * 一律走 PSRAM：封面 JPEG 最大 48KB，用普通 malloc 会在内部 RAM 里挖一个大洞，
 * 而内部 RAM 只有约 306KB，WiFi/LWIP 与 mbedTLS 都要挤在那里。
 * 下载缓冲只是普通数据，不参与 DMA。
 */
static void *body_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = malloc(n);   /* PSRAM 不足时退回内部 RAM */
    }
    return p;
}

/** 扩容（不用 realloc：它不保证保持 PSRAM 属性） */
static void *body_grow(void *old, size_t old_len, size_t want)
{
    void *p = body_alloc(want);
    if (!p) {
        return NULL;
    }
    if (old && old_len) {
        memcpy(p, old, old_len);
    }
    free(old);
    return p;
}

static esp_err_t on_event(esp_http_client_event_t *e)
{
    body_t *b = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b) {
        if (b->len + e->data_len + 1 > b->cap) {
            size_t want = b->cap + e->data_len + 2048;
            if (want > BODY_MAX) {
                b->overflow = true;
                return ESP_FAIL;
            }
            uint8_t *p = body_grow(b->buf, b->len, want);
            if (!p) {
                b->overflow = true;
                return ESP_FAIL;
            }
            b->buf = p;
            b->cap = want;
        }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
    }
    return ESP_OK;
}

/* 后台任务主体（定义在文件末尾，这里先声明） */
static void cover_task_body(void *arg);

void cover_init(void)
{
    if (s_ready) return;
    s_q = xQueueCreate(QUEUE_N, sizeof(req_t));
    s_lock = xSemaphoreCreateMutex();
    if (!s_q || !s_lock) {
        ESP_LOGE(TAG, "封面队列/锁创建失败");
        return;
    }
    s_ready = true;
    /* 栈必须在内部 RAM：PSRAM 栈上跑 TLS / 读 flash 会断言崩溃。
       缓冲与缓存另用 heap_caps 单独放到 PSRAM，不占内部 RAM。 */
    if (xTaskCreate(cover_task_body, "cover", TASK_STACK, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "封面任务创建失败");
        s_ready = false;
        return;
    }
    ESP_LOGI(TAG, "封面后台任务已启动（缓存 %d 张）", CACHE_N);
}

int cover_cached_count(void)
{
    int n = 0;
    if (!s_ready) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].used) n++;
    }
    xSemaphoreGive(s_lock);
    return n;
}

/** 查缓存（内部调用，需已持锁） */
static cache_item_t *find_locked(int64_t id)
{
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].used && s_cache[i].id == id) {
            s_cache[i].stamp = ++s_stamp;
            return &s_cache[i];
        }
    }
    return NULL;
}

bool cover_peek(int64_t song_id, const char *url, cover_t *out)
{
    if (!s_ready || !out) {
        return false;
    }
    bool hit = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cache_item_t *it = find_locked(song_id);
    if (it) {
        memcpy(out, &it->img, sizeof(cover_t));
        hit = true;
    }
    xSemaphoreGive(s_lock);

    if (!hit && url && *url) {
        cover_request(song_id, url);   /* 没命中就后台去取，UI 不等 */
    }
    return hit;
}

void cover_request(int64_t song_id, const char *url)
{
    if (!s_ready || !url || !*url) {
        return;
    }
    /* 已在缓存里就不用排队 */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool have = find_locked(song_id) != NULL;
    xSemaphoreGive(s_lock);
    if (have) {
        return;
    }

    req_t r;
    memset(&r, 0, sizeof(r));
    r.id = song_id;
    strncpy(r.url, url, sizeof(r.url) - 1);
    /* 队列满就丢最旧的，避免 UI 线程卡在发送上 */
    if (xQueueSend(s_q, &r, 0) != pdTRUE) {
        req_t drop;
        xQueueReceive(s_q, &drop, 0);
        xQueueSend(s_q, &r, 0);
    }
}

/**
 * 规范化封面 URL。
 *
 * 歌单接口返回的封面 URL 有时自带参数（?param=...、?imageView...），
 * 直接拼 &param=200y200 服务端不一定认，实测会返回 PNG（89 50 4E 47），
 * 而我们的解码器只吃 JPEG。所以这里把原来的参数整段砍掉，重新指定：
 *
 *    ?param=200y200&type=jpg
 *
 * param 是网易云 CDN 的缩略图参数，type=jpg 明确要 JPEG。
 * 200x200 是为播放页 96x96 留余量，下载量约 7KB，远小于原图。
 */
static void build_cover_url(const char *src, char *dst, size_t cap)
{
    if (!src || !*src) {
        dst[0] = '\0';
        return;
    }
    /* 找到 '?' 之前的位置，保留 scheme + host + path */
    const char *q = strchr(src, '?');
    size_t n = q ? (size_t)(q - src) : strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';

    const char *suffix = "?param=200y200&type=jpg";
    if (n + strlen(suffix) < cap) {
        memcpy(dst + n, suffix, strlen(suffix) + 1);
    }
}

/** 下载并解码一张封面，成功返回 true */
static bool fetch_one(const req_t *r, cover_t *out)
{
    char u[SONG_COVER_LEN + 32];
    build_cover_url(r->url, u, sizeof(u));

    body_t b = { .buf = body_alloc(4096), .len = 0, .cap = 4096, .overflow = false };
    if (!b.buf) {
        return false;
    }

    ESP_LOGI(TAG, "下封面 %s", u);

    esp_http_client_config_t cfg = {
        .url = u,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_event,
        .user_data = &b,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        free(b.buf);
        return false;
    }
    esp_http_client_set_header(cli, "User-Agent", UA);
    esp_http_client_set_header(cli, "Accept", "image/jpeg,image/*");

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    bool ok = false;
    if (err == ESP_OK && status >= 200 && status < 300 && !b.overflow && b.len > 128) {
        /* 先确认确实是图片（JPEG 的 FF D8 或 PNG 的 89 50 4E 47）。
           网易云 CDN 两种都给：早期多 JPEG，现在 param 参数有时返回 PNG。
           之前只认 JPEG，导致大部分封面被丢掉、只显示占位图。 */
        bool is_jpeg = (b.buf[0] == 0xFF && b.buf[1] == 0xD8);
        bool is_png  = (b.buf[0] == 0x89 && b.buf[1] == 'P' &&
                        b.buf[2] == 'N' && b.buf[3] == 'G');
        if (is_jpeg || is_png) {
            ok = jpeg_decode_rgb565(b.buf, b.len, out->px, COVER_W, COVER_H);
            if (!ok) {
                ESP_LOGW(TAG, "解码失败 id=%lld（%s，%u 字节）",
                         (long long)r->id, is_png ? "PNG" : "JPEG",
                         (unsigned)b.len);
            }
        } else {
            ESP_LOGW(TAG, "不是图片 id=%lld（头 %02X %02X %02X %02X，%u 字节）",
                     (long long)r->id, b.buf[0], b.buf[1], b.buf[2], b.buf[3],
                     (unsigned)b.len);
        }
    } else {
        ESP_LOGW(TAG, "封面下载失败 id=%lld（status=%d err=%s len=%u）",
                 (long long)r->id, status, esp_err_to_name(err), (unsigned)b.len);
    }
    free(b.buf);
    return ok;
}

static void cache_store(const req_t *r, const cover_t *img)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* 先看有没有同 id 的旧条目 */
    cache_item_t *slot = find_locked(r->id);
    if (!slot) {
        /* 找空位，没有就淘汰最旧的 */
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < CACHE_N; i++) {
            if (!s_cache[i].used) {
                slot = &s_cache[i];
                break;
            }
            if (s_cache[i].stamp < oldest) {
                oldest = s_cache[i].stamp;
                slot = &s_cache[i];
            }
        }
    }
    if (slot) {
        slot->id = r->id;
        slot->used = true;
        slot->stamp = ++s_stamp;
        memcpy(&slot->img, img, sizeof(cover_t));
        strncpy(slot->url, r->url, sizeof(slot->url) - 1);
        slot->url[sizeof(slot->url) - 1] = '\0';
    }
    xSemaphoreGive(s_lock);
}

/**
 * 后台任务主体：取一个请求 → 下载 → 解码 → 入缓存 → 通知界面重绘。
 *
 * 解码缓冲放 PSRAM：cover_t 是 56*56*2 = 6.2KB，放栈上会挤掉 TLS 握手要用的空间。
 */
static EXT_RAM_BSS_ATTR cover_t s_decode_buf;

static void cover_task_body(void *arg)
{
    (void)arg;
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!fetch_one(&r, &s_decode_buf)) {
            continue;   /* 失败就不入缓存，下一次界面重绘会再排一次 */
        }
        cache_store(&r, &s_decode_buf);
        ESP_LOGI(TAG, "封面就绪 id=%lld（缓存 %d 张）",
                 (long long)r.id, cover_cached_count());
        /* 通知 UI 重绘，封面就会出现 */
        ui_redraw();
    }
}
