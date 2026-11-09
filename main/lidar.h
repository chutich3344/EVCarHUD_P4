/* Lidar do khoang cach mot diem qua UART (cam vao header GPIO cua board P4), bo tro canh bao vat can bang camera.
 *
 * Ho tro (tu nhan biet, khong can chon):
 *   - Nooploop TOFSense-F2 / F2 P / F2 PH: khung NLink_TOFSense_Frame0 16 byte (57 00 ... tong kiem), mac dinh 921600 baud
 *   - Benewake TF02-Pro / TF03 / TFmini: khung 9 byte (59 59 ... tong kiem), mac dinh 115200 baud
 * Tu do toc do baud (921600, 115200, 460800, 230400, 256000) va tu thu dao chan RX/TX neu cam nguoc day.
 *
 * Noi day (F2 PH, day theo anh nha ban): do = 5V, den = GND, vang (TX cam bien) = GPIO RX, xanh duong (RX cam bien) = GPIO TX.
 * Muc tin hieu cam bien 3,3 V: cam thang vao P4, khong can mach chuyen muc. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LIDAR_OFF = 0, LIDAR_OK, LIDAR_LOST };   /* OFF: chua tung thay cam bien; LOST: da thay roi mat tin > 3 s */

void lidar_start(void);
/* Khoang cach hop le moi nhat (m, da loc trung vi 5 mau) neu doc duoc trong max_age_ms vua qua. false = khong co vat / mat tin. */
bool lidar_get(float *dist_m, uint32_t max_age_ms);
int lidar_state(void);
const char *lidar_model(void);                 /* "TOFSense" / "Benewake" / "" */

#ifdef __cplusplus
}
#endif
