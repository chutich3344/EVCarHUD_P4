/* Canh bao vat can tren HUD - phuong an D "Ket hop" (giao lap: /mnt/project-files/layout-hud/vat-can/):
 *   Chu y    : dai duoi 64 px (cho dai canh bao Waze) "Xe phia truoc / Xe may / Nguoi di bo" + so met + "Va cham sau x s"
 *   Nguy hiem: dai do chop 2 Hz + vien man hinh do chop, tit 2 tieng (nhac lai moi 2,5 s)
 *   Khan cap : ca man do "PHANH!" + so met + thoi gian va cham, tit 3 tieng moi giay
 * Ve tren lv_layer_top (de len giao dien HUD, uu tien hon canh bao Waze). Moi ham goi trong board_ui_lock. */
#pragma once

enum { OBS_NONE = 0, OBS_CAUTION = 1, OBS_DANGER = 2, OBS_CRITICAL = 3 };

void obstacle_ui_init(void);
/* level OBS_*; cls DET_CLS_* (detect.h); dist_m > 0; ttc_s > 0 neu dang lai gan (0 = khong) */
void obstacle_ui_set(int level, int cls, int dist_m, float ttc_s);
