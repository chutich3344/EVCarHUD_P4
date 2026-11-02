/* EVCarHUD - giao dien cho man hinh 2.8" ngang 320x240 (LVGL 9). Ba man, tu chon theo du lieu tu dien thoai:
 *
 *  Man cho (chua co du lieu, hoac dung yen > 30 s ma khong dan duong): dong ho lat / dong ho dien tu.
 *
 *  Dan duong (phuong an A "Mui ten lon"):         Chay thuong (phuong an C "Vien mau trang thai"):
 *  +-------------------------+-----------+        +=========================================+
 *  |  [MUI TEN TO]     [tiep]|    52     |        | (o) Nguyen Huu Tho                17:32 |
 *  |                         |   km/h    |        |-----------------------------------------|
 *  |  183 m                  |           |        |   52  km/h                     ( 60 )   |
 *  |  ======-----            |  ( 60 )   |        |                                         |
 *  |  Bach Thanh Phong       |   17:52   |        | [cam] 200 m  =========-------           |  <- dai vang khi co canh bao
 *  +-------------------------+-----------+        +=========================================+
 *  [cam] 200 m ==========------  <- dai vang       vien man hinh: xanh = on, vang = canh bao, do = qua toc do
 *
 * It chu: chi so + bieu tuong. Mau co nghia: xanh = on, vang = sap toi camera / canh bao, do = qua toc do
 * (dan duong: so do + vien do nhap nhay; chay thuong: nen do). Thanh duoi khoang cach ngan dan khi toi cho re,
 * thanh trong dai vang ngan dan khi toi canh bao. Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem.
 * Giu tay 5 giay: cai dat (Am thanh / Ket noi). Bam BOOT 2 lan: xoay 180 do. */
#include "dashboard.h"
#include "dashboard_signs.h"
#include "ui_fonts.h"
#include <stdio.h>
#include <string.h>

#define M        10                         /* le man hinh (man cho) */
#define R_CARD   10
#define NEAR_KMH 5
#define MOVE_KMH 3                          /* >= 3 km/h: dang chay */
#define STOP_MS  30000                      /* dung yen 30 s (khong dan duong): ve man cho */

/* Dan duong (A) */
#define A_PAD    14
#define A_SPLIT  204                        /* vach chia: trai chi duong, phai toc do */
#define A_RX     (A_SPLIT + 2)
#define A_RW     (UI_W - A_RX)
#define A_ARROW  115                        /* mui ten re 64 px -> 115 px */
#define A_BAR_W  176
/* Chay thuong (C) */
#define C_BD     5                          /* vien trang thai */
#define C_PAD    14
#define C_TOP_H  48
/* Dai canh bao (ca hai man) */
#define BAND_H   44

typedef struct {
    lv_color_t bg, card, border, fg, mut;
    lv_color_t accent, arrow, track, divider, ok, warn, over, over_bg, band, band_fg;
    lv_color_t flip_top, flip_top2, flip_bot, flip_line;
    lv_opa_t shadow;
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_DAY = {
    .bg = C(0xF3F3EF), .card = C(0xFFFFFF), .border = C(0xE4E4DF), .fg = C(0x0B1220), .mut = C(0x5B6B7B),
    .accent = C(0x0E7490), .arrow = C(0x0B74D0), .track = C(0xDCE2E8), .divider = C(0xD9DDE2),
    .ok = C(0x16A34A), .warn = C(0xD97706), .over = C(0xDC2626), .over_bg = C(0xFDE2E2),
    .band = C(0xF5B400), .band_fg = C(0x111111),
    .flip_top = C(0xFFFFFF), .flip_top2 = C(0xFFFFFF), .flip_bot = C(0xF1F0EC), .flip_line = C(0xDCDCDA),
    .shadow = 18,
};
static const theme_t TH_NIGHT = {
    .bg = C(0x05080C), .card = C(0x111827), .border = C(0x1F2937), .fg = C(0xFFFFFF), .mut = C(0x8AA0B4),
    .accent = C(0x22D3EE), .arrow = C(0x2EA8FF), .track = C(0x1C2733), .divider = C(0x1C2733),
    .ok = C(0x22D37A), .warn = C(0xFFB020), .over = C(0xFF3B3B), .over_bg = C(0x4A0909),
    .band = C(0xE8A400), .band_fg = C(0x111111),
    .flip_top = C(0x1E293B), .flip_top2 = C(0x0F172A), .flip_bot = C(0x0C111D), .flip_line = C(0x020408),
    .shadow = 0,
};

static const char *const MODE_NAME[3] = {"Tự động", "Ban ngày", "Ban đêm"};
static const char *const WDAY_NAME[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
enum { SCR_STANDBY = 0, SCR_CRUISE, SCR_NAV };
static const theme_t *th = &TH_DAY;

/* --- doi tuong --- */
static lv_obj_t *scr;
/* dan duong (A) */
static lv_obj_t *nav, *a_arrow, *a_exit, *a_next, *a_dist, *a_dunit, *a_bar, *a_bar_fill, *a_street, *a_div;
static lv_obj_t *a_eta, *a_eta2;
/* chay thuong (C) */
static lv_obj_t *cru, *c_border, *c_pin, *c_street, *c_clock, *c_line;
/* dung chung: toc do, bien bao, dai canh bao */
static lv_obj_t *lbl_speed, *lbl_unit;
static lv_obj_t *sign_ring, *lbl_limit, *img_sign;
static lv_obj_t *band, *band_img, *band_dist, *band_unit, *band_bar, *band_fill;
/* man cho */
static lv_obj_t *stby, *lbl_sb_brand, *lbl_sb_stat, *flip[2], *flip_bot[2], *flip_line[2], *lbl_clk[2];
static lv_obj_t *sb_bar, *lbl_date, *lbl_gps;
static lv_obj_t *dg_h, *dg_c, *dg_m;        /* dong ho dien tu 24 gio (chua ket noi dien thoai) */
static lv_timer_t *colon_tmr;
static lv_obj_t *frame, *toast, *lbl_toast;
static lv_timer_t *blink_tmr, *toast_tmr;

/* --- trang thai --- */
static int speed = -2, limit = -1, level = -1;
static bool nav_on, has_route;
static int clk_h = -1, clk_m = -1, day = -1, month = -1, wday = -1;
static int screen = -1;
static bool sig_ok, phone, waze = true, moving, clk_synced;
static uint32_t stop_tick;                  /* 0: khong dung yen */
static ui_alert_t al_list[4];
static int al_n;
static const ui_alert_t *ev_now;            /* canh bao (su kien) gan nhat, NULL = khong co */
static const ui_alert_t *sign_now;          /* bien bao tren duong gan nhat */
static int ev_code, ev_start;               /* dai vang: khoang cach luc bat dau thay canh bao */
static int turn_now = -1, dist_now = -1, turn_start;   /* thanh tien do toi cho re */
static char eta_txt[8], street_now[64], street_nav[64];
static int eta_rm, eta_rmin;
static int mode = UI_MODE_AUTO, ambient = -1;
static bool night;
static void (*mode_cb)(int);
static void (*night_cb)(bool);
static void (*level_cb)(int);
static void (*rotate_cb)(void);
/* loa */
static bool snd_on = true;
static int snd_vol = 70;
static bool turn_beeped;                    /* da tit bao con 100 m toi cho re */
static void (*beep_cb)(int);
static void (*sound_cb)(bool, int);
static lv_timer_t *over_tmr;                /* nhac lai 2 tieng tit khi van vuot toc do */
/* cham man hinh: giu 1 giay roi tha = xoay, giu 5 giay = cai dat */
static uint32_t press_tick;
static bool hold_used;
/* man cai dat am luong */
static lv_obj_t *set_scr, *set_toggle, *set_spk_cone, *set_arc[2], *set_x[2], *lbl_set_onoff;
static lv_obj_t *set_minus, *set_plus, *lbl_set_vol, *vol_bar, *vol_bar_fill;
static lv_timer_t *set_tmr;
/* trang Ket noi: chon nguon du lieu Waze Mod / GOFA (doi nguon: luu roi cho bam RST) */
static lv_obj_t *set_tab[2], *set_tab_line[2], *lbl_tab[2], *page_snd, *page_src, *src_card[2], *lbl_src_st[2];
static lv_obj_t *rb_scr, *rb_tile, *lbl_rb_title, *lbl_rb_cnt;
static int set_page, src = UI_SRC_WAZE, rb_src;
static void (*source_cb)(int);
/* nut BOOT trong cai dat: bam 1 lan = sang muc ke, bam 2 lan = chon (cam ung dien tro kho bam) */
static int set_focus = -1;
static lv_obj_t *lbl_mac;                   /* MAC Bluetooth: de dang ky tren hud.platform.gofa.vn */
static char mac_txt[24];
static lv_obj_t *gofa_dbg, *lbl_gofa_dbg[2];
static lv_timer_t *gofa_tmr;                /* an dai GOFA sau 6 giay khong co goi moi */   /* che do GOFA: goi Bluetooth vua nhan (doan giao thuc) */                  /* -1 = chua dung nut BOOT: khong ve khung chon */

/* ======================= tien ich ======================= */
static lv_obj_t *mk_box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *mk_fill(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = mk_box(p, x, y, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *mk_card(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = mk_fill(p, x, y, w, h, r);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mk_label_w(lv_obj_t *p, const lv_font_t *f, int x, int y, int w, lv_text_align_t al)
{
    lv_obj_t *l = mk_label(p, f, x, y, "");
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static lv_obj_t *mk_image(lv_obj_t *p, int x, int y, int scale)
{
    lv_obj_t *im = lv_image_create(p);
    lv_obj_set_pos(im, x, y);
    lv_image_set_pivot(im, 0, 0);
    lv_image_set_scale(im, scale);
    return im;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool shown(lv_obj_t *o) { return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

/* khoang cach tu dinh label toi chan chu */
static int baseline(const lv_font_t *f) { return f->line_height - f->base_line; }

static int text_w(const char *s, const lv_font_t *f) { return lv_text_get_width(s, strlen(s), f, 0); }

/* 183 -> "180" "m", 1240 -> "1,2" "km", 12400 -> "12" "km" */
static void fmt_dist2(char *num, size_t nc, const char **unit, int m)
{
    if (m < 0) { num[0] = 0; *unit = ""; }
    else if (m < 1000) { snprintf(num, nc, "%d", m < 50 ? m : (m + 5) / 10 * 10); *unit = "m"; }
    else if (m < 10000) { snprintf(num, nc, "%d,%d", m / 1000, (m % 1000 + 50) / 100 % 10); *unit = "km"; }
    else { snprintf(num, nc, "%d", (m + 500) / 1000); *unit = "km"; }
}

/* Bien bao tren duong (hien canh bien gioi han) khac su kien (hien o dai vang) */
static bool is_road_sign(int code)
{
    switch (code) {
    case 7: case 9: case 10: case 11: case 12: case 37: case 38: case 56: case 57: case 59: case 61:
        return true;
    default:
        return (code >= 22 && code <= 34) || (code >= 65 && code <= 75);
    }
}

/* 0..1000: phan da di (thanh tien do) */
static int progress(int start, int now)
{
    if (start <= 0 || now < 0) return 0;
    if (now >= start) return 0;
    return (start - now) * 1000 / start;
}

static void set_bar(lv_obj_t *track, lv_obj_t *fill, int permille)
{
    int w = lv_obj_get_width(track);
    int fw = w * permille / 1000;
    if (fw < lv_obj_get_height(track)) fw = permille > 0 ? lv_obj_get_height(track) : 0;
    lv_obj_set_width(fill, fw);
    show(fill, fw > 0);
}

static void beep(int times)
{
    if (snd_on && beep_cb) beep_cb(times);
}

static void render_all(void);
static void settings_render(void);
static void render_standby(void);

/* ======================= mau sac ======================= */
static void apply_theme(void)
{
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *cards[] = {sb_bar};
    for (size_t i = 0; i < sizeof cards / sizeof cards[0]; i++) {
        lv_obj_set_style_bg_color(cards[i], th->card, 0);
        lv_obj_set_style_border_color(cards[i], th->border, 0);
        lv_obj_set_style_shadow_width(cards[i], th->shadow ? 8 : 0, 0);
        lv_obj_set_style_shadow_offset_y(cards[i], 2, 0);
        lv_obj_set_style_shadow_color(cards[i], lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(cards[i], th->shadow, 0);
    }
    lv_obj_t *fg[] = {a_dist, a_street, a_eta, a_exit, c_street, c_clock, lbl_sb_brand, lbl_clk[0], lbl_date, dg_h};
    for (size_t i = 0; i < sizeof fg / sizeof fg[0]; i++) lv_obj_set_style_text_color(fg[i], th->fg, 0);
    lv_obj_t *mut[] = {a_dunit, a_eta2, lbl_unit};
    for (size_t i = 0; i < sizeof mut / sizeof mut[0]; i++) lv_obj_set_style_text_color(mut[i], th->mut, 0);
    lv_obj_t *acc[] = {lbl_clk[1], lbl_gps, dg_c, dg_m};
    for (size_t i = 0; i < sizeof acc / sizeof acc[0]; i++) lv_obj_set_style_text_color(acc[i], th->accent, 0);

    lv_obj_set_style_image_recolor(a_arrow, th->arrow, 0);
    lv_obj_set_style_image_recolor(a_next, th->mut, 0);
    lv_obj_set_style_image_recolor(c_pin, th->arrow, 0);
    lv_obj_set_style_bg_color(a_bar, th->track, 0);
    lv_obj_set_style_bg_color(a_bar_fill, th->arrow, 0);
    lv_obj_set_style_bg_color(a_div, th->divider, 0);
    lv_obj_set_style_bg_color(c_line, th->divider, 0);
    lv_obj_set_style_bg_color(band, th->band, 0);
    lv_obj_set_style_text_color(band_dist, th->band_fg, 0);
    lv_obj_set_style_text_color(band_unit, th->band_fg, 0);
    lv_obj_set_style_bg_color(band_fill, th->band_fg, 0);
    lv_obj_set_style_bg_color(band_bar, th->band_fg, 0);

    lv_obj_set_style_bg_color(stby, th->bg, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(flip[i], th->flip_top, 0);
        lv_obj_set_style_bg_grad_color(flip[i], th->flip_top2, 0);
        lv_obj_set_style_border_color(flip[i], th->border, 0);
        lv_obj_set_style_shadow_opa(flip[i], th->shadow, 0);
        lv_obj_set_style_bg_color(flip_bot[i], th->flip_bot, 0);
        lv_obj_set_style_bg_color(flip_line[i], th->flip_line, 0);
    }
    lv_obj_set_style_bg_color(toast, th->fg, 0);
    lv_obj_set_style_text_color(lbl_toast, th->bg, 0);
    lv_obj_set_style_border_color(frame, th->over, 0);
    if (set_scr && shown(set_scr)) settings_render();
    render_all();
    render_standby();
}

static void update_night(void)
{
    bool n;
    if (mode == UI_MODE_DAY) n = false;
    else if (mode == UI_MODE_NIGHT) n = true;
    else if (ambient >= 0) n = ambient != 0;
    else n = clk_h >= 0 && (clk_h < 6 || clk_h >= 18);            /* chua co gio: de ban ngay */
    const theme_t *want = n ? &TH_NIGHT : &TH_DAY;
    if (want == th) return;
    night = n;
    th = want;
    apply_theme();
    if (night_cb) night_cb(night);
}

/* ======================= ve tung phan ======================= */
/* So toc do: dan duong o cot phai (72 px), chay thuong ben trai (110 px); 3 chu so thi nho lai. */
static void render_speed(bool band_on)
{
    char b[12];
    if (speed < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", speed % 1000);
    lv_label_set_text(lbl_speed, b);
    lv_color_t c;
    if (speed < 0) c = th->mut;
    else if (level == UI_LEVEL_OVER) c = (screen == SCR_CRUISE && night) ? th->fg : th->over;   /* chay thuong: nen da do */
    else if (level == UI_LEVEL_NEAR && screen == SCR_NAV) c = th->warn;
    else c = th->fg;
    lv_obj_set_style_text_color(lbl_speed, c, 0);
    lv_obj_set_style_text_color(lbl_unit, level == UI_LEVEL_OVER && screen == SCR_CRUISE ? c : th->mut, 0);

    if (screen == SCR_NAV) {
        const lv_font_t *f = speed >= 100 ? &ui_font_speed62 : &ui_font_speed72;
        lv_obj_set_style_text_font(lbl_speed, f, 0);
        lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_speed, A_RW);
        lv_obj_set_pos(lbl_speed, A_RX, 14 + baseline(&ui_font_speed72) - baseline(f));
        lv_obj_set_style_text_align(lbl_unit, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_unit, A_RW);
        lv_obj_set_pos(lbl_unit, A_RX, 14 + baseline(&ui_font_speed72) + 6);
    } else {
        const lv_font_t *f = speed >= 100 ? &ui_font_speed88 : &ui_font_speed110;
        int y0 = C_BD + C_TOP_H + 8, y1 = band_on ? UI_H - C_BD - BAND_H - 8 : UI_H - C_BD - 12;
        int y = (y0 + y1) / 2 - baseline(f) / 2;
        lv_obj_set_style_text_font(lbl_speed, f, 0);
        lv_obj_set_style_text_align(lbl_speed, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(lbl_speed, LV_SIZE_CONTENT);
        lv_obj_set_pos(lbl_speed, C_BD + C_PAD - 2, y);
        lv_obj_set_style_text_align(lbl_unit, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(lbl_unit, LV_SIZE_CONTENT);
        lv_obj_set_pos(lbl_unit, C_BD + C_PAD - 2 + text_w(b, f) + 6, y + baseline(f) - baseline(&ui_font_txt14));
    }
}

/* Bien gioi han (luon co, "-" khi chua biet) + bien tren duong gan nhat neu co.
 * row: hai bien nam ngang (dan duong), khong thi xep doc (chay thuong). */
static void render_signs(int x, int y, int w, int h, bool row)
{
    bool two = sign_now != NULL;
    int R, S = 0;
    if (!two) {
        R = w < h ? w : h;
    } else if (row) {
        R = (w - 8) / 2;
        if (R > h) R = h;
        S = R - 4;
    } else {
        R = h * 56 / 100;
        if (R > w) R = w;
        S = h - R - 8;
        if (S > w * 7 / 10) S = w * 7 / 10;
    }
    lv_obj_set_size(sign_ring, R, R);
    lv_obj_set_style_border_width(sign_ring, R >= 60 ? R / 10 : 5, 0);
    int rx, ry, sx = 0, sy = 0;
    if (!two) { rx = x + (w - R) / 2; ry = y + (h - R) / 2; }
    else if (row) { rx = x + (w - R - 8 - S) / 2; ry = y + (h - R) / 2; sx = rx + R + 8; sy = y + (h - S) / 2; }
    else { int t = R + 8 + S; ry = y + (h - t) / 2; rx = x + (w - R) / 2; sy = ry + R + 8; sx = x + (w - S) / 2; }
    lv_obj_set_pos(sign_ring, rx, ry);

    char b[12];
    if (limit > 0) snprintf(b, sizeof b, "%d", limit % 1000);
    else snprintf(b, sizeof b, "-");                               /* chua biet gioi han */
    lv_label_set_text(lbl_limit, b);
    static const lv_font_t *const steps[] = {&ui_font_sign40, &ui_font_sign32, &ui_font_sign24, &ui_font_txt16};
    int i = R >= 80 ? 0 : R >= 58 ? 1 : R >= 46 ? 2 : 3;
    if (limit >= 100 && i < 3) i++;                                /* 3 chu so: nho lai cho vua vong */
    lv_obj_set_style_text_font(lbl_limit, steps[i], 0);
    lv_obj_center(lbl_limit);

    show(img_sign, two);
    if (two) {
        lv_image_set_src(img_sign, sign_for_alert(sign_now->code));
        lv_image_set_scale(img_sign, (uint32_t)(S * 256 / 48));   /* bien goc 48 px */
        lv_obj_set_pos(img_sign, sx, sy);
    }
}

/* Dai vang: icon canh bao + khoang cach + thanh ngan dan khi toi gan. */
static void render_band(void)
{
    show(band, ev_now != NULL && screen != SCR_STANDBY);
    if (!ev_now) return;
    int x = screen == SCR_CRUISE ? C_BD : 0, w = UI_W - 2 * x;
    lv_obj_set_pos(band, x, UI_H - x - BAND_H);
    lv_obj_set_width(band, w);
    lv_image_set_src(band_img, sign_for_alert(ev_now->code));
    char num[12];
    const char *unit;
    if (ev_now->code == 8 && ev_now->value > 0) {                  /* giam toc: hien toc do moi */
        snprintf(num, sizeof num, "%d", ev_now->value % 1000);
        unit = "km/h";
    } else {
        fmt_dist2(num, sizeof num, &unit, ev_now->dist);
    }
    lv_label_set_text(band_dist, num);
    lv_label_set_text(band_unit, unit);
    int dx = 12 + 32 + 10, dy = (BAND_H - baseline(&ui_font_num30)) / 2;
    lv_obj_set_pos(band_dist, dx, dy);
    int ux = dx + text_w(num, &ui_font_num30) + 4;
    lv_obj_set_pos(band_unit, ux, dy + baseline(&ui_font_num30) - baseline(&ui_font_txt16));
    int bx = ux + text_w(unit, &ui_font_txt16) + 14, bw = w - bx - 14;
    bool bar = bw > 30 && ev_now->dist >= 0;
    show(band_bar, bar);
    if (bar) {
        lv_obj_set_pos(band_bar, bx, (BAND_H - 8) / 2);
        lv_obj_set_width(band_bar, bw);
        lv_obj_update_layout(band_bar);
        set_bar(band_bar, band_fill, progress(ev_start, ev_now->dist));
    }
}

/* Dan duong (A): mui ten to + khoang cach + thanh tien do + ten duong ben trai; toc do + bien ben phai. */
static void render_nav(void)
{
    bool b_on = ev_now != NULL;
    /* mui ten goc 64 px, hinh nam trong khoang ~44 px: phong 1,8 lan (1,55 khi co dai vang) */
    int ay = b_on ? 0 : 2, asz = b_on ? 99 : A_ARROW;
    const lv_image_dsc_t *arr = arrow_for_turn(turn_now);
    if (arr) lv_image_set_src(a_arrow, arr);
    show(a_arrow, arr != NULL);
    lv_image_set_scale(a_arrow, (uint32_t)(asz * 256 / 64));       /* mui ten goc 64 px */
    lv_obj_set_pos(a_arrow, A_PAD - 4, ay);
    lv_obj_set_pos(a_exit, A_PAD - 4 + asz + 2, ay + 10);
    lv_obj_set_pos(a_next, A_SPLIT - 12 - 30, ay + 10);

    char num[12];
    const char *unit;
    fmt_dist2(num, sizeof num, &unit, dist_now);
    lv_label_set_text(a_dist, num);
    lv_label_set_text(a_dunit, unit);
    int dy = b_on ? 100 : 122;
    lv_obj_set_pos(a_dist, A_PAD, dy);
    lv_obj_set_pos(a_dunit, A_PAD + text_w(num, &ui_font_num54) + 5, dy + baseline(&ui_font_num54) - baseline(&ui_font_unit22));
    lv_obj_set_pos(a_bar, A_PAD, dy + baseline(&ui_font_num54) + 12);
    set_bar(a_bar, a_bar_fill, progress(turn_start, dist_now));
    lv_obj_set_pos(a_street, A_PAD, dy + baseline(&ui_font_num54) + 26);
    lv_label_set_text(a_street, street_nav);
    lv_obj_set_height(a_div, b_on ? UI_H - BAND_H - 24 : UI_H - 24);

    int sy = 14 + baseline(&ui_font_speed72) + 6 + 18 + 6;        /* duoi chu km/h */
    int sh = (b_on ? UI_H - BAND_H - 8 : UI_H - 66) - sy;
    render_signs(A_RX + 4, sy, A_RW - 8, sh, true);

    bool eta = has_route && !b_on;
    show(a_eta, eta);
    show(a_eta2, eta);
    if (eta) {
        char d[12], b[32];
        const char *u;
        lv_label_set_text(a_eta, eta_txt[0] ? eta_txt : "--:--");
        fmt_dist2(d, sizeof d, &u, eta_rm > 0 ? eta_rm : -1);
        if (d[0] && eta_rmin > 0) {
            if (eta_rmin >= 60) snprintf(b, sizeof b, "%s %s · %dg%02d", d, u, eta_rmin / 60, eta_rmin % 60);
            else snprintf(b, sizeof b, "%s %s · %d'", d, u, eta_rmin);
        } else if (d[0]) snprintf(b, sizeof b, "%s %s", d, u);
        else if (eta_rmin > 0) snprintf(b, sizeof b, "%d phút", eta_rmin);
        else b[0] = 0;
        lv_label_set_text(a_eta2, b);
    }
}

/* Chay thuong (C): vien mau trang thai, hang tren ten duong + gio, giua toc do to + bien. */
static void render_cruise(void)
{
    bool b_on = ev_now != NULL;
    lv_color_t st = level == UI_LEVEL_OVER ? th->over : b_on ? th->band : th->ok;
    lv_obj_set_style_border_color(c_border, st, 0);
    lv_obj_set_style_shadow_color(c_border, st, 0);
    lv_label_set_text(c_street, !waze ? "Chỉ có GPS" : street_now);
    char b[8];
    if (clk_h >= 0) snprintf(b, sizeof b, "%02d:%02d", clk_h % 24, clk_m % 60);
    else snprintf(b, sizeof b, "--:--");
    lv_label_set_text(c_clock, b);

    int y0 = C_BD + C_TOP_H + 8, y1 = b_on ? UI_H - C_BD - BAND_H - 6 : UI_H - C_BD - 10;
    int w = 96;
    render_signs(UI_W - C_BD - C_PAD + 4 - w, y0, w, y1 - y0, false);
}

static void render_all(void)
{
    if (!scr || screen < 0) return;
    bool over = level == UI_LEVEL_OVER;
    lv_obj_set_style_bg_color(scr, screen == SCR_CRUISE && over ? th->over_bg : th->bg, 0);
    if (screen == SCR_STANDBY) { show(band, false); return; }
    render_band();
    render_speed(ev_now != NULL);
    if (screen == SCR_NAV) render_nav();
    else render_cruise();
    lv_obj_move_foreground(band);
    lv_obj_move_foreground(frame);
    if (set_scr && shown(set_scr)) lv_obj_move_foreground(set_scr);
    lv_obj_move_foreground(toast);
}

/* ======================= chon man ======================= */
static void set_screen(int s)
{
    if (s == screen) return;
    screen = s;
    show(stby, s == SCR_STANDBY);
    show(nav, s == SCR_NAV);
    show(cru, s == SCR_CRUISE);
    show(lbl_speed, s != SCR_STANDBY);
    show(lbl_unit, s != SCR_STANDBY);
    show(sign_ring, s != SCR_STANDBY);
    if (s == SCR_STANDBY) show(img_sign, false);
    if (level == UI_LEVEL_OVER && s == SCR_NAV) lv_timer_resume(blink_tmr);   /* vien do nhap nhay: chi khi dan duong */
    else { lv_timer_pause(blink_tmr); show(frame, false); }
    render_all();
    if (s == SCR_STANDBY) {
        lv_obj_move_foreground(stby);
        render_standby();
    }
    if (shown(set_scr)) lv_obj_move_foreground(set_scr);
    lv_obj_move_foreground(toast);
}

/* Chua co du lieu -> man cho; dang dan duong -> dan duong; dang chay -> chay thuong; dung yen lau -> man cho. */
static void update_screen(void)
{
    set_screen(!sig_ok || (!nav_on && !moving) ? SCR_STANDBY : nav_on ? SCR_NAV : SCR_CRUISE);
}

/* ======================= canh bao toc do ======================= */
static void blink_cb(lv_timer_t *t)
{
    (void)t;
    show(frame, !shown(frame));
}

static void update_level(void)
{
    int lv = UI_LEVEL_OK;
    if (limit > 0 && speed >= 0) {
        if (speed > limit) lv = UI_LEVEL_OVER;
        else if (speed >= limit - NEAR_KMH) lv = UI_LEVEL_NEAR;
    }
    if (lv != level) {
        level = lv;
        if (lv == UI_LEVEL_OVER && screen == SCR_NAV) {
            show(frame, true);
            lv_timer_resume(blink_tmr);
        } else {
            lv_timer_pause(blink_tmr);
            show(frame, false);
        }
        if (lv == UI_LEVEL_OVER) {                                 /* vuot toc do: tit tit, nhac lai moi 8 giay */
            beep(2);
            lv_timer_reset(over_tmr);
            lv_timer_resume(over_tmr);
        } else {
            lv_timer_pause(over_tmr);
        }
        if (level_cb) level_cb(lv);
    }
    render_all();
}

/* ======================= cham man hinh ======================= */
static void toast_hide_cb(lv_timer_t *t)
{
    (void)t;
    show(toast, false);
    lv_timer_pause(toast_tmr);
}

void ui_toast(const char *text)
{
    lv_label_set_text(lbl_toast, text);
    lv_obj_center(lbl_toast);
    show(toast, true);
    lv_obj_move_foreground(toast);
    lv_timer_reset(toast_tmr);
    lv_timer_resume(toast_tmr);
}

static void on_click(lv_event_t *e)
{
    (void)e;
    if (hold_used || shown(set_scr)) return;
    ui_cycle_mode();
}

static void settings_open(void);

void ui_rotate(void)
{
    if (!rotate_cb) return;
    rotate_cb();
    ui_toast("Đã xoay màn hình");
}

/* Giu tay tren man hinh 5 giay: mo cai dat (khong con xoay bang cam ung: xoay bang nut BOOT bam 2 lan).
 * Cam ung XPT2046 hay mat tin hieu vai tram ms khi giu lau: nha tay < 600 ms roi cham lai van tinh la dang giu. */
static uint32_t release_tick;
static bool hold_hint;
static void on_press(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        if (!release_tick || lv_tick_elaps(release_tick) > 600) {  /* lan cham moi */
            press_tick = lv_tick_get();
            hold_used = hold_hint = false;
        }
    } else if (c == LV_EVENT_PRESSING) {
        uint32_t held = lv_tick_elaps(press_tick);
        if (held >= 1000 && !hold_hint) {
            hold_hint = hold_used = true;                          /* giu lau: khong doi Ngay/Dem khi tha */
            ui_toast("Giữ tiếp để mở cài đặt");
        }
        if (held >= 5000 && !shown(set_scr)) {
            show(toast, false);
            settings_open();
        }
    } else if (c == LV_EVENT_RELEASED) {
        release_tick = lv_tick_get() | 1;
    }
}

static void over_cb(lv_timer_t *t)
{
    (void)t;
    if (level == UI_LEVEL_OVER) beep(2);
}

/* ======================= cai dat am luong =======================
 * Cam ung chua hieu chinh toa do nen nut rat to: hang tren bat / tat loa, hang duoi "-" so "+".
 * Khong cham gi 8 giay: tu dong dong va luu. */
/* Cac muc nut BOOT di qua, theo trang dang mo: o tab dau tien de doi trang. */
static int focus_items(lv_obj_t **out)
{
    int n = 0;
    out[n++] = set_tab[set_page];
    if (set_page == 0) {
        out[n++] = set_toggle;
        if (snd_on) { out[n++] = set_minus; out[n++] = set_plus; }
    } else {
        out[n++] = src_card[0];
        out[n++] = src_card[1];
    }
    return n;
}

static void focus_render(void)
{
    lv_obj_t *all[] = {set_tab[0], set_tab[1], set_toggle, set_minus, set_plus, src_card[0], src_card[1]};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_obj_set_style_outline_width(all[i], 0, 0);
    if (set_focus < 0) return;
    lv_obj_t *it[6];
    int n = focus_items(it);
    if (set_focus >= n) set_focus = n - 1;
    lv_obj_t *o = it[set_focus];
    lv_obj_set_style_outline_width(o, 4, 0);
    lv_obj_set_style_outline_pad(o, o == set_tab[set_page] ? -6 : 3, 0);
    lv_obj_set_style_outline_color(o, th->warn, 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(set_tab[set_page], 10, 0);
}

static void settings_render(void)
{
    lv_obj_set_style_bg_color(set_scr, th->bg, 0);
    for (int i = 0; i < 2; i++) {
        bool cur = set_page == i;
        lv_obj_set_style_text_color(lbl_tab[i], cur ? th->fg : th->mut, 0);
        lv_obj_set_style_bg_color(set_tab_line[i], cur ? th->ok : th->border, 0);
        lv_obj_set_height(set_tab_line[i], cur ? 3 : 1);
        lv_obj_set_y(set_tab_line[i], cur ? 37 : 39);
        bool on = (i == 1) == (src == UI_SRC_GOFA);
        lv_obj_set_style_bg_color(src_card[i], on ? (night ? lv_color_hex(0x0D2A1C) : lv_color_hex(0xE3F6EA)) : th->card, 0);
        lv_obj_set_style_border_color(src_card[i], on ? th->ok : th->border, 0);
        lv_obj_set_style_border_width(src_card[i], on ? 3 : 1, 0);
        lv_obj_set_style_text_color((lv_obj_t *)lv_obj_get_user_data(src_card[i]), th->fg, 0);
        lv_label_set_text(lbl_src_st[i], on ? "Đang dùng" : "Bấm để chọn");
        lv_obj_set_style_text_color(lbl_src_st[i], on ? th->ok : th->mut, 0);
    }
    lv_label_set_text_fmt(lbl_mac, "MAC %s", mac_txt[0] ? mac_txt : "--");
    lv_obj_set_style_text_color(lbl_mac, th->mut, 0);
    show(page_snd, set_page == 0);
    show(page_src, set_page == 1);
    lv_color_t on_c = th->ok;
    lv_obj_set_style_bg_color(set_toggle, snd_on ? on_c : th->card, 0);
    lv_obj_set_style_border_color(set_toggle, snd_on ? on_c : th->border, 0);
    lv_color_t ink = snd_on ? lv_color_white() : th->mut;
    lv_obj_set_style_line_color(set_spk_cone, ink, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_arc_color(set_arc[i], ink, LV_PART_INDICATOR);
        show(set_arc[i], snd_on);
        lv_obj_set_style_line_color(set_x[i], ink, 0);
        show(set_x[i], !snd_on);
    }
    lv_label_set_text(lbl_set_onoff, snd_on ? "ON" : "OFF");
    lv_obj_set_style_text_color(lbl_set_onoff, ink, 0);

    lv_obj_t *ctl[] = {set_minus, set_plus, lbl_set_vol, vol_bar};
    for (size_t i = 0; i < sizeof ctl / sizeof ctl[0]; i++) show(ctl[i], snd_on);   /* tat loa: an phan chinh am */
    lv_label_set_text_fmt(lbl_set_vol, "%d", snd_vol);
    lv_obj_set_style_text_color(lbl_set_vol, th->fg, 0);
    lv_obj_t *btn[] = {set_minus, set_plus};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(btn[i], th->card, 0);
        lv_obj_set_style_border_color(btn[i], th->border, 0);
        for (uint32_t k = 0; k < lv_obj_get_child_count(btn[i]); k++) lv_obj_set_style_bg_color(lv_obj_get_child(btn[i], k), th->fg, 0);
    }
    lv_obj_set_style_bg_color(vol_bar, th->track, 0);
    lv_obj_set_style_bg_color(vol_bar_fill, th->ok, 0);
    set_bar(vol_bar, vol_bar_fill, snd_vol * 10);
    focus_render();
}

static void settings_close(void)
{
    lv_timer_pause(set_tmr);
    show(set_scr, false);
    ui_toast(snd_on ? "Đã lưu âm lượng" : "Đã tắt tiếng");
}

static void set_tmr_cb(lv_timer_t *t)
{
    (void)t;
    if (set_page == 1) return;                                     /* trang Ket noi (co MAC): khong tu dong, de chep MAC */
    settings_close();
}

static void settings_changed(void)
{
    lv_timer_reset(set_tmr);
    if (sound_cb) sound_cb(snd_on, snd_vol);
    settings_render();
    beep(1);                                                       /* nghe thu muc am moi */
}

/* Doi nguon: luu ngay vao NVS roi dung o man MAC Bluetooth (de chep MAC dang ky GOFA).
 * Khong tu khoi dong lai: nguoi dung bam RST. Cham man hinh / bam BOOT: huy, tra lai nguon cu. */
static void rb_cancel(void)
{
    if (source_cb) source_cb(src);                                 /* luu lai nguon dang chay */
    show(rb_scr, false);
    lv_timer_reset(set_tmr);
    lv_timer_resume(set_tmr);
}

static void rb_start(int want)
{
    rb_src = want;
    if (source_cb) source_cb(want);                                /* board luu NVS, chua khoi dong lai */
    lv_timer_pause(set_tmr);                                       /* khong tu dong dong cai dat */
    if (rb_tile) lv_obj_delete(rb_tile);
    rb_tile = ui_partner_tile(rb_scr, want, 64);
    lv_obj_set_pos(rb_tile, (UI_W - 64) / 2, 14);
    lv_obj_add_flag(rb_tile, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(lbl_rb_title, want == UI_SRC_GOFA ? "Đã chọn GOFA" : "Đã chọn Waze Mod");
    lv_obj_set_style_bg_color(rb_scr, lv_color_hex(0x05080C), 0);
    lv_obj_set_style_text_color(lbl_rb_title, lv_color_white(), 0);
    lv_label_set_text(lbl_rb_cnt, mac_txt[0] ? mac_txt : "--");
    show(rb_scr, true);
    lv_obj_move_foreground(rb_scr);
}

static void on_set_btn(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_timer_reset(set_tmr);
    if (o == rb_scr) { rb_cancel(); return; }                     /* dang dem: cham de huy */
    if (o == set_tab[0] || o == set_tab[1]) { set_page = o == set_tab[1]; settings_render(); return; }
    if (o == src_card[0] || o == src_card[1]) {
        int want = o == src_card[1] ? UI_SRC_GOFA : UI_SRC_WAZE;
        if (want != src) rb_start(want);
        return;
    }
    if (o == set_toggle) snd_on = !snd_on;
    else if (o == set_minus) snd_vol = snd_vol > 10 ? snd_vol - 10 : 10;
    else if (o == set_plus) snd_vol = snd_vol < 100 ? snd_vol + 10 : 100;
    else return;                                                   /* cham nen: chi giu man mo */
    settings_changed();
}

static lv_obj_t *mk_bar_line(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = mk_fill(p, x, y, w, h, h / 2);
    return o;
}

static void build_settings(void)
{
    set_scr = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(set_scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(set_scr, LV_OBJ_FLAG_CLICKABLE);              /* cham nen khong doi Ngay/Dem */
    lv_obj_add_event_cb(set_scr, on_set_btn, LV_EVENT_CLICKED, NULL);

    /* thanh chon trang: Am thanh | Ket noi */
    static const char *const tabs[2] = {"Âm thanh", "Kết nối"};
    for (int i = 0; i < 2; i++) {
        set_tab[i] = mk_box(set_scr, i * UI_W / 2, 0, UI_W / 2, 40);
        lv_obj_add_flag(set_tab[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(set_tab[i], on_set_btn, LV_EVENT_CLICKED, NULL);
        lbl_tab[i] = mk_label(set_tab[i], &ui_font_txt16, 0, 0, tabs[i]);
        lv_obj_center(lbl_tab[i]);
        set_tab_line[i] = mk_fill(set_tab[i], 0, 37, UI_W / 2, 3, 0);
    }
    page_snd = mk_box(set_scr, 0, 44, UI_W, UI_H - 44);
    page_src = mk_box(set_scr, 0, 44, UI_W, UI_H - 44);

    /* trang Am thanh, hang tren: loa ON / OFF */
    set_toggle = mk_card(page_snd, 12, 8, UI_W - 24, 76, 18);
    lv_obj_add_flag(set_toggle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(set_toggle, on_set_btn, LV_EVENT_CLICKED, NULL);
    int ix = 92, iy = 38 - 1;                                     /* tam icon loa trong nut */
    lv_obj_t *body = mk_fill(set_toggle, ix - 22, iy - 9, 12, 18, 2);
    lv_obj_add_flag(body, LV_OBJ_FLAG_EVENT_BUBBLE);
    static lv_point_precise_t cone[] = {{0, 9}, {12, 0}, {12, 36}, {0, 27}, {0, 9}};
    set_spk_cone = lv_line_create(set_toggle);
    lv_line_set_points(set_spk_cone, cone, 5);
    lv_obj_set_pos(set_spk_cone, ix - 12, iy - 18);
    lv_obj_set_style_line_width(set_spk_cone, 6, 0);
    lv_obj_set_style_line_rounded(set_spk_cone, true, 0);
    for (int i = 0; i < 2; i++) {
        int r = 14 + i * 11;
        set_arc[i] = lv_arc_create(set_toggle);
        lv_obj_remove_style_all(set_arc[i]);
        lv_obj_set_size(set_arc[i], 2 * r, 2 * r);
        lv_obj_set_pos(set_arc[i], ix + 2 - r, iy - r);
        lv_arc_set_bg_angles(set_arc[i], 0, 0);
        lv_arc_set_angles(set_arc[i], 315, 45);
        lv_obj_set_style_arc_width(set_arc[i], 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(set_arc[i], true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(set_arc[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_remove_flag(set_arc[i], LV_OBJ_FLAG_CLICKABLE);
        static lv_point_precise_t xs[2][2] = {{{0, 0}, {18, 18}}, {{0, 18}, {18, 0}}};
        set_x[i] = lv_line_create(set_toggle);
        lv_line_set_points(set_x[i], xs[i], 2);
        lv_obj_set_pos(set_x[i], ix + 8, iy - 9);
        lv_obj_set_style_line_width(set_x[i], 5, 0);
        lv_obj_set_style_line_rounded(set_x[i], true, 0);
    }
    lbl_set_onoff = mk_label(set_toggle, &ui_font_onoff44, ix + 48, iy - baseline(&ui_font_onoff44) / 2, "ON");

    /* hang duoi: [ - ]  50  [ + ] */
    int by = 94, bh = UI_H - 44 - by - 10, bw = 96;
    set_minus = mk_card(page_snd, 12, by, bw, bh, 18);
    set_plus = mk_card(page_snd, UI_W - 12 - bw, by, bw, bh, 18);
    lv_obj_t *bt[] = {set_minus, set_plus};
    for (int i = 0; i < 2; i++) {
        lv_obj_add_flag(bt[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bt[i], on_set_btn, LV_EVENT_CLICKED, NULL);
        lv_obj_t *h = mk_bar_line(bt[i], (bw - 2 - 40) / 2, (bh - 2 - 8) / 2, 40, 8);   /* dau "-" ve bang khoi */
        (void)h;
        if (i == 1) mk_bar_line(bt[i], (bw - 2 - 8) / 2, (bh - 2 - 40) / 2, 8, 40);       /* them gach doc: "+" */
    }
    int mx = 12 + bw + 8, mw = UI_W - 2 * mx;
    lbl_set_vol = mk_label_w(page_snd, &ui_font_num54, mx, by + (bh - baseline(&ui_font_num54)) / 2 - 8, mw, LV_TEXT_ALIGN_CENTER);
    vol_bar = mk_fill(page_snd, mx + 8, by + bh - 20, mw - 16, 8, 4);
    vol_bar_fill = mk_fill(vol_bar, 0, 0, 0, 8, 4);

    /* trang Ket noi: 2 the to Waze Mod | GOFA */
    static const char *const names[2] = {"Waze Mod", "GOFA"};
    int cw = (UI_W - 24 - 8) / 2, ch = UI_H - 44 - 8 - 30;
    for (int i = 0; i < 2; i++) {
        src_card[i] = mk_card(page_src, 12 + i * (cw + 8), 8, cw, ch, 18);
        lv_obj_add_flag(src_card[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(src_card[i], on_set_btn, LV_EVENT_CLICKED, NULL);
        lv_obj_t *t = ui_partner_tile(src_card[i], i, 72);
        lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_pos(t, (cw - 2 - 72) / 2, 22);
        lv_obj_t *n = mk_label_w(src_card[i], &ui_font_txt16, 0, 106, cw - 2, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(n, names[i]);
        lv_obj_set_style_text_color(n, lv_color_white(), 0);
        lv_obj_set_user_data(src_card[i], n);
        lbl_src_st[i] = mk_label_w(src_card[i], &ui_font_txt14, 0, 126, cw - 2, LV_TEXT_ALIGN_CENTER);
    }
    lbl_mac = mk_label_w(page_src, &ui_font_mono14, 0, 8 + ch + 4, UI_W, LV_TEXT_ALIGN_CENTER);

    /* doi nguon: "Da chon GOFA" + MAC Bluetooth, cho bam RST; cham de huy */
    rb_scr = mk_box(set_scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(rb_scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(rb_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rb_scr, on_set_btn, LV_EVENT_CLICKED, NULL);
    lbl_rb_title = mk_label_w(rb_scr, &ui_font_txt16, 0, 88, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *l = mk_label_w(rb_scr, &ui_font_txt14, 0, 116, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "MAC Bluetooth");
    lv_obj_set_style_text_color(l, lv_color_hex(0x8AA0B4), 0);
    lbl_rb_cnt = mk_label_w(rb_scr, &ui_font_dist24, 0, 136, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_rb_cnt, lv_color_hex(0x22D37A), 0);
    l = mk_label_w(rb_scr, &ui_font_txt16, 0, 178, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Bấm RST để khởi động lại");
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFB020), 0);
    l = mk_label_w(rb_scr, &ui_font_sm12, 0, 214, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Chạm màn hình hoặc bấm BOOT để huỷ");
    lv_obj_set_style_text_color(l, lv_color_hex(0x8AA0B4), 0);
    show(rb_scr, false);
    show(set_scr, false);
    set_tmr = lv_timer_create(set_tmr_cb, 8000, NULL);
    lv_timer_pause(set_tmr);
}

static void settings_open(void)
{
    set_page = 0;
    set_focus = -1;
    show(rb_scr, false);
    settings_render();
    show(set_scr, true);
    lv_obj_move_foreground(set_scr);
    lv_obj_move_foreground(toast);
    lv_timer_reset(set_tmr);
    lv_timer_resume(set_tmr);
}

void ui_set_sound(bool on, int volume)
{
    snd_on = on;
    snd_vol = volume < 10 ? 10 : volume > 100 ? 100 : volume / 10 * 10;
}

void ui_open_settings(void) { settings_open(); }
void ui_open_settings_page(int page) { settings_open(); set_page = page ? 1 : 0; settings_render(); }

void ui_set_source(int s) { src = s == UI_SRC_GOFA ? UI_SRC_GOFA : UI_SRC_WAZE; render_standby(); }
void ui_set_source_hook(void (*cb)(int)) { source_cb = cb; }
void ui_test_switch(int want) { rb_start(want); }
static void gofa_tmr_cb(lv_timer_t *t)
{
    (void)t;
    show(gofa_dbg, false);
    lv_timer_pause(gofa_tmr);
}

void ui_gofa_debug(const char *state, const char *last)
{
    if (!gofa_tmr) gofa_tmr = lv_timer_create(gofa_tmr_cb, 6000, NULL);
    lv_timer_reset(gofa_tmr);
    lv_timer_resume(gofa_tmr);
    if (!gofa_dbg) {
        gofa_dbg = mk_box(scr, 0, UI_H - 52, UI_W, 52);
        lv_obj_set_style_bg_color(gofa_dbg, lv_color_hex(0x05080C), 0);
        lv_obj_set_style_bg_opa(gofa_dbg, LV_OPA_COVER, 0);
        lbl_gofa_dbg[0] = mk_label_w(gofa_dbg, &ui_font_sm12, 6, 2, UI_W - 12, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lbl_gofa_dbg[0], lv_color_hex(0x22D37A), 0);
        lbl_gofa_dbg[1] = mk_label_w(gofa_dbg, &ui_font_mono14, 6, 18, UI_W - 12, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lbl_gofa_dbg[1], lv_color_white(), 0);
        lv_label_set_long_mode(lbl_gofa_dbg[1], LV_LABEL_LONG_CLIP);
        lv_obj_set_height(lbl_gofa_dbg[1], 34);
    }
    lv_label_set_text(lbl_gofa_dbg[0], state ? state : "");
    lv_label_set_text(lbl_gofa_dbg[1], last && last[0] ? last : "Chờ app GOFA gửi dữ liệu...");
    show(gofa_dbg, true);
    lv_obj_move_foreground(gofa_dbg);
    if (shown(set_scr)) lv_obj_move_foreground(set_scr);            /* cai dat van nam tren cung */
    lv_obj_move_foreground(toast);
}

void ui_set_mac(const char *mac) { snprintf(mac_txt, sizeof mac_txt, "%s", mac ? mac : ""); }

bool ui_settings_key(int key)
{
    if (!ui_settings_shown()) return false;
    lv_timer_reset(set_tmr);
    if (shown(rb_scr)) { rb_cancel(); ui_toast("Đã huỷ"); return true; }   /* dang dem khoi dong lai: bam la huy */
    lv_obj_t *it[6];
    int n = focus_items(it);
    if (key == UI_KEY_NEXT) {
        set_focus = set_focus < 0 ? 1 : (set_focus + 1) % n;       /* lan dau: nhay vao muc dau tien cua trang */
        settings_render();
        beep(1);
        return true;
    }
    if (set_focus < 0) set_focus = 1;
    lv_obj_t *o = it[set_focus < n ? set_focus : n - 1];
    if (o == set_tab[set_page]) {                                  /* o tab: doi trang, khung chon o lai tab */
        set_page = !set_page;
        set_focus = 0;
        settings_render();
        beep(1);
        return true;
    }
    lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);                  /* nhu cham vao muc dang chon */
    return true;
}

/* O logo doi tac (tam thoi ve bang chu: doi file logo chinh thuc). */
lv_obj_t *ui_partner_tile(lv_obj_t *parent, int source, int size)
{
    lv_obj_t *t = mk_fill(parent, 0, 0, size, size, size * 22 / 100);
    bool g = source == UI_SRC_GOFA;
    lv_obj_set_style_bg_color(t, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
    const lv_font_t *f = size >= 64 ? &ui_font_unit22 : size >= 30 ? &ui_font_cap12 : &ui_font_sm12;
    lv_obj_t *l = mk_label(t, f, 0, 0, g ? "GOFA" : "waze");
    lv_obj_set_style_text_color(l, g ? lv_color_white() : lv_color_hex(0x05263A), 0);
    lv_obj_center(l);
    return t;
}

void ui_set_beep_hook(void (*cb)(int)) { beep_cb = cb; }
void ui_set_sound_hook(void (*cb)(bool, int)) { sound_cb = cb; }

/* ======================= dung giao dien ======================= */
static void build_nav(void)
{
    nav = mk_box(scr, 0, 0, UI_W, UI_H);
    a_arrow = mk_image(nav, A_PAD, 12, A_ARROW * 256 / 64);
    lv_obj_set_style_image_recolor_opa(a_arrow, LV_OPA_COVER, 0);
    a_exit = mk_label(nav, &ui_font_unit22, 0, 0, "");             /* so loi ra vong xuyen */
    a_next = mk_image(nav, 0, 0, 30 * 256 / 64);                   /* luot re tiep theo: mui ten nho 30 px */
    lv_obj_set_style_image_recolor_opa(a_next, LV_OPA_COVER, 0);
    a_dist = mk_label(nav, &ui_font_num54, A_PAD, 126, "");
    a_dunit = mk_label(nav, &ui_font_unit22, 0, 0, "");
    a_bar = mk_fill(nav, A_PAD, 0, A_BAR_W, 5, 3);
    a_bar_fill = mk_fill(a_bar, 0, 0, 0, 5, 3);
    a_street = mk_label_w(nav, &ui_font_txt16, A_PAD, 0, A_SPLIT - A_PAD - 8, LV_TEXT_ALIGN_LEFT);
    a_div = mk_fill(nav, A_SPLIT, 12, 1, UI_H - 24, 0);
    a_eta = mk_label_w(nav, &ui_font_dist24, A_RX, UI_H - 60, A_RW, LV_TEXT_ALIGN_CENTER);
    a_eta2 = mk_label_w(nav, &ui_font_sm12, A_RX, UI_H - 30, A_RW, LV_TEXT_ALIGN_CENTER);
}

static void build_cruise(void)
{
    cru = mk_box(scr, 0, 0, UI_W, UI_H);
    c_border = mk_box(cru, 0, 0, UI_W, UI_H);
    lv_obj_set_style_border_width(c_border, C_BD, 0);
    lv_obj_set_style_border_opa(c_border, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c_border, 4, 0);
    c_pin = mk_image(cru, C_BD + C_PAD - 4, C_BD + (C_TOP_H - 28) / 2, 28 * 256 / 48);
    lv_image_set_src(c_pin, &icon_nav_marker);
    lv_obj_set_style_image_recolor_opa(c_pin, LV_OPA_COVER, 0);
    int cw = text_w("00:00", &ui_font_dist24) + 4, cx = UI_W - C_BD - C_PAD - cw;
    c_clock = mk_label_w(cru, &ui_font_dist24, cx, C_BD + (C_TOP_H - ui_font_dist24.line_height) / 2, cw, LV_TEXT_ALIGN_RIGHT);
    int sx = C_BD + C_PAD + 30;
    c_street = mk_label_w(cru, &ui_font_txt16, sx, C_BD + (C_TOP_H - ui_font_txt16.line_height) / 2, cx - 10 - sx, LV_TEXT_ALIGN_LEFT);
    c_line = mk_fill(cru, C_BD + C_PAD, C_BD + C_TOP_H, UI_W - 2 * (C_BD + C_PAD), 1, 0);
}

static void build_shared(void)
{
    lbl_speed = mk_label(scr, &ui_font_speed72, 0, 0, "--");
    lv_label_set_long_mode(lbl_speed, LV_LABEL_LONG_CLIP);
    lbl_unit = mk_label(scr, &ui_font_txt14, 0, 0, "km/h");

    sign_ring = mk_fill(scr, 0, 0, 60, 60, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(sign_ring, lv_color_white(), 0);
    lv_obj_set_style_border_color(sign_ring, lv_color_hex(0xE11D2A), 0);
    lv_obj_set_style_border_opa(sign_ring, LV_OPA_COVER, 0);
    lbl_limit = mk_label(sign_ring, &ui_font_sign24, 0, 0, "-");
    lv_obj_set_style_text_color(lbl_limit, lv_color_hex(0x111111), 0);
    img_sign = mk_image(scr, 0, 0, 256);
    show(img_sign, false);

    band = mk_fill(scr, 0, UI_H - BAND_H, UI_W, BAND_H, 0);
    band_img = mk_image(band, 12, (BAND_H - 32) / 2, 32 * 256 / 48);
    band_dist = mk_label(band, &ui_font_num30, 0, 0, "");
    band_unit = mk_label(band, &ui_font_txt16, 0, 0, "");
    band_bar = mk_fill(band, 0, 0, 40, 8, 4);
    lv_obj_set_style_bg_opa(band_bar, LV_OPA_20, 0);
    band_fill = mk_fill(band_bar, 0, 0, 0, 8, 4);
    show(band, false);
}
/* Man cho: dong ho lat 2 the (gio | phut), dong trang thai ket noi o tren, ngay va GPS o duoi. */
#define FLIP_W 124
#define FLIP_H 104
#define FLIP_Y 44
static void build_standby(void)
{
    stby = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(stby, LV_OPA_COVER, 0);
    lbl_sb_brand = mk_label(stby, &ui_font_txt14, M + 2, 14, "EVCarHUD");
    lbl_sb_stat = mk_label_w(stby, &ui_font_sm12, 86, 16, UI_W - 86 - M - 2, LV_TEXT_ALIGN_RIGHT);
    int gap = 12, x0 = (UI_W - 2 * FLIP_W - gap) / 2;
    for (int i = 0; i < 2; i++) {
        flip[i] = mk_card(stby, x0 + i * (FLIP_W + gap), FLIP_Y, FLIP_W, FLIP_H, 14);
        lv_obj_set_style_bg_grad_dir(flip[i], LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_clip_corner(flip[i], true, 0);
        lv_obj_set_style_shadow_width(flip[i], 6, 0);
        lv_obj_set_style_shadow_offset_y(flip[i], 2, 0);
        lv_obj_set_style_shadow_color(flip[i], lv_color_black(), 0);
        flip_bot[i] = mk_box(flip[i], 0, FLIP_H / 2 - 1, FLIP_W - 2, FLIP_H / 2 - 1);   /* nua duoi toi hon */
        lv_obj_set_style_bg_opa(flip_bot[i], LV_OPA_COVER, 0);
        lbl_clk[i] = mk_label_w(flip[i], &ui_font_clock60, 0, 0, FLIP_W - 2, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_y(lbl_clk[i], (FLIP_H - 2) / 2 - baseline(&ui_font_clock60) + 22);   /* giua chu so (cao ~44 px) nam tren khe lat */
        flip_line[i] = mk_box(flip[i], 0, FLIP_H / 2 - 2, FLIP_W - 2, 2);            /* khe lat giua the */
        lv_obj_set_style_bg_opa(flip_line[i], LV_OPA_COVER, 0);
    }
    /* chua ket noi dien thoai: dong ho dien tu 24 gio HH:MM, dau ":" nhap nhay moi nua giay */
    const lv_font_t *df = &ui_font_clock80;
    int w2 = text_w("00", df), wc = text_w(":", df), dx = (UI_W - 2 * w2 - wc) / 2;
    int dy = 112 - (baseline(df) - 29);                           /* giua chu so (cao ~58 px) o y = 112 */
    dg_h = mk_label_w(stby, df, dx, dy, w2, LV_TEXT_ALIGN_CENTER);
    dg_c = mk_label(stby, df, dx + w2, dy, ":");
    dg_m = mk_label_w(stby, df, dx + w2 + wc, dy, w2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(dg_h, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(dg_m, LV_LABEL_LONG_CLIP);
    sb_bar = mk_card(stby, M, UI_H - M - 40, UI_W - 2 * M, 40, R_CARD);
    lbl_date = mk_label_w(sb_bar, &ui_font_txt14, 10, 10, 180, LV_TEXT_ALIGN_LEFT);
    lbl_gps = mk_label_w(sb_bar, &ui_font_txt14, UI_W - 2 * M - 2 - 10 - 110, 10, 110, LV_TEXT_ALIGN_RIGHT);
}

static void colon_cb(lv_timer_t *t)
{
    (void)t;
    if (screen != SCR_STANDBY || phone || clk_h < 0) lv_obj_set_style_text_opa(dg_c, LV_OPA_COVER, 0);
    else lv_obj_set_style_text_opa(dg_c, lv_obj_get_style_text_opa(dg_c, 0) ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static void render_standby(void)
{
    if (!stby) return;
    char b[48];
    char hh[4] = "--", mm[4] = "--";
    if (clk_h >= 0) {
        snprintf(hh, sizeof hh, "%02d", clk_h % 24);
        snprintf(mm, sizeof mm, "%02d", clk_m % 60);
    }
    lv_label_set_text(lbl_clk[0], hh);
    lv_label_set_text(lbl_clk[1], mm);
    lv_label_set_text(dg_h, hh);
    lv_label_set_text(dg_m, mm);
    for (int i = 0; i < 2; i++) show(flip[i], phone);              /* da ket noi: dong ho lat; chua: dong ho dien tu */
    show(dg_h, !phone);
    show(dg_c, !phone);
    show(dg_m, !phone);
    if (day > 0 && month > 0 && wday >= 0) snprintf(b, sizeof b, "%s, %d Tháng %d", WDAY_NAME[wday % 7], day, month);
    else if (wday >= 0) snprintf(b, sizeof b, "%s", WDAY_NAME[wday % 7]);
    else snprintf(b, sizeof b, "Chờ giờ từ điện thoại");
    lv_label_set_text(lbl_date, b);
    lv_label_set_text(lbl_sb_stat, phone ? "Đã kết nối điện thoại" : src == UI_SRC_GOFA ? "Chờ kết nối GOFA qua điện thoại" : "Chờ kết nối Waze qua điện thoại");
    lv_obj_set_style_text_color(lbl_sb_stat, th->accent, 0);
    lv_obj_set_style_text_opa(lbl_sb_stat, phone ? LV_OPA_COVER : LV_OPA_80, 0);
    const char *g = phone ? "GPS sẵn sàng" : (clk_h >= 0 && !clk_synced) ? "Giờ chưa chỉnh" : "Chưa có GPS";
    lv_label_set_text(lbl_gps, g);
    lv_obj_set_style_text_color(lbl_gps, phone ? th->accent : th->mut, 0);
}

/* Man chao: logo EVCarHUD (ui_logo.c, tao boi tools/logo/gen_logo.py) tren nen toi, hien dan ra; dong duoi: phien ban. */
extern const lv_image_dsc_t ui_logo;

lv_obj_t *ui_splash_create(const char *version, int source)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080F), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &ui_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -36);
    lv_obj_fade_in(img, 500, 0);
    /* "KET NOI VOI" + logo doi tac (Waze Mod mac dinh / GOFA) */
    lv_obj_t *cap = lv_label_create(scr);
    lv_obj_set_style_text_font(cap, &ui_font_cap12, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x8AA0B4), 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_label_set_text(cap, "KẾT NỐI VỚI");
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 14);
    bool g = source == UI_SRC_GOFA;
    const char *nm = g ? "GOFA" : "Waze Mod";
    int tw = 36 + 8 + text_w(nm, &ui_font_txt16), x0 = (UI_W - tw) / 2;
    lv_obj_t *tile = ui_partner_tile(scr, source, 36);
    lv_obj_set_pos(tile, x0, 146);
    lv_obj_t *n = mk_label(scr, &ui_font_txt16, x0 + 44, 146 + (36 - ui_font_txt16.line_height) / 2, nm);
    lv_obj_set_style_text_color(n, lv_color_white(), 0);
    lv_obj_fade_in(tile, 500, 200);
    lv_obj_fade_in(n, 500, 200);
    lv_obj_t *v = lv_label_create(scr);
    lv_obj_set_style_text_font(v, &ui_font_sm12, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(0x9CA3AF), 0);
    if (version && version[0]) lv_label_set_text_fmt(v, "Phiên bản %s", version);
    else lv_label_set_text(v, "");
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -14);
    return scr;
}

void dashboard_create(lv_obj_t *screen_obj)
{
    scr = screen_obj;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_click, LV_EVENT_SHORT_CLICKED, NULL);   /* cham: Ngay/Dem; giu 5 giay: cai dat */
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_RELEASED, NULL);

    build_nav();
    build_cruise();
    build_shared();
    build_standby();
    colon_tmr = lv_timer_create(colon_cb, 500, NULL);

    /* vien do nhap nhay khi vuot toc do (dan duong) */
    frame = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    show(frame, false);
    blink_tmr = lv_timer_create(blink_cb, 400, NULL);
    lv_timer_pause(blink_tmr);
    over_tmr = lv_timer_create(over_cb, 8000, NULL);
    lv_timer_pause(over_tmr);
    build_settings();

    /* thong bao nho */
    toast = mk_box(scr, 24, 102, UI_W - 48, 36);
    lv_obj_set_style_radius(toast, 18, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lbl_toast = mk_label(toast, &ui_font_txt14, 0, 0, "");
    show(toast, false);
    toast_tmr = lv_timer_create(toast_hide_cb, 2000, NULL);
    lv_timer_pause(toast_tmr);

    th = NULL;
    update_night();
    ui_set_speed(-1);
    ui_set_speed_limit(0);
    ui_set_nav(false, 0, -1, 0, "", 0);
    ui_set_route("", 0, 0);
    ui_set_alerts(NULL, 0);
    set_screen(SCR_STANDBY);
}

/* ======================= toc do / dong ho ======================= */
void ui_set_speed(int kmh)
{
    if (kmh >= MOVE_KMH) {                                         /* dang chay / dung yen lau: chon man */
        moving = true;
        stop_tick = 0;
    } else if (moving) {
        if (!stop_tick) stop_tick = lv_tick_get() | 1;
        else if (lv_tick_elaps(stop_tick) > STOP_MS) moving = false;
    }
    update_screen();
    if (kmh == speed) return;
    speed = kmh;
    update_level();
}

int ui_get_speed(void) { return speed; }

lv_obj_t *ui_screen(void) { return scr; }

void ui_set_speed_limit(int kmh)
{
    if (kmh == limit) return;
    limit = kmh;
    update_level();
}

void ui_set_time(int hour, int minute, int wd)
{
    bool changed = hour != clk_h || minute != clk_m;
    clk_h = hour;
    clk_m = minute;
    wday = wd;
    if (changed && screen == SCR_CRUISE) render_all();
    render_standby();
    update_night();
}

void ui_set_clock_synced(bool synced)
{
    clk_synced = synced;
    render_standby();
}

void ui_set_date(int d, int m)
{
    day = d;
    month = m;
    render_standby();
}

bool ui_get_clock(int *h, int *m)
{
    if (clk_h < 0) return false;
    *h = clk_h;
    *m = clk_m;
    return true;
}

/* ======================= dan duong ======================= */
void ui_set_nav(bool active, int turn, int dist_m, int exit_n, const char *street, int turn2)
{
    const lv_image_dsc_t *arr = active ? arrow_for_turn(turn) : NULL;
    nav_on = active && arr;
    if (!street) street = "";
    if (!nav_on) {                                                 /* chay thuong: ten duong o hang tren */
        snprintf(street_now, sizeof street_now, "%s", street);
    } else {
        /* thanh tien do: tinh tu luc thay luot re moi (doi huong, doi duong, hoac khoang cach tang lai) */
        if (turn != turn_now || strcmp(street, street_nav) != 0 || dist_m > turn_start) {
            turn_start = dist_m;
            turn_beeped = false;
        }
        if (!turn_beeped && dist_m >= 0 && dist_m <= 100 && turn_start > 150) {   /* con 100 m toi cho re: tit */
            turn_beeped = true;
            beep(1);
        }
        turn_now = turn;
        dist_now = dist_m;
        snprintf(street_nav, sizeof street_nav, "%s", street);
        if (exit_n > 0) {
            char e[8];
            snprintf(e, sizeof e, "%d", exit_n % 100);
            lv_label_set_text(a_exit, e);
        }
        show(a_exit, exit_n > 0);
        const lv_image_dsc_t *arr2 = turn2 ? arrow_for_turn(turn2) : NULL;
        if (arr2) lv_image_set_src(a_next, arr2);
        show(a_next, arr2 != NULL && exit_n <= 0);
    }
    update_screen();
    render_all();
}

void ui_set_route(const char *eta, int remaining_m, int remaining_min)
{
    has_route = (eta && eta[0]) || remaining_m > 0;
    snprintf(eta_txt, sizeof eta_txt, "%s", eta ? eta : "");
    eta_rm = remaining_m;
    eta_rmin = remaining_min;
    if (screen == SCR_NAV) render_all();
}

void ui_set_alerts(const ui_alert_t *alerts, int n)
{
    al_n = 0;
    for (int i = 0; alerts && i < n && al_n < (int)(sizeof al_list / sizeof al_list[0]); i++) al_list[al_n++] = alerts[i];
    ev_now = sign_now = NULL;
    for (int i = 0; i < al_n; i++) {
        if (is_road_sign(al_list[i].code)) {
            if (!sign_now) sign_now = &al_list[i];
        } else if (!ev_now) {
            ev_now = &al_list[i];
        }
    }
    int code = ev_now ? ev_now->code : 0;
    if (code && code != ev_code) beep(1);                          /* canh bao moi: tit */
    if (ev_now && (code != ev_code || ev_now->dist > ev_start)) ev_start = ev_now->dist;
    ev_code = code;
    render_all();
}

void ui_set_chip(int kind, const char *text)
{
    (void)kind; (void)text;                                       /* camera / cam vuot da nam trong dai canh bao */
}

void ui_set_signal(bool ok)
{
    sig_ok = ok;
    if (!ok) {
        moving = false;
        stop_tick = 0;
        ui_set_speed(-1);
        ui_set_speed_limit(0);
        ui_set_nav(false, 0, -1, 0, "", 0);
        ui_set_route("", 0, 0);
        ui_set_alerts(NULL, 0);
    }
    update_screen();
}

void ui_set_phone(bool connected)
{
    phone = connected;
    render_standby();
}

void ui_set_waze(bool on)
{
    if (on == waze) return;
    waze = on;
    render_all();
}

void ui_set_gps(int lvl) { (void)lvl; }

void ui_set_hint(const char *text) { (void)text; }               /* man cho da co dong "Cho ket noi Waze..." */

/* ======================= Ngay / Dem ======================= */
void ui_set_mode(int m)
{
    if (m < UI_MODE_AUTO || m > UI_MODE_NIGHT) m = UI_MODE_AUTO;
    mode = m;
    update_night();
}

int ui_get_mode(void) { return mode; }

void ui_cycle_mode(void)
{
    ui_set_mode((mode + 1) % 3);
    char b[40];
    snprintf(b, sizeof b, "Chế độ: %s", MODE_NAME[mode]);
    ui_toast(b);
    if (mode_cb) mode_cb(mode);
}

void ui_set_ambient(int dark)
{
    if (dark == ambient) return;
    ambient = dark;
    update_night();
}

void ui_set_mode_hook(void (*cb)(int)) { mode_cb = cb; }
void ui_set_night_hook(void (*cb)(bool)) { night_cb = cb; if (cb) cb(night); }
void ui_set_level_hook(void (*cb)(int)) { level_cb = cb; }
void ui_set_rotate_hook(void (*cb)(void)) { rotate_cb = cb; }

bool ui_settings_shown(void) { return set_scr && shown(set_scr); }
