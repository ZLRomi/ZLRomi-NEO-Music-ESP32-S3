/**
 * @file api.h
 * @brief Netease Cloud Music API for NEO Music Player
 * 
 * APIs:
 * - Recommend playlists: netease-cloud-music-wheat.vercel.app/personalized
 * - New songs: apis.netstart.cn/music/personalized/newsong
 * - Search: apis.netstart.cn/music/cloudsearch
 * - Song detail: apis.netstart.cn/music/song/detail
 * - Play URL: musicserver.haitangw.cc/v1/music/resolve-url (POST)
 * - Lyrics: api.vkeys.cn/v2/music/netease/lyric
 * - Playlist tracks: apis.netstart.cn/music/playlist/track/all
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- Data structures ----

typedef struct {
    int64_t id;
    char name[128];
    char pic_url[256];
    int play_count;
    int track_count;
} RecommendPlaylist;

typedef struct {
    int64_t id;
    char name[128];
    char artist[128];
    char album[128];
    char cover_url[256];
    int duration_ms;
} SongInfo;

typedef struct {
    char url[512];
    int bitrate;
    bool valid;
} PlayUrlInfo;

typedef struct {
    int time_ms;
    char text[256];
} LyricLine;

typedef struct {
    LyricLine lines[128];
    int count;
    bool has_lyrics;
} LyricsData;

// ---- Quality levels ----
typedef enum {
    QUALITY_STANDARD = 0,  // 128k
    QUALITY_HIGH = 1,      // 320k
    QUALITY_LOSSLESS = 2   // FLAC
} AudioQuality;

// ---- API functions ----

// Initialize (check WiFi)
bool api_init(void);

// Fetch recommend playlists, returns count (-1 on error)
int api_get_recommend_playlists(RecommendPlaylist* out, int max_count);

// Fetch daily new songs, returns count (-1 on error)
int api_get_new_songs(SongInfo* out, int max_count);

// Search songs by keyword, returns count (-1 on error)
int api_search(const char* keyword, SongInfo* out, int max_count);

// Get song detail by ID
bool api_get_song_detail(int64_t id, SongInfo* out);

// Get play URL with quality fallback (exhigh->high->standard)
bool api_get_play_url(int64_t id, AudioQuality quality, PlayUrlInfo* out);

// Get lyrics (vkeys primary, netstart fallback)
bool api_get_lyrics(int64_t id, LyricsData* out);

// Get playlist tracks
int api_get_playlist_tracks(int64_t playlist_id, SongInfo* out, int max_count, int offset);

// Parse LRC text into LyricLine array, returns line count
int api_parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines);

// Find current lyric line index by time (binary search)
int api_find_lyric_line(const LyricsData* lyrics, int time_ms);
