/**
 * @file api.cpp
 * @brief ZLRomi NEO Music Player - 网易云音乐API实现
 */

#include "api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

// WiFi客户端(复用)
static WiFiClientSecure g_wifi_client;
static bool g_api_ready = false;

/**
 * @brief URL编码
 */
static void url_encode(const char* src, char* dst, size_t dst_size) {
    const char* hex = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

/**
 * @brief 执行HTTP GET请求，带重试
 */
static bool http_get(const char* url, String& response, int timeout_ms = API_TIMEOUT_MS) {
    for (int retry = 0; retry <= API_MAX_RETRIES; retry++) {
        HTTPClient http;
        g_wifi_client.setInsecure(); // 跳过证书验证
        
        if (!http.begin(g_wifi_client, url)) {
            Serial.printf("[API] HTTP begin failed (retry %d)\n", retry);
            continue;
        }
        
        http.setTimeout(timeout_ms);
        http.addHeader("User-Agent", "ZLRomi-NEO/1.0");
        http.addHeader("Accept", "application/json");
        
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            response = http.getString();
            http.end();
            return true;
        }
        
        Serial.printf("[API] HTTP %d (retry %d)\n", code, retry);
        http.end();
        
        if (retry < API_MAX_RETRIES) {
            delay(500 * (retry + 1)); // 递增延迟
        }
    }
    return false;
}

bool api_init(void) {
    // 检查WiFi连接
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[API] WiFi not connected");
        g_api_ready = false;
        return false;
    }
    
    g_api_ready = true;
    Serial.println("[API] Initialized");
    return true;
}

int api_search(const char* keyword, SongInfo* results, int max_results) {
    if (!g_api_ready || !keyword || !results || max_results <= 0) {
        return -1;
    }
    
    // 构建搜索URL
    char encoded_kw[256];
    url_encode(keyword, encoded_kw, sizeof(encoded_kw));
    
    char url[512];
    snprintf(url, sizeof(url), "%s%s?keywords=%s&limit=%d",
             API_BASE_URL, API_SEARCH_PATH, encoded_kw, max_results);
    
    Serial.printf("[API] Search: %s\n", keyword);
    
    String response;
    if (!http_get(url, response)) {
        Serial.println("[API] Search request failed");
        return -1;
    }
    
    // 解析JSON响应
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[API] JSON parse error: %s\n", error.c_str());
        return -1;
    }
    
    JsonObject root = doc.as<JsonObject>();
    JsonArray songs = root["result"]["songs"];
    if (songs.isNull()) {
        Serial.println("[API] No songs in response");
        return 0;
    }
    
    int count = 0;
    for (JsonVariant song : songs) {
        if (count >= max_results) break;
        
        SongInfo* info = &results[count];
        info->id = song["id"].as<int64_t>();
        
        // 歌曲名
        const char* name = song["name"].as<const char*>();
        strncpy(info->name, name ? name : "", sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        
        // 歌手(取第一个)
        JsonArray artists = song["ar"];
        if (!artists.isNull() && artists.size() > 0) {
            const char* artist = artists[0]["name"].as<const char*>();
            strncpy(info->artist, artist ? artist : "", sizeof(info->artist) - 1);
        } else {
            strcpy(info->artist, "未知歌手");
        }
        info->artist[sizeof(info->artist) - 1] = '\0';
        
        // 专辑
        const char* album = song["al"]["name"].as<const char*>();
        strncpy(info->album, album ? album : "", sizeof(info->album) - 1);
        info->album[sizeof(info->album) - 1] = '\0';
        
        // 封面
        const char* picUrl = song["al"]["picUrl"].as<const char*>();
        if (picUrl) {
            snprintf(info->cover_url, sizeof(info->cover_url), "%s?param=200y200", picUrl);
        } else {
            info->cover_url[0] = '\0';
        }
        
        // 时长
        info->duration_ms = song["dt"].as<int>();
        
        count++;
    }
    
    Serial.printf("[API] Found %d songs\n", count);
    return count;
}

bool api_get_detail(int64_t id, SongInfo* song) {
    if (!g_api_ready || !song) return false;
    
    char url[256];
    snprintf(url, sizeof(url), "%s%s?ids=%lld", API_BASE_URL, API_DETAIL_PATH, id);
    
    String response;
    if (!http_get(url, response)) return false;
    
    JsonDocument doc;
    if (deserializeJson(doc, response)) return false;
    
    JsonArray songs = doc["songs"];
    if (songs.isNull() || songs.size() == 0) return false;
    
    JsonObject s = songs[0];
    song->id = s["id"].as<int64_t>();
    
    const char* name = s["name"].as<const char*>();
    strncpy(song->name, name ? name : "", sizeof(song->name) - 1);
    song->name[sizeof(song->name) - 1] = '\0';
    
    JsonArray artists = s["ar"];
    if (!artists.isNull() && artists.size() > 0) {
        const char* artist = artists[0]["name"].as<const char*>();
        strncpy(song->artist, artist ? artist : "", sizeof(song->artist) - 1);
    }
    song->artist[sizeof(song->artist) - 1] = '\0';
    
    const char* album = s["al"]["name"].as<const char*>();
    strncpy(song->album, album ? album : "", sizeof(song->album) - 1);
    song->album[sizeof(song->album) - 1] = '\0';
    
    const char* picUrl = s["al"]["picUrl"].as<const char*>();
    if (picUrl) {
        snprintf(song->cover_url, sizeof(song->cover_url), "%s?param=300y300", picUrl);
    }
    
    song->duration_ms = s["dt"].as<int>();
    
    return true;
}

bool api_get_url(int64_t id, AudioQuality quality, UrlInfo* info) {
    if (!g_api_ready || !info) return false;
    
    // 音质对应的br参数
    int br = 128000;
    switch (quality) {
        case QUALITY_HIGH: br = 320000; break;
        case QUALITY_LOSSLESS: br = 999000; break;
        default: br = 128000; break;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "%s%s?id=%lld&br=%d", API_BASE_URL, API_URL_PATH, id, br);
    
    Serial.printf("[API] Get URL: id=%lld, br=%d\n", id, br);
    
    String response;
    if (!http_get(url, response)) return false;
    
    JsonDocument doc;
    if (deserializeJson(doc, response)) return false;
    
    JsonArray data = doc["data"];
    if (data.isNull() || data.size() == 0) return false;
    
    JsonObject d = data[0];
    const char* playUrl = d["url"].as<const char*>();
    
    if (!playUrl || strlen(playUrl) == 0) {
        Serial.println("[API] No valid URL");
        info->valid = false;
        return false;
    }
    
    strncpy(info->url, playUrl, sizeof(info->url) - 1);
    info->url[sizeof(info->url) - 1] = '\0';
    info->bitrate = d["br"].as<int>();
    info->size = d["size"].as<int>();
    info->valid = true;
    
    Serial.printf("[API] Got URL: br=%d, size=%d\n", info->bitrate, info->size);
    return true;
}

bool api_get_lyrics(int64_t id, LyricsData* lyrics) {
    if (!g_api_ready || !lyrics) return false;
    
    memset(lyrics, 0, sizeof(LyricsData));
    
    char url[256];
    snprintf(url, sizeof(url), "%s%s?id=%lld", API_BASE_URL, API_LYRIC_PATH, id);
    
    String response;
    if (!http_get(url, response)) return false;
    
    JsonDocument doc;
    if (deserializeJson(doc, response)) return false;
    
    const char* lrc = doc["lrc"]["lyric"].as<const char*>();
    if (!lrc || strlen(lrc) == 0) {
        lyrics->has_lyrics = false;
        return true; // 成功但无歌词
    }
    
    lyrics->count = parse_lrc(lrc, lyrics->lines, 512);
    lyrics->has_lyrics = (lyrics->count > 0);
    
    Serial.printf("[API] Parsed %d lyric lines\n", lyrics->count);
    return true;
}

int parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines) {
    if (!lrc_text || !lines || max_lines <= 0) return 0;
    
    int count = 0;
    const char* p = lrc_text;
    
    while (*p && count < max_lines) {
        // 查找时间标签 [mm:ss.xx] 或 [mm:ss]
        if (*p == '[') {
            p++;
            // 解析分钟
            int min = 0, sec = 0, ms = 0;
            
            // 跳过非数字字符
            while (*p && (*p < '0' || *p > '9')) p++;
            
            // 读取分钟
            while (*p >= '0' && *p <= '9') {
                min = min * 10 + (*p - '0');
                p++;
            }
            
            // 跳过分隔符
            if (*p == ':') p++;
            
            // 读取秒
            while (*p >= '0' && *p <= '9') {
                sec = sec * 10 + (*p - '0');
                p++;
            }
            
            // 读取毫秒(可选)
            if (*p == '.' || *p == ':') {
                p++;
                int digits = 0;
                while (*p >= '0' && *p <= '9' && digits < 3) {
                    ms = ms * 10 + (*p - '0');
                    p++;
                    digits++;
                }
                // 补齐到毫秒
                if (digits == 1) ms *= 100;
                else if (digits == 2) ms *= 10;
            }
            
            // 跳过到']'
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            
            // 计算总毫秒
            int time_ms = (min * 60 + sec) * 1000 + ms;
            
            // 读取歌词文本直到换行
            char text[256];
            int ti = 0;
            while (*p && *p != '\n' && *p != '\r' && ti < 255) {
                text[ti++] = *p++;
            }
            text[ti] = '\0';
            
            // 跳过换行
            while (*p == '\n' || *p == '\r') p++;
            
            // 只保存有内容的行
            if (ti > 0) {
                lines[count].time_ms = time_ms;
                strncpy(lines[count].text, text, sizeof(lines[count].text) - 1);
                lines[count].text[sizeof(lines[count].text) - 1] = '\0';
                count++;
            }
        } else {
            p++;
        }
    }
    
    return count;
}

int lyrics_get_current_line(const LyricsData* lyrics, int time_ms) {
    if (!lyrics || !lyrics->has_lyrics || lyrics->count == 0) return -1;
    
    // 二分查找当前行
    int left = 0, right = lyrics->count - 1;
    int result = -1;
    
    while (left <= right) {
        int mid = (left + right) / 2;
        if (lyrics->lines[mid].time_ms <= time_ms) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return result;
}
