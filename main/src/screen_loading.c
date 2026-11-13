/*
 * screen_loading.c —— 「正在加载」页
 *
 * 为什么需要它：
 *   拉歌单曲目 / 每日新歌都是 HTTPS 请求（60s 榜单一次返回 97KB JSON），
 *   以前是在 UI 主任务里同步发的，界面会冻住好几秒，屏幕上什么都不变 ——
 *   用户的感觉就是「点了没反应 / 卡死」。
 *
 *   现在改成：进来这一页（转圈），后台任务去拉，拉完自动跳列表。
 *   失败也在这里给明确提示，不再无声无息。
 *
 * 全程可取消：按 SET 或 OK 就退回上一页，不等结果。
 */
#include "ui.h"

#include <stdio.h>

#include "gfx.h"
#include "esp_timer.h"
#include "icon.h"
#include "music_state.h"
#include "theme.h"

static const char *TAG_LOAD = "loading";

/* 本次要拉什么：
   s_target >= 0  → 拉该下标的歌单
   s_target == -1 → 拉每日新歌
   s_target == -2 → 搜索（关键词在 s_kw） */
#define TARGET_DAILY   (-1)
#define TARGET_SEARCH  (-2)

static int s_target = TARGET_DAILY;
static char s_kw[64] = "";

/* 转圈动画：4 帧，每 250ms 一帧 */
#define SPIN_FRAMES  4
#define SPIN_MS      250

void ui_show_loading(int playlist_index)
{
    s_target = playlist_index;
    s_kw[0] = '\0';
    ui_goto(SCR_LOADING, NULL);
}

void ui_show_loading_search(const char *keyword)
{
    s_target = TARGET_SEARCH;
    strncpy(s_kw, keyword ? keyword : "", sizeof(s_kw) - 1);
    s_kw[sizeof(s_kw) - 1] = '\0';
    ui_goto(SCR_LOADING, NULL);
}

static void loading_enter(void *arg)
{
    (void)arg;
    if (s_target >= 0) {
        music_load_playlist_async(s_target);
    } else if (s_target == TARGET_SEARCH) {
        music_load_search_async(s_kw);
    } else {
        music_load_daily_async();
    }
}

static void loading_draw(void)
{
    gfx_fill(g_theme.bg);

    /* 顶部标题 */
    gfx_icon(8, 4, ICON_DISC, g_theme.accent);
    gfx_text(28, 2, "正在加载", g_theme.accent, g_theme.bg);
    gfx_hline(8, GFX_W - 8, 28, g_theme.line);

    /* 中央：转圈的音符 + 文案 */
    int64_t now = esp_timer_get_time() / 1000;
    int frame = (int)(now / SPIN_MS) % SPIN_FRAMES;

    int cx = GFX_W / 2 - 16;
    int cy = 72;
    gfx_rect(cx - 12, cy - 12, cx + 44, cy + 44, g_theme.surface);
    gfx_frame(cx - 12, cy - 12, cx + 44, cy + 44, g_theme.line);

    /* 四个位置轮换高亮，做成转圈 */
    static const int dx[SPIN_FRAMES] = { 0, 16, 16, 0 };
    static const int dy[SPIN_FRAMES] = { -8, -8, 8, 8 };
    for (int i = 0; i < SPIN_FRAMES; i++) {
        int fwd = (i == frame) ? 1 : 0;
        gfx_rect(cx + 16 + dx[i] - 3, cy + 16 + dy[i] - 3,
                 cx + 16 + dx[i] + 3, cy + 16 + dy[i] + 3,
                 fwd ? g_theme.accent : g_theme.accent_dim);
    }

    const char *what = (s_target >= 0) ? "正在获取歌单曲目…"
                 : (s_target == TARGET_SEARCH) ? "正在搜索…"
                 : "正在获取每日新歌…";
    gfx_text_center_small(146, what, g_theme.text, g_theme.bg);
    gfx_text_center_small(166, "按 SET 或 OK 取消", g_theme.text_dim, g_theme.bg);

    /* 底部来一点流动感，让人知道没死机 */
    int bar_w = GFX_W - 48;
    int bar_x = 24, bar_y = 200;
    gfx_rect(bar_x, bar_y, bar_x + bar_w, bar_y + 4, g_theme.surface_hi);
    int seg = 24;
    int pos = (int)(now / 60) % (bar_w + seg * 2) - seg;
    if (pos < 0) pos = 0;
    if (pos + seg > bar_w) seg = bar_w - pos;
    if (seg > 0) {
        gfx_rect(bar_x + pos, bar_y, bar_x + pos + seg, bar_y + 4, g_theme.accent);
    }
}

static void loading_tick(void)
{
    load_status_t st = music_load_status();
    if (st == LOAD_OK) {
        ui_goto(SCR_SONGS, NULL);
    } else if (st == LOAD_FAIL) {
        ui_toast("加载失败，请检查网络");
        ui_back();
    }
}

static bool loading_event(input_event_t ev)
{
    /* SET / OK 取消；返回后加载任务仍在后台跑，结果落进 g_music 不影响 */
    if (ev == EV_SET || ev == EV_OK) {
        ui_back();
        return true;
    }
    return false;
}

const screen_ops_t g_screen_loading = {
    .enter    = loading_enter,
    .draw     = loading_draw,
    .on_event = loading_event,
    .tick     = loading_tick,
};