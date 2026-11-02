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

/* Board khong co pin giu gio: luc bat may cho dong ho chay tu gio uoc tinh (gio da luu / gio build), dien thoai
 * gui `hi` thi chinh lai. Hook duoc goi moi phut khi gio da chinh theo dien thoai (de luu lai). */
void hlp_dash_set_clock_guess(int64_t unix_s, int tz_min);
void hlp_dash_set_clock_hook(void (*cb)(int64_t unix_s, int tz_min));

/* Nguon ngoai khong phai HLP (app GOFA, gofa.c): bat = da noi dien thoai + co tin hieu, khong het han sau 3 s */
void hlp_dash_external(bool on);
void hlp_dash_external_touch(void);
void hlp_dash_set_local_time(int hour, int minute, int second);   /* gio dia phuong, giu ngay dang co */

#ifdef __cplusplus
}
#endif
#endif
