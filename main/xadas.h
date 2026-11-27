/* xADAS HUD Link v1 (github.com/drivedeck/xadas-hud-link, docs/giao-thuc.md): app xADAS tren Android do khoang cach toi
 * xe phia truoc bang camera roi ghi 1 khung 10 byte vao dac tinh 39b30002-52bb-461f-a6fd-c2ff0f067cfe (dich vu
 * 39b30001-...), Write Without Response, >= 1 khung/giay, toi da 10 khung/giay. Khong dang ky, khong ghep doi.
 *   [0] version = 1  [1] seq  [2] state  [3] level  [4] flags (bit0 he Anh, bit1 khung thu)
 *   [5] speed km/h (255 = khong co)  [6..7] distance m LE (65535 = khong co)  [8..9] minimum m LE (65535 = khong co)
 * Qua 3 giay khong co khung moi hoac mat BLE: bo so cu, bao mat tin hieu. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { XA_ST_WAIT = 0, XA_ST_MEASURE = 1, XA_ST_SLOW = 2, XA_ST_HOT = 3, XA_ST_NOIMG = 4 };
enum { XA_LV_NONE = 0, XA_LV_SAFE = 1, XA_LV_NEAR = 2, XA_LV_CLOSE = 3, XA_LV_NOGPS = 4 };
#define XA_NONE16 0xFFFFu
#define XA_NONE8 0xFFu
#define XA_STALE_MS 3000

typedef struct {
    uint8_t seq, state, level, speed;
    bool imperial, test;
    uint16_t dist, min;
} xadas_frame_t;

/* false: khung ngan qua hoac version != 1 (bo khung). Doc 10 byte dau, bo qua phan thua (tuong thich ve sau). */
bool xadas_parse(const uint8_t *b, size_t n, xadas_frame_t *f);

/* Hien gi tren chip thanh tren: chuoi + mau UI_XA_* (dashboard.h). live = co khung trong 3 s va con ket noi. */
void xadas_chip_text(const xadas_frame_t *f, bool live, char *out, size_t n, int *color);

/* Muc o canh bao (OBS_* trong obstacle_ui.h) tu 1 khung: chi state 1 co so: level 2 -> Chu y, level 3 -> Nguy hiem. */
int xadas_obs_level(const xadas_frame_t *f);

/* ble_link.c goi tu task host NimBLE moi khi xADAS ghi; xadas_link_down khi ngat BLE. */
void xadas_on_write(const uint8_t *b, size_t n);
void xadas_link_down(void);

#ifdef __cplusplus
}
#endif
