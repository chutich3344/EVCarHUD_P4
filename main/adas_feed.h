/* Noi ket qua nhan dien (detect.cpp) -> loi ADAS (adas.c) -> canh bao vat can tren HUD (obstacle_ui.c). */
#pragma once
#include <stdbool.h>
#include "detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Goi sau MOI lan nhan dien (ke ca n = 0), tu task camera. bx[i].dist_m duoc ghi: > 0 o khung "xe phia truoc". */
void adas_feed(detect_box_t *bx, int n, int w, int h);
void adas_feed_init(void);          /* goi mot lan luc khoi dong, truoc usb_cam_start / lidar_start */
void adas_feed_lost(void);          /* camera bi rut / dung: xoa canh bao */
void adas_feed_lidar(void);         /* task Lidar goi moi 100 ms: camera khong chay thi canh bao chi bang Lidar */
bool adas_feed_calibrated(void);    /* chan troi tu chinh da on dinh (chip CAM tren thanh tren: xanh) */
int adas_feed_legal_m(int kmh);     /* khoang cach an toan toi thieu theo Thong tu 38/2024 (m), 0 = duoi 55 km/h luat khong ghi so */
float adas_feed_horizon(void);      /* chan troi dang dung (hang diem anh tren anh nhan dien) - de ve len anh xem truoc */
float adas_feed_lane_half(float y); /* nua be rong vung lan minh tai hang y (diem anh), 0 neu tren chan troi */

#ifdef __cplusplus
}
#endif
