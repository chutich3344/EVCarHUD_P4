/* ===== Phan cung (theo so do mach, da doi chieu) =====
 *  LCD ST7796 (SPI): MOSI 20, SCLK 21, MISO 22, CS 23, DC 26, RST 27, den nen 28 (qua MOSFET, muc cao = sang)
 *  Cam ung FT6336 (I2C 0x38): SDA 7, SCL 8, RST 29, INT 50
 * Chan, SPI mode 3, BGR, dao mau, FT5x06: da doi chieu voi BSP chinh hang waveshare/esp32_p4_wifi6_touch_lcd_3_5 v2.0.2.
 *  (BSP de man hinh doc voi mirror(true,false), cam ung khong lat; chay SPI 80 MHz, khong dung MISO.)
 * Panel goc 320x480 doc; xoay ngang bang phan cung (swap_xy + mirror) de LVGL thay man hinh 480x320.
 * Voi swap_xy va khong lat man hinh thi cam ung can swap_xy + lat X (mac dinh trong Kconfig). */
#include "board.h"
#include "audio.h"
#include "dashboard.h"
#include "settings.h"
#include "sdkconfig.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_EVCARHUD_TOUCH
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#endif

static const char *TAG = "board";

static i2c_master_bus_handle_t s_i2c_bus;                       /* bus I2C dung chung, tao trong i2c_init */
i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c_bus; }

#define PIN_LCD_MOSI  20
#define PIN_LCD_SCLK  21
#define PIN_LCD_MISO  22
#define PIN_LCD_CS    23
#define PIN_LCD_DC    26
#define PIN_LCD_RST   27
#define PIN_LCD_BL    28
#define PIN_TP_SDA    7
#define PIN_TP_SCL    8
#define PIN_TP_RST    29
#define PIN_TP_INT    50

#define LCD_SPI_HOST  SPI2_HOST
#define LCD_H_RES     480                 /* sau khi xoay ngang */
#define LCD_V_RES     320
#define DRAW_LINES    40                  /* bo dem ve tung phan: 480 x 40 diem x 2 byte, 2 bo, RAM trong (DMA) */

#ifdef CONFIG_EVCARHUD_LCD_INVERT
#define LCD_INVERT true
#else
#define LCD_INVERT false
#endif
#ifdef CONFIG_EVCARHUD_LCD_BGR
#define LCD_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#else
#define LCD_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#endif
#ifdef CONFIG_EVCARHUD_LCD_MIRROR_X
#define LCD_MIRROR_X true
#else
#define LCD_MIRROR_X false
#endif
#ifdef CONFIG_EVCARHUD_LCD_MIRROR_Y
#define LCD_MIRROR_Y true
#else
#define LCD_MIRROR_Y false
#endif

static lv_display_t *s_disp;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_mutex;
static bool s_rot;                        /* xoay 180 do = lat ca hai chieu (hinh va cam ung) */

const char *board_lcd_name(void) { return "ST7796"; }

bool board_get_rotated(void) { return s_rot; }

void board_set_rotated(bool on)                  /* goi trong khoa LVGL; lenh SPI tu doi het lan ve dang chay */
{
    s_rot = on;
    if (!s_panel) return;
    esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X != on, LCD_MIRROR_Y != on);
    lv_obj_invalidate(lv_screen_active());
}

void board_set_led(bool r, bool g, bool b) { (void)r; (void)g; (void)b; }
int board_light_raw(void) { return -1; }
void board_set_volume(int percent) { audio_set_volume(percent); }
void board_beep(int times) { audio_beep(times); }

/* ---------------------- den nen (PWM) ---------------------- */
static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void board_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)percent * 1023u / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---------------------- LVGL <-> esp_lcd ---------------------- */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io; (void)ev;
    lv_display_flush_ready((lv_display_t *)ctx);                   /* chay trong ngat SPI */
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)disp;
    int w = lv_area_get_width(area), h = lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px, (uint32_t)(w * h));                 /* LVGL: little-endian; ST7796: big-endian */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
        uint32_t wait = lv_timer_handler();
        xSemaphoreGiveRecursive(s_mutex);
        if (wait < 5) wait = 5;
        if (wait > 40) wait = 40;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

bool board_ui_lock(void) { return xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY) == pdTRUE; }
void board_ui_unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

/* ---------------------- bus I2C dung chung (cam ung FT6336, codec ES8311) ---------------------- */
static void i2c_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = -1,                                            /* tu chon cong I2C */
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bc, &s_i2c_bus);
    if (err != ESP_OK) {
        s_i2c_bus = NULL;
        ESP_LOGW(TAG, "khong tao duoc bus I2C (%s): bo qua cam ung va loa", esp_err_to_name(err));
    }
}

/* ---------------------- cam ung ---------------------- */
#if CONFIG_EVCARHUD_TOUCH
static esp_lcd_touch_handle_t s_tp;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x = 0, y = 0;
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_tp);
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, &x, &y, NULL, &cnt, 1) && cnt > 0;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (pressed) {
        if (x >= LCD_H_RES) x = LCD_H_RES - 1;
        if (y >= LCD_V_RES) y = LCD_V_RES - 1;
        data->point.x = s_rot ? LCD_H_RES - 1 - x : x;
        data->point.y = s_rot ? LCD_V_RES - 1 - y : y;
    }
}

static void touch_init(lv_display_t *disp)
{
    i2c_master_bus_handle_t bus = s_i2c_bus;
    if (!bus) return;
    esp_err_t err;
    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "khong tao duoc IO cam ung (%s): bo qua cam ung", esp_err_to_name(err));
        return;
    }
    esp_lcd_touch_config_t tc = {
        .x_max = 320,                                              /* kich thuoc goc cua cam ung (doc) */
        .y_max = 480,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {
#ifdef CONFIG_EVCARHUD_TOUCH_SWAP
            .swap_xy = 1,
#endif
#ifdef CONFIG_EVCARHUD_TOUCH_MIRROR_X
            .mirror_x = 1,
#endif
#ifdef CONFIG_EVCARHUD_TOUCH_MIRROR_Y
            .mirror_y = 1,
#endif
        },
    };
    err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tc, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336 khong phan hoi (%s): bo qua cam ung", esp_err_to_name(err));
        return;
    }
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);
    ESP_LOGI(TAG, "cam ung san sang");
}
#endif

/* ---------------------- khoi tao ---------------------- */
lv_display_t *board_display_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    backlight_init();                                              /* den nen tat cho den khi co hinh dau tien */

    lv_init();
    lv_tick_set_cb(tick_cb);
    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    const size_t buf_bytes = (size_t)LCD_H_RES * DRAW_LINES * sizeof(uint16_t);
    void *b1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!b1 || !b2) {
        ESP_LOGE(TAG, "khong cap duoc bo dem ve (%u byte x2)", (unsigned)buf_bytes);
        return NULL;
    }
    lv_display_set_buffers(s_disp, b1, b2, (uint32_t)buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)buf_bytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = CONFIG_EVCARHUD_LCD_SPI_MHZ * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,                                             /* theo BSP cua Waveshare (waveshare/esp32_p4_wifi6_touch_lcd_3_5) */
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = s_disp,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io));

    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_ORDER,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &pc, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, LCD_INVERT));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));         /* panel doc -> ngang */
    s_rot = settings_get_rotated();
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X != s_rot, LCD_MIRROR_Y != s_rot));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    i2c_init();
#if CONFIG_EVCARHUD_TOUCH
    touch_init(s_disp);
#endif
    audio_start();                                                 /* codec ES8311 nam tren bus I2C cua cam ung */

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "man hinh %dx%d san sang (SPI %d MHz)", LCD_H_RES, LCD_V_RES, CONFIG_EVCARHUD_LCD_SPI_MHZ);
    return s_disp;
}
