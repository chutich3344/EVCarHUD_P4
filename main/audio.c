#include "sdkconfig.h"
#include "audio.h"


#include <math.h>
#include <stdint.h>
#include <string.h>
#include "board.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Chan I2S va chan bat loa: lay tu BSP chinh hang cua Waveshare cho ESP32-P4-WIFI6-Touch-LCD-3.5
 * (waveshare/esp32_p4_wifi6_touch_lcd_3_5: BSP_I2S_* va BSP_POWER_AMP_IO). Codec ES8311 nam tren bus I2C dung chung (dia chi 0x18). */
#define PIN_I2S_MCLK 13
#define PIN_I2S_SCLK 12
#define PIN_I2S_LRCK 10
#define PIN_I2S_DOUT 9
#define PIN_I2S_DIN  11                  /* micro: ADC cua ES8311 (cung bo Waveshare, theo Xiaozhi) */
#define PIN_PA_EN    53

#define RATE     16000                   /* mau/giay, 16 bit, 1 kenh */
#define TONE_MAX (RATE * 120 / 1000)     /* mot tieng dai nhat 120 ms */
#define AMP      30000                   /* bien do (toi da 32767); am luong dat bang esp_codec_dev_set_out_vol */

static const char *TAG = "audio";
static QueueHandle_t s_q;
static esp_codec_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;                                  /* tieng tit va giong tro ly AI dung chung loa */
static int16_t s_buf[TONE_MAX];
static volatile int s_vol = 70;                                   /* 0..100; 0 = tat tieng */
static volatile bool s_ready;                                     /* codec da mo (loa + micro) */

static void tone(int hz, int ms)                                  /* phat mot tieng (chan cho den khi day xong xuong I2S) */
{
    int n = RATE * ms / 1000;
    if (n > TONE_MAX) n = TONE_MAX;
    int fade = RATE * 6 / 1000;                                   /* vao/ra em 6 ms de khong bi "bup" */
    float w = 2.0f * 3.14159265f * (float)hz / (float)RATE;
    for (int i = 0; i < n; i++) {
        float env = i < fade ? (float)i / (float)fade : i > n - fade ? (float)(n - i) / (float)fade : 1.0f;
        s_buf[i] = (int16_t)((float)AMP * env * sinf(w * (float)i));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_codec_dev_write(s_dev, s_buf, n * (int)sizeof(int16_t));
    xSemaphoreGive(s_lock);
}

static void silence(int ms)
{
    int n = RATE * ms / 1000;
    if (n > TONE_MAX) n = TONE_MAX;
    memset(s_buf, 0, (size_t)n * sizeof(int16_t));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_codec_dev_write(s_dev, s_buf, n * (int)sizeof(int16_t));
    xSemaphoreGive(s_lock);
}

static bool audio_init(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (!bus) {
        ESP_LOGW(TAG, "khong co bus I2C (cam ung khoi tao hong?): bo qua am thanh");
        return false;
    }
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;                                   /* het du lieu thi phat im lang, khong lap lai tieng cu */
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t e = i2s_new_channel(&chan_cfg, &tx, &rx);           /* song cong: loa + micro chung dong ho I2S */
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "i2s_new_channel: %s", esp_err_to_name(e));
        return false;
    }
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_SCLK,
            .ws = PIN_I2S_LRCK,
            .dout = PIN_I2S_DOUT,
            .din = PIN_I2S_DIN,
        },
    };
    e = i2s_channel_init_std_mode(tx, &std_cfg);
    if (e == ESP_OK) e = i2s_channel_init_std_mode(rx, &std_cfg);
    if (e == ESP_OK) e = i2s_channel_enable(tx);
    if (e == ESP_OK) e = i2s_channel_enable(rx);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "khoi tao I2S: %s", esp_err_to_name(e));
        return false;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus};
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) {
        ESP_LOGW(TAG, "khong tao duoc giao tiep voi codec");
        return false;
    }
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,               /* loa + micro (tro ly AI) */
        .pa_pin = PIN_PA_EN,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!codec_if) {
        ESP_LOGW(TAG, "khong thay codec ES8311 tren bus I2C");
        return false;
    }
    esp_codec_dev_cfg_t dev_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = codec_if, .data_if = data_if};
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) {
        ESP_LOGW(TAG, "esp_codec_dev_new that bai");
        return false;
    }
    esp_codec_dev_sample_info_t fs = {.sample_rate = RATE, .channel = 1, .bits_per_sample = 16};
    int r = esp_codec_dev_open(s_dev, &fs);
    if (r != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "mo codec that bai (%d)", r);
        return false;
    }
    esp_codec_dev_set_out_vol(s_dev, s_vol);
    esp_codec_dev_set_in_gain(s_dev, 30.0f);                      /* micro: nhu Xiaozhi cho bo nay */
    s_ready = true;
    ESP_LOGI(TAG, "loa san sang (am luong %d%%)", s_vol);
    return true;
}

static void audio_task(void *arg)
{
    (void)arg;
    if (!audio_init()) {
        QueueHandle_t q = s_q;
        s_q = NULL;                                               /* audio_beep tu day la ham rong */
        vQueueDelete(q);
        vTaskDelete(NULL);
        return;
    }
    silence(60);
    for (;;) {
        int n = 0;
        if (xQueueReceive(s_q, &n, portMAX_DELAY) != pdTRUE) continue;
        for (int i = 0; i < n; i++) {                             /* giong ban S3: moi tieng 90 ms, nghi 70 ms */
            tone(2400, 90);
            silence(70);
        }
    }
}

void audio_beep(int times)
{
    QueueHandle_t q = s_q;
    if (q && times > 0 && s_vol > 0) xQueueSend(q, &times, 0);
}

void audio_set_volume(int percent)
{
    s_vol = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    if (s_dev) esp_codec_dev_set_out_vol(s_dev, s_vol);
}

bool audio_ready(void) { return s_ready; }

int audio_mic_read(int16_t *buf, int n)
{
    if (!s_ready) return -1;
    return esp_codec_dev_read(s_dev, buf, n * (int)sizeof(int16_t)) == ESP_CODEC_DEV_OK ? n : -1;
}

void audio_play(const int16_t *pcm, int n)
{
    if (!s_ready || n <= 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_codec_dev_write(s_dev, (void *)pcm, n * (int)sizeof(int16_t));
    xSemaphoreGive(s_lock);
}

void audio_start(void)
{
    if (s_q) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreate(4, sizeof(int));
    if (!s_q) return;
    if (xTaskCreate(audio_task, "audio", 4096, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
    }
}

