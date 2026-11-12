#include "music_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "player.h"
#include "settings.h"

static const char *TAG = "music";

EXT_RAM_BSS_ATTR music_state_t g_music;

/* song_list_t 有 8.6KB，放栈上会直接把主任务栈撑爆（栈只有 12KB）。
   临时缓冲统一放这里，三处复用。 */
static EXT_RAM_BSS_ATTR song_list_t s_tmp;

void music_state_init(void)
{
    memset(&g_music, 0, sizeof(g_music));
    g_music.current = -1;
    g_music.mode = g_cfg.play_mode;
    strncpy(g_music.list_title, "每日新歌", sizeof(g_music.list_title) - 1);
}

static void set_title(const char *s)
{
    strncpy(g_music.list_title, s, sizeof(g_music.list_title) - 1);
    g_music.list_title[sizeof(g_music.list_title) - 1] = '\0';
}

bool music_load_home(void)
{
    /* 歌单与每日新歌各自独立，一个失败不影响另一个 */
    bool ok_pl = netease_playlists(&g_music.playlists);
    if (!ok_pl) {
        g_music.playlists.count = 0;
    }
    bool ok_daily = music_load_daily();
    ESP_LOGI(TAG, "首页加载：歌单 %d 个(%s)，每日新歌 %d 首(%s)",
             g_music.playlists.count, ok_pl ? "成功" : "失败",
             g_music.songs.count, ok_daily ? "成功" : "失败");
    return ok_pl || ok_daily;
}

bool music_load_daily(void)
{
    song_list_t *tmp = &s_tmp;
    if (!netease_daily(tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp.count > 0 ? 0 : -1;
    set_title("每日新歌");
    return true;
}

bool music_load_playlist(int index)
{
    if (index < 0 || index >= g_music.playlists.count) return false;
    song_list_t *tmp = &s_tmp;
    if (!netease_playlist_tracks(g_music.playlists.items[index].id, tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp.count > 0 ? 0 : -1;
    set_title(g_music.playlists.items[index].name);
    return true;
}

bool music_load_search(const char *keyword)
{
    if (!keyword || !*keyword) return false;
    song_list_t *tmp = &s_tmp;
    if (!netease_search(keyword, 0, tmp)) return false;
    g_music.songs = *tmp;
    g_music.current = tmp.count > 0 ? 0 : -1;
    snprintf(g_music.list_title, sizeof(g_music.list_title), "搜索：%s", keyword);
    return true;
}

song_t *music_current(void)
{
    if (g_music.current < 0 || g_music.current >= g_music.songs.count) return NULL;
    return &g_music.songs.items[g_music.current];
}

const char *music_current_title(void)
{
    song_t *s = music_current();
    return s ? s->name : "暂无歌曲";
}

bool music_play_current(void)
{
    song_t *s = music_current();
    if (!s) return false;

    g_music.lyric_ok = netease_lyric(s->id, g_music.lyric, sizeof(g_music.lyric));
    if (!g_music.lyric_ok) g_music.lyric[0] = '\0';
    ESP_LOGI(TAG, "播放 %lld %s / %s", (long long)s->id, s->name, s->artist);
    return player_play(s, g_cfg.quality);
}

bool music_next(void)
{
    if (g_music.songs.count == 0) return false;
    if (g_music.mode == MODE_SHUFFLE && g_music.songs.count > 1) {
        int n = g_music.current;
        while (n == g_music.current) {
            n = (int)(esp_random() % (uint32_t)g_music.songs.count);
        }
        g_music.current = n;
    } else {
        g_music.current = (g_music.current + 1) % g_music.songs.count;
    }
    return music_play_current();
}

bool music_prev(void)
{
    if (g_music.songs.count == 0) return false;
    g_music.current = (g_music.current - 1 + g_music.songs.count) % g_music.songs.count;
    return music_play_current();
}

void music_set_mode(int mode)
{
    if (mode < 0 || mode > 2) mode = 0;
    g_music.mode = mode;
    g_cfg.play_mode = (uint8_t)mode;
    settings_save();
}

const char *music_mode_text(void)
{
    switch (g_music.mode) {
    case MODE_LOOP_ONE: return "单曲循环";
    case MODE_SHUFFLE:  return "随机播放";
    default:            return "顺序播放";
    }
}
