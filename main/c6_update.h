#pragma once
#include <stdbool.h>
/* Neu khe ota_1 co anh firmware chip C6 (nap kem qua USB): gui cho C6 qua SDIO roi khoi dong lai P4.
 * Goi sau khi esp_hosted da noi voi C6 (sau esp_wifi_init). Tra ve false khi khong co gi de nap hoac nap loi. */
bool c6_update_check(void);
