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

#define SONG_NAME_LEN    64
#define SONG_ARTIST_LEN  48
#define SONG_COVER_LEN   160
#define SONG_URL_LEN     512
#define PLAYLIST_MAX     12
#define SONG_MAX         30
#define LYRIC_MAX        4096

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
/** 榜单列表（代理 /toplist，失败退 60s 榜单列表） */
bool netease_ranks(playlist_list_t *out);
/** 每日新歌（代理 /personalized/newsong，失败退某榜单曲目） */
bool netease_daily(song_list_t *out);
/** 歌单曲目（代理 /playlist/track/all，失败退 60s 榜单曲目） */
bool netease_playlist_tracks(int64_t playlist_id, song_list_t *out);
/** 榜单曲目（同 netease_playlist_tracks，保留语义清晰的别名） */
bool netease_rank_songs(int64_t rank_id, song_list_t *out);
/** 搜索，offset 用于翻页 */
bool netease_search(const char *keyword, int offset, song_list_t *out);

/* ---- 单曲类 ---- */
/** 歌词（代理 /lyric，失败退 vkeys），写入 LRC 原文 */
bool netease_lyric(int64_t id, char *out, size_t cap);
/** 播放地址：海棠网关为主源（0=标准 1=极高 2=无损） */
bool netease_resolve_url(int64_t id, int quality, char *url, size_t cap);
