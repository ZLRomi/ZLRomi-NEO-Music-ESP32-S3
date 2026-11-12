#include "netease.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "netease";

#define API_ROOT          "https://apis.netstart.cn/music"
#define URL_PERSONALIZED  "https://netease-cloud-music-wheat.vercel.app/personalized"
#define URL_RESOLVE       "https://musicserver.haitangw.cc/v1/music/resolve-url"
#define URL_RESOLVE_ALT   "https://qt.haitangw.net/v1/music/resolve-url"
#define URL_LYRIC         "https://api.vkeys.cn/v2/music/netease/lyric?id=%lld"
#define URL_LYRIC_FALLBACK "https://apis.netstart.cn/music/lyric?id=%lld"
#define HTTP_BODY_MAX     (48 * 1024)

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   overflow;
} body_t;

static esp_err_t on_event(esp_http_client_event_t *e)
{
    body_t *b = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b) {
        if (b->len + e->data_len >= b->cap) {
            size_t want = b->cap + e->data_len + 1024;
            if (want > HTTP_BODY_MAX) {
                b->overflow = true;
                return ESP_FAIL;
            }
            char *p = realloc(b->buf, want);
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

static char *http_request(const char *url, const char *post_body)
{
    body_t b = { .buf = malloc(4096), .len = 0, .cap = 4096, .overflow = false };
    if (!b.buf) return NULL;
    b.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = post_body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = 12000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_event,
        .user_data = &b,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        free(b.buf);
        return NULL;
    }
    if (post_body) {
        esp_http_client_set_post_field(cli, post_body, (int)strlen(post_body));
        esp_http_client_set_header(cli, "Content-Type", "application/json");
    }
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    if (err != ESP_OK || status < 200 || status >= 300 || b.overflow || b.len == 0) {
        ESP_LOGW(TAG, "请求失败 %s (err=%s, status=%d)", url, esp_err_to_name(err), status);
        free(b.buf);
        return NULL;
    }
    return b.buf;   /* 调用者 free */
}

static char *http_get(const char *url)
{
    return http_request(url, NULL);
}

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

/** 从各种返回格式里找到真正的数组：result / data / songs / playlists / recommend */
static const cJSON *find_array(const cJSON *root, const char *inner)
{
    if (cJSON_IsArray(root)) return root;
    const char *keys[] = { "result", "data", "songs", "playlists", "recommend", "list" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const cJSON *v = field(root, keys[i]);
        if (!v) continue;
        if (cJSON_IsArray(v)) return v;
        if (inner) {
            const cJSON *n = field(v, inner);
            if (cJSON_IsArray(n)) return n;
        }
        const cJSON *n2 = field(v, "songs");
        if (cJSON_IsArray(n2)) return n2;
    }
    return NULL;
}

/** 封面字段各家不同：picUrl / coverImgUrl / pic / imgurl / cover */
static const char *cover_of(const cJSON *obj)
{
    const char *names[] = { "picUrl", "coverImgUrl", "cover", "pic", "imgurl", "img" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *s = str_of(obj, names[i]);
        if (s && *s) return s;
    }
    const cJSON *al = field(obj, "album");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *s = str_of(al, names[i]);
        if (s && *s) return s;
    }
    const cJSON *al2 = field(obj, "al");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *s = str_of(al2, names[i]);
        if (s && *s) return s;
    }
    return NULL;
}

static const char *artist_of(const cJSON *obj)
{
    const cJSON *ar = field(obj, "artists");
    if (!cJSON_IsArray(ar)) ar = field(obj, "ar");
    if (cJSON_IsArray(ar) && cJSON_GetArraySize(ar) > 0) {
        const char *n = str_of(cJSON_GetArrayItem(ar, 0), "name");
        if (n) return n;
    }
    const char *keys[] = { "artist", "author", "singer" };
    for (size_t i = 0; i < 3; i++) {
        const char *s = str_of(obj, keys[i]);
        if (s) return s;
    }
    return "未知歌手";
}

static void parse_song(const cJSON *item, song_t *s)
{
    memset(s, 0, sizeof(*s));
    const cJSON *inner = field(item, "song");
    const cJSON *o = cJSON_IsObject(inner) ? inner : item;

    s->id = (int64_t)num_of(o, "id");
    copy_str(s->name, sizeof(s->name), str_of(o, "name") ? str_of(o, "name") : "未命名");
    copy_str(s->artist, sizeof(s->artist), artist_of(o));
    const char *cv = cover_of(o);
    if (cv) copy_str(s->cover, sizeof(s->cover), cv);
    s->duration_ms = (int)num_of(o, "duration");
    if (s->duration_ms == 0) s->duration_ms = (int)num_of(o, "dt");
}

static bool parse_songs(const char *json, song_list_t *out, const char *inner)
{
    out->count = 0;
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    const cJSON *arr = find_array(root, inner);
    if (!arr) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && out->count < SONG_MAX; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(it)) continue;
        song_t *s = &out->items[out->count];
        parse_song(it, s);
        if (s->id != 0) out->count++;
    }
    cJSON_Delete(root);
    return out->count > 0;
}

bool netease_playlists(playlist_list_t *out)
{
    out->count = 0;
    char *json = http_get(URL_PERSONALIZED);
    if (!json) return false;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return false;
    const cJSON *arr = find_array(root, NULL);
    if (!arr) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && out->count < PLAYLIST_MAX; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(it)) continue;
        playlist_t *p = &out->items[out->count];
        memset(p, 0, sizeof(*p));
        p->id = (int64_t)num_of(it, "id");
        copy_str(p->name, sizeof(p->name), str_of(it, "name") ? str_of(it, "name") : "推荐歌单");
        const char *cv = cover_of(it);
        if (cv) copy_str(p->cover, sizeof(p->cover), cv);
        if (p->id != 0) out->count++;
    }
    cJSON_Delete(root);
    return out->count > 0;
}

bool netease_daily(song_list_t *out)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/personalized/newsong", API_ROOT);
    char *json = http_get(url);
    if (!json) return false;
    bool ok = parse_songs(json, out, "songs");
    free(json);
    return ok;
}

bool netease_search(const char *keyword, int offset, song_list_t *out)
{
    /* 中文关键词必须 URL 编码，否则 HTTPS 会 400 */
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
    snprintf(url, sizeof(url), "%s/cloudsearch?keywords=%s&limit=%d&offset=%d",
             API_ROOT, enc, SONG_MAX, offset);
    char *json = http_get(url);
    if (!json) return false;
    bool ok = parse_songs(json, out, "songs");
    free(json);
    return ok;
}

bool netease_playlist_tracks(int64_t pid, song_list_t *out)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/playlist/track/all?id=%lld&limit=100&offset=0",
             API_ROOT, (long long)pid);
    char *json = http_get(url);
    if (!json) return false;
    bool ok = parse_songs(json, out, "songs");
    free(json);
    return ok;
}

bool netease_song_detail(int64_t id, song_t *out)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/song/detail?ids=%lld", API_ROOT, (long long)id);
    char *json = http_get(url);
    if (!json) return false;
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return false;
    const cJSON *arr = find_array(root, "songs");
    memset(out, 0, sizeof(*out));
    bool ok = false;
    if (arr && cJSON_GetArraySize(arr) > 0) {
        parse_song(cJSON_GetArrayItem(arr, 0), out);
        ok = out->id != 0;
    }
    cJSON_Delete(root);
    return ok;
}

bool netease_resolve_url(int64_t id, int quality, char *url, size_t cap)
{
    const char *qname = quality == 2 ? "lossless" : (quality == 1 ? "exhigh" : "standard");
    char body[160];
    snprintf(body, sizeof(body), "{\"id\":%lld,\"quality\":\"%s\"}", (long long)id, qname);

    const char *hosts[] = { URL_RESOLVE, URL_RESOLVE_ALT };
    for (int i = 0; i < 2; i++) {
        char *json = http_request(hosts[i], body);
        if (!json) continue;
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) continue;

        const cJSON *data = field(root, "data");
        const cJSON *u = NULL;
        if (cJSON_IsObject(data)) u = field(data, "url");
        if (!u) u = field(root, "url");
        if (!u && cJSON_IsString(data)) u = data;
        bool ok = false;
        if (cJSON_IsString(u) && u->valuestring && strncmp(u->valuestring, "http", 4) == 0) {
            copy_str(url, cap, u->valuestring);
            ok = true;
        }
        cJSON_Delete(root);
        if (ok) return true;
    }
    return false;
}

bool netease_lyric(int64_t id, char *out, size_t cap)
{
    char u1[192], u2[192];
    snprintf(u1, sizeof(u1), URL_LYRIC, (long long)id);
    snprintf(u2, sizeof(u2), URL_LYRIC_FALLBACK, (long long)id);
    const char *urls[] = { u1, u2 };

    for (int i = 0; i < 2; i++) {
        char *json = http_get(urls[i]);
        if (!json) continue;
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) continue;

        const char *lrc = NULL;
        const cJSON *l = field(root, "lrc");
        if (cJSON_IsObject(l)) lrc = str_of(l, "lyric");
        if (!lrc) {
            const cJSON *d = field(root, "data");
            if (cJSON_IsObject(d)) lrc = str_of(d, "lyric");
            if (!lrc && cJSON_IsString(d)) lrc = d->valuestring;
        }
        if (!lrc) lrc = str_of(root, "lyric");
        if (lrc && *lrc) {
            copy_str(out, cap, lrc);
            cJSON_Delete(root);
            return true;
        }
        cJSON_Delete(root);
    }
    return false;
}
