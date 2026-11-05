/* Noi ket qua nhan dien (detect.cpp) -> loi ADAS (adas.c) -> canh bao vat can tren HUD (obstacle_ui.c). */
#pragma once
#include "detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Goi sau MOI lan nhan dien (ke ca n = 0), tu task camera. bx[i].dist_m duoc ghi: > 0 o khung "xe phia truoc". */
void adas_feed(detect_box_t *bx, int n, int w, int h);
void adas_feed_lost(void);          /* camera bi rut / dung: xoa canh bao */
float adas_feed_horizon(void);      /* chan troi dang dung (hang diem anh tren anh nhan dien) - de ve len anh xem truoc */
float adas_feed_lane_half(float y); /* nua be rong vung lan minh tai hang y (diem anh), 0 neu tren chan troi */

#ifdef __cplusplus
}
#endif
