/* Wi-Fi qua chip ESP32-C6 (esp_hosted + esp_wifi_remote): tu ket noi mang da luu, dong bo gio NTP, thoi tiet Open-Meteo.
 * Trang Wi-Fi tren man hinh goi vao day qua ui_set_wifi_hooks. Khong co Wi-Fi (firmware C6 cu) thi chi bao loi, khong treo. */
#pragma once
void wifi_link_start(void);
