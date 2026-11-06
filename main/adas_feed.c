/* Noi ket qua nhan dien (detect.cpp) -> loi ADAS (adas.c) -> canh bao vat can (obstacle_ui.c).
 * Phan chon xe phia truoc / loc nhay so lay tu repo EVCarHUD/esp (adas_feed.c); muc canh bao tinh giong het trang giao lap
 * /mnt/project-files/layout-hud/vat-can/gia-lap-canh-bao-vat-can.html:
 *   khan cap : TTC < 1,5 s
 *   nguy hiem: gan hon 1/2 khoang cach 2 giay, hoac TTC < 2,7 s
 *   chu y    : gan hon khoang cach 2 giay (toc do / 3,6 x 2, toi thieu 10 m), hoac nguoi di bo trong lan < 20 m
 *   khong bao: xe minh < 5 km/h (ket xe, dung den do). Chua biet toc do (chua noi dien thoai): coi nhu 30 km/h.
 * Chong keu pho dong (5..25 km/h): chi bao khi that su sap cham (xe < 4 m dang lai gan, nguoi trong lan < 8 m, TTC < 1,5 s).
 * Mot muc phai thay lien tiep 3 lan nhan dien (khan cap: 2) moi hien.
 * Chi xet vat trong "lan cua minh": hinh thang tu chan troi, nua be rong tai hang y = (y - chan_troi) x 1,6 m / do cao camera.
 * Chan troi tu chinh: tu moi xe con / xe lon ro rang giua anh: chan_troi = y_day - f x do_cao_camera / khoang_cach_theo_be_rong. */
#include "sdkconfig.h"
#include "adas_feed.h"

#if CONFIG_EVCARHUD_DETECT

#include <math.h>
#include <stdint.h>
#include "adas.h"
#include "board.h"
#include "dashboard.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "obstacle_ui.h"

static const char *TAG = "adas";

#define MIN_SCORE 0.30f
#define MAX_AGE_MS 1000
#define TTC_STREAK 3                    /* TTC chi tin khi dang lai gan lien tiep ngan nay lan do */
#define TTC_MIN_CLOSING 1.0f            /* ... va lai gan it nhat 1 m/s */
#define MIN_DIST_M 1.5f
#define JUMP_FRAMES 2
#define MAX_CLOSING 25.0f
#define CAM_H_M ((float)CONFIG_EVCARHUD_ADAS_CAM_H_CM / 100.0f)
#define LANE_HALF_M 1.6f              /* tam vat lech khoi giua lan it hon the nay moi tinh la cung lan */
#define CITY_KMH 25

static const float k_width_m[DET_CLS_N] = {1.80f, 2.50f, 0.75f, 0.55f};
static const char *const k_name[DET_CLS_N] = {"o to", "xe lon", "xe 2 banh", "nguoi"};

static adas_track_t s_trk;
static int s_ttc_streak, s_jump_n, s_lead_cls;
static float s_jump_d;
static int64_t s_log_us;
static int s_shown_lv = -1, s_shown_m = -1, s_shown_t10 = -1;
static float s_hor = -1.0f;                  /* chan troi uoc luong (hang diem anh) */
static int s_img_w = 320, s_img_h = 240;
static int s_cand = 0, s_cand_n = 0, s_level = 0;

float adas_feed_horizon(void) { return s_hor >= 0 ? s_hor : (float)s_img_h * CONFIG_EVCARHUD_ADAS_HORIZON_PCT / 100.0f; }
float adas_feed_lane_half(float y)
{
    float h = adas_feed_horizon();
    if (y <= h) return 0.0f;
    return (y - h) * LANE_HALF_M / CAM_H_M + (float)s_img_w * 0.02f;
}

static bool in_lane(const adas_box_t *b)
{
    float cx = 0.5f * (b->x1 + b->x2);
    float yb = b->y2 < (float)s_img_h ? b->y2 : (float)s_img_h;
    float half = adas_feed_lane_half(yb);
    return half > 0.0f && fabsf(cx - 0.5f * (float)s_img_w) <= half;
}

static float jump_tol(float d)
{
    float t = d * 0.30f;
    return t < 2.0f ? 2.0f : t;
}

static int calc_level(int cls, float dist, float ttc, float closing, bool *city)
{
    int v = ui_get_speed();
    if (v < 0) v = 30;
    *city = v < CITY_KMH;
    if (v < 5) return OBS_NONE;
    if (v < CITY_KMH) {                                        /* pho dong / bo ket xe: chi khi sap cham */
        if (ttc > 0.0f && ttc < 1.5f && dist < 12.0f) return OBS_CRITICAL;
        if ((cls != DET_CLS_PERSON && dist < 4.0f && closing < -1.0f) || (cls == DET_CLS_PERSON && dist < 8.0f)) return OBS_DANGER;
        return OBS_NONE;
    }
    float safe = (float)v / 3.6f * 2.0f;
    if (safe < 10.0f) safe = 10.0f;
    if (ttc > 0.0f && ttc < 1.5f) return OBS_CRITICAL;
    if (dist < safe * 0.5f || (ttc > 0.0f && ttc < 2.7f)) return OBS_DANGER;
    if (cls == DET_CLS_PERSON) return dist < 20.0f ? OBS_CAUTION : OBS_NONE;
    if (dist < safe) return OBS_CAUTION;
    return OBS_NONE;
}

static void show(int lv, int cls, int m, float ttc, bool city)
{
    int t10 = (int)(ttc * 10.0f + 0.5f);
    if (lv == s_shown_lv && m == s_shown_m && t10 == s_shown_t10) {
        board_ui_lock();
        obstacle_ui_touch();
        board_ui_unlock();
        return;
    }
    s_shown_lv = lv;
    s_shown_m = m;
    s_shown_t10 = t10;
    board_ui_lock();
    obstacle_ui_set(lv, cls, m, ttc, city);
    board_ui_unlock();
}

void adas_feed(detect_box_t *bx, int n, int w, int h)
{
    if (!bx || w <= 0 || h <= 0) return;
    if (n > DET_BOX_MAX) n = DET_BOX_MAX;
    if (n < 0) n = 0;

    adas_cam_t cam = {
        .f_px = (float)w * 0.5f / tanf((float)CONFIG_EVCARHUD_ADAS_HFOV_DEG * 0.5f * 3.14159265f / 180.0f),
        .cam_h_m = 0.0f,                                       /* chua hieu chuan do cao: chi do theo be rong khung */
        .horizon_y = 0.0f,
        .veh_w_m = 1.8f,
        .img_w = w,
        .img_h = h,
    };
    s_img_w = w;
    s_img_h = h;
    if (s_hor < 0) s_hor = (float)h * (float)CONFIG_EVCARHUD_ADAS_HORIZON_PCT / 100.0f;
    cam.horizon_y = s_hor;
    adas_box_t ab[DET_BOX_MAX];
    for (int i = 0; i < n; i++) {
        int c = bx[i].cls;
        ab[i].x1 = (float)bx[i].x1;
        ab[i].y1 = (float)bx[i].y1;
        ab[i].x2 = (float)bx[i].x2;
        ab[i].y2 = (float)bx[i].y2;
        ab[i].score = (float)bx[i].score / 100.0f;
        ab[i].is_vehicle = (c >= 0 && c < DET_CLS_N);
        ab[i].w_m = ab[i].is_vehicle ? k_width_m[c] : 0.0f;
        bx[i].dist_m = 0;
        if (ab[i].is_vehicle) {
            float dw = adas_dist_width_m(&cam, ab[i].x2 - ab[i].x1, ab[i].w_m);
            if (dw > 0.0f && dw < MIN_DIST_M) ab[i].is_vehicle = 0;
        }
    }

    /* nguoi ngoi tren xe may (Viet Nam: rat nhieu) khong phai nguoi di bo: khung nguoi chong len khung xe 2 banh
     * (phan chung >= 50% be ngang nguoi, chan nguoi nam trong khung xe) -> bo khung nguoi, xe 2 banh van duoc xet */
    for (int i = 0; i < n; i++) {
        if (bx[i].cls != DET_CLS_PERSON || !ab[i].is_vehicle) continue;
        float pw = ab[i].x2 - ab[i].x1;
        for (int j = 0; j < n && pw > 0.0f; j++) {
            if (bx[j].cls != DET_CLS_TWO) continue;
            float ov = fminf(ab[i].x2, ab[j].x2) - fmaxf(ab[i].x1, ab[j].x1);
            if (ov >= 0.5f * pw && ab[i].y2 > ab[j].y1 && ab[i].y2 <= ab[j].y2 + 0.15f * (ab[j].y2 - ab[j].y1)) {
                ab[i].is_vehicle = 0;
                break;
            }
        }
    }

#if CONFIG_EVCARHUD_ADAS_AUTO_HORIZON
    /* chan troi tu chinh (doc, xe chui dau, camera lap lech): moi xe con / xe lon ro rang, gan giua anh, khong bi cat day */
    for (int i = 0; i < n; i++) {
        int c = bx[i].cls;
        float bw = ab[i].x2 - ab[i].x1, cx = 0.5f * (ab[i].x1 + ab[i].x2);
        if ((c != DET_CLS_CAR && c != DET_CLS_BIG) || ab[i].score < 0.5f || bw < 14.0f || ab[i].y2 >= (float)h - 2.0f ||
            fabsf(cx - 0.5f * (float)w) > 0.3f * (float)w) continue;
        float zw = adas_dist_width_m(&cam, bw, ab[i].w_m);
        if (zw < 4.0f || zw > 60.0f) continue;
        float est = ab[i].y2 - cam.f_px * CAM_H_M / zw;
        s_hor += (est - s_hor) * 0.05f;
    }
    if (s_hor < 0.25f * (float)h) s_hor = 0.25f * (float)h;
    if (s_hor > 0.65f * (float)h) s_hor = 0.65f * (float)h;
#endif

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    float d = 0.0f;
    int lead = -1;
    {                                                          /* vat gan nhat trong lan minh (khoang cach theo be rong khung) */
        float bd = 1e9f;
        for (int i = 0; i < n; i++) {
            if (!ab[i].is_vehicle || ab[i].score < MIN_SCORE || !in_lane(&ab[i])) continue;
            /* khung cham mep trai / phai: vat moi lot vao nua chung, khung to dan khi no di vao -> tuong dang lao toi (TTC ao) */
            if ((ab[i].x1 <= 2.0f || ab[i].x2 >= (float)w - 3.0f) && ab[i].x2 - ab[i].x1 < 0.6f * (float)w) continue;
            float di = adas_dist_width_m(&cam, ab[i].x2 - ab[i].x1, ab[i].w_m);
            if (di > 0.0f && di < bd) {
                bd = di;
                lead = i;
            }
        }
        if (lead >= 0) d = bd;
    }
    bool used = false;
    if (lead < 0) {
        s_jump_n = 0;
        adas_track_miss(&s_trk, now, MAX_AGE_MS);
    } else if (!s_trk.valid || fabsf(d - s_trk.dist) <= jump_tol(s_trk.dist)) {
        s_jump_n = 0;
        adas_track_update(&s_trk, d, now);
        used = true;
    } else {
        s_jump_n = (s_jump_n > 0 && fabsf(d - s_jump_d) <= jump_tol(s_jump_d)) ? s_jump_n + 1 : 1;
        s_jump_d = d;
        if (s_jump_n >= JUMP_FRAMES) {
            adas_track_reset(&s_trk);
            adas_track_update(&s_trk, d, now);
            s_jump_n = 0;
            s_ttc_streak = 0;
            used = true;
        } else {
            adas_track_miss(&s_trk, now, MAX_AGE_MS);
        }
    }
    if (s_trk.valid) {
        if (s_trk.closing > MAX_CLOSING) s_trk.closing = MAX_CLOSING;
        if (s_trk.closing < -MAX_CLOSING) s_trk.closing = -MAX_CLOSING;
        /* vat phia truoc khong the lai gan nhanh hon toc do xe minh (+2 m/s cho xe truoc phanh / lui): khung nho ban dem
         * rung lam khoang cach nhay -> TTC ao (vd 29 m, 0,9 s khi dang bo 10 km/h). Chua biet toc do: coi nhu 30 km/h. */
        int v = ui_get_speed();
        float vmax = (float)(v >= 0 ? v : 30) / 3.6f + 2.0f;
        if (s_trk.closing < -vmax) {
            s_trk.closing = -vmax;
            s_trk.ttc = s_trk.dist / vmax;
        }
    }

    int m = 0, raw = OBS_NONE;
    bool city = false;
    float ttc = 0.0f;
    if (s_trk.valid) {
        m = (int)(s_trk.dist + 0.5f);
        if (m < 1) m = 1;
        if (used) {
            s_ttc_streak = (s_trk.ttc > 0.0f && s_trk.closing <= -TTC_MIN_CLOSING) ? s_ttc_streak + 1 : 0;
            bx[lead].dist_m = m;
            s_lead_cls = bx[lead].cls;
        }
        if (s_ttc_streak >= TTC_STREAK) ttc = s_trk.ttc;
        /* "nguoi" chay cung chieu gan bang toc do xe minh = nguoi ngoi xe may (ban dem mo hinh hay chi thay nguoi, khong thay xe);
         * nguoi di bo dung / bang qua duong thi lai gan dung bang toc do xe minh */
        int v = ui_get_speed();
        if (s_lead_cls == DET_CLS_PERSON && v >= 10 && s_trk.closing > -0.5f * (float)v / 3.6f) s_lead_cls = DET_CLS_TWO;
        raw = calc_level(s_lead_cls, s_trk.dist, ttc, s_trk.closing, &city);
    } else {
        s_ttc_streak = 0;
    }
    /* len muc phai thay lien tiep 3 lan (khan cap: 2); xuong muc: obstacle_ui giu them 0,7 s */
    if (raw == s_cand) s_cand_n++;
    else {
        s_cand = raw;
        s_cand_n = 1;
    }
    if (raw > s_level) {
        if (s_cand_n >= (raw >= OBS_CRITICAL && !city ? 2 : 3)) s_level = raw;    /* pho dong: Khan cap cung phai 3 lan */
    } else {
        s_level = raw;
    }
    int lv = s_level;
    show(lv, s_lead_cls, m, ttc, city);

    int64_t us = esp_timer_get_time();
    if (us - s_log_us >= 3000000) {                            /* 3 giay mot dong: de hieu chuan goc nhin camera */
        s_log_us = us;
        if (lead >= 0) {
            ESP_LOGI(TAG, "phia truoc: %s, khung rong %d px (anh %d px) -> %.1f m, tiep can %.1f m/s, TTC %.1f s, muc %d (%d km/h), chan troi %d%%",
                     k_name[bx[lead].cls], bx[lead].x2 - bx[lead].x1, w, (double)d, (double)s_trk.closing, (double)ttc, lv,
                     ui_get_speed(), (int)(s_hor * 100.0f / (float)h));
        } else {
            ESP_LOGI(TAG, "phia truoc: khong co vat nao trong lan (%d vat trong anh)", n);
        }
    }
}

void adas_feed_lost(void)
{
    adas_track_reset(&s_trk);
    s_ttc_streak = 0;
    s_jump_n = 0;
    s_level = s_cand = s_cand_n = 0;
    s_shown_lv = s_shown_m = s_shown_t10 = -1;
    board_ui_lock();
    obstacle_ui_clear();
    board_ui_unlock();
}

#else

void adas_feed(detect_box_t *bx, int n, int w, int h) { (void)bx; (void)n; (void)w; (void)h; }
void adas_feed_lost(void) {}
float adas_feed_horizon(void) { return 0.0f; }
float adas_feed_lane_half(float y) { (void)y; return 0.0f; }

#endif
