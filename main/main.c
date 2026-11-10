/* EVCarHUD cho board Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 (man 3.5" ngang 480x320), cung chuc nang voi ban S3 (2.8"):
 * dien thoai chay Waze Mod (HLP/1) hoac app GOFA gui du lieu dan duong qua Bluetooth LE -> giai ma -> giao dien 480x320.
 * Giu BOOT 5 giay: bat/tat che do trinh dien (du lieu gia, khong can dien thoai). */
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "ble_link.h"
#include "board.h"
#include "buttons.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "demo.h"
#include "link.h"
#include "settings.h"
#include "gofa.h"
#include "rtc_ds3231.h"
#include "wifi_link.h"
#include "ota.h"
#include "ai.h"
#include "usb_cam.h"
#include <sys/time.h>
#include <time.h>
#include "ui_fonts.h"

static const char *TAG = "evcarhud";

/* --- giao dien -> phan cung --- */
static void on_night(bool night)
{
    board_set_backlight(night ? CONFIG_EVCARHUD_BL_NIGHT : CONFIG_EVCARHUD_BL_DAY);
    static int saved = -1;                                   /* nho theme lan cuoi (chi ghi NVS khi doi) */
    if (saved < 0) saved = settings_get_last_night();
    if (saved != night) {
        settings_set_last_night(night);
        saved = night;
    }
}

static void on_mode(int mode) { settings_set_mode(mode); }

/* Moi lan co gio day du (RTC / dien thoai / NTP): dat gio he thong (dong ho trong chip, van chay khi ngu sau),
 * ghi module DS3231 neu co, luu mui gio. */
static bool has_ds3231;
static void on_clock(int64_t unix_s, int tz_min, int src)
{
    if (src != UI_TSRC_NTP && src != UI_TSRC_RTC) {
        struct timeval tv = {.tv_sec = (time_t)unix_s};
        settimeofday(&tv, NULL);
    }
    if (has_ds3231 && src >= UI_TSRC_PHONE) ds3231_write(unix_s);
    if (tz_min != settings_get_tz()) settings_set_tz(tz_min);
}

/* Luc bat may: khong doan gio. Dong ho trong chip con gio (day sau ngu sau / khoi dong lai mem) -> dung; khong thi doc
 * module DS3231 (neu gan). Khong co gi: man cho hien "--:--" cho Wi-Fi (NTP) hoac dien thoai. */
static bool ds_inited;
static void ds_init_once(void)
{
    if (!ds_inited) has_ds3231 = ds3231_init(board_i2c_bus());
    ds_inited = true;
}

/* Theme cho man khoi dong (va Tu dong khi chua co gio): Ngay / Dem da chon; Tu dong: gio trong chip hoac DS3231
 * (6:00-18:00 la ngay), khong co gio thi mac dinh Dem (theme lan cuoi co the la ban ngay -> sai luc toi). */
static bool boot_is_night(void)
{
    int m = settings_get_mode();
    if (m == UI_MODE_DAY) return false;
    if (m == UI_MODE_NIGHT) return true;
    int64_t t = time(NULL);
    ds_init_once();
    if (t <= 1700000000 && !(has_ds3231 && ds3231_read(&t))) return true;   /* chua co gio: mac dinh Dem */
    int h = (int)(((t + settings_get_tz() * 60) % 86400 + 86400) % 86400 / 3600);
    return h < 6 || h >= 18;
}

static void clock_boot(void)
{
    int tz = settings_get_tz();
    ds_init_once();
    time_t now = time(NULL);
    int64_t t;
    if (now > 1700000000) {
        ESP_LOGI(TAG, "gio con trong chip (%s)", esp_reset_reason() == ESP_RST_DEEPSLEEP ? "day sau ngu sau" : "khoi dong lai");
        hlp_dash_set_clock(now, tz, UI_TSRC_RTC);
    } else if (has_ds3231 && ds3231_read(&t)) {
        struct timeval tv = {.tv_sec = (time_t)t};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "lay gio tu module DS3231");
        hlp_dash_set_clock(t, tz, UI_TSRC_RTC);
    } else {
        ESP_LOGI(TAG, "chua co gio: cho NTP / dien thoai");
    }
}

static void on_rotate(void)                                 /* bam BOOT 2 lan: xoay 180 do va nho lai */
{
    bool r = !board_get_rotated();
    board_set_rotated(r);
    settings_set_rotated(r);
}   /* nho che do Ngay/Dem qua lan tat may */

static void on_level(int level)                              /* vuot toc do: LED do (tieng tit: on_beep) */
{
    board_set_led(level == UI_LEVEL_OVER, false, false);
}

static void on_beep(int times) { board_beep(times); }

static void on_source(int src)                              /* doi Waze Mod / GOFA: chi luu, nguoi dung bam RST de chay nguon moi */
{
    settings_set_source(src);
    ESP_LOGI(TAG, "nguon du lieu lan khoi dong sau: %s", src ? "GOFA" : "Waze Mod");
}

static void on_sound(bool on, int vol)                      /* man cai dat am luong: dat loa + luu NVS */
{
    board_set_volume(on ? vol : 0);
    settings_set_sound(on);
    settings_set_volume(vol);
}

/* Man kiem tra mau 1,5 giay: 4 o DO / XANH LA / TRANG / XANH DUONG (tren-trai, tren-phai, duoi-trai, duoi-phai).
 * Sai mau / sai huong: idf.py menuconfig > EVCarHUD > Man hinh (README). */
static void color_test(void)
{
#if CONFIG_EVCARHUD_COLOR_TEST
    static const struct { uint32_t c; const char *name; int x, y; } q[] = {
        {0xFF0000, "ĐỎ", 0, 0}, {0x00FF00, "XANH LÁ", UI_W / 2, 0},
        {0xFFFFFF, "TRẮNG", 0, UI_H / 2}, {0x0000FF, "XANH DƯƠNG", UI_W / 2, UI_H / 2},
    };
    board_ui_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_obj_create(scr);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, q[i].x, q[i].y);
        lv_obj_set_size(b, UI_W / 2, UI_H / 2);
        lv_obj_set_style_bg_color(b, lv_color_hex(q[i].c), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &ui_font_txt16, 0);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_label_set_text(l, q[i].name);
        lv_obj_center(l);
    }
    lv_obj_t *t = lv_label_create(scr);
    lv_obj_set_style_text_font(t, &ui_font_txt14, 0);
    lv_obj_set_style_bg_color(t, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_pad_all(t, 6, 0);
    const esp_app_desc_t *ad = esp_app_get_description();
    lv_label_set_text_fmt(t, "EVCarHUD 3.5\" %s · %s", ad ? ad->version : "", board_lcd_name());
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    lv_screen_load(scr);
    board_ui_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    board_set_backlight(CONFIG_EVCARHUD_BL_DAY);
    vTaskDelay(pdMS_TO_TICKS(1500));                         /* board P4 chi co mot chip man hinh: khong can doi chip */
#endif
}

/* Man chao logo 3 giay (ui_splash_create); bang dieu khien thay man nay va xoa no (auto_del). */
#define SPLASH_MS 3000
static void splash(void)
{
    const esp_app_desc_t *ad = esp_app_get_description();
    bool bn = boot_is_night();
    board_ui_lock();
    ui_set_boot_night(bn);
    lv_screen_load(ui_splash_create(ad ? ad->version : "", settings_get_source()));
    board_ui_unlock();
    board_set_backlight(bn ? CONFIG_EVCARHUD_BL_NIGHT : CONFIG_EVCARHUD_BL_DAY);
    vTaskDelay(pdMS_TO_TICKS(SPLASH_MS));
}

#if CONFIG_EVCARHUD_LDR
static void light_task(void *arg)                            /* quang tro: Tu dong Ngay/Dem, co tre de khong nhap nhay */
{
    (void)arg;
    int dark_cnt = 0, n = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int v = board_light_raw();
        if (v < 0) continue;
        if (++n % 10 == 0) ESP_LOGI(TAG, "anh sang: %d (toi khi > %d)", v, CONFIG_EVCARHUD_LDR_DARK_ABOVE);
        bool dark = v > CONFIG_EVCARHUD_LDR_DARK_ABOVE;
        dark_cnt = dark ? (dark_cnt < 5 ? dark_cnt + 1 : 5) : (dark_cnt > 0 ? dark_cnt - 1 : 0);
        if (dark_cnt == 5 || dark_cnt == 0) {
            board_ui_lock();
            ui_set_ambient(dark_cnt == 5);
            board_ui_unlock();
        }
    }
}
#endif

/* Chip Wi-Fi C6 khong bi reset khi P4 khoi dong lai (OTA, nut RST, loi): esp_hosted 3.x noi lai voi C6 dang o trang thai cu
 * -> Wi-Fi nghen (khong thoi tiet, khong toi may chu) cho den khi rut nguon. Moi lan bat may: keo EN C6 xuong de no khoi
 * dong sach cung P4. Lam truoc moi thu dung esp_hosted (BLE, Wi-Fi), con ket noi C6 chi bat dau khi goi Wi-Fi/BLE lan dau. */
#ifndef CONFIG_ESP_HOSTED_HOST_RESET_GPIO
#define CONFIG_ESP_HOSTED_HOST_RESET_GPIO 54
#endif
static void c6_reset_on_boot(void)
{
    const gpio_num_t pin = (gpio_num_t)CONFIG_ESP_HOSTED_HOST_RESET_GPIO;
    gpio_config_t io = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(pin, 1);                               /* C6 khoi dong (~0,5 s) trong luc P4 dung man hinh */
}

void app_main(void)
{
    c6_reset_on_boot();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (!board_display_init()) {
        ESP_LOGE(TAG, "khong khoi tao duoc man hinh");
        return;
    }
    color_test();
    splash();

    bool demo = settings_get_demo();
    board_ui_lock();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    dashboard_create(scr);
    ui_set_mode(settings_get_mode());
    ui_set_night_hook(on_night);                             /* dat den nen ngay theo Ngay/Dem */
    ui_set_mode_hook(on_mode);
    ui_set_level_hook(on_level);
    board_set_volume(settings_get_sound() ? settings_get_volume() : 0);
    ui_set_sound(settings_get_sound(), settings_get_volume());
    ui_set_beep_hook(on_beep);
    gofa_set_beep(on_beep);
    ui_set_sound_hook(on_sound);
    ui_set_source(settings_get_source());
    ui_set_source_hook(on_source);
    /* MAC Bluetooth (cua chip C6) dien vao trang Ket noi khi Bluetooth san sang (ble_link.c on_sync) */
    ui_set_rotate_hook(on_rotate);
    ui_set_rotated(settings_get_rotated());
    {
        const esp_app_desc_t *ad = esp_app_get_description();
        ui_set_version(ad ? ad->version : "");
    }
    link_init();                                             /* bo giai ma HLP + chon duong truyen */
    hlp_dash_set_clock_hook(on_clock);
    clock_boot();
    if (demo) {
        demo_start();
        ui_toast("Trình diễn (giữ BOOT 3 giây để tắt)");
    } else {
        char hint[96];
        snprintf(hint, sizeof hint, "Mở Waze Mod > HUD > Bluetooth LE,\nchọn \"%s\"", CONFIG_EVCARHUD_BLE_NAME);
        ui_set_hint(hint);
    }
    board_ui_unlock();

    buttons_start();
    if (!demo) ble_link_start();                             /* cho Waze Mod ket noi qua Bluetooth LE */
    ota_start();                                             /* cap nhat OTA: xac nhan ban moi, man Cap nhat firmware */
    wifi_link_start();                                       /* Wi-Fi da luu: gio NTP + thoi tiet */
    ai_start();                                              /* tro ly Mi Mi: nut micro (can Wi-Fi + loa) */
    usb_cam_start();                                         /* camera USB (thu nghiem): xem truoc + FPS */
#if CONFIG_EVCARHUD_LDR
    xTaskCreate(light_task, "anh_sang", 2560, NULL, 2, NULL);
#endif
    ESP_LOGI(TAG, "EVCarHUD chay (%s). RAM trong con %u byte", demo ? "trinh dien" : "Bluetooth",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
