/*
 * ui.h —— 界面框架与焦点模型
 *
 * 硬件没有触摸屏，全程只能用 7 个按键操作，交互照电视遥控来做：
 *   方向键       移动焦点（可见高亮，长按连发）
 *   OK           确认 / 触发当前焦点元素
 *   SET          返回上一级；首页长按 SET 进设置
 *   RST 长按     重启
 * 软键盘是一张二维焦点网格：方向键在键格间移动，OK 按下当前键，
 * 候选词栏同样用方向键选中后 OK 上屏。任何控件都不依赖触摸。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

typedef enum {
    SCR_HOME = 0,
    SCR_SONGS,
    SCR_PLAYER,
    SCR_SEARCH,
    SCR_SETTINGS,
    SCR_WIFI,           /* 扫描周围网络 / 输密码 */
    SCR_RESTART,        /* RST 长按 3 秒：二次确认，再长按 3 秒才重启 */
    SCR_COUNT
} screen_id_t;

/* ── 焦点网格 ─────────────────────────────────────────────── */
typedef struct {
    int cols;
    int rows;
    int index;      /* 一维下标 = row * cols + col */
} focus_t;

void focus_init(focus_t *f, int cols, int rows);
bool focus_move(focus_t *f, input_event_t ev);
void focus_clamp(focus_t *f, int item_count);
int  focus_col(const focus_t *f);
int  focus_row(const focus_t *f);

/* ── 界面 ─────────────────────────────────────────────────── */
typedef struct {
    void (*enter)(void *arg);              /* 进入时必须把焦点重置到 0 */
    void (*draw)(void);
    bool (*on_event)(input_event_t ev);    /* true = 已消费，不再向上冒泡 */
    void (*tick)(void);
} screen_ops_t;

extern const screen_ops_t g_screen_home;
extern const screen_ops_t g_screen_songs;
extern const screen_ops_t g_screen_player;
extern const screen_ops_t g_screen_search;
extern const screen_ops_t g_screen_settings;
extern const screen_ops_t g_screen_wifi;
extern const screen_ops_t g_screen_restart;

void ui_init(void);
void ui_tick(void);
void ui_goto(screen_id_t id, void *arg);
void ui_back(void);
void ui_redraw(void);
void ui_toast(const char *msg);

/* 供各界面查询当前路由 */
screen_id_t ui_current(void);
