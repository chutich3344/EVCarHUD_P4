#include "adas.h"
#include <math.h>

#define PI_F 3.14159265f

float adas_focal_from_diag_fov(int w, int h, float diag_fov_deg)
{
    if (w <= 0 || h <= 0 || diag_fov_deg <= 1.0f || diag_fov_deg >= 170.0f) return 0.0f;
    float diag = sqrtf((float)w * (float)w + (float)h * (float)h);
    return (diag / 2.0f) / tanf(diag_fov_deg * 0.5f * PI_F / 180.0f);
}

float adas_dist_ground(const adas_cam_t *c, float y_bottom)
{
    if (!c || c->f_px <= 0 || c->cam_h_m <= 0) return 0.0f;
    float dy = y_bottom - c->horizon_y;
    if (dy < 1.0f) return 0.0f;                         /* day khung ngang/tren chan troi: khong hop le */
    return c->f_px * c->cam_h_m / dy;
}

float adas_dist_width_m(const adas_cam_t *c, float w_px, float w_m)
{
    if (!c || c->f_px <= 0 || w_px < 2.0f) return 0.0f;
    float w = w_m > 0 ? w_m : (c->veh_w_m > 0 ? c->veh_w_m : 1.8f);
    return c->f_px * w / w_px;
}

float adas_dist_width(const adas_cam_t *c, float w_px) { return adas_dist_width_m(c, w_px, 0.0f); }

float adas_fuse(float dg, float dw)
{
    if (dg <= 0 && dw <= 0) return 0.0f;
    if (dg <= 0) return dw;
    if (dw <= 0) return dg;
    float lo = dg < dw ? dg : dw, hi = dg < dw ? dw : dg;
    if (hi / lo < 1.35f) return 0.5f * (dg + dw);       /* nhat quan */
    return dw;                                          /* lech nhieu: goc nghieng camera co the sai, tin be rong */
}

bool adas_in_ego_lane(const adas_cam_t *c, const adas_box_t *b)
{
    if (!c || !b || c->img_w <= 0) return false;
    float cx = 0.5f * (b->x1 + b->x2);
    float yb = b->y2;
    float top = c->horizon_y, bot = (float)c->img_h;
    if (yb <= top) return false;
    float t = (yb - top) / (bot - top);                 /* 0 o chan troi ... 1 o day anh */
    if (t > 1.0f) t = 1.0f;
    float half = (0.06f + 0.34f * t) * (float)c->img_w; /* nua be rong hanh lang: hep o xa, rong o gan */
    return fabsf(cx - 0.5f * (float)c->img_w) <= half;
}

int adas_pick_lead(const adas_cam_t *c, const adas_box_t *boxes, int n, float min_score, float *dist)
{
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        const adas_box_t *b = &boxes[i];
        if (!b->is_vehicle || b->score < min_score || !adas_in_ego_lane(c, b)) continue;
        float d = adas_fuse(adas_dist_ground(c, b->y2), adas_dist_width_m(c, b->x2 - b->x1, b->w_m));
        if (d > 0 && d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best >= 0 && dist) *dist = bd;
    return best;
}

int adas_level(float d, int prev)
{
    const float H = 1.5f;
    int lv = d > 25.0f ? 0 : (d > 12.0f ? 1 : 2);
    if (lv < prev) {                                    /* de ra khoi vung nguy hiem phai vuot them H met moi ha muc */
        if (prev == 2 && d <= 12.0f + H) lv = 2;
        else if (prev == 1 && d <= 25.0f + H) lv = 1;
    }
    return lv;
}

void adas_track_reset(adas_track_t *t)
{
    t->valid = false;
    t->dist = t->closing = t->ttc = 0.0f;
    t->last_ms = 0;
    t->level = 0;
}

void adas_track_update(adas_track_t *t, float d, uint32_t now_ms)
{
    if (d <= 0) return;
    if (!t->valid) {
        t->valid = true;
        t->dist = d;
        t->closing = 0.0f;
        t->ttc = 0.0f;
        t->last_ms = now_ms;
        t->level = adas_level(d, 0);
        return;
    }
    float dt = (float)(now_ms - t->last_ms) / 1000.0f;
    if (dt <= 0.0f) return;
    const float a = 0.5f, b = 0.3f;                     /* bo loc alpha-beta don gian */
    float pred = t->dist + t->closing * dt;
    float r = d - pred;
    t->dist = pred + a * r;
    t->closing += b * r / dt;
    t->last_ms = now_ms;
    t->ttc = (t->closing < -0.3f && t->dist > 0) ? t->dist / -t->closing : 0.0f;
    t->level = adas_level(t->dist, t->level);
}

void adas_track_miss(adas_track_t *t, uint32_t now_ms, uint32_t max_age_ms)
{
    if (t->valid && (uint32_t)(now_ms - t->last_ms) > max_age_ms) adas_track_reset(t);
}
