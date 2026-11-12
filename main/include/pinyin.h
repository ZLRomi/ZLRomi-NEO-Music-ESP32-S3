/*
 * pinyin.h —— 7 键焦点软键盘 + 拼音输入
 *
 * 键盘是 10 列 x 4 行的焦点网格，和整机用同一套焦点规则：
 *   方向键在键格间移动，OK 按下当前格，SET 返回。
 * 第 4 行前 8 格是候选汉字（也是可聚焦格子，OK 直接上屏），
 * 后 2 格是「搜索」「清除」。没有任何需要触摸的操作。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PY_COLS        10
#define PY_ROWS        4
#define PY_KEYS        (PY_COLS * PY_ROWS)
#define PY_INPUT_MAX   24
#define PY_TEXT_MAX    64
#define PY_CAND_MAX    8

typedef enum {
    PK_NONE = 0,
    PK_LETTER,
    PK_BACK,
    PK_CLEAR_ALL,
    PK_SPACE,
    PK_SEARCH,
    PK_CANDIDATE,
    PK_CLEAR_INPUT,
} pinyin_key_t;

typedef struct {
    char input[PY_INPUT_MAX];             /* 正在输入的拼音 */
    char text[PY_TEXT_MAX];               /* 已上屏的中文（UTF-8） */
    char cands[PY_CAND_MAX][4];           /* 候选汉字，每个 3 字节 + 结束符 */
    int  cand_count;
    bool submit;                          /* 按了「搜索」，界面读取后清零 */
} pinyin_t;

void pinyin_reset(pinyin_t *p);
void pinyin_update_candidates(pinyin_t *p);

/** 第 index 个键格显示什么（候选格内容随输入变化） */
void pinyin_key_label(int index, const pinyin_t *p, char *buf, size_t cap);
pinyin_key_t pinyin_key_type(int index);

/** OK 按下当前焦点键格 */
void pinyin_activate(pinyin_t *p, int index);

const char *pinyin_text(const pinyin_t *p);
