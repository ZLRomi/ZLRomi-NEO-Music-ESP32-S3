/*
 * netease.h —— 网易云音乐接口
 *
 * 端点全部来自项目需求基线（用户提供的 API 文档）：
 *   推荐歌单 netease-cloud-music-wheat.vercel.app/personalized（带封面）
 *   每日新歌 apis.netstart.cn/music/personalized/newsong
 *   搜索     apis.netstart.cn/music/cloudsearch
 *   详情     apis.netstart.cn/music/song/detail
 *   播放地址 POST musicserver.haitangw.cc/v1/music/resolve-url（备用 qt.haitangw.net）
 *   歌词     api.vkeys.cn/v2/music/netease/lyric，降级 apis.netstart.cn/music/lyric
 *   歌单曲目 apis.netstart.cn/music/playlist/track/all
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SONG_NAME_LEN    64
#define SONG_ARTIST_LEN  48
#define SONG_COVER_LEN   160
#define SONG_URL_LEN     256
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
    char    cover[SONG_COVER_LEN];
} playlist_t;

typedef struct {
    int      count;
    song_t   items[SONG_MAX];
} song_list_t;

typedef struct {
    int         count;
    playlist_t  items[PLAYLIST_MAX];
} playlist_list_t;

/** 以下函数返回 true 表示成功；网络不可用 / 解析失败都返回 false，绝不伪造数据 */
bool netease_playlists(playlist_list_t *out);
bool netease_daily(song_list_t *out);
bool netease_search(const char *keyword, int offset, song_list_t *out);
bool netease_playlist_tracks(int64_t pid, song_list_t *out);
bool netease_song_detail(int64_t id, song_t *out);
bool netease_resolve_url(int64_t id, int quality, char *url, size_t cap);
bool netease_lyric(int64_t id, char *out, size_t cap);
