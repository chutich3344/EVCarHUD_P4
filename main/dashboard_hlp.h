/* Noi HLP/1 (Waze HUD Link) vao giao dien: nhan byte tu transport (WebSocket/BLE/UART...),
 * tach dong, giai ma, goi ui_set_*. Khong phu thuoc loai transport. */
#ifndef DASHBOARD_HLP_H
#define DASHBOARD_HLP_H

#include "dashboard.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gui 1 dong len dien thoai. `line` KHONG co LF: transport tu them '\n' (hoac 1 WebSocket text message). */
typedef void (*hlp_tx_cb_t)(const char *line, void *user);

void hlp_dash_init(hlp_tx_cb_t tx, void *user);   /* goi sau dashboard_create(); tx co the la NULL */
void hlp_dash_on_connect(void);                   /* transport vua noi: gui `dev` xin truong du lieu */
void hlp_dash_on_disconnect(void);                /* transport mat: hien NO SIGNAL */
void hlp_dash_feed(const uint8_t *data, size_t len);   /* byte nhan duoc, chunk bat ky */

/* Dong ho: khong doan gio. Chua co nguon nao thi man cho hien "--:--". Nguon (UI_TSRC_*): RTC (module DS3231 hoac dong ho
 * trong chip sau khi ngu sau) < dien thoai (`hi` cua Waze Mod) < NTP qua Wi-Fi; nguon thap khong de len nguon cao.
 * Hook duoc goi moi lan nhan gio day du (co ngay): main.c dat gio he thong, ghi DS3231, luu mui gio. */
void hlp_dash_set_clock(int64_t unix_s, int tz_min, int src);
void hlp_dash_set_clock_hook(void (*cb)(int64_t unix_s, int tz_min, int src));
int hlp_dash_clock_source(void);
bool hlp_dash_now(int64_t *unix_s, int *tz_min);          /* false: chua co gio day du */

/* Nguon ngoai khong phai HLP (app GOFA, gofa.c): bat = da noi dien thoai + co tin hieu, khong het han sau 3 s */
void hlp_dash_external(bool on);
void hlp_dash_external_touch(void);
const char *hlp_dev_line(void);                    /* dong `dev` xin du truong (gui cho Waze Mod) */
void hlp_dash_set_local_time(int hour, int minute, int second);   /* gio dia phuong, giu ngay dang co */

#ifdef __cplusplus
}
#endif
#endif
