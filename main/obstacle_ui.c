/* Canh bao vat can - xem obstacle_ui.h. */
#include "obstacle_ui.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "dashboard.h"
#include "detect.h"
#include "ui_fonts.h"

extern const lv_image_dsc_t obs_ico_car28, obs_ico_moto28, obs_ico_person28;
extern const lv_image_dsc_t obs_ico_car64, obs_ico_moto64, obs_ico_person64;

/* Dai duoi: chi dung o man cho (khong co bien de thay cho) */
#define ST_X 14
#define ST_H 64
#define ST_W (UI_W - 2 * ST_X)
#define ST_Y (UI_H - 12 - ST_H)
/* O vat can (phuong an E): de len cho bien gioi han, bien nho o goc tren phai */
#define BX_W 200
#define MS 54                               /* duong kinh bien nho */
#define HOLD_MS 700                         /* het vat can: giu them chut cho khoi nhap nhay */
#define TICK_MS 50                          /* bam theo bien khi phan tren doi co (0,4 s); chop 2 Hz = 5 nhip */

static lv_obj_t *s_frame, *s_strip, *s_ibox, *s_icon, *s_name, *s_sub, *s_dlbl;
static lv_obj_t *s_box, *s_bicon, *s_bdist, *s_bsub, *s_ms, *s_mslbl;
static lv_obj_t *s_crit, *s_cicon, *s_ctitle, *s_cdist;
static lv_timer_t *s_tmr;
static int s_level, s_cls, s_dist, s_tick;
static float s_ttc;
static bool s_dim;
static lv_area_t s_last_a;                  /* vi tri bien lan ve truoc (bam theo khi doi co) */
static int s_last_lim;
static uint32_t s_beep_tick, s_off_tick, s_end_tick;
static bool s_city;

static void vis(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const lv_image_dsc_t *icon_for(int cls, bool big)
{
    if (cls == DET_CLS_PERSON) return big ? &obs_ico_person64 : &obs_ico_person28;
    if (cls == DET_CLS_TWO) return big ? &obs_ico_moto64 : &obs_ico_moto28;
    return big ? &obs_ico_car64 : &obs_ico_car28;
}

static const char *name_for(int cls)
{
    switch (cls) {
    case DET_CLS_PERSON: return "Người đi bộ";
    case DET_CLS_TWO: return "Xe máy phía trước";
    case DET_CLS_BIG: return "Xe lớn phía trước";
    default: return "Xe phía trước";
    }
}

static const char *short_for(int cls)
{
    switch (cls) {
    case DET_CLS_PERSON: return "Người đi bộ";
    case DET_CLS_TWO: return "Xe máy";
    case DET_CLS_BIG: return "Xe lớn";
    default: return "Xe phía trước";
    }
}

static void fmt_ttc(char *b, size_t n, float t)       /* "1,8 s" */
{
    int d = (int)(t * 10.0f + 0.5f);
    snprintf(b, n, "%d,%d s", d / 10, d % 10);
}

static lv_obj_t *box(lv_obj_t *p)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void set_txt(lv_obj_t *l, const char *t)          /* chi doi khi khac: tranh ve lai vo ich */
{
    if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t);
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, "");
    return l;
}

/* mau theo muc + che do: nen, vien, mau nhan (icon, so), chu */
static void colors(bool danger, bool night, lv_color_t *bg, lv_color_t *bd, lv_color_t *fg, lv_color_t *txt)
{
    if (danger && night) {
        *bg = lv_color_hex(0x2A0B10); *bd = lv_color_hex(0xFF4D4F); *fg = lv_color_hex(0xFF4D4F); *txt = lv_color_white();
    } else if (danger) {
        *bg = lv_color_hex(0xFFE0E4); *bd = lv_color_hex(0xBE123C); *fg = lv_color_hex(0xBE123C); *txt = lv_color_hex(0x14112B);
    } else if (night) {
        *bg = lv_color_hex(0x1A1606); *bd = lv_color_hex(0xFFD400); *fg = lv_color_hex(0xFFD400); *txt = lv_color_white();
    } else {
        *bg = lv_color_hex(0xFFF4C2); *bd = lv_color_hex(0xB45309); *fg = lv_color_hex(0xB45309); *txt = lv_color_hex(0x14112B);
    }
}

/* O vat can de len cho bien. Tra ve false neu man hien tai khong co bien (man cho) -> dung dai duoi. */
static bool paint_box(bool show, bool danger, bool night, const char *d, const char *t)
{
    lv_area_t a;
    int lim = 0;
    bool slot = ui_sign_slot(&a, &lim, show);
    s_last_a = a;
    s_last_lim = lim;
    if (!show || !slot) {
        vis(s_box, false);
        vis(s_ms, false);
        return slot;
    }
    int rs = a.y2 - a.y1 + 1, cx = (a.x1 + a.x2) / 2;
    int bh = rs + 4;
    if (bh < 120) bh = 120;
    int bw = BX_W;
    if (rs < 130 && a.x2 > UI_W - 20) bw = rs + 24;           /* so 3 chu so: bien nho, sat mep phai -> o hep */
    int x = cx - bw / 2;
    if (x + bw > UI_W - 14) x = UI_W - 14 - bw;
    if (x < UI_W / 2) x = UI_W / 2;
    int y = (a.y1 + a.y2) / 2 - bh / 2;
    if (y < 36) y = 36;
    lv_obj_set_pos(s_box, x, y);
    lv_obj_set_size(s_box, bw, bh);

    lv_color_t bg, bd, fg, txt;
    colors(danger, night, &bg, &bd, &fg, &txt);
    lv_obj_set_style_bg_color(s_box, bg, 0);
    lv_obj_set_style_border_color(s_box, bd, 0);
    lv_obj_set_style_opa(s_box, danger && s_dim ? 140 : LV_OPA_COVER, 0);
    lv_image_set_src(s_bicon, icon_for(s_cls, true));
    lv_obj_set_style_image_recolor(s_bicon, fg, 0);
    set_txt(s_bdist, d);
    lv_obj_set_style_text_color(s_bdist, fg, 0);
    char sub[40];
    if (bw < 170) snprintf(sub, sizeof sub, "%s", t[0] ? t : short_for(s_cls));
    else if (t[0]) snprintf(sub, sizeof sub, "Va chạm sau %s", t);
    else snprintf(sub, sizeof sub, "%s", short_for(s_cls));
    set_txt(s_bsub, sub);
    lv_obj_set_style_text_color(s_bsub, txt, 0);

    vis(s_ms, lim > 0);                                       /* bien nho, goc tren phai */
    if (lim > 0) {
        char b[8];
        snprintf(b, sizeof b, "%d", lim % 1000);
        set_txt(s_mslbl, b);
        lv_obj_set_style_transform_scale(s_mslbl, lim >= 100 ? 150 : 175, 0);
        lv_obj_center(s_mslbl);
        lv_obj_set_pos(s_ms, x + bw - MS + 8, y - 10);
        lv_obj_move_foreground(s_ms);
    }
    vis(s_box, true);
    lv_obj_move_foreground(s_box);
    if (lim > 0) lv_obj_move_foreground(s_ms);
    return true;
}

static void paint(void)
{
    bool night = ui_is_night();
    bool mid = s_level >= OBS_CAUTION && s_level < OBS_CRITICAL;
    bool danger = s_level == OBS_DANGER;

    char d[16], t[16], sub[40];
    snprintf(d, sizeof d, "%d m", s_dist);
    if (s_ttc > 0.0f) {
        fmt_ttc(t, sizeof t, s_ttc);
        snprintf(sub, sizeof sub, "Va chạm sau %s", t);
    } else {
        snprintf(sub, sizeof sub, "Giữ khoảng cách");
        t[0] = 0;
    }

    bool in_box = paint_box(mid, danger, night, d, t);
    bool show_strip = mid && !in_box;
    vis(s_strip, show_strip);
    vis(s_frame, danger);
    vis(s_crit, s_level == OBS_CRITICAL);

    if (show_strip) {
        lv_color_t bg, bd, fg, txt;
        if (danger && night) {                               /* dem: dai do tuoi / do sam chop */
            bg = lv_color_hex(s_dim ? 0x5A0C12 : 0xE11D2A);
            bd = bg;
            fg = txt = lv_color_white();
        } else if (danger) {
            bg = lv_color_hex(0xFFE0E4); bd = lv_color_hex(0xF5A3AB); fg = lv_color_hex(0xBE123C); txt = lv_color_hex(0x14112B);
        } else if (night) {
            bg = lv_color_hex(0x1A1606); bd = lv_color_hex(0x3A3108); fg = lv_color_hex(0xFFD400); txt = lv_color_white();
        } else {
            bg = lv_color_hex(0xFFF4C2); bd = lv_color_hex(0xF2D36B); fg = lv_color_hex(0xB45309); txt = lv_color_hex(0x14112B);
        }
        lv_obj_set_style_bg_color(s_strip, bg, 0);
        lv_obj_set_style_border_color(s_strip, bd, 0);
        lv_obj_set_style_opa(s_strip, (danger && !night && s_dim) ? 140 : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_ibox, night || danger ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_ibox, night || danger ? 64 : 180, 0);
        lv_image_set_src(s_icon, icon_for(s_cls, false));
        lv_obj_set_style_image_recolor(s_icon, fg, 0);
        lv_label_set_text(s_name, name_for(s_cls));
        lv_obj_set_style_text_color(s_name, txt, 0);
        lv_label_set_text(s_sub, sub);
        lv_obj_set_style_text_color(s_sub, txt, 0);
        lv_label_set_text(s_dlbl, d);
        lv_obj_set_style_text_color(s_dlbl, fg, 0);
        lv_obj_move_foreground(s_strip);
    }
    if (danger) {
        lv_obj_set_style_border_opa(s_frame, s_dim ? 64 : LV_OPA_COVER, 0);
        lv_obj_move_foreground(s_frame);
    }
    if (s_level == OBS_CRITICAL) {
        lv_obj_set_style_bg_color(s_crit, lv_color_hex(s_dim ? 0x7A0A12 : 0xE11D2A), 0);
        lv_image_set_src(s_cicon, icon_for(s_cls, true));
        char cd[40];
        if (t[0]) snprintf(cd, sizeof cd, "%d m · %s", s_dist, t);
        else snprintf(cd, sizeof cd, "%d m", s_dist);
        lv_label_set_text(s_cdist, cd);
        lv_obj_move_foreground(s_crit);
    }
}

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_level == OBS_NONE) return;
    if (++s_tick >= 250 / TICK_MS) {                         /* chop 2 Hz + nhac tieng */
        s_tick = 0;
        s_dim = !s_dim;
        uint32_t every = s_level == OBS_CRITICAL ? 1000 : (s_level == OBS_DANGER && !s_city) ? 4000 : 0;
        if (every && lv_tick_elaps(s_beep_tick) >= every) {
            s_beep_tick = lv_tick_get();
            board_beep(s_level == OBS_CRITICAL ? 3 : 2);
        }
        paint();
        return;
    }
    if (s_level == OBS_CRITICAL || lv_obj_has_flag(s_box, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;                                             /* giua 2 lan chop: chi ve lai khi bien doi cho / doi man */
    int lim = 0;
    if (!ui_sign_slot(&a, &lim, true) || lim != s_last_lim || a.x1 != s_last_a.x1 || a.y1 != s_last_a.y1 || a.x2 != s_last_a.x2 ||
        a.y2 != s_last_a.y2)
        paint();
}

void obstacle_ui_init(void)
{
    lv_obj_t *top = lv_layer_top();

    s_frame = box(top);
    lv_obj_set_size(s_frame, UI_W, UI_H);
    lv_obj_set_style_border_width(s_frame, 7, 0);
    lv_obj_set_style_border_color(s_frame, lv_color_hex(0xFF3D4F), 0);
    lv_obj_set_style_radius(s_frame, 4, 0);

    s_strip = box(top);
    lv_obj_set_pos(s_strip, ST_X, ST_Y);
    lv_obj_set_size(s_strip, ST_W, ST_H);
    lv_obj_set_style_radius(s_strip, 12, 0);
    lv_obj_set_style_bg_opa(s_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_strip, 1, 0);

    s_ibox = box(s_strip);
    lv_obj_set_size(s_ibox, 40, 40);
    lv_obj_set_pos(s_ibox, 13, (ST_H - 40) / 2);
    lv_obj_set_style_radius(s_ibox, 10, 0);
    s_icon = lv_image_create(s_ibox);
    lv_obj_center(s_icon);
    lv_obj_set_style_image_recolor_opa(s_icon, LV_OPA_COVER, 0);

    s_name = label(s_strip, &ui_font_txt20);
    lv_obj_set_pos(s_name, 66, 8);
    lv_obj_set_width(s_name, 250);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    s_sub = label(s_strip, &ui_font_txt14);
    lv_obj_set_pos(s_sub, 66, 36);
    s_dlbl = label(s_strip, &ui_font_dist30);
    lv_obj_align(s_dlbl, LV_ALIGN_RIGHT_MID, -14, 0);

    /* o vat can (E) */
    s_box = box(top);
    lv_obj_set_style_radius(s_box, 16, 0);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_box, 2, 0);
    lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_box, 2, 0);
    s_bicon = lv_image_create(s_box);
    lv_obj_set_style_image_recolor_opa(s_bicon, LV_OPA_COVER, 0);
    lv_image_set_scale(s_bicon, 150);                        /* 64 -> ~38 px */
    lv_obj_set_size(s_bicon, 40, 40);
    lv_image_set_inner_align(s_bicon, LV_IMAGE_ALIGN_CENTER);
    s_bdist = label(s_box, &ui_font_warn40);
    s_bsub = label(s_box, &ui_font_txt14);

    s_ms = box(top);                                         /* bien gioi han nho */
    lv_obj_set_size(s_ms, MS, MS);
    lv_obj_set_style_radius(s_ms, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ms, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ms, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_ms, 7, 0);
    lv_obj_set_style_border_color(s_ms, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_shadow_width(s_ms, 10, 0);
    lv_obj_set_style_shadow_opa(s_ms, 120, 0);
    lv_obj_set_style_shadow_color(s_ms, lv_color_black(), 0);
    s_mslbl = label(s_ms, &ui_font_r36);
    lv_obj_set_style_text_color(s_mslbl, lv_color_hex(0x111111), 0);
    lv_obj_set_style_transform_pivot_x(s_mslbl, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(s_mslbl, lv_pct(50), 0);

    s_crit = box(top);
    lv_obj_set_size(s_crit, UI_W, UI_H);
    lv_obj_set_style_bg_opa(s_crit, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_crit, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_crit, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_crit, 6, 0);
    s_cicon = lv_image_create(s_crit);
    lv_obj_set_style_image_recolor(s_cicon, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(s_cicon, LV_OPA_COVER, 0);
    s_ctitle = label(s_crit, &ui_font_warn64);
    lv_label_set_text(s_ctitle, "PHANH!");
    lv_obj_set_style_text_color(s_ctitle, lv_color_white(), 0);
    s_cdist = label(s_crit, &ui_font_warn40);
    lv_obj_set_style_text_color(s_cdist, lv_color_white(), 0);

    lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ms, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_crit, LV_OBJ_FLAG_HIDDEN);
    s_tmr = lv_timer_create(tick_cb, TICK_MS, NULL);
}

void obstacle_ui_set(int level, int cls, int dist_m, float ttc_s, bool city)
{
    s_city = city;
    if (!s_strip) return;
    if (ui_settings_shown()) level = OBS_NONE;               /* dang mo cai dat: khong de len */
    if (level == OBS_NONE && s_level != OBS_NONE) {          /* het vat can: giu them HOLD_MS */
        if (!s_off_tick) s_off_tick = lv_tick_get();
        if (lv_tick_elaps(s_off_tick) < HOLD_MS) return;
    } else {
        s_off_tick = 0;
    }
    if (level > s_level) {                                   /* len muc: Khan cap luon tit; Nguy hiem tit neu khong o pho / vua bao */
        bool again = s_end_tick && lv_tick_elaps(s_end_tick) < 5000;
        if (level >= OBS_CRITICAL) {
            s_beep_tick = lv_tick_get();
            board_beep(3);
        } else if (level == OBS_DANGER && !city && !again) {
            s_beep_tick = lv_tick_get();
            board_beep(2);
        }
    }
    if (level == OBS_NONE && s_level != OBS_NONE) s_end_tick = lv_tick_get();
    s_level = level;
    if (level != OBS_NONE) {
        s_cls = cls;
        s_dist = dist_m;
        s_ttc = ttc_s;
    }
    paint();
}
