/* Duong ket noi Bluetooth LE cho Waze Mod (HLP/1 qua GATT, cung ma dich vu voi ban P4).
 * Trong Waze Mod: HUD -> Transport = "Bluetooth LE GATT" (hoac Tu dong); HUD quang ba ten CONFIG_EVCARHUD_BLE_NAME. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ble_link_start(void);       /* tao task rieng, tra ve ngay */
int ble_link_state(void);        /* 0 chua len, 1 dang quang ba (cho dien thoai), 2 dien thoai dang noi, -1 loi */

#ifdef __cplusplus
}
#endif
