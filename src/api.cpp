/**
 * @file api.cpp
 * @brief Netease Cloud Music API implementation
 */
#include "api.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

// ---- API endpoints ----
#define API_RECOMMEND    "https://netease-cloud-music-wheat.vercel.app/personalized"
#define API_NEWSONG      "https://apis.netstart.cn/music/personalized/newsong"
#define API_SEARCH       "https://apis.netstart.cn/music/cloudsearch"
#define API_DETAIL       "https://apis.netstart.cn/music/song/detail"
#define API_LYRIC_VKEYS  "https://api.vkeys.cn/v2/music/netease/lyric"
#define API_LYRIC_NET    "https://apis.netstart.cn/music/lyric"
#define API_PLAYLIST     "https://apis.netstart.cn/music/playlist/track/all"
#define API_RESOLVE_PRI  "https://musicserver.haitangw.cc/v1/music/resolve-url"
#define API_RESOLVE_BAK  "https://qt.haitangw.net/v1/music/resolve-url"

#define HTTP_TIMEOUT_MS  12000

static WiFiClientSecure g_wifi_client;
static bool g_ready = false;

// ---- Internal helpers ----

static const char* quality_to_level(AudioQuality q) {
    switch (q) {
        case QUALITY_LOSSLESS: return "lossless";
        case QUALITY_HIGH:     return "exhigh";
        default:               return "standard";
    }
}

static AudioQuality next_lower(AudioQuality q) {
    if (q == QUALITY_LOSSLESS) return QUALITY_HIGH;
    if (q == QUALITY_HIGH) return QUALITY_STANDARD;
    return QUALITY_STANDARD;
}

static bool http_get(const char* url, String& response, int timeout_ms = HTTP_TIMEOUT_MS) {
    HTTPClient http;
    g_wifi_client.setInsecure();
    if (!http.begin(g_wifi_client, url)) return false;
    http.setTimeout(timeout_ms);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "NEO-Music/1.0");
    int code = http.GET();
    if (code == 200) {
        response = http.getString();
        http.end();
        return true;
    }
    http.end();
    // Retry once
    delay(280);
    HTTPClient http2;
    g_wifi_client.setInsecure();
    if (!http2.begin(g_wifi_client, url)) return false;
    http2.setTimeout(timeout_ms);
    http2.addHeader("Accept", "application/json");
    int code2 = http2.GET();
    if (code2 == 200) {
        response = http2.getString();
        http2.end();
        return true;
    }
    http2.end();
    return false;
}

static bool http_post_json(const char* url, const char* body, String& response, int timeout_ms = HTTP_TIMEOUT_MS) {
    HTTPClient http;
    g_wifi_client.setInsecure();
    if (!http.begin(g_wifi_client, url)) return false;
    http.setTimeout(timeout_ms);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    int code = http.POST(body);
    if (code == 200 || code == 201) {
        response = http.getString();
        http.end();
        return true;
    }
    http.end();
    return false;
}

// ---- Public API ----

bool api_init(void) {
    g_wifi_client.setInsecure();
    g_ready = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[API] Init: wifi=%s\n", g_ready ? "OK" : "NO");
    return g_ready;
}

int api_get_recommend_playlists(RecommendPlaylist* out, int max_count) {
    if (!g_ready || !out || max_count <= 0) return -1;
    String response;
    if (!http_get(API_RECOMMEND, response, 15000)) {
        Serial.println("[API] Recommend playlists fetch failed");
        return -1;
    }
    JsonDocument doc;
    if (deserializeJson(doc, response)) return -1;
    int code = doc["code"].as<int>();
    if (code != 200) return -1;
    JsonArray result = doc["result"].as<JsonArray>();
    int count = 0;
    for (JsonObject item : result) {
        if (count >= max_count) break;
        out[count].id = item["id"].as<int64_t>();
        if (out[count].id == 0) continue;
        strncpy(out[count].name, item["name"].as<const char*>() ?: "", sizeof(out[count].name) - 1);
        strncpy(out[count].pic_url, item["picUrl"].as<const char*>() ?: "", sizeof(out[count].pic_url) - 1);
        out[count].play_count = item["playCount"].as<int>();
        out[count].track_count = item["trackCount"].as<int>();
        count++;
    }
    Serial.printf("[API] Got %d recommend playlists\n", count);
    return count;
}

int api_get_new_songs(SongInfo* out, int max_count) {
    if (!g_ready || !out || max_count <= 0) return -1;
    String response;
    if (!http_get(API_NEWSONG, response)) return -1;
    JsonDocument doc;
    if (deserializeJson(doc, response)) return -1;
    JsonArray result = doc["result"].as<JsonArray>();
    if (result.isNull()) return -1;
    int count = 0;
    for (JsonObject item : result) {
        if (count >= max_count) break;
        out[count].id = item["id"].as<int64_t>();
        if (out[count].id == 0) continue;
        strncpy(out[count].name, item["name"].as<const char*>() ?: "", sizeof(out[count].name) - 1);
        JsonArray artists = item["artists"].as<JsonArray>();
        if (!artists.isNull() && artists.size() > 0) {
            strncpy(out[count].artist, artists[0]["name"].as<const char*>() ?: "", sizeof(out[count].artist) - 1);
        }
        JsonObject album = item["album"];
        if (!album.isNull()) {
            strncpy(out[count].album, album["name"].as<const char*>() ?: "", sizeof(out[count].album) - 1);
            const char* pic = album["picUrl"].as<const char*>();
            if (pic) snprintf(out[count].cover_url, sizeof(out[count].cover_url), "%s?param=200y200", pic);
        }
        out[count].duration_ms = item["duration"].as<int>();
        count++;
    }
    Serial.printf("[API] Got %d new songs\n", count);
    return count;
}

int api_search(const char* keyword, SongInfo* out, int max_count) {
    if (!g_ready || !keyword || !out || max_count <= 0) return -1;
    char url[512];
    snprintf(url, sizeof(url), "%s?keywords=%s&limit=%d&offset=0", API_SEARCH, keyword, max_count);
    String response;
    if (!http_get(url, response)) return -1;
    JsonDocument doc;
    if (deserializeJson(doc, response)) return -1;
    JsonObject result = doc["result"];
    if (result.isNull()) return -1;
    JsonArray songs = result["songs"].as<JsonArray>();
    if (songs.isNull()) return -1;
    int count = 0;
    for (JsonObject s : songs) {
        if (count >= max_count) break;
        out[count].id = s["id"].as<int64_t>();
        if (out[count].id == 0) continue;
        strncpy(out[count].name, s["name"].as<const char*>() ?: "", sizeof(out[count].name) - 1);
        JsonArray ar = s["ar"].as<JsonArray>();
        if (!ar.isNull() && ar.size() > 0) {
            strncpy(out[count].artist, ar[0]["name"].as<const char*>() ?: "", sizeof(out[count].artist) - 1);
        }
        JsonObject al = s["al"];
        if (!al.isNull()) {
            strncpy(out[count].album, al["name"].as<const char*>() ?: "", sizeof(out[count].album) - 1);
            const char* pic = al["picUrl"].as<const char*>();
            if (pic) snprintf(out[count].cover_url, sizeof(out[count].cover_url), "%s?param=200y200", pic);
        }
        out[count].duration_ms = s["dt"].as<int>();
        count++;
    }
    Serial.printf("[API] Search '%s': %d results\n", keyword, count);
    return count;
}

bool api_get_song_detail(int64_t id, SongInfo* out) {
    if (!g_ready || !out) return false;
    char url[256];
    snprintf(url, sizeof(url), "%s?ids=%lld", API_DETAIL, id);
    String response;
    if (!http_get(url, response)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, response)) return false;
    JsonArray songs = doc["songs"].as<JsonArray>();
    if (songs.isNull() || songs.size() == 0) return false;
    JsonObject s = songs[0];
    out->id = s["id"].as<int64_t>();
    strncpy(out->name, s["name"].as<const char*>() ?: "", sizeof(out->name) - 1);
    JsonArray ar = s["ar"].as<JsonArray>();
    if (!ar.isNull() && ar.size() > 0) {
        strncpy(out->artist, ar[0]["name"].as<const char*>() ?: "", sizeof(out->artist) - 1);
    }
    JsonObject al = s["al"];
    if (!al.isNull()) {
        strncpy(out->album, al["name"].as<const char*>() ?: "", sizeof(out->album) - 1);
        const char* pic = al["picUrl"].as<const char*>();
        if (pic) snprintf(out->cover_url, sizeof(out->cover_url), "%s?param=300y300", pic);
    }
    out->duration_ms = s["dt"].as<int>();
    return true;
}

bool api_get_play_url(int64_t id, AudioQuality quality, PlayUrlInfo* out) {
    if (!g_ready || !out) return false;
    memset(out, 0, sizeof(PlayUrlInfo));
    AudioQuality try_q = quality;
    bool got = false;

    while (!got) {
        const char* level = quality_to_level(try_q);
        char body[256];
        snprintf(body, sizeof(body), "{\"source\":\"wy\",\"rid\":\"%lld\",\"level\":\"%s\"}", id, level);
        String response;
        bool ok = http_post_json(API_RESOLVE_PRI, body, response);
        if (!ok) {
            Serial.println("[API] Primary resolve failed, trying backup...");
            ok = http_post_json(API_RESOLVE_BAK, body, response);
        }
        if (ok) {
            JsonDocument doc;
            if (!deserializeJson(doc, response)) {
                int code = doc["code"].as<int>();
                if (code == 0) {
                    const char* url = doc["data"]["url"].as<const char*>();
                    if (url && strlen(url) > 10 && (strncmp(url, "http", 4) == 0)) {
                        strncpy(out->url, url, sizeof(out->url) - 1);
                        out->bitrate = doc["data"]["quality"].as<int>();
                        out->valid = true;
                        got = true;
                        Serial.printf("[API] Got URL: level=%s br=%d\n", level, out->bitrate);
                    }
                }
            }
        }
        if (!got) {
            if (try_q == QUALITY_STANDARD) {
                Serial.println("[API] All quality levels failed");
                break;
            }
            try_q = next_lower(try_q);
            Serial.println("[API] Falling back to lower quality...");
            delay(200);
        }
    }
    return got;
}

bool api_get_lyrics(int64_t id, LyricsData* out) {
    if (!g_ready || !out) return false;
    memset(out, 0, sizeof(LyricsData));
    const char* lrc_text = nullptr;
    String response;
    char url[256];

    // Try vkeys first
    snprintf(url, sizeof(url), "%s?id=%lld", API_LYRIC_VKEYS, id);
    if (http_get(url, response)) {
        JsonDocument doc;
        if (!deserializeJson(doc, response)) {
            if (doc.containsKey("data") && doc["data"].containsKey("lyric")) {
                lrc_text = doc["data"]["lyric"].as<const char*>();
            } else if (doc.containsKey("data") && doc["data"].containsKey("lrc")) {
                lrc_text = doc["data"]["lrc"].as<const char*>();
            }
        }
    }

    // Fallback to netstart
    if (!lrc_text || strlen(lrc_text) == 0) {
        snprintf(url, sizeof(url), "%s?id=%lld", API_LYRIC_NET, id);
        if (http_get(url, response)) {
            JsonDocument doc;
            if (!deserializeJson(doc, response)) {
                if (doc.containsKey("lrc") && doc["lrc"].containsKey("lyric")) {
                    lrc_text = doc["lrc"]["lyric"].as<const char*>();
                }
            }
        }
    }

    if (lrc_text && strlen(lrc_text) > 0) {
        out->count = api_parse_lrc(lrc_text, out->lines, 128);
        out->has_lyrics = (out->count > 0);
        Serial.printf("[API] Lyrics: %d lines\n", out->count);
        return out->has_lyrics;
    }
    Serial.println("[API] No lyrics found");
    return false;
}

int api_get_playlist_tracks(int64_t playlist_id, SongInfo* out, int max_count, int offset) {
    if (!g_ready || !out || max_count <= 0) return -1;
    char url[512];
    snprintf(url, sizeof(url), "%s?id=%lld&limit=%d&offset=%d", API_PLAYLIST, playlist_id, max_count, offset);
    String response;
    if (!http_get(url, response, 20000)) return -1;
    JsonDocument doc;
    if (deserializeJson(doc, response)) return -1;
    JsonArray songs = doc["songs"].as<JsonArray>();
    if (songs.isNull()) return -1;
    int count = 0;
    for (JsonObject s : songs) {
        if (count >= max_count) break;
        out[count].id = s["id"].as<int64_t>();
        if (out[count].id == 0) continue;
        strncpy(out[count].name, s["name"].as<const char*>() ?: "", sizeof(out[count].name) - 1);
        JsonArray ar = s["ar"].as<JsonArray>();
        if (!ar.isNull() && ar.size() > 0) {
            strncpy(out[count].artist, ar[0]["name"].as<const char*>() ?: "", sizeof(out[count].artist) - 1);
        }
        JsonObject al = s["al"];
        if (!al.isNull()) {
            strncpy(out[count].album, al["name"].as<const char*>() ?: "", sizeof(out[count].album) - 1);
            const char* pic = al["picUrl"].as<const char*>();
            if (pic) snprintf(out[count].cover_url, sizeof(out[count].cover_url), "%s?param=200y200", pic);
        }
        out[count].duration_ms = s["dt"].as<int>();
        count++;
    }
    Serial.printf("[API] Playlist %lld: %d tracks\n", playlist_id, count);
    return count;
}

int api_parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines) {
    if (!lrc_text || !lines || max_lines <= 0) return 0;
    int count = 0;
    const char* p = lrc_text;
    while (*p && count < max_lines) {
        // Find [mm:ss.xx] or [mm:ss]
        if (*p == '[') {
            const char* bracket = p + 1;
            int min = 0, sec = 0, ms = 0;
            // Parse minutes
            min = atoi(bracket);
            const char* colon = strchr(bracket, ':');
            if (colon) {
                sec = atoi(colon + 1);
                const char* dot = strchr(colon, '.');
                if (dot) {
                    ms = atoi(dot + 1);
                    // Handle 2-digit or 3-digit milliseconds
                    if (ms < 100 && strlen(dot + 1) <= 2) ms *= 10;
                }
            }
            const char* close = strchr(p, ']');
            if (close) {
                const char* text_start = close + 1;
                // Skip whitespace
                while (*text_start == ' ' || *text_start == '\t') text_start++;
                // Find end of line
                const char* line_end = strchr(text_start, '\n');
                if (!line_end) line_end = text_start + strlen(text_start);
                // Skip empty lines
                int text_len = line_end - text_start;
                if (text_len > 0) {
                    lines[count].time_ms = min * 60000 + sec * 1000 + ms;
                    int copy_len = (text_len < 255) ? text_len : 255;
                    memcpy(lines[count].text, text_start, copy_len);
                    lines[count].text[copy_len] = '\0';
                    // Trim trailing \r
                    char* cr = strchr(lines[count].text, '\r');
                    if (cr) *cr = '\0';
                    count++;
                }
                p = (*line_end) ? line_end + 1 : line_end;
                continue;
            }
        }
        p++;
    }
    return count;
}

int api_find_lyric_line(const LyricsData* lyrics, int time_ms) {
    if (!lyrics || !lyrics->has_lyrics || lyrics->count == 0) return -1;
    // Binary search for the last line with time_ms <= target
    int lo = 0, hi = lyrics->count - 1;
    int result = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lyrics->lines[mid].time_ms <= time_ms) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}
