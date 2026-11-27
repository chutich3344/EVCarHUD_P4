/* Nhan dien vat can tren board (ROADMAP_ADAS): chay mo hinh tu huan luyen (ESPDet-Pico 4 lop, anh vao 320x256, xem ml/train_evhud.py)
 * bang ESP-DL tren khung hinh camera. Mo hinh nam o vung flash "evdet" (file esp/models/evdet.espdl). Lop boc C cho ma C++ cua ESP-DL. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thu tu lop phai khop voi NAMES trong ml/prep_coco.py */
enum {
    DET_CLS_CAR = 0,         /* xe con (rong ~1,8 m) */
    DET_CLS_BIG = 1,         /* xe buyt, xe tai (rong ~2,5 m) */
    DET_CLS_TWO = 2,         /* xe may, xe dap (rong ~0,7 m) */
    DET_CLS_PERSON = 3,      /* nguoi di bo */
    DET_CLS_N = 4,
};

#define DET_BOX_MAX 16       /* so vat toi da moi khung hinh (duong dong xe may can nhieu hon 8) */

typedef struct {
    int x1, y1, x2, y2;      /* khung (diem anh tren anh dua vao) */
    int score;               /* 0..100 */
    int cls;                 /* DET_CLS_* */
    int dist_m;              /* > 0: khung nay duoc chon lam "xe phia truoc", khoang cach uoc luong (m) - do adas_feed() ghi; 0: khong */
} detect_box_t;

/* Chay nhan dien tren anh RGB565 do bo giai ma JPEG phan cung xuat ra (xem detect.cpp ve thu tu byte). Lan goi dau nap mo hinh nen lau hon.
 * Tra ve so khung tim duoc (0..max), hoac -1 neu tinh nang dang tat hay vung flash "evdet" chua co mo hinh. *ms = thoi gian chay (ms). */
int detect_run_rgb565(const uint8_t *rgb565, int w, int h, detect_box_t *out, int max, int *ms);

#ifdef __cplusplus
}
#endif
