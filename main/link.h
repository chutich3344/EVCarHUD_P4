/* Chon duong truyen HLP dang dung. Waze Mod co the noi toi HUD bang Wi-Fi (WebSocket, hlp_server.c) hoac Bluetooth LE
 * (ble_link.c). Bo giai ma HLP (dashboard_hlp.c) chi co mot dau vao, nen file nay giu "duong dang dung":
 * duong nao noi truoc thi duoc dung; duong do rot ma duong kia dang noi thi chuyen sang duong kia.
 * Moi ham tu khoa LVGL (board_ui_lock): goi tu task cua tung duong truyen, KHONG goi khi dang giu khoa. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LINK_WS = 0, LINK_BLE = 1, LINK_N };
typedef void (*link_tx_fn)(const char *line);          /* gui 1 dong (khong co LF) len dien thoai; khong duoc chan lau */

void link_init(void);                                  /* goi mot lan sau dashboard_create() */
void link_up(int id, link_tx_fn tx);                   /* duong `id` vua noi */
void link_down(int id);                                /* duong `id` vua rot */
void link_rx(int id, const uint8_t *data, size_t len, bool whole_line);   /* byte nhan duoc; whole_line: tu them LF (WebSocket) */
int link_active(void);                                 /* LINK_WS / LINK_BLE, -1 = chua co duong nao */

#ifdef __cplusplus
}
#endif
