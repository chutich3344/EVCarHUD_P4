/* Camera USB (UVC) cam vao cong Type-C "USB OTG" cua board: ban thu nghiem.
 * Nhan anh MJPEG 640x480 -> giai ma bang bo JPEG phan cung cua P4 -> khung xem truoc 320x240 noi tren man hinh
 * kem so do: FPS camera, thoi gian giai ma, FPS hien thi. Day la buoc 1 truoc khi chay mo hinh nhan dien vat can.
 * Cham vao khung de an; rut camera ra cam lai de hien lai. */
#pragma once

#include "sdkconfig.h"
#if CONFIG_EVCARHUD_USB_CAM
void usb_cam_start(void);
#else
static inline void usb_cam_start(void) {}
#endif   /* goi sau khi giao dien da tao (can board_ui_lock) */
