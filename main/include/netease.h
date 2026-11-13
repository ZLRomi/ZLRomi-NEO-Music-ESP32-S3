/*
 * netease.h —— 在线音乐数据源
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
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 容量常量。
 *
 * 这些结构体（song_list_t / playlist_list_t / music_state_t）全部放在 PSRAM 里，
 * 所以可以放心往大里留 —— 8MB PSRAM 对这些占用量来说是九牛一毛，
 * 而内部 RAM 只有约 306KB，寸土寸金。
 *
 * 留大一点是为了将来：歌单想一次拉更多首、歌词想存更多行、搜索想翻更多页，
 * 都不用再回头改结构体和内存布局。
 */
/*
 * 容量常量。
 *
 * 这些结构体（song_list_t / playlist_list_t / music_state_t）全部放在 PSRAM 里，
 * 所以可以放心往大里留 —— 8MB PSRAM 对这些占用量来说是九牛一毛，
 * 而内部 RAM 只有约 306KB，寸土寸金。
 *
 * 但**下载量**是另一回事：一次拉 50 首的 JSON 有 100KB+，
 * 在 ESP32 上 TLS 下载 + cJSON 解析要好几秒（实测加载一个歌单 30 秒出不来）。
 * 所以每页只拉 SONG_PAGE 首，底部放上一页/下一页翻页，
 * 响应体从 250KB 降到 21KB，快一个数量级。
 */
#define SONG_NAME_LEN    64
#define SONG_ARTIST_LEN  48
#define SONG_COVER_LEN   192
#define SONG_URL_LEN     512
#define PLAYLIST_MAX     16      /* 首页最多展示 16 个歌单 */
#define SONG_PAGE        10      /* 每页曲目数（也是单次请求拉多少首） */
#define SONG_MAX         SONG_PAGE  /* song_list_t 只需装得下一页 */
#define LYRIC_MAX        16384   /* 歌词原文上限：长歌词（尤其逐字）能到十几 KB */

typedef struct {
    int64_t id;
    char    name[SONG_NAME_LEN];
    char    artist[SONG_ARTIST_LEN];
    char    cover[SONG_COVER_LEN];
    int     duration_ms;
} song_t;

typedef struct {
    int64_t id;
    char    name[SONG_NAME_LEN];
    char    desc[64];
    char    cover[SONG_COVER_LEN];
} playlist_t;

typedef struct {
    int  count;
    song_t items[SONG_MAX];
} song_list_t;

typedef struct {
    int        count;
    playlist_t items[PLAYLIST_MAX];
} playlist_list_t;

/* ---- 列表类 ---- */
/** 推荐歌单（代理 /personalized，失败退 60s 榜单列表） */
bool netease_playlists(playlist_list_t *out);
/** 榜单列表（和 netease_playlists 同一份数据） */
bool netease_ranks(playlist_list_t *out);
/** 每日新歌，从第 offset 首起取一页（代理 /personalized/newsong 优先） */
bool netease_daily(int offset, song_list_t *out);
/** 歌单曲目，从第 offset 首起取一页（SONG_PAGE 首） */
bool netease_playlist_tracks(int64_t playlist_id, int offset, song_list_t *out);
/** 榜单曲目（同 netease_playlist_tracks，保留语义清晰的别名） */
bool netease_rank_songs(int64_t rank_id, int offset, song_list_t *out);
/** 搜索，offset 用于翻页 */
bool netease_search(const char *keyword, int offset, song_list_t *out);

/** 后台任务定期调：回收空闲超过 30 秒的池连接（服务端会主动断开） */
void netease_http_pool_trim(void);

/* ---- 单曲类 ---- */
/** 歌词（代理 /lyric，失败退 vkeys），写入 LRC 原文 */
bool netease_lyric(int64_t id, char *out, size_t cap);
/** 播放地址：海棠网关为主源（0=标准 1=极高 2=无损） */
bool netease_resolve_url(int64_t id, int quality, char *url, size_t cap);
