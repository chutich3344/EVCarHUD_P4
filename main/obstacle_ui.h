/* Canh bao vat can tren HUD - phuong an E "O vat can" (giao lap: /mnt/project-files/layout-hud/vat-can/):
 *   Chu y    : o vang de len cho bien gioi han (bien thu nho vao goc tren phai): hinh xe + so met + "Va cham sau x s", IM LANG.
 *              Khong dung hang o re / dai canh bao Waze o duoi. Man cho (khong co bien): dai duoi 64 px.
 *   Nguy hiem: o do chop 2 Hz + vien man hinh do chop, tit 2 tieng (nhac lai toi da 4 s mot lan; pho dong: im;
 *              vua het canh bao < 5 s ma quay lai: khong tit lai)
 *   Khan cap : ca man do "PHANH!" + so met + thoi gian va cham, tit 3 tieng moi giay
 * Ve tren lv_layer_top (de len giao dien HUD). Moi ham goi trong board_ui_lock. */
#pragma once

enum { OBS_NONE = 0, OBS_CAUTION = 1, OBS_DANGER = 2, OBS_CRITICAL = 3 };

void obstacle_ui_init(void);
/* level OBS_*; cls DET_CLS_* (detect.h); dist_m > 0; ttc_s > 0 neu dang lai gan (0 = khong) */
/* city: xe minh dang bo trong pho (< 25 km/h): chi keu o muc Khan cap */
#include <stdbool.h>
void obstacle_ui_set(int level, int cls, int dist_m, float ttc_s, bool city);
