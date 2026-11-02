/* EVCarHUD cho board Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 (man 3.5" ngang 480x320), cung chuc nang voi ban S3 (2.8"):
 * dien thoai chay Waze Mod (HLP/1) hoac app GOFA gui du lieu dan duong qua Bluetooth LE -> giai ma -> giao dien 480x320.
 * Giu BOOT 5 giay: bat/tat che do trinh dien (du lieu gia, khong can dien thoai). */
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
#include "ui_fonts.h"

static const char *TAG = "evcarhud";

/* --- giao dien -> phan cung --- */
static void on_night(bool night) { board_set_backlight(night ? CONFIG_EVCARHUD_BL_NIGHT : CONFIG_EVCARHUD_BL_DAY); }

static void on_mode(int mode) { settings_set_mode(mode); }

/* Gio theo dien thoai: luu 10 phut/lan (va lan dau) de lan bat may sau dong ho chay tiep ngay, khong phai "--:--" */
static void on_clock(int64_t unix_s, int tz_min)
{
    static int64_t saved;
    if (saved && unix_s - saved < 600) return;
    saved = unix_s;
    settings_set_clock(unix_s, tz_min);
}

/* Gio luc build (__DATE__ __TIME__, may build tren GitHub dung gio UTC) -> giay UTC: dung khi chua tung co gio */
static int64_t build_unix(void)
{
    static const char MON[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d = 1, y = 2026, hh = 0, mm = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", m, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    const char *p = strstr(MON, m);
    int mo = p ? (int)(p - MON) / 3 + 1 : 1;
    /* days_from_civil (H. Hinnant) */
    y -= mo <= 2;
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    return days * 86400 + hh * 3600 + mm * 60 + ss;
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
    board_ui_lock();
    lv_screen_load(ui_splash_create(ad ? ad->version : "", settings_get_source()));
    board_ui_unlock();
    board_set_backlight(CONFIG_EVCARHUD_BL_DAY);
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

void app_main(void)
{
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
    {
        uint8_t a[6];
        if (esp_read_mac(a, ESP_MAC_BT) == ESP_OK) {                 /* MAC Bluetooth (= MAC WiFi + 2): dang ky tren GOFA */
            char t[20];
            snprintf(t, sizeof t, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
            ui_set_mac(t);
            ESP_LOGI(TAG, "MAC Bluetooth: %s", t);
        }
    }
    ui_set_rotate_hook(on_rotate);
    link_init();                                             /* bo giai ma HLP + chon duong truyen */
    {
        int64_t t;
        int tz;
        if (!settings_get_clock(&t, &tz)) {                  /* moi nap firmware (NVS da xoa): lay gio build, mui gio VN */
            t = build_unix();
            tz = 7 * 60;
        }
        hlp_dash_set_clock_guess(t, tz);                     /* dong ho chay ngay; dien thoai noi se chinh lai */
        if (!demo) hlp_dash_set_clock_hook(on_clock);        /* trinh dien dung gio gia: khong luu */
    }
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
#if CONFIG_EVCARHUD_LDR
    xTaskCreate(light_task, "anh_sang", 2560, NULL, 2, NULL);
#endif
    ESP_LOGI(TAG, "EVCarHUD chay (%s). RAM trong con %u byte", demo ? "trinh dien" : "Bluetooth",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
