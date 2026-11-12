#include "player.h"

#include <string.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "board.h"
#include "driver/i2s_std.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "netease.h"
#include "settings.h"

static const char *TAG = "player";

#define IN_BUF_SIZE   (16 * 1024)
#define READ_CHUNK    (4 * 1024)
#define AUDIO_STACK   (32 * 1024)

typedef enum { CMD_PLAY, CMD_STOP, CMD_TOGGLE, CMD_VOL } cmd_type_t;

typedef struct {
    cmd_type_t type;
    song_t     song;
    int        quality;
    int        vol;
} cmd_t;

static i2s_chan_handle_t s_tx;
static QueueHandle_t     s_q;
static void            (*s_end_cb)(void);

static volatile player_state_t s_state = PLAYER_IDLE;
static volatile int            s_pos_ms;
static volatile int            s_dur_ms;
static volatile int            s_vol = 70;
static volatile bool           s_paused;
static volatile bool           s_stop_req;
static song_t                  s_song;

/* minimp3 解码器自身十几 KB，加上函数内约 15KB scratch，必须 static */
static mp3dec_t s_dec;
static int16_t  s_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t  s_stereo[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];

static inline int16_t gain_sample(int16_t v, int gain)
{
    int32_t x = ((int32_t)v * gain) >> 8;
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    return (int16_t)x;
}

static void write_pcm(const mp3dec_frame_info_t *info, int samples)
{
    int gain = (s_vol * 256) / 100;
    int frames = samples / info->channels;

    if (info->channels == 1) {
        for (int i = 0; i < frames; i++) {
            int16_t v = gain_sample(s_pcm[i], gain);
            s_stereo[i * 2] = v;
            s_stereo[i * 2 + 1] = v;
        }
    } else {
        for (int i = 0; i < frames; i++) {
            s_stereo[i * 2]     = gain_sample(s_pcm[i * 2], gain);
            s_stereo[i * 2 + 1] = gain_sample(s_pcm[i * 2 + 1], gain);
        }
    }

    size_t bytes = (size_t)frames * 2 * sizeof(int16_t);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(s_tx, s_stereo, bytes, &written, pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S 写入失败: %s", esp_err_to_name(err));
    }
}

static bool poll_cmd(cmd_t *c)
{
    return xQueueReceive(s_q, c, 0) == pdTRUE;
}

static void decode_stream(const song_t *song, int quality)
{
    char url[SONG_URL_LEN];
    s_state = PLAYER_LOADING;
    if (!netease_resolve_url(song->id, quality, url, sizeof(url))) {
        ESP_LOGE(TAG, "取播放地址失败（id=%lld）", (long long)song->id);
        s_state = PLAYER_ERROR;
        return;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = READ_CHUNK,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli || esp_http_client_open(cli, 0) != ESP_OK) {
        ESP_LOGE(TAG, "打开音频流失败");
        if (cli) esp_http_client_cleanup(cli);
        s_state = PLAYER_ERROR;
        return;
    }
    esp_http_client_fetch_headers(cli);

    uint8_t *buf = heap_caps_malloc(IN_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = malloc(IN_BUF_SIZE);
    }
    if (!buf) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        s_state = PLAYER_ERROR;
        return;
    }

    mp3dec_init(&s_dec);
    int have = 0, cur_hz = 0;
    s_pos_ms = 0;
    s_dur_ms = song->duration_ms;
    s_state = PLAYER_PLAYING;

    while (!s_stop_req) {
        /* 播放过程中仍要能收切歌 / 暂停 / 音量命令 */
        cmd_t c;
        while (poll_cmd(&c)) {
            if (c.type == CMD_VOL) {
                s_vol = c.vol;
            } else if (c.type == CMD_TOGGLE) {
                s_paused = !s_paused;
                s_state = s_paused ? PLAYER_PAUSED : PLAYER_PLAYING;
            } else if (c.type == CMD_STOP) {
                s_stop_req = true;
            } else if (c.type == CMD_PLAY) {
                /* 新歌请求回灌队首，结束当前流后立刻接上 */
                xQueueSendToFront(s_q, &c, 0);
                s_stop_req = true;
            }
        }
        if (s_stop_req) break;

        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        int space = IN_BUF_SIZE - have;
        if (space < READ_CHUNK) {
            /* 长时间找不到帧同步时滑掉前半缓冲，避免死等 */
            int drop = IN_BUF_SIZE / 2;
            memmove(buf, buf + drop, have - drop);
            have -= drop;
            space = IN_BUF_SIZE - have;
        }
        int r = esp_http_client_read(cli, (char *)buf + have, space);
        if (r < 0) break;
        if (r == 0 && have == 0) break;
        if (r > 0) have += r;

        int off = 0;
        while (have - off >= 4) {
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(&s_dec, buf + off, have - off, s_pcm, &info);
            if (info.frame_bytes <= 0) break;      /* 数据不足，等下一包 */
            off += info.frame_bytes;

            if (samples > 0 && info.hz > 0) {
                if (info.hz != cur_hz) {
                    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(info.hz);
                    i2s_channel_reconfig_std_clock(s_tx, &clk);
                    cur_hz = info.hz;
                }
                write_pcm(&info, samples);
                s_pos_ms += samples * 1000 / (info.hz * info.channels);
            }
        }
        if (off > 0) {
            memmove(buf, buf + off, have - off);
            have -= off;
        }
        if (r == 0 && have < 4) break;             /* 流结束且无剩余帧 */
    }

    free(buf);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    s_state = PLAYER_IDLE;

    /* 只有流自然播完才通知界面接下一首；主动停止或切歌都不触发 */
    if (!s_stop_req && s_end_cb) {
        s_end_cb();
    }
    s_stop_req = false;
    s_paused = false;
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t c;
        if (xQueueReceive(s_q, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.type) {
        case CMD_VOL:
            s_vol = c.vol;
            break;
        case CMD_TOGGLE:
            s_paused = !s_paused;
            s_state = s_paused ? PLAYER_PAUSED : PLAYER_PLAYING;
            break;
        case CMD_STOP:
            s_stop_req = true;
            break;
        case CMD_PLAY:
            memcpy(&s_song, &c.song, sizeof(s_song));
            s_stop_req = false;
            s_paused = false;
            decode_stream(&s_song, c.quality);
            break;
        }
    }
}

void player_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan.dma_desc_num = 4;
    chan.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S 通道失败: %s（音频不可用，界面继续）", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   /* PCM5102A 的 SCK 接地，用内部 PLL */
            .bclk = I2S_BCK,
            .ws   = I2S_LRCK,
            .dout = I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &std);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置 I2S 失败: %s（音频不可用，界面继续）", esp_err_to_name(err));
        return;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启用 I2S 失败: %s（音频不可用，界面继续）", esp_err_to_name(err));
        return;
    }

    s_q = xQueueCreate(6, sizeof(cmd_t));
    if (!s_q) {
        ESP_LOGE(TAG, "命令队列创建失败");
        return;
    }
    if (xTaskCreateWithCaps(audio_task, "audio", AUDIO_STACK, NULL, 6, NULL,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "音频任务创建失败");
        return;
    }
    s_vol = g_cfg.volume;
    ESP_LOGI(TAG, "I2S 就绪：BCK=GPIO%d LRCK=GPIO%d DIN=GPIO%d", I2S_BCK, I2S_LRCK, I2S_DIN);
}

void player_set_end_callback(void (*cb)(void))
{
    s_end_cb = cb;
}

bool player_play(const song_t *song, int quality)
{
    if (!s_q || !song) return false;
    cmd_t c = { 0 };
    c.type = CMD_PLAY;
    c.song = *song;
    c.quality = quality;
    return xQueueSend(s_q, &c, pdMS_TO_TICKS(100)) == pdTRUE;
}

void player_toggle(void)
{
    if (!s_q) return;
    cmd_t c = { .type = CMD_TOGGLE };
    xQueueSend(s_q, &c, 0);
}

void player_stop(void)
{
    if (!s_q) return;
    cmd_t c = { .type = CMD_STOP };
    xQueueSend(s_q, &c, 0);
}

void player_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_vol = vol;
    if (s_q) {
        cmd_t c = { .type = CMD_VOL, .vol = vol };
        xQueueSend(s_q, &c, 0);
    }
}

player_state_t player_state(void)   { return s_state; }
int  player_position_ms(void)       { return s_pos_ms; }
int  player_duration_ms(void)       { return s_dur_ms; }
int  player_volume(void)            { return s_vol; }
const song_t *player_song(void)     { return &s_song; }
