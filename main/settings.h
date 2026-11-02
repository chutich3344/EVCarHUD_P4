/* Cai dat luu trong NVS (con lai sau khi tat may). Nap firmware bang file gop (dia chi 0x0) se xoa cac gia tri nay. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

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
