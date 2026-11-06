/* Loi tinh toan ADAS (C thuan, khong phu thuoc LVGL/ESP-IDF): uoc luong khoang cach xe phia truoc tu khung nhan dien
 * cua mot camera, loc xe cung lan, theo doi (toc do tiep can, TTC) va chia muc canh bao co tre (hysteresis).
 * Firmware dung qua adas_feed.c (esp/main). Khoang cach tu mot camera chi la uoc luong. */
#ifndef ADAS_H
#define ADAS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float f_px;          /* tieu cu theo diem anh (o do phan giai dang dung) */
    float cam_h_m;       /* chieu cao camera so voi mat duong (m) */
    float horizon_y;     /* hang diem anh cua duong chan troi (da hieu chuan, tinh ca goc nghieng camera) */
    float veh_w_m;       /* be rong xe gia dinh (m), mac dinh 1.8 */
    int img_w, img_h;
} adas_cam_t;

typedef struct {
    float x1, y1, x2, y2;    /* khung nhan dien (diem anh) */
    float score;
    int is_vehicle;          /* da loc cac lop xe (car/truck/bus...) */
    float w_m;               /* be rong that cua loai vat nay (m): o to 1,8, xe lon 2,5, xe hai banh 0,75...; <= 0: dung veh_w_m cua camera */
} adas_box_t;

/* Tieu cu (diem) tu goc nhin cheo (do) va kich thuoc anh */
float adas_focal_from_diag_fov(int w, int h, float diag_fov_deg);

/* Khoang cach theo mat phang duong: Z = f*H/(y_day - y_chan_troi). <= 0 neu khong hop le. */
float adas_dist_ground(const adas_cam_t *c, float y_bottom);
/* Khoang cach theo be rong khung: Z = f*W/w. <= 0 neu khong hop le. */
float adas_dist_width(const adas_cam_t *c, float w_px);
/* Nhu tren nhung voi be rong that w_m cua rieng vat do (<= 0: dung veh_w_m). */
float adas_dist_width_m(const adas_cam_t *c, float w_px, float w_m);
/* Ket hop 2 cach: nhat quan (lech < 35%) thi lay trung binh co trong so, lech nhieu thi tin cach theo be rong
 * (khong phu thuoc goc nghieng). Mot cach <= 0 thi dung cach con lai; ca hai <= 0 thi tra 0. */
float adas_fuse(float d_ground, float d_width);

/* Khung co nam trong "hanh lang" lan xe cua minh khong (hinh thang co dinh o giua anh, hep dan theo chan troi). */
bool adas_in_ego_lane(const adas_cam_t *c, const adas_box_t *b);

/* Chon xe gan nhat trong lan trong n khung; tra chi so, hoac -1 neu khong co. *dist nhan khoang cach uoc luong. */
int adas_pick_lead(const adas_cam_t *c, const adas_box_t *boxes, int n, float min_score, float *dist);

/* Theo doi: loc so bo khoang cach, toc do tiep can (m/s, am = dang lai gan), TTC (s, 0 neu khong tiep can). */
typedef struct {
    bool valid;
    float dist;
    float closing;           /* m/s; am = xe truoc gan lai */
    float ttc;               /* giay den luc cham neu giu nguyen toc do tiep can; 0 = khong tiep can */
    uint32_t last_ms;
    int level;               /* 0 xanh/khong co, 1 vang (<=25 m), 2 do (<=12 m), co hysteresis */
} adas_track_t;

void adas_track_reset(adas_track_t *t);
void adas_track_update(adas_track_t *t, float dist_m, uint32_t now_ms);   /* co khung hop le */
void adas_track_miss(adas_track_t *t, uint32_t now_ms, uint32_t max_age_ms);   /* khong thay xe: qua han thi xoa */

int adas_level(float dist_m, int prev_level);   /* 0/1/2 voi hysteresis 1.5 m */

#ifdef __cplusplus
}
#endif
#endif
