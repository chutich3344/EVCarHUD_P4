/* Noi ket qua nhan dien (detect.cpp) -> loi ADAS (adas.c) -> canh bao vat can tren HUD (obstacle_ui.c). */
#pragma once
#include "detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Goi sau MOI lan nhan dien (ke ca n = 0), tu task camera. bx[i].dist_m duoc ghi: > 0 o khung "xe phia truoc". */
void adas_feed(detect_box_t *bx, int n, int w, int h);
void adas_feed_lost(void);          /* camera bi rut / dung: xoa canh bao */

#ifdef __cplusplus
}
#endif
