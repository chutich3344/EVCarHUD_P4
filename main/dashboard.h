/* EVCarHUD P4 - giao dien LVGL 9 cho man hinh 3.5" ngang 480x320.
 * Giao dien chi nhan du lieu qua cac ham ui_set_*, khong goi truc tiep phan cung hay giao thuc
 * (dashboard_hlp.c giai ma Waze Mod HLP/1 roi goi vao day). Moi ham phai goi trong khoa LVGL (board_ui_lock). */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#if defined(__has_include)
#  if __has_include("lvgl.h")
#    include "lvgl.h"
#  else
#    include "lvgl/lvgl.h"
#  endif
#else
#  include "lvgl.h"
#endif
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_W 480
#define UI_H 320

/* Canh bao sap toi (ma theo HLP `alr`, 0..75). dist/value = -1 neu khong co. */
typedef struct {
    int code;
    int dist;      /* met */
    int value;     /* km/h, chi co voi SPEED_DROP (8) */
} ui_alert_t;

enum { UI_CHIP_NONE = 0, UI_CHIP_CAMERA = 1, UI_CHIP_NOPASS = 2 };
enum { UI_GPS_GOOD = 0, UI_GPS_WEAK, UI_GPS_LOST };
enum { UI_MODE_AUTO = 0, UI_MODE_DAY = 1, UI_MODE_NIGHT = 2 };
enum { UI_LEVEL_OK = 0, UI_LEVEL_NEAR = 1, UI_LEVEL_OVER = 2 };   /* toc do so voi gioi han */

void dashboard_create(lv_obj_t *screen);
lv_obj_t *ui_screen(void);                               /* man bang dieu khien (dashboard_create) */
enum { UI_SRC_WAZE = 0, UI_SRC_GOFA = 1 };              /* nguon du lieu dan duong (chon trong cai dat > Ket noi) */
/* Ngay/Dem luc bat may (truoc ui_splash_create): man khoi dong dung theme nay; Tu dong ma chua co gio cung giu theme nay */
void ui_set_boot_night(bool night);
lv_obj_t *ui_splash_create(const char *version, int source);   /* man chao: logo + "Ket noi voi" Waze/GOFA (chua nap) */
lv_obj_t *ui_partner_tile(lv_obj_t *parent, int source, int size);   /* o logo Waze / GOFA */

/* --- toc do / dong ho --- */
void ui_set_speed(int kmh);
void ui_set_speed_limit(int kmh);                 /* 0 = chua biet: an bien, khong canh bao */
void ui_set_time(int hour, int minute, int wday); /* wday: 0 = Chu nhat ... 6 = Thu bay, -1 = chua biet */
int ui_get_speed(void);
bool ui_is_night(void);                          /* giao dien dang Dem (canh bao vat can lay mau theo) */
void ui_set_date(int day, int month);
bool ui_get_clock(int *h, int *m);                /* false: chua co gio */
enum { UI_TSRC_NONE = 0, UI_TSRC_RTC = 1, UI_TSRC_PHONE = 2, UI_TSRC_NTP = 3 };   /* nguon gio, thap -> cao */
void ui_set_time_source(int src);                 /* man cho: o "Gio: Wi-Fi / dien thoai / RTC" */

/* --- Wi-Fi + thoi tiet (man cho) ---
 * Giao dien chi ve; wifi_link.c (board) lam viec that qua cac hook. Cham bieu tuong Wi-Fi hoac dai vang o man cho:
 * mo trang Wi-Fi (bat/tat, do mang, nhap mat khau bang ban phim tren man hinh). */
enum { UI_WIFI_OFF = 0, UI_WIFI_IDLE, UI_WIFI_CONNECTING, UI_WIFI_CONNECTED, UI_WIFI_FAIL };
typedef struct {
    char ssid[33];
    int rssi;                                     /* dBm */
    bool lock;                                    /* co mat khau */
} ui_wifi_ap_t;
typedef struct {
    void (*enable)(bool on);                      /* cong tac Wi-Fi */
    void (*scan)(void);                           /* do mang: tra ket qua bang ui_wifi_scan_done */
    void (*join)(const char *ssid, const char *pass);   /* ket noi + luu NVS */
} ui_wifi_hooks_t;
void ui_set_wifi_hooks(const ui_wifi_hooks_t *h);
void ui_wifi_set_state(int state, const char *ssid);   /* UI_WIFI_*; ssid: mang dang/da ket noi */
void ui_wifi_scan_done(const ui_wifi_ap_t *aps, int n);
void ui_open_wifi(void);
void ui_wifi_test_pass(const char *ssid, const char *pwd, bool err);   /* chi de chup anh xem truoc */
void ui_wifi_test_join(void);
enum { UI_WX_NONE = 0, UI_WX_SUN, UI_WX_CLOUD, UI_WX_RAIN, UI_WX_STORM, UI_WX_FOG };
void ui_set_weather(int kind, const char *text);  /* kind UI_WX_NONE: "Chua co thoi tiet"; text vd "TP. Ho Chi Minh · 31°C · Nang" */

/* --- du lieu dan duong (Waze HLP) --- */
void ui_set_nav(bool active, int turn, int dist_m, int exit_n, const char *street, int turn2);
void ui_set_route(const char *eta, int remaining_m, int remaining_min);   /* eta rong va remaining <= 0: hien dong ho */
void ui_set_alerts(const ui_alert_t *alerts, int n);   /* gan -> xa; n = 0: khong co canh bao */
void ui_set_chip(int kind, const char *text);
void ui_set_signal(bool ok);                           /* false: chua co du lieu tu dien thoai -> man cho */
void ui_set_phone(bool connected);                     /* dien thoai noi Bluetooth (man cho: "Da ket noi dien thoai") */
void ui_set_waze(bool on);                             /* false: chi co toc do GPS, chua co du lieu Waze */
void ui_set_gps(int level);
void ui_set_hint(const char *text);
/* Tram sac (app Tram sac qua BLE): name NULL = xoa. side 'A' truoc, 'R' phai, 'L' trai, 'B' sau, '?' chua biet.
 * angle: goc lech do (duong = phai), UI_TS_NO_ANGLE = khong ro. mode 'N' tram gan nhat: dai giua man Bien toc do khi khong
 * co canh bao. mode 'D' tram dich (nguoi dung bam "Gui len HUD"): man dan duong (mui ten theo goc, khoang cach, ten tram),
 * ca khi dung yen; Waze dang dan duong thi uu tien Waze. Tu an sau 90 giay khong co ban tin. */
#define UI_TS_NO_ANGLE (-999)
void ui_set_station(const char *name, int dist_m, char side, int angle, int kw, int ports, int count, char mode);                    /* bo qua: man cho tu hien "Cho ket noi Waze..." */

/* --- Ngay / Dem ---
 * Tu dong: theo cam bien anh sang neu board co (ui_set_ambient), khong thi theo gio (6:00-18:00 la ngay).
 * Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem. */
void ui_set_mode(int mode);
int ui_get_mode(void);
void ui_cycle_mode(void);
void ui_set_ambient(int dark);                         /* 1 toi, 0 sang, -1 khong co cam bien */
void ui_set_mode_hook(void (*cb)(int mode));           /* nguoi dung doi che do: de luu NVS */
void ui_set_night_hook(void (*cb)(bool night));        /* giao dien vua doi sang Ngay/Dem: board chinh den nen */
void ui_rotate(void);                                  /* xoay 180 do (bam BOOT 2 lan khi khong mo cai dat; giu BOOT 1-3 s roi tha = mo cai dat) */
void ui_set_rotate_hook(void (*cb)(void));             /* bam BOOT 2 lan: board xoay 180 do */
void ui_set_level_hook(void (*cb)(int level));         /* UI_LEVEL_*: board bat den LED / loa */
void ui_toast(const char *text);
/* --- loa ---
 * Tieng tit theo trang thai: 1 tieng khi co canh bao moi (camera, canh sat...) va khi con 100 m toi cho re;
 * 2 tieng khi vuot toc do (nhac lai moi 8 giay neu van vuot). Giu tay 5 giay tren man hinh: man cai dat am luong. */
bool ui_settings_shown(void);                          /* man cai dat dang mo */
void ui_open_settings(void);                           /* mo man cai dat am luong (giu tay 5 giay) */
void ui_open_settings_page(int page);                  /* 0 = Hien thi, 1 = Canh bao (am thanh), 2 = Ket noi, 3 = He thong */
void ui_set_version(const char *version);              /* phien ban firmware (trang He thong) */
void ui_set_rotated(bool on);                          /* trang thai xoay 180 do da luu (NVS) */
void ui_set_source(int source);                        /* nguon da luu (NVS), UI_SRC_* */
void ui_set_source_hook(void (*cb)(int source));       /* nguoi dung doi nguon: luu NVS (chay nguon moi sau khi bam RST) */
enum { UI_KEY_NEXT = 0, UI_KEY_OK = 1 };
bool ui_settings_key(int key);                         /* nut BOOT khi cai dat dang mo: 1 lan = muc ke, 2 lan = chon. false = cai dat dang dong */
void ui_gofa_debug(const char *state, const char *last);   /* che do GOFA: dai duoi man hinh hien goi Bluetooth vua nhan */
void ui_set_mac(const char *mac);                      /* MAC Bluetooth "AA:BB:.." hien o trang Ket noi (dang ky GOFA) */
void ui_test_switch(int source);                       /* chi de chup anh xem truoc: man "Da chon" + MAC */
void ui_set_sound(bool on, int volume);                /* gia tri da luu (NVS), volume 10..100 */
void ui_set_beep_hook(void (*cb)(int times));          /* giao dien muon phat n tieng tit */
void ui_set_sound_hook(void (*cb)(bool on, int volume)); /* nguoi dung doi am luong: board dat loa + luu NVS */                       /* thong bao nho 2 giay o giua man hinh */

#ifdef __cplusplus
}
#endif
#endif
