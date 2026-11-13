/* Gui log cua HUD len may chu (evcarhud.com/api/log) de doc tu xa, khoi chup man hinh console.
 * cloud_log_init() goi dau app_main (bat ca log khoi dong). Log gom vao vong dem PSRAM 64 KB,
 * co Wi-Fi thi moi ~60 s gui phan moi; gap dong loi (E) thi gui sau ~5 s. */
#pragma once
#include <stdbool.h>
void cloud_log_init(void);
void cloud_log_wifi(bool up);
