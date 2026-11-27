/* Camera USB (UVC) cam vao cong Type-C "USB OTG" cua board.
 * Nhan anh MJPEG 640x480 -> giai ma bang bo JPEG phan cung cua P4 -> nhan dien vat can -> canh bao tren HUD.
 * Cam vao: chi hien thong bao nho "Da nhan camera", KHONG tu mo khung xem truoc. Xem anh camera (de chinh goc lap):
 * Cai dat > Hien thi > "Xem camera" (khung xem truoc tu an sau 60 s, cham vao de an). */
#pragma once

#include "sdkconfig.h"
#if CONFIG_EVCARHUD_USB_CAM
void usb_cam_start(void);   /* goi sau khi giao dien da tao (can board_ui_lock) */
#else
static inline void usb_cam_start(void) {}
#endif
