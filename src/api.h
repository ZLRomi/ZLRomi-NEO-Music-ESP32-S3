#pragma once
#include "types.h"

// API配置
#define API_BASE_URL        "https://apis.netstart.cn/music"
#define API_SEARCH_PATH     "/cloudsearch"
#define API_DETAIL_PATH     "/song/detail"
#define API_URL_PATH        "/song/url"
#define API_LYRIC_PATH      "/lyric"
#define API_TIMEOUT_MS      10000
#define API_MAX_RETRIES     2

// 初始化API模块
bool api_init(void);

// 搜索歌曲，返回结果数，-1=失败
int api_search(const char* keyword, SongInfo* results, int max_results);

// 获取歌曲详情
bool api_get_detail(int64_t id, SongInfo* song);

// 获取播放URL
bool api_get_url(int64_t id, AudioQuality quality, UrlInfo* info);

// 获取歌词
bool api_get_lyrics(int64_t id, LyricsData* lyrics);

// 解析LRC文本
int parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines);

// 根据时间获取当前歌词行索引
int lyrics_get_current_line(const LyricsData* lyrics, int time_ms);
