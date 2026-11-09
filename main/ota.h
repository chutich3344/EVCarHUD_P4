/* Cap nhat firmware OTA (Cai dat > He thong > Cap nhat firmware). Hai kenh, cung ghi vao khe ota_0 / ota_1 dang khong chay:
 *  - May chu: Wi-Fi -> evcarhud.com/api/ota (ban moi nhat tai len o trang admin) -> tai file, cai, khoi dong lai.
 *  - Trinh duyet: HUD mo trang http://<IP HUD>/ trong mang Wi-Fi dang noi, chon file .bin tren dien thoai / may tinh.
 * Nhan ca file gop (nap 0x0, bo qua bootloader + bang phan vung) lan file app-0x10000; chi ghi phan firmware.
 * Ban moi khoi dong lan dau: 30 giay sau khi len man chinh moi xac nhan; sap truoc do thi bootloader quay ve ban cu. */
#pragma once
#include <stdbool.h>

void ota_start(void);                        /* goi sau khi dung bang dieu khien: xac nhan ban moi, gan hook giao dien */
void ota_wifi_changed(bool up, const char *ip);   /* wifi_link.c: co / mat mang (lan dau co mang: tu kiem tra ban moi) */
