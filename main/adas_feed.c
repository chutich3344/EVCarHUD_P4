/* Noi ket qua nhan dien (detect.cpp) -> loi ADAS (adas.c) -> canh bao vat can (obstacle_ui.c).
 * Phan chon xe phia truoc / loc nhay so lay tu repo EVCarHUD/esp (adas_feed.c); muc canh bao tinh giong het trang giao lap
 * /mnt/project-files/layout-hud/vat-can/gia-lap-canh-bao-vat-can.html:
 *   khan cap : TTC < 1,5 s
 *   nguy hiem: gan hon 1/2 khoang cach 2 giay, hoac TTC < 2,7 s
 *   chu y    : gan hon khoang cach 2 giay (toc do / 3,6 x 2, toi thieu 10 m), hoac nguoi di bo trong lan < 30 m
 *   khong bao: xe minh < 5 km/h (ket xe, dung den do). Chua biet toc do (chua noi dien thoai): coi nhu 30 km/h. */
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

static const float k_width_m[DET_CLS_N] = {1.80f, 2.50f, 0.75f, 0.55f};
static const char *const k_name[DET_CLS_N] = {"o to", "xe lon", "xe 2 banh", "nguoi"};

static adas_track_t s_trk;
static int s_ttc_streak, s_jump_n, s_lead_cls;
static float s_jump_d;
static int64_t s_log_us;
static int s_shown_lv = -1, s_shown_m = -1, s_shown_t10 = -1;

static float jump_tol(float d)
{
    float t = d * 0.30f;
    return t < 2.0f ? 2.0f : t;
}

static int calc_level(int cls, float dist, float ttc)
{
    int v = ui_get_speed();
    if (v < 0) v = 30;
    if (v < 5) return OBS_NONE;
    float safe = (float)v / 3.6f * 2.0f;
    if (safe < 10.0f) safe = 10.0f;
    if (ttc > 0.0f && ttc < 1.5f) return OBS_CRITICAL;
    if (dist < safe * 0.5f || (ttc > 0.0f && ttc < 2.7f)) return OBS_DANGER;
    if (dist < safe || (cls == DET_CLS_PERSON && dist < 30.0f)) return OBS_CAUTION;
    return OBS_NONE;
}

static void show(int lv, int cls, int m, float ttc)
{
    int t10 = (int)(ttc * 10.0f + 0.5f);
    if (lv == s_shown_lv && m == s_shown_m && t10 == s_shown_t10) return;
    s_shown_lv = lv;
    s_shown_m = m;
    s_shown_t10 = t10;
    board_ui_lock();
    obstacle_ui_set(lv, cls, m, ttc);
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
        .horizon_y = (float)h * (float)CONFIG_EVCARHUD_ADAS_HORIZON_PCT / 100.0f,
        .veh_w_m = 1.8f,
        .img_w = w,
        .img_h = h,
    };
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

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    float d = 0.0f;
    int lead = n > 0 ? adas_pick_lead(&cam, ab, n, MIN_SCORE, &d) : -1;
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
    }

    int m = 0, lv = OBS_NONE;
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
        lv = calc_level(s_lead_cls, s_trk.dist, ttc);
    } else {
        s_ttc_streak = 0;
    }
    show(lv, s_lead_cls, m, ttc);

    int64_t us = esp_timer_get_time();
    if (us - s_log_us >= 3000000) {                            /* 3 giay mot dong: de hieu chuan goc nhin camera */
        s_log_us = us;
        if (lead >= 0) {
            ESP_LOGI(TAG, "phia truoc: %s, khung rong %d px (anh %d px) -> %.1f m, tiep can %.1f m/s, TTC %.1f s, muc %d (%d km/h)",
                     k_name[bx[lead].cls], bx[lead].x2 - bx[lead].x1, w, (double)d, (double)s_trk.closing, (double)ttc, lv,
                     ui_get_speed());
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
    show(OBS_NONE, 0, 0, 0.0f);
}

#else

void adas_feed(detect_box_t *bx, int n, int w, int h) { (void)bx; (void)n; (void)w; (void)h; }
void adas_feed_lost(void) {}

#endif
