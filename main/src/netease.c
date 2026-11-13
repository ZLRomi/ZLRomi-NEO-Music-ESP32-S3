/*
 * netease.c —— 在线音乐数据源
 *
 * 主源是自建代理 netstartapi.zlromi.com（增强版 NeteaseCloudMusicApi，路径同官方）：
 *   推荐歌单  GET  {B}/personalized
 *   每日新歌  GET  {B}/personalized/newsong
 *   榜单      GET  {B}/toplist
 *   歌单曲目  GET  {B}/playlist/track/all?id={歌单ID}&limit={N}&offset={偏移}
 *   搜索      GET  {B}/search?keywords={词}&limit=30&offset=0
 *   歌词      GET  {B}/lyric?id={歌曲ID}
 *
 * 播放地址**必须走海棠**（用户明确要求，主源）：
 *   POST https://musicserver.haitangw.cc/v1/music/resolve-url   （带 X-Qing-* HMAC-SHA256 签名）
 *   备   POST https://qt.haitangw.net/v1/music/resolve-url
 *   海棠两个网关都失败时才退到 {B}/song/url/v1，最后才用网易云官方外链。
 *
 * 兜底源（代理不可用时自动切换）：
 *   榜单/榜单曲目  60s.viki.moe/v2/ncm-rank/{list|{id}}
 *   搜索           music.163.com/api/search/get/web
 *   歌词           api.vkeys.cn/v2/music/netease/lyric
 *
 * ⚠️ 原先用的 apis.netstart.cn 与 netease-cloud-music-wheat.vercel.app 在实测中
 *    20 秒无响应 / TCP 连不上，已弃用。
 */
#include "netease.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "nvs.h"

static const char *TAG = "netease";

#define PROXY      "https://netstartapi.zlromi.com"
#define VIKI       "https://60s.viki.moe/v2/ncm-rank"
/* 60s 默认优先：榜单列表 + 榜单曲目 */
#define RANK_LIST  VIKI "/list"
#define RANK_SONG  VIKI "/%lld"
#define NCM_SEARCH "https://music.163.com/api/search/get/web"
#define VIKI_LYRIC "https://api.vkeys.cn/v2/music/netease/lyric"
#define NCM_OUTER  "https://music.163.com/song/media/outer/url"

#define HAI_GATEWAY     "https://musicserver.haitangw.cc/v1"
#define HAI_GATEWAY_ALT "https://qt.haitangw.net/v1"
#define HAI_PATH        "/v1/music/resolve-url"
#define HAI_APP_ID      "qingmusic-android"
#define HAI_SECRET      "qm-gw-7c1f4e9a2b8d5360e1a7c4f9b2d8036c"
#define HAI_VERSION     "1.4.0"

/* 这些站点前面挂 Cloudflare，没有浏览器 UA 会被判 403 / 1015 */
static const char *UA_BROWSER =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120 Safari/537.36";

#define BODY_MAX (96 * 1024)

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   overflow;
} body_t;

/**
 * 分配 HTTP 响应缓冲。
 *
 * 一律走 PSRAM：普通 malloc 在 IDF 里优先给内部 RAM，而内部 RAM 只有约 306KB，
 * WiFi/LWIP、mbedTLS、任务栈都挤在那里。歌词任务、音频任务、封面任务可能同时
 * 在请求，各占 4KB 内部 RAM 就很容易把内部堆吃紧。
 * 响应体只是普通数据，不参与 DMA，放 PSRAM 完全没问题。
 */
static void *body_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        /* PSRAM 分配失败时退回内部 RAM，至少别让功能直接不可用 */
        p = malloc(n);
    }
    return p;
}

/**
 * 扩容缓冲 —— 必须用 heap_caps_realloc。
 *
 * ⚠️ 这里以前是「heap_caps_malloc 新块 + memcpy + free 旧块」，是平方级拷贝：
 *    250KB 的 JSON 按每片约 1KB 到达，会触发 ~80 次扩容，每次都要把整个旧缓冲
 *    拷一遍（累计约 8MB），而且拷的是 PSRAM（比内部 RAM 慢数倍）。
 *    实测表现就是「加载一个歌单要 30 秒都出不来」。
 *
 * heap_caps_realloc 原地扩容，几乎不拷贝；再配 2 倍增长，
 * 扩容次数从 ~80 次降到 ~5 次，总拷贝量降到约 500KB。
 */
static void *body_grow(void *old, size_t old_cap, size_t want)
{
    void *p = heap_caps_realloc(old, want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        /* PSRAM 里腾不出连续块：退回内部 RAM 再试一次 */
        p = malloc(want);
        if (p && old && old_cap) {
            memcpy(p, old, old_cap);
        }
        free(old);
        return p;
    }
    return p;
}

static esp_err_t on_http_event(esp_http_client_event_t *e)
{
    body_t *b = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b) {
        if (b->len + e->data_len + 1 > b->cap) {
            /* 翻倍增长，而不是 +2048：
               2 倍增长下扩容次数是 log 级，总拷贝量 O(n)；
               +2048 增量下扩容次数是 O(n/片大小)，总拷贝量 O(n²)。 */
            size_t want = b->cap * 2;
            if (want < b->cap + e->data_len + 2048) {
                want = b->cap + e->data_len + 2048;
            }
            if (want > BODY_MAX) {
                b->overflow = true;
                return ESP_FAIL;
            }
            char *p = body_grow(b->buf, b->cap, want);
            if (!p) {
                b->overflow = true;
                return ESP_FAIL;
            }
            b->buf = p;
            b->cap = want;
        }
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
        b->buf[b->len] = '\0';
    }
    return ESP_OK;
}

/**
 * 发起请求并把响应体收进 malloc 的缓冲（调用者 free）。
 * retry：网络层失败时额外重试几次（Cloudflare 偶发 429/1015 时有用）。
 */
static char *http_get(const char *url, const char *const *hdr_names,
                      const char *const *hdr_values, int hdr_count,
                      int timeout_ms, int retry)
{
    body_t b = { .buf = body_alloc(4096), .len = 0, .cap = 4096, .overflow = false };
    if (!b.buf) {
        return NULL;
    }
    b.buf[0] = '\0';

    char *result = NULL;
    for (int attempt = 0; attempt <= retry; attempt++) {
        b.len = 0;
        b.buf[0] = '\0';
        b.overflow = false;

        esp_http_client_config_t cfg = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = timeout_ms,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = on_http_event,
            .user_data = &b,
            .buffer_size = 4096,
        };
        esp_http_client_handle_t cli = esp_http_client_init(&cfg);
        if (!cli) {
            continue;
        }
        esp_http_client_set_header(cli, "User-Agent", UA_BROWSER);
        esp_http_client_set_header(cli, "Accept", "application/json");
        for (int i = 0; i < hdr_count; i++) {
            if (hdr_names[i] && hdr_values[i]) {
                esp_http_client_set_header(cli, hdr_names[i], hdr_values[i]);
            }
        }
        esp_err_t err = esp_http_client_perform(cli);
        int status = esp_http_client_get_status_code(cli);
        esp_http_client_cleanup(cli);

        if (err == ESP_OK && status >= 200 && status < 300 && !b.overflow && b.len > 0) {
            result = b.buf;
            b.buf = NULL;
            break;
        }
        ESP_LOGW(TAG, "请求失败 %s (err=%s status=%d)", url, esp_err_to_name(err), status);
    }

    free(b.buf);
    return result;
}

/** POST JSON（海棠网关用） */
static char *http_post_json(const char *url, const char *body,
                            const char *const *hdr_names, const char *const *hdr_values,
                            int hdr_count, int timeout_ms)
{
    body_t b = { .buf = body_alloc(4096), .len = 0, .cap = 4096, .overflow = false };
    if (!b.buf) {
        return NULL;
    }
    b.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http_event,
        .user_data = &b,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        free(b.buf);
        return NULL;
    }
    esp_http_client_set_header(cli, "User-Agent", UA_BROWSER);
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    for (int i = 0; i < hdr_count; i++) {
        if (hdr_names[i] && hdr_values[i]) {
            esp_http_client_set_header(cli, hdr_names[i], hdr_values[i]);
        }
    }
    esp_http_client_set_post_field(cli, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    if (err != ESP_OK || status < 200 || status >= 300 || b.overflow || b.len == 0) {
        ESP_LOGW(TAG, "POST 失败 %s (err=%s status=%d)", url, esp_err_to_name(err), status);
        free(b.buf);
        return NULL;
    }
    return b.buf;
}

/* ---------------- 小工具 ---------------- */

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static const cJSON *field(const cJSON *obj, const char *name)
{
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, name) : NULL;
}

static const char *str_of(const cJSON *obj, const char *name)
{
    const cJSON *v = field(obj, name);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double num_of(const cJSON *obj, const char *name)
{
    const cJSON *v = field(obj, name);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

/** 歌手：官方/代理用 artists[] 或 ar[]，60s 榜单用 artist[]，还有字符串形态 */
static const char *artist_of(const cJSON *obj)
{
    static const char *arr_keys[] = { "artists", "ar", "artist" };
    for (int k = 0; k < 3; k++) {
        const cJSON *a = field(obj, arr_keys[k]);
        if (cJSON_IsArray(a) && cJSON_GetArraySize(a) > 0) {
            const cJSON *f = cJSON_GetArrayItem(a, 0);
            const char *n = cJSON_IsString(f) ? f->valuestring : str_of(f, "name");
            if (n && *n) {
                return n;
            }
        }
    }
    static const char *str_keys[] = { "artist", "singer", "author" };
    for (int k = 0; k < 3; k++) {
        const char *s = str_of(obj, str_keys[k]);
        if (s && *s) {
            return s;
        }
    }
    return "未知歌手";
}

/** 封面：album.picUrl / album.cover / coverImgUrl / picUrl ... */
static const char *cover_of(const cJSON *obj)
{
    static const char *keys[] = { "picUrl", "coverImgUrl", "cover", "pic", "imgurl", "img" };
    for (int k = 0; k < 6; k++) {
        const char *s = str_of(obj, keys[k]);
        if (s && *s) {
            return s;
        }
    }
    const cJSON *al = field(obj, "album");
    if (!al) {
        al = field(obj, "al");
    }
    if (al) {
        for (int k = 0; k < 6; k++) {
            const char *s = str_of(al, keys[k]);
            if (s && *s) {
                return s;
            }
        }
    }
    const cJSON *ar = field(obj, "artist");
    if (cJSON_IsArray(ar) && cJSON_GetArraySize(ar) > 0) {
        const cJSON *f = cJSON_GetArrayItem(ar, 0);
        for (int k = 0; k < 6; k++) {
            const char *s = str_of(f, keys[k]);
            if (s && *s) {
                return s;
            }
        }
    }
    return NULL;
}

/** 一首歌 → song_t，兼容代理格式（name/ar/dt/al.picUrl）与 60s 格式（title/artist/album.cover） */
static void parse_song(const cJSON *item, song_t *s)
{
    memset(s, 0, sizeof(*s));
    s->id = (int64_t)num_of(item, "id");

    const char *nm = str_of(item, "name");
    if (!nm) {
        nm = str_of(item, "title");
    }
    copy_str(s->name, sizeof(s->name), nm ? nm : "未命名");

    copy_str(s->artist, sizeof(s->artist), artist_of(item));

    const char *cv = cover_of(item);
    if (cv) {
        copy_str(s->cover, sizeof(s->cover), cv);
    }

    s->duration_ms = (int)num_of(item, "duration");
    if (s->duration_ms == 0) {
        s->duration_ms = (int)num_of(item, "dt");
    }
}

/* ---------------- 海棠签名（播放地址备选源用） ---------------- */

static void hex_of(const uint8_t *in, size_t len, char *out)
{
    static const char *H = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 15];
    }
    out[len * 2] = '\0';
}

/**
 * SHA-256 → hex。
 * 统一走 mbedtls_md，不直接用 mbedtls_sha256_*：
 * 后者在 mbedtls 2.x 是 starts(ctx)，3.x 是 starts(ctx, is224)，
 * 类型名也从 mbedtls_sha256_context 变过，跨 IDF 版本容易炸。
 */
static void sha256_hex(const char *msg, char *out65)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) {
        out65[0] = '\0';
        return;
    }
    uint8_t d[32] = { 0 };
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md, 0) == 0 &&
        mbedtls_md_starts(&ctx) == 0 &&
        mbedtls_md_update(&ctx, (const uint8_t *)msg, strlen(msg)) == 0 &&
        mbedtls_md_finish(&ctx, d) == 0) {
        hex_of(d, sizeof(d), out65);
    } else {
        out65[0] = '\0';
    }
    mbedtls_md_free(&ctx);
}

static void hmac_sha256_hex(const char *key, const char *msg, char *out65)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) {
        out65[0] = '\0';
        return;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    uint8_t mac[32];
    int r = mbedtls_md_setup(&ctx, md, 0);
    if (r == 0) {
        r = mbedtls_md_hmac_starts(&ctx, (const uint8_t *)key, strlen(key));
    }
    if (r == 0) {
        r = mbedtls_md_hmac_update(&ctx, (const uint8_t *)msg, strlen(msg));
    }
    if (r == 0) {
        r = mbedtls_md_hmac_finish(&ctx, mac);
    }
    mbedtls_md_free(&ctx);
    if (r != 0) {
        out65[0] = '\0';
        return;
    }
    hex_of(mac, sizeof(mac), out65);
}

/** 设备标识要稳定；单独存 NVS 的 hid 键，不动 settings 结构体（免得把已存的 WiFi 弄丢） */
static const char *hai_device_id(void)
{
    static char id[40];
    if (id[0]) {
        return id;
    }
    nvs_handle_t h;
    if (nvs_open("neo", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(id);
        if (nvs_get_str(h, "hid", id, &len) == ESP_OK && id[0]) {
            nvs_close(h);
            return id;
        }
        nvs_close(h);
    }
    snprintf(id, sizeof(id), "qingmusic-android-%08x%08x",
             (unsigned)esp_random(), (unsigned)esp_random());
    if (nvs_open("neo", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "hid", id);
        nvs_commit(h);
        nvs_close(h);
    }
    return id;
}

/**
 * 海棠网关需要的 6 个签名头。
 *
 * ⚠️ 存储必须由调用方提供（不能用函数内的局部数组），否则函数返回后
 *    这些指针全部悬空，HTTP 层拿到的就是被覆盖的垃圾，签名必然校验失败。
 */
typedef struct {
    char app_id[32];     /* 正好等于 HAI_APP_ID，留点余量 */
    char version[16];
    char device[48];
    char ts[24];
    char nonce[40];
    char sign[80];
} hai_hdr_t;

/**
 * 当前 Unix 毫秒时间戳。
 *
 * 服务端签的是 Date.now()，不能用 esp_timer_get_time()（那是开机后毫秒数，
 * 2026 年的设备会得到几千，服务端必然判 ts 非法）。
 * 系统时间在 SNTP 对时前是 1970 年，所以这里加一道守卫：
 * 时间小于 2020-01-01 时视为未对时，回落到「开机毫秒 + 上次对时基准」。
 */
static int64_t now_ms(void)
{
    time_t sec = 0;
    struct timeval tv = { 0 };
    if (gettimeofday(&tv, NULL) == 0) {
        sec = tv.tv_sec;
    }
    /* 1577836800 = 2020-01-01 00:00:00 UTC */
    if (sec > 1577836800) {
        return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }
    /* 未对时：至少给出单调递增且形态合理的值，避免 0 或负数 */
    return 1577836800000LL + esp_timer_get_time() / 1000;
}

/**
 * 生成海棠签名头：
 *   message = APP_ID \n ts \n nonce \n METHOD \n path \n sha256(body)
 *   sign    = HMAC-SHA256(message, SECRET)
 */
static void build_hai_headers(const char *method, const char *path, const char *body,
                              hai_hdr_t *h)
{
    copy_str(h->app_id, sizeof(h->app_id), HAI_APP_ID);
    copy_str(h->version, sizeof(h->version), HAI_VERSION);
    copy_str(h->device, sizeof(h->device), hai_device_id());

    snprintf(h->ts, sizeof(h->ts), "%lld", (long long)now_ms());

    uint8_t rnd[16];
    for (int i = 0; i < 16; i++) {
        rnd[i] = (uint8_t)(esp_random() & 0xFF);
    }
    hex_of(rnd, sizeof(rnd), h->nonce);

    char bh[65];
    sha256_hex(body ? body : "", bh);

    char msg[512];
    snprintf(msg, sizeof(msg), "%s\n%s\n%s\n%s\n%s\n%s",
             HAI_APP_ID, h->ts, h->nonce, method, path, bh);
    hmac_sha256_hex(HAI_SECRET, msg, h->sign);
}

/* ---------------- 对外接口 ---------------- */

/* ---------------- 歌单解析（三种形态共用） ---------------- */

/**
 * 从数组里解析歌单。兼容：
 *   代理 /personalized → { result: [ {id,name,picUrl, copywriter} ] }
 *   代理 /toplist      → { list:   [ {id,name,coverImgUrl,description} ] }
 *   60s 榜单           → { data:   [ {id,name,cover,description} ] }
 */
static int parse_playlists(const cJSON *arr, playlist_list_t *out)
{
    out->count = 0;
    if (!cJSON_IsArray(arr)) {
        return 0;
    }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && out->count < PLAYLIST_MAX; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(it)) {
            continue;
        }
        playlist_t *p = &out->items[out->count];
        memset(p, 0, sizeof(*p));
        p->id = (int64_t)num_of(it, "id");

        const char *nm = str_of(it, "name");
        copy_str(p->name, sizeof(p->name), nm ? nm : "未命名歌单");

        const char *ds = str_of(it, "description");
        if (!ds) {
            ds = str_of(it, "copywriter");
        }
        if (ds) {
            copy_str(p->desc, sizeof(p->desc), ds);
        }

        const char *cv = cover_of(it);
        if (cv) {
            copy_str(p->cover, sizeof(p->cover), cv);
        }
        if (p->id != 0 && p->name[0]) {
            out->count++;
        }
    }
    return out->count;
}

/** 把一首歌解析进列表（带 id 过滤） */
static int parse_songs(const cJSON *arr, song_list_t *out)
{
    out->count = 0;
    if (!cJSON_IsArray(arr)) {
        return 0;
    }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && out->count < SONG_MAX; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(it)) {
            continue;
        }
        song_t *s = &out->items[out->count];
        parse_song(it, s);
        if (s->id != 0) {
            out->count++;
        }
    }
    return out->count;
}

/* ---------------- 对外接口：列表类 ---------------- */

/* netease_daily 的兜底会用到，先声明 */
bool netease_playlist_tracks(int64_t playlist_id, int offset, song_list_t *out);

/** 通用：从一个 URL 取 JSON 并挑一个数组字段解析歌单。
 *  60s API 返回 HTTP 200 但 body 里有 code 字段，需显式判断 code==200。
 */
static int fetch_playlists(const char *url, const char *key, playlist_list_t *out,
                           int timeout_ms)
{
    char *json = http_get(url, NULL, NULL, 0, timeout_ms, 1);
    if (!json) {
        return 0;
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return 0;
    }
    /* 60s API 返回 code 字段，code != 200 视为失败 */
    const cJSON *code_obj = field(root, "code");
    if (cJSON_IsNumber(code_obj) && code_obj->valueint != 200) {
        const char *msg = str_of(root, "message");
        ESP_LOGW(TAG, "API 返回错误 code=%d: %s", code_obj->valueint, msg ? msg : "未知");
        cJSON_Delete(root);
        return 0;
    }
    int got = parse_playlists(field(root, key), out);
    cJSON_Delete(root);
    return got;
}

/*
 * 数据源优先级（2026-11-13 重排，按实测耗时）：
 *
 *   优先用自建代理。之前一度改成 60s 优先，实测是错的：
 *     - 60s 榜单列表  TTFB 3.02s / 总量 3.85s / 32KB
 *     - 60s 榜单曲目  TTFB 3.08s / 总量 3.32s / 97KB
 *     - 60s 单曲信息  00x.split
 *   代理快得多：
 *     - /personalized          1.58s / 9.8KB     → 歌单列表首选
 *     - /playlist/detail       0.82s / 250KB     → 歌单曲目首选（曲目在 playlist.tracks）
 *     - /personalized/newsong  2.04s / 130KB     → 每日新歌首选
 *     - /toplist               1.94s / 82KB
 *
 * 唯一被网易云风控的是 /playlist/track/all（返回 code:-462 人机验证），
 * 所以歌单曲目改走 /playlist/detail（曲目嵌在 playlist.tracks 里），绕开风控。
 *
 * 60s 保留为兜底：主源挂了还有得看。
 * 播放地址仍必须走海棠（用户明确要求），不受此优先级影响。
 */
bool netease_playlists(playlist_list_t *out)
{
    out->count = 0;
    char url[192];

    /* 主源：自建代理推荐歌单（最快，1.6s） */
    snprintf(url, sizeof(url), "%s/personalized?limit=%d", PROXY, PLAYLIST_MAX);
    if (fetch_playlists(url, "result", out, 10000) > 0) {
        ESP_LOGI(TAG, "推荐歌单 %d 个", out->count);
        return true;
    }

    /* 兜底：60s 榜单列表当歌单用 */
    snprintf(url, sizeof(url), "%s", RANK_LIST);
    if (fetch_playlists(url, "data", out, 10000) > 0) {
        ESP_LOGW(TAG, "代理不可用，改用 60s 榜单 %d 个", out->count);
        return true;
    }

    /* 再兜底：代理榜单 */
    snprintf(url, sizeof(url), "%s/toplist", PROXY);
    if (fetch_playlists(url, "list", out, 10000) > 0) {
        ESP_LOGW(TAG, "改用代理榜单 %d 个", out->count);
        return true;
    }
    ESP_LOGE(TAG, "歌单获取失败（代理与 60s 都不可用）");
    return false;
}

bool netease_ranks(playlist_list_t *out)
{
    /* 和 netease_playlists 同一份数据：60s 榜单列表就是「歌单」 */
    return netease_playlists(out);
}

bool netease_daily(int offset, song_list_t *out)
{
    out->count = 0;

    /* 主源：自建代理每日新歌（快，1.6s；带 offset 翻页） */
    {
        char url[192];
        snprintf(url, sizeof(url), "%s/personalized/newsong?limit=%d&offset=%d",
                 PROXY, SONG_PAGE, offset);
        char *json = http_get(url, NULL, NULL, 0, 10000, 0);
        if (json) {
            cJSON *root = cJSON_Parse(json);
            free(json);
            if (root) {
                const cJSON *code_obj = field(root, "code");
                if (!cJSON_IsNumber(code_obj) || code_obj->valueint != 200) {
                    const char *msg = str_of(root, "message");
                    ESP_LOGW(TAG, "每日新歌 API 返回 code=%d: %s",
                             code_obj ? code_obj->valueint : -1, msg ? msg : "未知");
                } else {
                    /* 字段是 result[]，每项里还有一层 song */
                    const cJSON *arr = field(root, "result");
                    if (cJSON_IsArray(arr)) {
                        int n = cJSON_GetArraySize(arr);
                        for (int i = 0; i < n && out->count < SONG_PAGE; i++) {
                            const cJSON *it = cJSON_GetArrayItem(arr, i);
                            if (!cJSON_IsObject(it)) {
                                continue;
                            }
                            /* 有 song 子对象时用它（完整字段），否则用本体 */
                            const cJSON *real = field(it, "song");
                            if (!cJSON_IsObject(real)) {
                                real = it;
                            }
                            song_t *s = &out->items[out->count];
                            parse_song(real, s);
                            if (s->id != 0) {
                                out->count++;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    if (out->count > 0) {
        ESP_LOGI(TAG, "每日新歌第 %d 首起 %d 首", offset, out->count);
        return true;
    }

    /* 兜底：60s 飙升榜（19723756） */
    if (netease_playlist_tracks(19723756, offset, out)) {
        ESP_LOGW(TAG, "代理不可用，改用 60s 飙升榜 %d 首", out->count);
        return true;
    }

    ESP_LOGE(TAG, "每日新歌获取失败");
    return false;
}

/**
 * 取一页歌单曲目（从第 offset 首开始，最多 SONG_PAGE 首）。
 *
 * 为什么分页：一次拉整张歌单（250KB JSON）在 ESP32 上要 TLS + cJSON 解析，
 *   实测加载一个歌单 30 秒出不来。每页 10 首的响应只有 21KB，快一个数量级。
 *   总页数接口不返回（没有 total 字段），由上层探测：翻到空页即到底。
 */
bool netease_playlist_tracks(int64_t playlist_id, int offset, song_list_t *out)
{
    out->count = 0;
    char url[192];

    /* 主源：自建代理歌单曲目（每页 SONG_PAGE 首）。
       track/all 偶发被网易云风控成 code:-462，但 detail 基本不被拦，
       所以兜底顺序是 detail → 60s。 */
    snprintf(url, sizeof(url),
             "%s/playlist/track/all?id=%lld&limit=%d&offset=%d",
             PROXY, (long long)playlist_id, SONG_PAGE, offset);
    char *json = http_get(url, NULL, NULL, 0, 10000, 0);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (root) {
            const cJSON *code_obj = field(root, "code");
            if (!cJSON_IsNumber(code_obj) || code_obj->valueint != 200) {
                const char *msg = str_of(root, "message");
                ESP_LOGW(TAG, "代理 track/all 返回 code=%d: %s",
                         code_obj ? code_obj->valueint : -1, msg ? msg : "未知");
            } else {
                parse_songs(field(root, "songs"), out);
            }
            cJSON_Delete(root);
        }
    }

    /* 兜底 1：代理 playlist/detail（不风控，但返回全部曲目，只取本页那段） */
    if (out->count == 0) {
        snprintf(url, sizeof(url), "%s/playlist/detail?id=%lld",
                 PROXY, (long long)playlist_id);
        json = http_get(url, NULL, NULL, 0, 12000, 0);
        if (json) {
            cJSON *root = cJSON_Parse(json);
            free(json);
            if (root) {
                const cJSON *pl = field(root, "playlist");
                const cJSON *arr = pl ? field(pl, "tracks") : NULL;
                if (cJSON_IsArray(arr)) {
                    int n = cJSON_GetArraySize(arr);
                    for (int i = offset; i < n && out->count < SONG_PAGE; i++) {
                        const cJSON *it = cJSON_GetArrayItem(arr, i);
                        song_t *s = &out->items[out->count];
                        parse_song(it, s);
                        if (s->id != 0) {
                            out->count++;
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    /* 兜底 2：60s 榜单曲目（榜单 ID 与网易云歌单 ID 通用） */
    if (out->count == 0) {
        snprintf(url, sizeof(url), RANK_SONG, (long long)playlist_id);
        json = http_get(url, NULL, NULL, 0, 10000, 0);
        if (json) {
            cJSON *root = cJSON_Parse(json);
            free(json);
            if (root) {
                const cJSON *code_obj = field(root, "code");
                if (!cJSON_IsNumber(code_obj) || code_obj->valueint != 200) {
                    const char *msg = str_of(root, "message");
                    ESP_LOGW(TAG, "歌单曲目 API 返回 code=%d: %s",
                             code_obj ? code_obj->valueint : -1, msg ? msg : "未知");
                } else {
                    const cJSON *arr = field(root, "data");
                    if (cJSON_IsArray(arr)) {
                        int n = cJSON_GetArraySize(arr);
                        for (int i = offset; i < n && out->count < SONG_PAGE; i++) {
                            const cJSON *it = cJSON_GetArrayItem(arr, i);
                            song_t *s = &out->items[out->count];
                            parse_song(it, s);
                            if (s->id != 0) {
                                out->count++;
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    ESP_LOGI(TAG, "歌单 %lld 第 %d 首起 取到 %d 首",
             (long long)playlist_id, offset, out->count);
    return out->count > 0;
}

bool netease_rank_songs(int64_t rank_id, int offset, song_list_t *out)
{
    return netease_playlist_tracks(rank_id, offset, out);
}

bool netease_search(const char *keyword, int offset, song_list_t *out)
{
    out->count = 0;
    if (!keyword || !*keyword) {
        return false;
    }

    /* 中文关键词必须 URL 编码 */
    char enc[256] = "";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)keyword; *p && n + 3 < sizeof(enc); p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            enc[n++] = (char)*p;
        } else {
            n += (size_t)snprintf(enc + n, sizeof(enc) - n, "%%%02X", *p);
        }
    }
    enc[n] = '\0';

    char url[512];
    /* 主源：代理 /search */
    snprintf(url, sizeof(url), "%s/search?keywords=%s&limit=30&offset=%d",
             PROXY, enc, offset);
    char *json = http_get(url, NULL, NULL, 0, 15000, 1);

    /* 兜底：网易云官方 Web 搜索 */
    if (!json) {
        snprintf(url, sizeof(url), "%s?csrf_token=&s=%s&type=1&offset=%d&total=true&limit=30",
                 NCM_SEARCH, enc, offset);
        json = http_get(url, NULL, NULL, 0, 15000, 1);
    }
    if (!json) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return false;
    }

    const cJSON *songs = NULL;
    const cJSON *result = field(root, "result");
    if (result) {
        songs = field(result, "songs");
    }
    if (!songs) {
        songs = field(root, "songs");
    }
    if (!cJSON_IsArray(songs)) {
        cJSON_Delete(root);
        return false;
    }
    int cnt = cJSON_GetArraySize(songs);
    for (int i = 0; i < cnt && out->count < SONG_MAX; i++) {
        const cJSON *it = cJSON_GetArrayItem(songs, i);
        if (!cJSON_IsObject(it)) continue;
        song_t *s = &out->items[out->count];
        parse_song(it, s);
        if (s->id != 0) out->count++;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "搜索 %s 得到 %d 首", keyword, out->count);
    return out->count > 0;
}

bool netease_lyric(int64_t id, char *out, size_t cap)
{
    char url[192];
    snprintf(url, sizeof(url), "%s/lyric?id=%lld", PROXY, (long long)id);
    char *json = http_get(url, NULL, NULL, 0, 15000, 1);
    if (!json) {
        /* 兜底：vkeys 逐字歌词源 */
        snprintf(url, sizeof(url), "%s?id=%lld", VIKI_LYRIC, (long long)id);
        json = http_get(url, NULL, NULL, 0, 15000, 1);
    }
    if (!json) {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return false;
    }

    const char *lrc = NULL;
    const cJSON *lo = field(root, "lrc");
    if (lo) {
        lrc = str_of(lo, "lyric");       /* 代理形态：lrc.lyric */
    }
    if (!lrc) {
        const cJSON *data = field(root, "data");
        lrc = str_of(data, "lrc");        /* vkeys 形态：data.lrc */
        if (!lrc) {
            lrc = str_of(data, "lyric");
        }
    }
    bool ok = false;
    if (lrc && *lrc) {
        copy_str(out, cap, lrc);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

/**
 * 取播放地址。
 *
 * **播放必须走海棠**（用户明确要求）：先走海棠网关，带完整 X-Qing-* 签名。
 * 海棠两个网关都失败时才退到自建代理 /song/url/v1，最后才用官方外链。
 * 这样高音质与会员曲目的可用性都以海棠为准，代理只做兜底。
 */
bool netease_resolve_url(int64_t id, int quality, char *url, size_t cap)
{
    static const char *levels[3] = { "standard", "exhigh", "lossless" };
    const char *level = levels[(quality < 0 || quality > 2) ? 0 : quality];

    /* ---------- 主源：海棠网关 ---------- */
    char body[160];
    snprintf(body, sizeof(body), "{\"source\":\"wy\",\"rid\":\"%lld\",\"level\":\"%s\"}",
             (long long)id, level);

    static const char *gateways[2] = { HAI_GATEWAY, HAI_GATEWAY_ALT };
    for (int g = 0; g < 2; g++) {
        char gu[160];
        snprintf(gu, sizeof(gu), "%s/music/resolve-url", gateways[g]);

        hai_hdr_t h;
        build_hai_headers("POST", HAI_PATH, body, &h);

        /* 必须与 http_post_json 的 const char *const * 类型一致，否则 -Werror 会报
           指针限定符不兼容（char** → const char* const* 不是合法的隐式转换） */
        char *names[6] = {
            (char *)"X-Qing-App-Id", (char *)"X-Qing-App-Version",
            (char *)"X-Qing-Device-Id", (char *)"X-Qing-Ts",
            (char *)"X-Qing-Nonce", (char *)"X-Qing-Sign",
        };
        char *values[6] = {
            h.app_id, h.version, h.device, h.ts, h.nonce, h.sign,
        };

        char *rj = http_post_json(gu, body, (const char *const *)names,
                                  (const char *const *)values, 6, 15000);
        if (!rj) {
            continue;                      /* 网络层失败 → 换备用网关 */
        }
        cJSON *root = cJSON_Parse(rj);
        free(rj);
        bool ok = false;
        if (root) {
            /* 成功时 code 是数字 0；失败时是字符串错误码（如 APP_UPGRADE_REQUIRED） */
            const cJSON *code = field(root, "code");
            bool code_ok = cJSON_IsNumber(code) && code->valueint == 0;
            const cJSON *data = field(root, "data");
            const char *u = str_of(data, "url");
            if (code_ok && u && strncmp(u, "http", 4) == 0) {
                copy_str(url, cap, u);
                ok = true;
            } else {
                const char *reason = str_of(root, "reason");
                if (!reason) {
                    reason = str_of(root, "message");
                }
                ESP_LOGW(TAG, "海棠拒绝(%s): %s", gateways[g], reason ? reason : "未知");
            }
            cJSON_Delete(root);
        }
        if (ok) {
            ESP_LOGI(TAG, "海棠取址成功 id=%lld level=%s", (long long)id, level);
            return true;
        }
    }

    /* ---------- 兜底一：自建代理 /song/url/v1 ---------- */
    char api[192];
    snprintf(api, sizeof(api), "%s/song/url/v1?id=%lld&level=%s",
             PROXY, (long long)id, level);
    char *json = http_get(api, NULL, NULL, 0, 15000, 1);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (root) {
            const cJSON *data = field(root, "data");
            const cJSON *first = cJSON_IsArray(data) ? cJSON_GetArrayItem(data, 0) : NULL;
            const char *u = str_of(first, "url");
            if (u && strncmp(u, "http", 4) == 0) {
                copy_str(url, cap, u);
                cJSON_Delete(root);
                ESP_LOGW(TAG, "海棠不可用，已退到自建代理取址");
                return true;
            }
            cJSON_Delete(root);
        }
    }

    /* ---------- 兜底二：网易云官方外链（302 直跳 CDN） ---------- */
    snprintf(url, cap, "%s?id=%lld.mp3", NCM_OUTER, (long long)id);
    ESP_LOGW(TAG, "海棠与代理都失败，走官方外链兜底");
    return true;
}
