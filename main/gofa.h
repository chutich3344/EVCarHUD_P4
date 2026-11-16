/* GOFA HUD Platform (hud.platform.gofa.vn/docs): app Gofa ghi ban tin protobuf vao cac dac tinh cua dich vu 0x2A60.
 *   0x3030 bien bao   0x3031 toc do   0x3032 am thanh   0x3033 qua toc do   0x3038 dan duong   0x3039 dong bo gio
 * Tai lieu chi liet ke ten truong theo thu tu, khong ghi so truong: o day gia dinh so truong = thu tu 1, 2, 3...
 * File nay chi giai ma va goi ui_* / hlp_dash_* (phai goi trong khoa LVGL); khong dung FreeRTOS hay phan cung. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GOFA_CHR_SIGN = 0x3030, GOFA_CHR_SPEED = 0x3031, GOFA_CHR_SOUND = 0x3032,
    GOFA_CHR_OVER = 0x3033, GOFA_CHR_NAV = 0x3038, GOFA_CHR_TIME = 0x3039,
};

void gofa_connected(bool on);                         /* app Gofa noi / ngat Bluetooth */
/* 1 lan ghi cua app vao dac tinh `chr`. summary: mo ta ngan da giai ma (de hien o dai GOFA tren man hinh). */
void gofa_handle(uint16_t chr, const uint8_t *data, size_t len, char *summary, size_t summary_len);
void gofa_set_beep(void (*cb)(int times));            /* ban tin am thanh: phat tieng tit */

#ifdef __cplusplus
}
#endif
