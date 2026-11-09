/* Cai dat luu trong NVS (con lai sau khi tat may). Nap firmware bang file gop (dia chi 0x0) se xoa cac gia tri nay. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool settings_get_last_night(void);  /* giao dien Dem lan cuoi (Tu dong, chua co gio luc bat may) */
void settings_set_last_night(bool night);
int settings_get_mode(void);          /* UI_MODE_*: Tu dong / Ngay / Dem */
void settings_set_mode(int mode);
bool settings_get_demo(void);         /* che do trinh dien (du lieu gia) */
void settings_set_demo(bool on);
bool settings_get_lcd_alt(void);      /* true: dung chip man hinh con lai (doi bang nut BOOT luc khoi dong) */
void settings_set_lcd_alt(bool on);
bool settings_get_rotated(void);      /* xoay giao dien 180 do (giu tay tren man hinh 1 giay); mac dinh: co */
void settings_set_rotated(bool on);
bool settings_get_clock(int64_t *unix_s, int *tz_min);   /* gio da luu lan cuoi (false: chua co) */
void settings_set_clock(int64_t unix_s, int tz_min);
bool settings_get_sound(void);        /* loa bat (mac dinh: co) */
void settings_set_sound(bool on);
int settings_get_volume(void);        /* am luong 10..100 (mac dinh 70) */
void settings_set_volume(int v);
int settings_get_source(void);        /* nguon du lieu: 0 Waze Mod (mac dinh), 1 GOFA */
void settings_set_source(int s);
int settings_get_tz(void);            /* mui gio (phut), mac dinh +7 (Viet Nam) */
void settings_set_tz(int tz_min);
bool settings_get_wifi_on(void);      /* cong tac Wi-Fi (mac dinh: bat) */
void settings_set_wifi_on(bool on);
#define WIFI_SAVED_MAX 6
bool settings_get_wifi(char ssid[33], char pass[65]);   /* mang Wi-Fi luu gan nhat (false: chua co) */
void settings_set_wifi(const char *ssid, const char *pass);   /* luu / cap nhat mang, dua len dau */
int settings_wifi_list(char ssid[][33], char pass[][65]);    /* moi mang da luu (mang WIFI_SAVED_MAX), tra so mang */
bool settings_wifi_find(const char *ssid, char pass[65]);     /* mang nay da luu chua (lay mat khau) */
void settings_wifi_forget(const char *ssid);
