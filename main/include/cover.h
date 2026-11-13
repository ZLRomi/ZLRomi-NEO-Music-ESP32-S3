/*
 * cover.h —— 歌曲封面下载与缓存（异步）
 *
 * 设计要点：
 *   封面下载和解码都在独立任务里做，UI 线程调用 cover_peek() 只是「查 + 登记」，
 *   绝不阻塞。同一张图反复请求不会重复排队。
 *
 *   cover_peek() 命中就返回真图；没命中立即返回 false 并顺带把这张排进下载队列，
 *   调用方照常用唱片占位图绘制，下一次重绘时真图就到了。
 *
 * 之前的实现是同步下载：首页 4 张封面串行下载（每张超时 10 秒）会把界面冻住，
 * 从歌单返回首页又会再冻一次 —— 那就是「点进去太卡」的直接原因。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COVER_W  56
#define COVER_H  56

typedef struct {
    uint16_t px[COVER_W * COVER_H];
} cover_t;

/** 启动后台下载任务（app_main 里调用一次） */
void cover_init(void);

/**
 * 只查缓存，不阻塞。
 * 命中 → true 并填充 out；未命中 → false（同时把这张图排进下载队列）。
 * 占位图由调用方自己画，本函数未命中时不写 out。
 */
bool cover_peek(int64_t song_id, const char *url, cover_t *out);

/** 显式登记下载（cover_peek 内部已自动登记，一般不用单独调） */
void cover_request(int64_t song_id, const char *url);

/** 缓存里已有多少张（调试用） */
int cover_cached_count(void);
