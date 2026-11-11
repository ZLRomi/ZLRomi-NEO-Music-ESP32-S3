#include "audio.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"

// ESP32-A2DP library - currently disabled due to platform compatibility
// To enable: install compatible ESP32-A2DP and set AUDIO_HAS_A2DP=1
#define AUDIO_HAS_A2DP 0

static const char* TAG = "AUDIO";

// ======================== Internal State ========================

static PlayState s_state = PLAY_STOPPED;
static AudioQuality s_quality = QUALITY_HIGH;
static int s_volume = 70;              // 0-100
static float s_volume_scale = 0.70f;   // precomputed multiplier

static int64_t s_bytes_written = 0;    // total PCM bytes sent to output
static int s_content_length = 0;       // from HTTP header, -1 if unknown
static int s_sample_rate = 44100;
static int s_bits_per_sample = 16;
static int s_channels = 2;

// Callbacks
static audio_progress_cb s_progress_cb = NULL;
static audio_state_cb s_state_cb = NULL;
static audio_error_cb s_error_cb = NULL;

// Streaming task
static TaskHandle_t s_stream_task_handle = NULL;
static volatile bool s_stream_running = false;
static char s_stream_url[512] = {0};

// Mutex for state access
static SemaphoreHandle_t s_mutex = NULL;

// A2DP source handle
#if AUDIO_HAS_A2DP
static BluetoothA2DPSource* s_a2dp_source = NULL;
static bool s_a2dp_connected = false;
#endif

// ======================== Helpers ========================

static void set_state(PlayState st) {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = st;
    if (s_mutex) xSemaphoreGive(s_mutex);
    if (s_state_cb) s_state_cb(st);
}

static void report_error(const char* msg) {
    ESP_LOGE(TAG, "%s", msg);
    if (s_error_cb) s_error_cb(msg);
}

static int bytes_per_second() {
    return s_sample_rate * s_channels * (s_bits_per_sample / 8);
}

static int position_ms_from_bytes(int64_t bytes) {
    int bps = bytes_per_second();
    if (bps <= 0) return 0;
    return (int)((bytes * 1000LL) / bps);
}

static void notify_progress() {
    if (!s_progress_cb) return;
    int pos = position_ms_from_bytes(s_bytes_written);
    int dur = 0;
    if (s_content_length > 0) {
        dur = position_ms_from_bytes((int64_t)s_content_length);
    }
    s_progress_cb(pos, dur);
}

// Apply software volume scaling to PCM16 LE stereo buffer in-place
static void apply_volume(uint8_t* data, size_t len) {
    if (s_volume >= 100) return;
    if (s_volume <= 0) {
        memset(data, 0, len);
        return;
    }
    int16_t* samples = (int16_t*)data;
    size_t count = len / sizeof(int16_t);
    float scale = s_volume_scale;
    for (size_t i = 0; i < count; i++) {
        int32_t val = (int32_t)samples[i];
        val = (int32_t)(val * scale);
        // Clamp
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        samples[i] = (int16_t)val;
    }
}

// ======================== I2S ========================

static bool i2s_initialized = false;

static void i2s_init_hw() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = s_sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = 9,     // BCK -> GPIO9
        .ws_io_num = 11,     // LRCK -> GPIO11
        .data_out_num = 10,  // DIN -> GPIO10
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %d", err);
        report_error("I2S driver install failed");
        return;
    }

    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %d", err);
        report_error("I2S set pin failed");
        return;
    }

    // Clear DMA buffers to avoid noise on startup
    i2s_zero_dma_buffer(I2S_NUM_0);

    i2s_initialized = true;
    ESP_LOGI(TAG, "I2S initialized: %dHz, 16bit, stereo, BCK=%d WS=%d DOUT=%d",
             s_sample_rate, pin_config.bck_io_num,
             pin_config.ws_io_num, pin_config.data_out_num);
}

static void i2s_write_pcm(const uint8_t* data, size_t len) {
    if (!i2s_initialized) return;
    size_t bytes_written = 0;
    esp_err_t err = i2s_write(I2S_NUM_0, data, len, &bytes_written, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_write error: %d", err);
    }
}

// ======================== Bluetooth A2DP ========================

#if AUDIO_HAS_A2DP

// A2DP data callback — provides PCM data to BT stack
// This is called by the A2DP library when it needs more audio data.
// We feed it from the same stream that audio_write receives.
static RingbufHandle_t s_bt_ringbuf = NULL;

static int32_t a2dp_data_callback(uint8_t* data, int32_t len) {
    if (!s_bt_ringbuf || !s_a2dp_connected) {
        memset(data, 0, len);
        return len;
    }
    size_t items_received = 0;
    // Try to read from ring buffer (non-blocking)
    uint8_t* item = (uint8_t*)xRingbufferReceiveUpTo(
        s_bt_ringbuf, &items_received, 0, len);
    if (item) {
        memcpy(data, item, items_received);
        vRingbufferReturnItem(s_bt_ringbuf, item);
        if ((int32_t)items_received < len) {
            memset(data + items_received, 0, len - items_received);
        }
        return len;
    }
    memset(data, 0, len);
    return len;
}

static void a2dp_connection_callback(bool connected) {
    s_a2dp_connected = connected;
    ESP_LOGI(TAG, "A2DP %s", connected ? "connected" : "disconnected");
}

static void bt_init() {
    // Create ring buffer for BT audio (holds ~0.5s of audio)
    s_bt_ringbuf = xRingbufferCreate(bytes_per_second() / 2, RINGBUF_TYPE_BYTEBUF);
    if (!s_bt_ringbuf) {
        ESP_LOGE(TAG, "Failed to create BT ring buffer");
        return;
    }

    s_a2dp_source = new BluetoothA2DPSource();
    if (!s_a2dp_source) {
        ESP_LOGE(TAG, "Failed to allocate A2DP source");
        return;
    }

    s_a2dp_source->set_on_connection_state_changed(a2dp_connection_callback);
    s_a2dp_source->start("NEO-Music", a2dp_data_callback);

    ESP_LOGI(TAG, "A2DP Source started, device name: NEO-Music");
}

static void bt_write_pcm(const uint8_t* data, size_t len) {
    if (!s_a2dp_connected || !s_bt_ringbuf) return;
    // Non-blocking write to ring buffer; drop if full
    xRingbufferSend(s_bt_ringbuf, data, len, 0);
}

static void bt_set_volume(int vol) {
    if (s_a2dp_source && s_a2dp_connected) {
        // A2DP volume range 0-127
        int a2dp_vol = (vol * 127) / 100;
        s_a2dp_source->set_volume(a2dp_vol);
    }
}

#else // !AUDIO_HAS_A2DP

static void bt_init() {
    ESP_LOGW(TAG, "Bluetooth A2DP not available (BT disabled or library missing)");
}

static void bt_write_pcm(const uint8_t* data, size_t len) {
    (void)data; (void)len;
}

static void bt_set_volume(int vol) {
    (void)vol;
}

#endif // AUDIO_HAS_A2DP

// ======================== Public API ========================

void audio_init() {
    s_mutex = xSemaphoreCreateMutex();

    // Initialize I2S hardware
    i2s_init_hw();

    // Initialize Bluetooth A2DP
    bt_init();

    s_state = PLAY_STOPPED;
    ESP_LOGI(TAG, "Audio system initialized");
}

void audio_write(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;

    // Make a mutable copy for volume adjustment
    // For large buffers we work in chunks to limit stack/heap usage
    static const size_t CHUNK_SIZE = 4096;
    uint8_t chunk_buf[CHUNK_SIZE];

    size_t offset = 0;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t chunk = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        memcpy(chunk_buf, data + offset, chunk);

        // Apply software volume
        apply_volume(chunk_buf, chunk);

        // Write to I2S
        i2s_write_pcm(chunk_buf, chunk);

        // Write to Bluetooth
        bt_write_pcm(chunk_buf, chunk);

        offset += chunk;
    }

    // Track bytes written for progress (use original length before volume scaling)
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_bytes_written += len;
    if (s_mutex) xSemaphoreGive(s_mutex);

    notify_progress();
}

void audio_set_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_volume = vol;
    // Precompute linear scale factor
    s_volume_scale = (float)vol / 100.0f;
    if (s_mutex) xSemaphoreGive(s_mutex);

    // Also set BT hardware volume
    bt_set_volume(vol);

    ESP_LOGI(TAG, "Volume set to %d", vol);
}

int audio_get_volume() {
    return s_volume;
}

// ======================== HTTP Streaming ========================

typedef struct {
    char url[512];
} stream_task_args_t;

static void stream_task(void* arg) {
    stream_task_args_t* args = (stream_task_args_t*)arg;
    const char* url = args->url;
    free(args);

    ESP_LOGI(TAG, "Stream task started: %s", url);
    set_state(PLAY_BUFFERING);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        report_error("HTTP client init failed");
        set_state(PLAY_ERROR);
        s_stream_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        char errmsg[128];
        snprintf(errmsg, sizeof(errmsg), "HTTP open failed: %d", err);
        report_error(errmsg);
        set_state(PLAY_ERROR);
        esp_http_client_cleanup(client);
        s_stream_running = false;
        vTaskDelete(NULL);
        return;
    }

    // Fetch headers
    int content_length = esp_http_client_fetch_headers(client);
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_content_length = content_length;
    s_bytes_written = 0;
    if (s_mutex) xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Content-Length: %d", content_length);

    set_state(PLAY_PLAYING);

    // Read and output in chunks
    static const int READ_BUF_SIZE = 4096;
    uint8_t* read_buf = (uint8_t*)malloc(READ_BUF_SIZE);
    if (!read_buf) {
        report_error("Stream buffer alloc failed");
        set_state(PLAY_ERROR);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_stream_running = false;
        vTaskDelete(NULL);
        return;
    }

    while (s_stream_running) {
        int read_len = esp_http_client_read(client, (char*)read_buf, READ_BUF_SIZE);
        if (read_len <= 0) {
            // End of stream or error
            if (read_len < 0) {
                report_error("HTTP read error");
                set_state(PLAY_ERROR);
            } else {
                ESP_LOGI(TAG, "Stream completed");
                set_state(PLAY_STOPPED);
            }
            break;
        }

        // Check pause state
        if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
        PlayState cur = s_state;
        if (s_mutex) xSemaphoreGive(s_mutex);

        if (cur == PLAY_PAUSED) {
            // Wait while paused, check every 100ms
            while (s_stream_running) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
                cur = s_state;
                if (s_mutex) xSemaphoreGive(s_mutex);
                if (cur != PLAY_PAUSED) break;
            }
            if (!s_stream_running) break;
        }

        // Write PCM data to outputs
        audio_write(read_buf, read_len);
    }

    free(read_buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Stream task exiting");
    s_stream_running = false;
    vTaskDelete(NULL);
}

bool audio_play_url(const char* url) {
    if (!url || strlen(url) == 0) {
        report_error("Empty URL");
        return false;
    }

    // Stop any current playback
    audio_stop();

    // Allocate task args
    stream_task_args_t* args = (stream_task_args_t*)calloc(1, sizeof(stream_task_args_t));
    if (!args) {
        report_error("Failed to allocate stream args");
        return false;
    }
    strncpy(args->url, url, sizeof(args->url) - 1);

    s_stream_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        stream_task,
        "audio_stream",
        8192,
        args,
        5,
        &s_stream_task_handle,
        0  // Core 0 (leave core 1 for UI/WiFi)
    );

    if (ret != pdPASS) {
        report_error("Failed to create stream task");
        free(args);
        s_stream_running = false;
        return false;
    }

    ESP_LOGI(TAG, "Playing URL: %s", url);
    return true;
}

void audio_pause() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state == PLAY_PLAYING) {
        s_state = PLAY_PAUSED;
        if (s_mutex) xSemaphoreGive(s_mutex);
        if (s_state_cb) s_state_cb(PLAY_PAUSED);
        ESP_LOGI(TAG, "Paused");
    } else {
        if (s_mutex) xSemaphoreGive(s_mutex);
    }
}

void audio_resume() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state == PLAY_PAUSED) {
        s_state = PLAY_PLAYING;
        if (s_mutex) xSemaphoreGive(s_mutex);
        if (s_state_cb) s_state_cb(PLAY_PLAYING);
        ESP_LOGI(TAG, "Resumed");
    } else {
        if (s_mutex) xSemaphoreGive(s_mutex);
    }
}

void audio_stop() {
    // Signal stream task to stop
    s_stream_running = false;

    if (s_stream_task_handle) {
        // Give the task time to notice and exit
        vTaskDelay(pdMS_TO_TICKS(200));
        s_stream_task_handle = NULL;
    }

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_bytes_written = 0;
    s_content_length = 0;
    if (s_mutex) xSemaphoreGive(s_mutex);

    set_state(PLAY_STOPPED);
    ESP_LOGI(TAG, "Stopped");
}

PlayState audio_get_state() {
    PlayState st;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    st = s_state;
    if (s_mutex) xSemaphoreGive(s_mutex);
    return st;
}

int audio_get_position_ms() {
    int64_t bw;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    bw = s_bytes_written;
    if (s_mutex) xSemaphoreGive(s_mutex);
    return position_ms_from_bytes(bw);
}

int audio_get_duration_ms() {
    int cl;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    cl = s_content_length;
    if (s_mutex) xSemaphoreGive(s_mutex);
    if (cl <= 0) return 0;
    return position_ms_from_bytes((int64_t)cl);
}

void audio_set_quality(AudioQuality q) {
    if (q >= QUALITY_COUNT) return;
    s_quality = q;
    ESP_LOGI(TAG, "Quality set to %d", (int)q);
}

AudioQuality audio_get_quality() {
    return s_quality;
}

void audio_task() {
    // This can be called from main loop or a dedicated task.
    // The actual streaming runs in its own FreeRTOS task (stream_task).
    // This function serves as a periodic housekeeping hook.
    if (s_stream_running) {
        notify_progress();
    }
}

// ======================== Callback Registration ========================

void audio_set_progress_callback(audio_progress_cb cb) {
    s_progress_cb = cb;
}

void audio_set_state_callback(audio_state_cb cb) {
    s_state_cb = cb;
}

void audio_set_error_callback(audio_error_cb cb) {
    s_error_cb = cb;
}
