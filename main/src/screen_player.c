/*
 * screen_player.c —— 播放界面（横屏 320x240）
 *
 * 左侧黑胶唱片 + 右侧信息/进度/同步歌词，底部 4 个可聚焦按钮。
 * 上下键调音量（长按连发），左右键移动焦点，OK 触发当前按钮。
 */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "icon.h"
#include "music_state.h"
#include "player.h"
#include "settings.h"
#include "theme.h"

static const char *TAG = "player";

#define DISC_SIZE  120
#define DISC_X     8
#define DISC_Y     32
#define INFO_X     134
#define INFO_W     (GFX_W - INFO_X - 8)

typedef struct {
    int  ms;
    char text[64];
} lrc_line_t;

static focus_t    s_focus;
static EXT_RAM_BSS_ATTR uint16_t s_disc[DISC_SIZE * DISC_SIZE];
static int        s_angle;
static int64_t    s_last_spin;
static lrc_line_t s_lines[64];
static int        s_line_count;
/* 已解析进 s_lines 的歌词属于哪首歌；-1 = 还没解析过 */
static int        s_lyric_id = -1;
/* 这首歌的歌词是否已经解析过（区分「同 id 但内容更新」的情况） */
static bool       s_lyric_parsed;
/* 从后台任务取歌词的落地缓冲（PSRAM，避免占内部 RAM） */
static EXT_RAM_BSS_ATTR char s_lrc_buf[LYRIC_MAX];

static void parse_lrc(const char *lrc)
{
    s_line_count = 0;
    if (!lrc || !*lrc) return;
    const char *p = lrc;
    while (*p && s_line_count < 64) {
        if (*p != '[') { p++; continue; }
        int min = 0, sec = 0, cs = 0;
        if (sscanf(p, "[%d:%d.%d", &min, &sec, &cs) < 2) { p++; continue; }
        const char *close = strchr(p, ']');
        if (!close) break;
        const char *text = close + 1;
        const char *next = strchr(text, '[');
        size_t n = next ? (size_t)(next - text) : strlen(text);
        /* 去掉行尾的 \r\n */
        while (n > 0 && (text[n - 1] == '\r' || text[n - 1] == '\n' ||
                         text[n - 1] == ' ')) n--;
        if (n > 0) {
            lrc_line_t *l = &s_lines[s_line_count++];
            l->ms = (min * 60 + sec) * 1000 + cs * 10;
            if (n >= sizeof(l->text)) n = sizeof(l->text) - 1;
            memcpy(l->text, text, n);
            l->text[n] = '\0';
        }
        p = next ? next : (close + 1 + n);
    }
}

static int current_lrc_line(int pos_ms)
{
    int cur = -1;
    for (int i = 0; i < s_line_count; i++) {
        if (s_lines[i].ms <= pos_ms) cur = i;
        else break;
    }
    return cur;
}

static void render_disc(int angle)
{
    const int c = DISC_SIZE / 2;
    const double a = angle * 3.14159265358979323846 / 180.0;
    double ca = cos(a), sa = sin(a);

    for (int y = 0; y < DISC_SIZE; y++) {
        for (int x = 0; x < DISC_SIZE; x++) {
            int dx = x - c, dy = y - c;
            int d2 = dx * dx + dy * dy;
            uint16_t col = g_theme.bg;

            if (d2 <= 58 * 58) {
                /* 黑胶底色，按半径做几道纹路 */
                int ring = (int)sqrt((double)d2) % 7;
                col = (ring < 2) ? C565(26, 27, 30) : C565(17, 18, 20);

                /* 旋转高光：一条随角度转动的扇形反光 */
                double proj = dx * ca + dy * sa;
                double perp = -dx * sa + dy * ca;
                if (proj > 12.0 && proj < 52.0 && fabs(perp) < 9.0) {
                    col = C565(58, 56, 50);
                }
                if (d2 <= 19 * 19) col = g_theme.accent_dim;   /* 中心标签 */
                if (d2 <= 15 * 15) col = g_theme.accent;
                if (d2 <= 4 * 4) col = g_theme.bg;             /* 轴孔 */
            }
            s_disc[y * DISC_SIZE + x] = col;
        }
    }
}

static void player_enter(void *arg)
{
    (void)arg;
    focus_init(&s_focus, 4, 1);
    s_focus.index = 1;                 /* 默认落在「播放/暂停」上 */
    s_angle = 0;
    s_last_spin = 0;

    song_t *s = music_current();
    if (s) {
        /* 歌词是异步取的：这里若已有歌词就立刻用上，否则等后台回调 */
        if (music_get_lyric(s_lrc_buf, sizeof(s_lrc_buf))) {
            parse_lrc(s_lrc_buf);
            s_lyric_id = (int)s->id;
            s_lyric_parsed = true;
        } else {
            s_lyric_parsed = false;
        }
    }
    ui_redraw();
}

/**
 * 歌词是后台异步取的，到得比界面晚。
 * 这里每帧检查一次：歌词就绪且还没解析过，就重新解析一次并重绘。
 */
static void player_check_lyric(void)
{
    song_t *s = music_current();
    if (!s) return;
    if (s_lyric_parsed && (int)s->id == s_lyric_id) return;
    if (!music_lyric_ready()) return;

    static EXT_RAM_BSS_ATTR char tmp_lrc[LYRIC_MAX];
    s_lyric_id = (int)s->id;
    s_lyric_parsed = true;
    if (music_get_lyric(s_lrc_buf, sizeof(s_lrc_buf))) {
        parse_lrc(s_lrc_buf);
        ESP_LOGI(TAG, "歌词已同步到播放页");
    } else {
        parse_lrc(NULL);
    }
}

static void draw_time(char *buf, size_t cap, int ms)
{
    if (ms < 0) ms = 0;
    int total = ms / 1000;
    snprintf(buf, cap, "%02d:%02d", total / 60, total % 60);
}

static void draw_button(int col, const char *label, icon_id_t icon, bool focused)
{
    int bw = 74, gap = 4, x0 = 4;
    int x = x0 + col * (bw + gap);
    int y = 166, h = 32;
    uint16_t bg = focused ? g_theme.focus : g_theme.surface;
    if (focused) {
        gfx_rect(x, y, x + bw, y + h, bg);
        gfx_frame(x, y, x + bw, y + h, g_theme.focus_edge);
    } else {
        gfx_rect(x, y, x + bw, y + h, bg);
    }
    int tw = gfx_text_width_small(label);
    int total = 16 + 4 + tw;
    int ix = x + (bw - total) / 2;
    gfx_icon(ix, y + 8, icon, focused ? g_theme.accent : g_theme.text_dim);
    gfx_text_small(ix + 20, y + 8, label,
                   focused ? g_theme.text : g_theme.text_dim, bg);
}

static void player_draw(void)
{
    gfx_fill(g_theme.bg);

    /* 顶栏 */
    gfx_icon(8, 4, ICON_NOTE, g_theme.accent);
    gfx_text(28, 2, "正在播放", g_theme.accent, g_theme.bg);
    const char *mode = music_mode_text();
    int mw = gfx_text_width_small(mode);
    gfx_text_small(GFX_W - mw - 8, 8, mode, g_theme.text_dim, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 28, g_theme.line);

    /* 唱片 */
    render_disc(s_angle);
    gfx_blit(DISC_X, DISC_Y, DISC_SIZE, DISC_SIZE, s_disc);

    /* 右侧：歌名 / 歌手 / 进度 / 歌词 */
    song_t *s = music_current();
    const char *name = s ? s->name : "暂无歌曲";
    const char *artist = s ? s->artist : "";
    int ix = INFO_X;

    /* 歌名太长就横向滚动 */
    int namew = gfx_text_width_small(name);
    if (namew > INFO_W) {
        int64_t now = esp_timer_get_time() / 1000;
        int  span  = namew - INFO_W + 20;
        int  cycle = span / 20 + 3;
        int  phase = (int)(now % (cycle * 2));
        int  off   = (phase < cycle) ? phase * 20 : (cycle * 2 - phase) * 20;
        if (off > span) off = span;
        gfx_text_small(ix - off, 34, name, g_theme.text, g_theme.bg);
        gfx_text_small(ix - off + namew + 20, 34, name, g_theme.text, g_theme.bg);
    } else {
        gfx_text_small(ix, 34, name, g_theme.text, g_theme.bg);
    }
    gfx_text_small(ix, 52, artist, g_theme.text_dim, g_theme.bg);

    int pos = player_position_ms();
    int dur = player_duration_ms();
    char t1[16], t2[16];
    draw_time(t1, sizeof(t1), pos);
    draw_time(t2, sizeof(t2), dur);
    gfx_text_small(ix, 70, t1, g_theme.text, g_theme.bg);
    int t2w = gfx_text_width_small(t2);
    gfx_text_small(INFO_X + INFO_W - t2w, 70, t2, g_theme.text_dim, g_theme.bg);

    int bar_y = 88, bar_w = INFO_W, bar_h = 5;
    gfx_rect(ix, bar_y, ix + bar_w, bar_y + bar_h, g_theme.surface_hi);
    if (dur > 0) {
        int fill = pos * bar_w / dur;
        if (fill > bar_w) fill = bar_w;
        if (fill > 0) gfx_rect(ix, bar_y, ix + fill, bar_y + bar_h, g_theme.accent);
    }

    /* 歌词：当前行高亮，前后各一行，随时间滚动 */
    int cur = current_lrc_line(pos);
    if (s_line_count == 0) {
        const char *tip = music_lyric_ready() ? "暂无歌词" : "歌词加载中";
        gfx_text_small(ix, 110, tip, g_theme.text_dim, g_theme.bg);
    } else {
        for (int k = -1; k <= 1; k++) {
            int li = cur + k;
            if (li < 0 || li >= s_line_count) continue;
            int y = 104 + (k + 1) * 20;
            gfx_text_small(ix, y, s_lines[li].text,
                           k == 0 ? g_theme.accent : g_theme.text_dim, g_theme.bg);
        }
    }

    /* 加载中 / 播放失败：明确告诉用户现在是什么状态，不留空白让人以为死机 */
    player_state_t pst = player_state();
    if (pst == PLAYER_LOADING) {
        gfx_text_center_small(150, "正在获取播放地址…", g_theme.text_dim, g_theme.bg);
    } else if (pst == PLAYER_ERROR) {
        gfx_text_center_small(150, "播放失败，按 OK 重试", g_theme.accent, g_theme.bg);
    }

    /* 底部按钮：图标 + 文字，焦点始终可见 */
    static const char *labels[4] = { "上一首", "暂停", "下一首", "模式" };
    static const icon_id_t icons[4] = { ICON_PREV, ICON_PAUSE, ICON_NEXT, ICON_DISC };
    bool paused = (player_state() == PLAYER_PAUSED);
    if (paused) labels[1] = "播放";
    for (int i = 0; i < 4; i++) {
        icon_id_t ic = icons[i];
        if (i == 1) ic = paused ? ICON_PLAY : ICON_PAUSE;
        draw_button(i, labels[i], ic, s_focus.index == i);
    }

    gfx_text_center_small(208, "上下调音量  左右选按钮  SET 返回列表",
                          g_theme.text_dim, g_theme.bg);
}

static void player_tick(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    player_check_lyric();
    if (player_state() == PLAYER_PLAYING && (now - s_last_spin) > 80) {
        s_last_spin = now;
        s_angle = (s_angle + 6) % 360;
        ui_redraw();
    } else if (now - s_last_spin > 400) {
        /* 静止时也定期刷新进度条与时间 */
        s_last_spin = now;
        ui_redraw();
    }
}

static bool player_event(input_event_t ev)
{
    /* 上下键调音量，长按连发由输入层提供 */
    if (ev == EV_UP || ev == EV_UP_REPEAT) {
        int v = g_cfg.volume + 5;
        if (v > 100) v = 100;
        g_cfg.volume = (uint8_t)v;
        player_set_volume(v);
        settings_save();
        char msg[32];
        snprintf(msg, sizeof(msg), "音量 %d", v);
        ui_toast(msg);
        return true;
    }
    if (ev == EV_DOWN || ev == EV_DOWN_REPEAT) {
        int v = g_cfg.volume - 5;
        if (v < 0) v = 0;
        g_cfg.volume = (uint8_t)v;
        player_set_volume(v);
        settings_save();
        char msg[32];
        snprintf(msg, sizeof(msg), "音量 %d", v);
        ui_toast(msg);
        return true;
    }

    if (focus_move(&s_focus, ev)) return true;

    if (ev == EV_OK) {
        /* 播放失败时，OK 用来重试当前这首 */
        if (player_state() == PLAYER_ERROR) {
            if (music_play_current()) {
                ui_toast("正在重试…");
            }
            return true;
        }
        switch (s_focus.index) {
        case 0: music_prev(); s_lyric_parsed = false; player_enter(NULL); break;
        case 1: player_toggle(); break;
        case 2: music_next(); s_lyric_parsed = false; player_enter(NULL); break;
        case 3:
            music_set_mode((g_music.mode + 1) % 3);
            ui_toast(music_mode_text());
            break;
        }
        return true;
    }
    if (ev == EV_OK_LONG) {
        ui_goto(SCR_SONGS, NULL);
        return true;
    }
    return false;
}

const screen_ops_t g_screen_player = {
    .enter    = player_enter,
    .draw     = player_draw,
    .on_event = player_event,
    .tick     = player_tick,
};
