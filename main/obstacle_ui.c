/* Canh bao vat can - xem obstacle_ui.h. */
#include "obstacle_ui.h"

#include <stdio.h>
#include "board.h"
#include "dashboard.h"
#include "detect.h"
#include "ui_fonts.h"

extern const lv_image_dsc_t obs_ico_car28, obs_ico_moto28, obs_ico_person28;
extern const lv_image_dsc_t obs_ico_car64, obs_ico_moto64, obs_ico_person64;

#define ST_X 14
#define ST_H 64
#define ST_W (UI_W - 2 * ST_X)
#define ST_Y (UI_H - 12 - ST_H)
#define HOLD_MS 700                 /* het vat can: giu them chut cho khoi nhap nhay */

static lv_obj_t *s_frame, *s_strip, *s_ibox, *s_icon, *s_name, *s_sub, *s_dist;
static lv_obj_t *s_crit, *s_cicon, *s_ctitle, *s_cdist;
static lv_timer_t *s_tmr;
static int s_level, s_cls, s_dist;
static float s_ttc;
static bool s_dim;
static uint32_t s_beep_tick, s_off_tick;

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

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, "");
    return l;
}

static void paint(void)
{
    bool night = ui_is_night();
    bool show_strip = s_level >= OBS_CAUTION && s_level < OBS_CRITICAL;
    bool danger = s_level == OBS_DANGER;

    vis(s_strip, show_strip);
    vis(s_frame, danger);
    vis(s_crit, s_level == OBS_CRITICAL);

    char d[16], t[16], sub[40];
    snprintf(d, sizeof d, "%d m", s_dist);
    if (s_ttc > 0.0f) {
        fmt_ttc(t, sizeof t, s_ttc);
        snprintf(sub, sizeof sub, "Va chạm sau %s", t);
    } else {
        snprintf(sub, sizeof sub, "Giữ khoảng cách");
        t[0] = 0;
    }

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
        lv_label_set_text(s_dist, d);
        lv_obj_set_style_text_color(s_dist, fg, 0);
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
    s_dim = !s_dim;
    uint32_t every = s_level == OBS_CRITICAL ? 1000 : s_level == OBS_DANGER ? 2500 : 0;
    if (every && lv_tick_elaps(s_beep_tick) >= every) {
        s_beep_tick = lv_tick_get();
        board_beep(s_level == OBS_CRITICAL ? 3 : 2);
    }
    if (s_level >= OBS_DANGER) paint();
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
    s_dist = label(s_strip, &ui_font_dist30);
    lv_obj_align(s_dist, LV_ALIGN_RIGHT_MID, -14, 0);

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
    lv_obj_add_flag(s_crit, LV_OBJ_FLAG_HIDDEN);
    s_tmr = lv_timer_create(tick_cb, 250, NULL);             /* chop 2 Hz */
}

void obstacle_ui_set(int level, int cls, int dist_m, float ttc_s)
{
    if (!s_strip) return;
    if (ui_settings_shown()) level = OBS_NONE;               /* dang mo cai dat: khong de len */
    if (level == OBS_NONE && s_level != OBS_NONE) {          /* het vat can: giu them HOLD_MS */
        if (!s_off_tick) s_off_tick = lv_tick_get();
        if (lv_tick_elaps(s_off_tick) < HOLD_MS) return;
    } else {
        s_off_tick = 0;
    }
    if (level > s_level) {                                   /* len muc: tit ngay */
        s_beep_tick = lv_tick_get();
        board_beep(level >= OBS_CRITICAL ? 3 : level);
    }
    s_level = level;
    if (level != OBS_NONE) {
        s_cls = cls;
        s_dist = dist_m;
        s_ttc = ttc_s;
    }
    paint();
}
