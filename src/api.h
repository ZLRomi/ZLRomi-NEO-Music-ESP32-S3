/**
 * @file api.h
 * @brief ZLRomi NEO Music Player - 音乐API模块
 * 
 * 搜索/详情/播放URL/歌词，支持音质降级和双歌词源
 */
#pragma once
#include "types.h"

// ---- API端点配置 ----
#define API_SEARCH_URL      "https://apis.netstart.cn/music/cloudsearch"
#define API_DETAIL_URL      "https://apis.netstart.cn/music/song/detail"
#define API_RESOLVE_PRIMARY "https://musicserver.haitangw.cc/v1/music/resolve-url"
#define API_RESOLVE_BACKUP  "https://qt.haitangw.net/v1/music/resolve-url"
#define API_LYRIC_PRIMARY   "https://api.vkeys.cn/v2/music/netease/lyric"
#define API_LYRIC_FALLBACK  "https://apis.netstart.cn/music/lyric"
#define API_TIMEOUT_MS      12000
#define API_GET_RETRIES     1

// 初始化API模块(检查WiFi)
bool api_init(void);

// 搜索歌曲，返回结果数，-1=失败
int api_search(const char* keyword, SongInfo* results, int max_results);

// 获取歌曲详情
bool api_get_detail(int64_t id, SongInfo* song);

// 获取播放URL(带音质降级: exhigh->high->standard)
bool api_get_url(int64_t id, AudioQuality quality, UrlInfo* info);

// 获取歌词(首选vkeys，降级netstart)
bool api_get_lyrics(int64_t id, LyricsData* lyrics);

// 解析LRC文本到LyricLine数组，返回行数
int parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines);

// 根据时间获取当前歌词行索引(二分查找)
int lyrics_get_current_line(const LyricsData* lyrics, int time_ms);
