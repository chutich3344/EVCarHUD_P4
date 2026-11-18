/* App "EVCarHUD Lan" tren dien thoai (android-adas/, mo hinh openpilot) ghi goi chu A1 vao dac tinh 4556d005
 * (dich vu 4556d001, chung voi app Tram sac), ~10 goi/giay, 1 goi/giay khi AI dung:
 *   A1|lan|lech cm|pL%|pR%|xe truoc m|dv km/h|TTC x10|muc|v km/h|trang thai
 *   lan: '-' / 'L' lan trai / 'R' lan phai.  muc: 0 khong, 1 gan hon TT38, 2 TTC < 2,5 s, 3 PHANH (TTC < 1,5 s)
 *   xe truoc 0 = khong thay.  trang thai: 0 chay, 1 xe do (AI dung), 2 nong giam tai, 3 nong da dung AI, 4 dang nap
 * HUD hien so met o o khoang cach duoi bien (cung khung xADAS / camera USB; camera USB uu tien hon), chip "LAN" tren cung
 * (xanh: thay 2 vach, xam: khong, do: lan lan), vach do chop o mep man hinh khi lan lan, PHANH toan man o muc 3. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "xadas.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ldw;            /* '-', 'L', 'R' */
    int off_cm, pl, pr, lead_m, dv_kmh, ttc10, level, kmh, state;
} phone_adas_t;

/* false: khong phai goi A1. Truong thieu (app cu) = 0. */
bool phone_adas_parse(const char *s, size_t n, phone_adas_t *p);
/* Doi sang khung xADAS de dung chung o khoang cach (legal_m: khoang cach toi thieu TT38 theo toc do, 0 = duoi 55 km/h). */
void phone_adas_to_xadas(const phone_adas_t *p, int legal_m, xadas_frame_t *f);
/* Goi D1 (dan duong theo tuyen app Lan tinh, ~1 goi/giay va khi doi chi dan, ghi co phan hoi):
 *   D1|ma re|met toi cho re|so loi ra vong xuyen|met con lai|phut con lai|gio den HH:MM|ten duong
 *   ma re giong Waze HLP (1 thang, 2 trai, 3 phai, 4/5 cheo, 6/7 gat, 8 quay dau, 11 vong xuyen, 13-16 nhanh, 17 toi noi).
 *   "D1|0" = het dan duong. HUD uu tien hon Waze / GOFA / tram sac, tu bo sau 15 s khong co goi. */
typedef struct {
    bool on;
    int turn, dist_m, exit_n, remain_m, remain_min;
    char eta[8];
    char street[96];
} phone_nav_t;
bool phone_nav_parse(const char *s, size_t n, phone_nav_t *p);

/* ble_link.c goi tu task host NimBLE khi app ghi 4556d005. */
void phone_adas_on_write(const char *s, size_t n);

#ifdef __cplusplus
}
#endif
