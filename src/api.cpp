/**
 * @file api.cpp
 * @brief ZLRomi NEO Music Player - 音乐API完整实现
 */

#include "api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

static WiFiClientSecure g_wifi_client;
static bool g_api_ready = false;

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

static bool http_get(const char* url, String& response) {
    for (int retry = 0; retry <= API_GET_RETRIES; retry++) {
        HTTPClient http;
        g_wifi_client.setInsecure();
        if (!http.begin(g_wifi_client, url)) {
            Serial.printf("[API] GET begin failed (retry %d)\n", retry);
            continue;
        }
        http.setTimeout(API_TIMEOUT_MS);
        http.addHeader("User-Agent", "ZLRomi-NEO/1.0");
        http.addHeader("Accept", "application/json");
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            response = http.getString();
            http.end();
            return true;
        }
        Serial.printf("[API] GET %d (retry %d): %s\n", code, retry, url);
        http.end();
        if (retry < API_GET_RETRIES) delay(500 * (retry + 1));
    }
    return false;
}

static bool http_post_json(const char* url, const char* json_body, String& response) {
    HTTPClient http;
    g_wifi_client.setInsecure();
    if (!http.begin(g_wifi_client, url)) {
        Serial.printf("[API] POST begin failed: %s\n", url);
        return false;
    }
    http.setTimeout(API_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ZLRomi-NEO/1.0");
    http.addHeader("Accept", "application/json");
    int code = http.POST(json_body);
    if (code == HTTP_CODE_OK || code == 201) {
        response = http.getString();
        http.end();
        return true;
    }
    Serial.printf("[API] POST %d: %s\n", code, url);
    http.end();
    return false;
}

static const char* quality_to_level(AudioQuality q) {
    switch (q) {
        case QUALITY_LOSSLESS: return "exhigh";
        case QUALITY_HIGH:     return "high";
        case QUALITY_STANDARD: return "standard";
        default:               return "standard";
    }
}

static AudioQuality next_lower_quality(AudioQuality q) {
    switch (q) {
        case QUALITY_LOSSLESS: return QUALITY_HIGH;
        case QUALITY_HIGH:     return QUALITY_STANDARD;
        default:               return QUALITY_STANDARD;
    }
}

bool api_init(void) {
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
    if (!g_api_ready || !keyword || !results || max_results <= 0) return -1;
    char encoded_kw[256];
    url_encode(keyword, encoded_kw, sizeof(encoded_kw));
    char url[512];
    snprintf(url, sizeof(url), "%s?keywords=%s&limit=%d&offset=0",
             API_SEARCH_URL, encoded_kw, max_results > 30 ? 30 : max_results);
    Serial.printf("[API] Search: %s\n", keyword);
    String response;
    if (!http_get(url, response)) {
        Serial.println("[API] Search failed");
        return -1;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[API] JSON parse error: %s\n", error.c_str());
        return -1;
    }
    JsonArray songs = doc["result"]["songs"];
    if (songs.isNull()) {
        Serial.println("[API] No songs in response");
        return 0;
    }
    int count = 0;
    for (JsonVariant song : songs) {
        if (count >= max_results) break;
        SongInfo* info = &results[count];
        memset(info, 0, sizeof(SongInfo));
        info->id = song["id"].as<int64_t>();
        const char* name = song["name"].as<const char*>();
        strncpy(info->name, name ? name : "", sizeof(info->name) - 1);
        JsonArray artists = song["ar"];
        if (!artists.isNull() && artists.size() > 0) {
            const char* artist = artists[0]["name"].as<const char*>();
            strncpy(info->artist, artist ? artist : "", sizeof(info->artist) - 1);
        } else {
            strcpy(info->artist, "\xe6\x9c\xaa\xe7\x9f\xa5\xe6\xad\x8c\xe6\x89\x8b");
        }
        const char* album = song["al"]["name"].as<const char*>();
        strncpy(info->album, album ? album : "", sizeof(info->album) - 1);
        const char* picUrl = song["al"]["picUrl"].as<const char*>();
        if (picUrl) {
            snprintf(info->cover_url, sizeof(info->cover_url), "%s?param=200y200", picUrl);
        }
        info->duration_ms = song["dt"].as<int>();
        count++;
    }
    Serial.printf("[API] Found %d songs\n", count);
    return count;
}

bool api_get_detail(int64_t id, SongInfo* song) {
    if (!g_api_ready || !song) return false;
    char url[256];
    snprintf(url, sizeof(url), "%s?ids=%lld", API_DETAIL_URL, id);
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
    JsonArray artists = s["ar"];
    if (!artists.isNull() && artists.size() > 0) {
        const char* artist = artists[0]["name"].as<const char*>();
        strncpy(song->artist, artist ? artist : "", sizeof(song->artist) - 1);
    }
    const char* album = s["al"]["name"].as<const char*>();
    strncpy(song->album, album ? album : "", sizeof(song->album) - 1);
    const char* picUrl = s["al"]["picUrl"].as<const char*>();
    if (picUrl) {
        snprintf(song->cover_url, sizeof(song->cover_url), "%s?param=300y300", picUrl);
    }
    song->duration_ms = s["dt"].as<int>();
    return true;
}

bool api_get_url(int64_t id, AudioQuality quality, UrlInfo* info) {
    if (!g_api_ready || !info) return false;
    memset(info, 0, sizeof(UrlInfo));
    AudioQuality try_quality = quality;
    bool got_url = false;
    while (!got_url) {
        const char* level = quality_to_level(try_quality);
        Serial.printf("[API] Resolve URL: id=%lld, level=%s\n", id, level);
        char body[256];
        snprintf(body, sizeof(body), "{\"source\":\"wy\",\"rid\":\"%lld\",\"level\":\"%s\"}", id, level);
        String response;
        bool ok = http_post_json(API_RESOLVE_PRIMARY, body, response);
        if (!ok) {
            Serial.println("[API] Primary resolve failed, trying backup...");
            ok = http_post_json(API_RESOLVE_BACKUP, body, response);
        }
        if (ok) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, response);
            if (!err) {
                const char* play_url = nullptr;
                int bitrate = 0;
                int fsize = 0;
                if (doc.containsKey("data")) {
                    JsonObject data = doc["data"];
                    play_url = data["url"].as<const char*>();
                    bitrate = data["bitrate"].as<int>();
                    fsize = data["size"].as<int>();
                } else if (doc.containsKey("url")) {
                    play_url = doc["url"].as<const char*>();
                    bitrate = doc["bitrate"].as<int>();
                    fsize = doc["size"].as<int>();
                }
                if (play_url && strlen(play_url) > 0) {
                    strncpy(info->url, play_url, sizeof(info->url) - 1);
                    info->bitrate = bitrate;
                    info->size = fsize;
                    info->valid = true;
                    got_url = true;
                    Serial.printf("[API] Got URL: br=%d, size=%d, level=%s\n", bitrate, fsize, level);
                }
            }
        }
        if (!got_url) {
            if (try_quality == QUALITY_STANDARD) {
                Serial.println("[API] All quality levels failed");
                break;
            }
            try_quality = next_lower_quality(try_quality);
            Serial.println("[API] Falling back to lower quality...");
            delay(200);
        }
    }
    return got_url;
}

bool api_get_lyrics(int64_t id, LyricsData* lyrics) {
    if (!g_api_ready || !lyrics) return false;
    memset(lyrics, 0, sizeof(LyricsData));
    const char* lrc_text = nullptr;
    String response;
    char url[256];
    snprintf(url, sizeof(url), "%s?id=%lld", API_LYRIC_PRIMARY, id);
    Serial.printf("[API] Fetching lyrics (primary): id=%lld\n", id);
    bool ok = http_get(url, response);
    if (ok) {
        JsonDocument doc;
        if (!deserializeJson(doc, response)) {
            if (doc.containsKey("data") && doc["data"].containsKey("lyric")) {
                lrc_text = doc["data"]["lyric"].as<const char*>();
            } else if (doc.containsKey("lrc") && doc["lrc"].containsKey("lyric")) {
                lrc_text = doc["lrc"]["lyric"].as<const char*>();
            } else if (doc.containsKey("lyric")) {
                lrc_text = doc["lyric"].as<const char*>();
            }
        }
    }
    if (!lrc_text || strlen(lrc_text) == 0) {
        Serial.println("[API] Primary lyric failed, trying fallback...");
        snprintf(url, sizeof(url), "%s?id=%lld", API_LYRIC_FALLBACK, id);
        response = "";
        ok = http_get(url, response);
        if (ok) {
            JsonDocument doc;
            if (!deserializeJson(doc, response)) {
                if (doc.containsKey("lrc") && doc["lrc"].containsKey("lyric")) {
                    lrc_text = doc["lrc"]["lyric"].as<const char*>();
                } else if (doc.containsKey("lyric")) {
                    lrc_text = doc["lyric"].as<const char*>();
                }
            }
        }
    }
    if (!lrc_text || strlen(lrc_text) == 0) {
        lyrics->has_lyrics = false;
        Serial.println("[API] No lyrics available");
        return true;
    }
    lyrics->count = parse_lrc(lrc_text, lyrics->lines, 128);
    lyrics->has_lyrics = (lyrics->count > 0);
    Serial.printf("[API] Parsed %d lyric lines\n", lyrics->count);
    return true;
}

static const char* parse_stamp(const char* s, const char* end, int* out_ms) {
    if (s >= end || *s != '[') return NULL;
    const char* p = s + 1;
    int minutes = 0, digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        if (++digits > 4) return NULL;
        minutes = minutes * 10 + (*p - '0');
        ++p;
    }
    if (digits == 0 || p >= end || *p != ':') return NULL;
    ++p;
    int seconds = 0;
    digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        if (++digits > 2) return NULL;
        seconds = seconds * 10 + (*p - '0');
        ++p;
    }
    if (digits == 0 || seconds > 59) return NULL;
    int frac = 0, fdigits = 0;
    if (p < end && (*p == '.' || *p == ':')) {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            if (fdigits < 3) frac = frac * 10 + (*p - '0');
            ++fdigits;
            ++p;
        }
        if (fdigits == 1) frac *= 100;
        else if (fdigits == 2) frac *= 10;
    }
    if (p >= end || *p != ']') return NULL;
    ++p;
    *out_ms = (minutes * 60 + seconds) * 1000 + (frac > 999 ? 999 : frac);
    return p;
}

static void insert_lyric_line(LyricLine* lines, int* count, int max_lines,
                               int time_ms, const char* text) {
    int pos = 0;
    while (pos < *count && lines[pos].time_ms < time_ms) ++pos;
    if (pos < *count && lines[pos].time_ms == time_ms) return;
    if (*count >= max_lines) return;
    for (int i = *count; i > pos; --i) lines[i] = lines[i - 1];
    lines[pos].time_ms = time_ms;
    strncpy(lines[pos].text, text, sizeof(lines[pos].text) - 1);
    lines[pos].text[sizeof(lines[pos].text) - 1] = '\0';
    ++(*count);
}

int parse_lrc(const char* lrc_text, LyricLine* lines, int max_lines) {
    if (!lrc_text || !lines || max_lines <= 0) return 0;
    int count = 0;
    const char* p = lrc_text;
    while (*p && count < max_lines) {
        const char* eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);
        while (line_len > 0 && (p[line_len-1]=='\r'||p[line_len-1]==' '||p[line_len-1]=='\t')) --line_len;
        const char* line_end = p + line_len;
        int stamps[8];
        int nstamp = 0;
        const char* q = p;
        while (q < line_end && *q == '[') {
            int ms = 0;
            const char* next = parse_stamp(q, line_end, &ms);
            if (!next) break;
            if (nstamp < 8) stamps[nstamp++] = ms;
            q = next;
            while (q < line_end && (*q == ' ' || *q == '\t')) ++q;
        }
        while (q < line_end && (*q == ' ' || *q == '\t')) ++q;
        size_t tlen = (size_t)(line_end - q);
        if (nstamp > 0 && tlen > 0) {
            char text_buf[256];
            size_t copy_len = tlen < 255 ? tlen : 255;
            memcpy(text_buf, q, copy_len);
            text_buf[copy_len] = '\0';
            for (int i = 0; i < nstamp && count < max_lines; ++i) {
                insert_lyric_line(lines, &count, max_lines, stamps[i], text_buf);
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    return count;
}

int lyrics_get_current_line(const LyricsData* lyrics, int time_ms) {
    if (!lyrics || !lyrics->has_lyrics || lyrics->count == 0) return -1;
    if (time_ms < lyrics->lines[0].time_ms) return -1;
    int lo = 0, hi = lyrics->count - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lyrics->lines[mid].time_ms <= time_ms) { found = mid; lo = mid + 1; }
        else { hi = mid - 1; }
    }
    return found;
}