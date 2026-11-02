/* EVCarHUD P4 - giao dien cho man hinh 3.5" ngang 480x320 (LVGL 9), theo 5 man mau "Layout HUD ESP32-P4 3.5 inch".
 * Cung du lieu va cung hanh vi voi ban S3 (2.8"): chi nhan du lieu qua ui_set_*, tu chon man theo trang thai:
 *
 *  Man cho      chua co du lieu, hoac dung yen > 30 s ma khong dan duong: dong ho lat (da noi dien thoai) / dong ho so.
 *  Toc do       dang chay, khong dan duong: so toc do rat to (xanh / vang / do so voi gioi han) + bien gioi han to.
 *  Canh bao     dang chay va co canh bao trong 800 m: the vang (bien, ten, khoang cach, thanh ngan dan) + 2 o phu.
 *  Cao toc      dang chay tren cao toc (gioi han >= 80 km/h hoac ten duong co "cao toc"): the xanh la + tram thu phi / dung nghi.
 *  Chi dan      dang dan duong: the xanh duong (mui ten, khoang cach, ten duong) + gio den / con lai / thoi gian.
 *
 * Moi man (tru man cho) co thanh tren: gio | ten duong | dien thoai + GPS. So dung font Inter (tools/fonts/gen_fonts.sh).
 * Cham man hinh hoac bam BOOT: Tu dong -> Ngay -> Dem. Giu tay 5 giay hoac giu BOOT 1-5 giay: Cai dat (mau xanh la,
 * 4 trang Hien thi / Canh bao / Ket noi / He thong). Bam BOOT 2 lan: xoay 180 do. */
#include "dashboard.h"
#include "dashboard_signs.h"
#include "ui_fonts.h"
#include <stdio.h>
#include <string.h>

#define NEAR_KMH  5
#define MOVE_KMH  3                          /* >= 3 km/h: dang chay */
#define STOP_MS   30000                      /* dung yen 30 s (khong dan duong): ve man cho */
#define ALERT_M   800                        /* canh bao trong 800 m: chuyen sang man Canh bao */
#define HW_KMH    80                         /* gioi han >= 80 km/h: coi la cao toc */

#define TOP_H     30                         /* thanh tren */
#define BODY_H    (UI_H - TOP_H)
#define PAD       14
#define SIDE_W    100                        /* cot phai: toc do nho + bien gioi han */

typedef struct {
    lv_color_t bg, card, card2, border, fg, mut;
    lv_color_t ok, warn, over, over_bg, over_fg;
    lv_color_t blue, blue_fg, blue_dim;
    lv_color_t al_bg, al_border, al_fg;
    lv_color_t hw, hw_fg, band;
    lv_color_t nav_bg, nav_bd, hw_bg, hw_bd;   /* the chi dan / cao toc: nen toi pha mau + vien manh */
    lv_color_t flip_top, flip_bot, flip_line;
    lv_color_t accent;
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_NIGHT = {                /* theo man mau: nen xanh den */
    .bg = C(0x060913), .card = C(0x0E1424), .card2 = C(0x0E1424), .border = C(0x141B2E), .fg = C(0xE8ECF4),
    .mut = C(0x9AA4B8), .ok = C(0x22C55E), .warn = C(0xF5B301), .over = C(0xFF4D4F), .over_bg = C(0x3A0D10),
    .over_fg = C(0xFFB4B4), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x1A3866),
    .al_bg = C(0x1A1606), .al_border = C(0x3A3108), .al_fg = C(0xFFD400), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xFFD400),
    .nav_bg = C(0x10284F), .nav_bd = C(0x2A5BA8), .hw_bg = C(0x0B2E1E), .hw_bd = C(0x1C7A47),
    .flip_top = C(0x1E293B), .flip_bot = C(0x0C111D), .flip_line = C(0x020408), .accent = C(0x22C55E),
};
static const theme_t TH_DAY = {                  /* ban ngay: nen sang, cung bo cuc */
    .bg = C(0xF3F3EF), .card = C(0xFFFFFF), .card2 = C(0xE6E9EE), .border = C(0xD9DDE2), .fg = C(0x0B1220),
    .mut = C(0x55657A), .ok = C(0x15803D), .warn = C(0xB45309), .over = C(0xDC2626), .over_bg = C(0xFDE2E2),
    .over_fg = C(0x991B1B), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x17478F),
    .al_bg = C(0xFFF6D6), .al_border = C(0xF0D264), .al_fg = C(0x92400E), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xF5B400),
    .nav_bg = C(0x1F5FBF), .nav_bd = C(0x1F5FBF), .hw_bg = C(0x0B7A3E), .hw_bd = C(0x0B7A3E),
    .flip_top = C(0xFFFFFF), .flip_bot = C(0xF1F0EC), .flip_line = C(0xDCDCDA), .accent = C(0x15803D),
};
/* Cai dat: luon nen toi, mau chu dao xanh la (xe dien) */
#define S_BG      lv_color_hex(0x05090A)
#define S_CARD    lv_color_hex(0x0D1416)
#define S_BORDER  lv_color_hex(0x1A2629)
#define S_ON_BG   lv_color_hex(0x0F2A1C)
#define S_GREEN   lv_color_hex(0x22C55E)
#define S_GREEN2  lv_color_hex(0x4ADE80)
#define S_GREEN3  lv_color_hex(0x86EFAC)
#define S_FG      lv_color_hex(0xE8F0EC)
#define S_MUT     lv_color_hex(0x94A3A0)
#define S_TRACK   lv_color_hex(0x253235)
#define RED_SIGN  lv_color_hex(0xD7262E)

static const char *const MODE_NAME[3] = {"Tự động", "Ban ngày", "Ban đêm"};
static const char *const WDAY_NAME[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
enum { SCR_STANDBY = 0, SCR_CRUISE, SCR_ALERT, SCR_HIGHWAY, SCR_NAV, SCR_N };
static const theme_t *th = &TH_NIGHT;

/* ---------------- doi tuong ---------------- */
static lv_obj_t *scr, *page[SCR_N];
/* thanh tren (dung chung cac man dang chay) */
static lv_obj_t *top, *top_line, *t_clock, *t_street, *t_badge, *t_gps_dot, *t_src, *t_src_l;
/* bien gioi han: moi man mot vong */
typedef struct { lv_obj_t *ring, *lbl; int size; } ring_t;
/* toc do: so + "KM/H" */
typedef struct { lv_obj_t *num, *unit; bool big; } spd_t;   /* big: 72 px, 3 chu so thi 62 px cho vua cot */
/* Toc do (cruise) */
static spd_t c_spd;
static ring_t c_ring;
static lv_obj_t *c_sign[2], *c_strip, *c_strip_icon, *c_strip_txt;
/* Canh bao */
static lv_obj_t *a_card, *a_img, *a_cap, *a_name, *a_dist, *a_unit, *a_bar, *a_fill;
static spd_t a_spd;
static ring_t a_ring;
static lv_obj_t *a_cell[2], *a_cell_img[2], *a_cell_t[2], *a_cell_s[2];
/* Cao toc */
static lv_obj_t *h_card, *h_img, *h_cap, *h_name, *h_sub;
static lv_obj_t *h_cell[3], *h_cell_l[3], *h_cell_img[2], *h_cell_v[2];
static ring_t h_ring;
static spd_t h_spd;
static lv_obj_t *h_min, *h_bar, *h_toll, *h_rest;
/* Chi dan */
static lv_obj_t *n_card, *n_arrow, *n_exit, *n_dist, *n_unit, *n_turn, *n_street, *n_bar, *n_fill, *n_next, *n_next_l;
static spd_t n_spd;
static ring_t n_ring;
static lv_obj_t *n_sign;
static lv_obj_t *n_eta, *n_eta_v[3], *n_eta_l[3];
static lv_obj_t *n_band, *n_band_img, *n_band_t, *n_band_d, *n_band_bar, *n_band_fill;
/* man cho */
static lv_obj_t *sb_brand, *sb_stat, *flip[2], *flip_bot[2], *flip_line[2], *lbl_clk[2];
static lv_obj_t *sb_bar, *lbl_date, *lbl_gps;
static lv_obj_t *dg_h, *dg_c, *dg_m;
static lv_timer_t *colon_tmr;
static lv_obj_t *frame, *toast, *lbl_toast;
static lv_timer_t *blink_tmr, *toast_tmr;

/* ---------------- trang thai ---------------- */
static int speed = -2, limit = -1, level = -1;
static bool nav_on, has_route;
static int clk_h = -1, clk_m = -1, day = -1, month = -1, wday = -1;
static int screen = -1;
static bool sig_ok, phone, waze = true, moving, clk_synced;
static uint32_t stop_tick;
static ui_alert_t al_list[4];
static int al_n;
static const ui_alert_t *ev_now;            /* canh bao (su kien) gan nhat */
static const ui_alert_t *sign_now;          /* bien bao tren duong gan nhat */
static int ev_code, ev_start;
static int turn_now = -1, dist_now = -1, turn_start, exit_now, turn2_now;
static char eta_txt[8], street_now[64], street_nav[64];
static int eta_rm, eta_rmin;
static int mode = UI_MODE_AUTO, ambient = -1;
static bool night;
static void (*mode_cb)(int);
static void (*night_cb)(bool);
static void (*level_cb)(int);
static void (*rotate_cb)(void);
static bool rot_on;
/* loa */
static bool snd_on = true;
static int snd_vol = 70;
static bool turn_beeped;
static void (*beep_cb)(int);
static void (*sound_cb)(bool, int);
static lv_timer_t *over_tmr;
/* cham man hinh */
static uint32_t press_tick;
static bool hold_used;
/* cai dat */
enum { PG_DISPLAY = 0, PG_ALERT, PG_LINK, PG_SYSTEM, PG_N };
static lv_obj_t *set_scr, *set_tab[PG_N], *set_page_obj[PG_N], *set_close, *set_pill_phone, *set_pill_src;
static lv_obj_t *mode_card[3], *mode_sub[3], *rot_row, *rot_sw;
static lv_obj_t *snd_row, *snd_sw, *vol_card, *vol_minus, *vol_plus, *lbl_vol, *vol_bar, *vol_fill;
static lv_obj_t *src_card[2], *lbl_src_st[2], *lbl_mac, *lbl_ble;
static lv_obj_t *lbl_sys[4];
static lv_obj_t *rb_scr, *rb_tile, *lbl_rb_title, *lbl_rb_mac;
static lv_timer_t *set_tmr;
static int set_page, src = UI_SRC_WAZE, set_focus = -1;
static void (*source_cb)(int);
static char mac_txt[24], ver_txt[24];
static lv_obj_t *gofa_dbg, *lbl_gofa_dbg[2];
static lv_timer_t *gofa_tmr;

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

static lv_obj_t *mk_image(lv_obj_t *p, int x, int y, int size, int src_size)
{
    lv_obj_t *im = lv_image_create(p);
    lv_obj_set_pos(im, x, y);
    lv_image_set_pivot(im, 0, 0);
    lv_image_set_scale(im, (uint32_t)(size * 256 / src_size));
    lv_image_set_antialias(im, true);
    return im;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static bool shown(lv_obj_t *o) { return !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

static int baseline(const lv_font_t *f) { return f->line_height - f->base_line; }   /* dinh label -> chan chu */

static int text_w(const char *s, const lv_font_t *f) { return lv_text_get_width(s, strlen(s), f, 0); }

/* so to: khoang chu -0.03em nhu ban mau */
#define TIGHT(f) (-(int)(f)->line_height * 3 / 100 - 1)
static int text_w_t(const char *s, const lv_font_t *f) { return lv_text_get_width(s, strlen(s), f, TIGHT(f)); }

/* 183 -> "180" "m", 1240 -> "1,2" "km", 12400 -> "12" "km" */
static void fmt_dist2(char *num, size_t nc, const char **unit, int m)
{
    if (m < 0) { num[0] = 0; *unit = ""; }
    else if (m < 1000) { snprintf(num, nc, "%d", m < 50 ? m : (m + 5) / 10 * 10); *unit = "m"; }
    else if (m < 10000) { snprintf(num, nc, "%d,%d", m / 1000, (m % 1000 + 50) / 100 % 10); *unit = "km"; }
    else { snprintf(num, nc, "%d", (m + 500) / 1000); *unit = "km"; }
}

static void fmt_dist(char *out, size_t n, int m)                /* "1,2 km" / "" */
{
    char num[12];
    const char *u;
    fmt_dist2(num, sizeof num, &u, m);
    if (num[0]) snprintf(out, n, "%s %s", num, u);
    else out[0] = 0;
}

/* Bien bao tren duong (hien canh bien gioi han) khac su kien (cam canh bao) */
static bool is_road_sign(int code)
{
    switch (code) {
    case 7: case 9: case 10: case 11: case 12: case 37: case 38: case 56: case 57: case 59: case 61:
        return true;
    default:
        return (code >= 22 && code <= 34) || (code >= 65 && code <= 75);
    }
}

/* Ten canh bao theo ma HLP `alr` (nhom theo icon trong dashboard_signs.c) */
static const char *alert_name(int code)
{
    switch (code) {
    case 1: return "Cảnh sát";
    case 2: return "Camera tốc độ";
    case 3: return "Camera đèn đỏ";
    case 5: return "Tai nạn";
    case 6: return "Kẹt xe";
    case 7: case 38: return "Đường cấm";
    case 8: return "Giảm tốc độ";
    case 9: return "Cấm vượt";
    case 10: return "Hết cấm vượt";
    case 11: return "Giao cắt đường sắt";
    case 12: return "Trạm thu phí";
    case 14: return "Công trường";
    case 15: return "Ổ gà";
    case 16: case 50: case 51: case 52: case 53: case 54: case 55: return "Thời tiết xấu";
    case 19: return "Trạm dừng nghỉ";
    case 20: case 21: case 35: case 36: return "Lối ra / trạm dừng";
    case 22: case 25: return "Hết mọi lệnh cấm";
    case 23: return "Khu đông dân cư";
    case 24: return "Hết khu đông dân cư";
    case 26: return "Cấm ô tô";
    case 27: return "Cấm xe máy";
    case 28: case 39: case 66: case 67: case 68: case 69: case 72: return "Cấm rẽ trái";
    case 29: case 65: case 70: case 71: case 73: return "Cấm rẽ phải";
    case 30: case 74: return "Cấm quay đầu";
    case 31: return "Cấm đi thẳng";
    case 32: return "Bắt buộc đi thẳng";
    case 33: return "Bắt buộc rẽ phải";
    case 34: return "Bắt buộc rẽ trái";
    case 37: return "Đường một chiều";
    case 41: return "Camera AI";
    case 40: case 42: case 43: case 44: case 45: case 46: return "Camera giao thông";
    case 56: return "Gờ giảm tốc";
    case 57: return "Người đi bộ";
    case 59: return "Đường cong nguy hiểm";
    case 61: case 75: return "Đèn giao thông";
    default: return "Nguy hiểm phía trước";
    }
}

static const char *alert_caption(int code)
{
    if (code == 1) return "CẢNH SÁT PHÍA TRƯỚC";
    if (code == 2 || code == 3 || (code >= 40 && code <= 46)) return "CAMERA PHÍA TRƯỚC";
    return "CẢNH BÁO PHÍA TRƯỚC";
}

/* Ten huong re theo ma HLP `trn` */
static const char *turn_name(int trn)
{
    switch (trn) {
    case 1: return "Đi thẳng";
    case 2: return "Rẽ trái vào";
    case 3: return "Rẽ phải vào";
    case 4: return "Chếch trái vào";
    case 5: return "Chếch phải vào";
    case 6: return "Rẽ gắt trái vào";
    case 7: return "Rẽ gắt phải vào";
    case 8: case 9: return "Quay đầu";
    case 10: case 11: case 12: return "Vòng xuyến, lối ra";
    case 13: return "Giữ làn trái";
    case 14: return "Giữ làn phải";
    case 15: return "Ra nhánh trái";
    case 16: return "Ra nhánh phải";
    case 17: return "Đến nơi";
    case 19: return "Đi thẳng qua vòng xuyến";
    case 20: return "Vòng xuyến rồi quay đầu";
    default: return "";
    }
}

static int progress(int start, int now)                          /* 0..1000: phan da di */
{
    if (start <= 0 || now < 0 || now >= start) return 0;
    return (start - now) * 1000 / start;
}

static void set_bar(lv_obj_t *track, lv_obj_t *fill, int permille)
{
    int w = lv_obj_get_width(track);
    int fw = w * permille / 1000;
    if (fw > 0 && fw < lv_obj_get_height(track)) fw = lv_obj_get_height(track);
    lv_obj_set_width(fill, fw);
    show(fill, fw > 0);
}

static void beep(int times)
{
    if (snd_on && beep_cb) beep_cb(times);
}

static bool on_highway(void)
{
    if (limit >= HW_KMH) return true;
    static const char *const keys[] = {"Cao tốc", "cao tốc", "CAO TỐC", "CT.", "Expressway"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (strstr(street_now, keys[i])) return true;
    return false;
}

static lv_color_t speed_color(void)
{
    if (speed < 0) return th->mut;
    if (level == UI_LEVEL_OVER) return th->over;
    if (level == UI_LEVEL_NEAR) return th->warn;
    return th->ok;
}

static void render_all(void);
static void settings_render(void);
static void render_standby(void);
static void update_screen(void);

/* ======================= khoi dung chung ======================= */
static ring_t mk_ring(lv_obj_t *p, int x, int y, int size)
{
    ring_t r;
    r.size = size;
    r.ring = mk_fill(p, x, y, size, size, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(r.ring, lv_color_white(), 0);
    lv_obj_set_style_border_color(r.ring, RED_SIGN, 0);
    lv_obj_set_style_border_opa(r.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r.ring, size >= 120 ? size * 16 / 150 : size >= 70 ? 9 : size >= 60 ? 8 : 6, 0);
    r.lbl = mk_label(r.ring, &ui_font_sign24, 0, 0, "-");
    lv_obj_set_style_text_color(r.lbl, lv_color_hex(0x111111), 0);
    return r;
}

static void render_ring(ring_t *r)
{
    char b[12];
    if (limit > 0) snprintf(b, sizeof b, "%d", limit % 1000);
    else snprintf(b, sizeof b, "-");
    lv_label_set_text(r->lbl, b);
    const lv_font_t *f;
    bool three = limit >= 100;
    if (r->size >= 120) f = three ? &ui_font_num54 : &ui_font_sign64;
    else if (r->size >= 70) f = &ui_font_sign24;                 /* bien 72 px (cao toc): so 24-25 px */
    else if (r->size >= 60) f = three ? &ui_font_sign24 : &ui_font_sign32;
    else f = three ? &ui_font_txt14 : &ui_font_txt16;
    lv_obj_set_style_text_font(r->lbl, f, 0);
    lv_obj_center(r->lbl);
}

static spd_t mk_speed(lv_obj_t *p, const lv_font_t *f, int x, int y, int w, const lv_font_t *uf)
{
    spd_t s;
    s.num = mk_label_w(p, f, x, y, w, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s.num, LV_LABEL_LONG_CLIP);
    s.big = f == &ui_font_speed72;
    if (s.big) lv_obj_set_style_text_letter_space(s.num, TIGHT(f), 0);
    s.unit = mk_label_w(p, uf, x, y + baseline(f) + 8, w, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(s.unit, 2, 0);
    lv_label_set_text(s.unit, "KM/H");
    return s;
}

static void render_speed(spd_t *s)
{
    char b[12];
    if (speed < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", speed % 1000);
    lv_label_set_text(s->num, b);
    if (s->big) {                                                  /* 3 chu so: ha co de khong tran cot 100 px */
        const lv_font_t *f = speed >= 100 ? &ui_font_speed62 : &ui_font_speed72;
        lv_obj_set_style_text_font(s->num, f, 0);
        lv_obj_set_style_translate_y(s->num, baseline(&ui_font_speed72) - baseline(f), 0);
    }
    lv_obj_set_style_text_color(s->num, speed_color(), 0);
    lv_obj_set_style_text_color(s->unit, th->mut, 0);
}

static void set_alert_icon(lv_obj_t *img, const ui_alert_t *a)
{
    lv_image_set_src(img, sign_for_alert(a->code));
}

/* "1,2 km" hoac "60 km/h" (giam toc) cho mot canh bao */
static void alert_value(const ui_alert_t *a, char *num, size_t nc, const char **unit)
{
    if (a->code == 8 && a->value > 0) {
        snprintf(num, nc, "%d", a->value % 1000);
        *unit = "km/h";
    } else {
        fmt_dist2(num, nc, unit, a->dist);
    }
}

/* ======================= thanh tren ======================= */
static void build_top(void)
{
    top = mk_box(scr, 0, 0, UI_W, TOP_H);
    t_clock = mk_label(top, &ui_font_clk18, PAD, (TOP_H - ui_font_clk18.line_height) / 2, "--:--");
    t_badge = mk_fill(top, 0, (TOP_H - 18) / 2, 50, 18, 4);
    lv_obj_t *bl = mk_label(t_badge, &ui_font_cap12, 0, 0, "CAO TỐC");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);
    lv_obj_set_width(t_badge, text_w("CAO TỐC", &ui_font_cap12) + 12);
    t_street = mk_label_w(top, &ui_font_txt14, 0, (TOP_H - ui_font_txt14.line_height) / 2, 260, LV_TEXT_ALIGN_CENTER);
    t_src = mk_fill(top, 0, (TOP_H - 18) / 2, 40, 18, 5);          /* o nguon du lieu: "waze" / "GOFA" */
    t_src_l = mk_label(t_src, &ui_font_cap12, 6, (18 - ui_font_cap12.line_height) / 2, "waze");
    t_gps_dot = mk_fill(top, 0, (TOP_H - 8) / 2, 8, 8, LV_RADIUS_CIRCLE);
    top_line = mk_fill(scr, 0, TOP_H - 1, UI_W, 1, 0);
}

static void render_top(void)
{
    bool on = screen != SCR_STANDBY;
    show(top, on);
    show(top_line, on);
    if (!on) return;
    char b[8];
    if (clk_h >= 0) snprintf(b, sizeof b, "%02d:%02d", clk_h % 24, clk_m % 60);
    else snprintf(b, sizeof b, "--:--");
    lv_label_set_text(t_clock, b);
    lv_obj_set_style_text_color(t_clock, th->fg, 0);
    lv_obj_set_style_bg_color(top_line, th->border, 0);

    const char *st = !waze ? "Chỉ có GPS" : street_now;
    lv_label_set_text(t_street, st);
    lv_obj_set_style_text_color(t_street, th->mut, 0);
    bool hw = screen == SCR_HIGHWAY;
    show(t_badge, hw);
    lv_obj_set_style_bg_color(t_badge, th->hw, 0);
    int sw = text_w(st, &ui_font_txt14);
    int maxw = 300 - (hw ? lv_obj_get_width(t_badge) + 6 : 0);
    if (sw > maxw) sw = maxw;
    int total = sw + (hw ? lv_obj_get_width(t_badge) + 6 : 0), x0 = (UI_W - total) / 2;
    if (hw) lv_obj_set_x(t_badge, x0);
    lv_obj_set_x(t_street, x0 + (hw ? lv_obj_get_width(t_badge) + 6 : 0));
    lv_obj_set_width(t_street, sw + 2);

    bool g = src == UI_SRC_GOFA;
    const char *sn = g ? "GOFA" : "waze";
    lv_label_set_text(t_src_l, sn);
    lv_obj_set_style_text_color(t_src_l, g ? lv_color_white() : lv_color_hex(0x05263A), 0);
    lv_obj_set_style_bg_color(t_src, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
    int gw = text_w(sn, &ui_font_cap12) + 12;
    lv_obj_set_width(t_src, gw);
    lv_obj_set_x(t_src, UI_W - PAD - gw);
    lv_obj_set_x(t_gps_dot, UI_W - PAD - gw - 6 - 8);
    lv_obj_set_style_bg_color(t_gps_dot, phone ? th->ok : th->mut, 0);
}

/* ======================= man Toc do (chay thuong) ======================= */
#define C_RING  150
#define C_STRIP_H 40
static void build_cruise(void)
{
    lv_obj_t *p = page[SCR_CRUISE] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int rx = UI_W - 18 - C_RING;
    c_spd = mk_speed(p, &ui_font_speed140, 12, 0, rx - 24, &ui_font_txt16);
    c_ring = mk_ring(p, rx, 18, C_RING);
    for (int i = 0; i < 2; i++) c_sign[i] = mk_image(p, 0, 0, 44, 48);
    c_strip = mk_fill(p, PAD, BODY_H - 12 - C_STRIP_H, UI_W - 2 * PAD, C_STRIP_H, 8);
    c_strip_icon = mk_image(c_strip, 10, (C_STRIP_H - 28) / 2, 28, 48);
    c_strip_txt = mk_label(c_strip, &ui_font_txt16, 0, (C_STRIP_H - ui_font_txt16.line_height) / 2, "");
}

static void render_cruise(void)
{
    render_speed(&c_spd);
    int rx = UI_W - 18 - C_RING;
    int sy = (BODY_H - C_STRIP_H - 12 - (baseline(&ui_font_speed140) + 8 + ui_font_txt16.line_height)) / 2;
    lv_obj_set_y(c_spd.num, sy);
    lv_obj_set_y(c_spd.unit, sy + baseline(&ui_font_speed140) + 8);
    render_ring(&c_ring);

    /* bien tren duong (canh duoi bien gioi han) */
    const ui_alert_t *sg[2] = {sign_now, NULL};
    for (int i = 0; i < al_n && !sg[1]; i++)
        if (&al_list[i] != sign_now && is_road_sign(al_list[i].code) && sign_now) sg[1] = &al_list[i];
    int n = sg[0] ? (sg[1] ? 2 : 1) : 0, gw = n * 44 + (n - 1) * 10;
    for (int i = 0; i < 2; i++) {
        show(c_sign[i], i < n);
        if (i < n) {
            set_alert_icon(c_sign[i], sg[i]);
            lv_obj_set_pos(c_sign[i], rx + (C_RING - gw) / 2 + i * 54, 18 + C_RING + 12);
        }
    }

    /* dai duoi: vuot toc do (do) / canh bao xa (van hien) / duong thoang */
    char b[64];
    lv_obj_t *txt = c_strip_txt;
    bool icon = false;
    if (level == UI_LEVEL_OVER) {
        lv_obj_set_style_bg_color(c_strip, th->over_bg, 0);
        lv_obj_set_style_text_color(txt, th->over_fg, 0);
        snprintf(b, sizeof b, "Vượt tốc độ %d km/h", speed - limit);
        lv_image_set_src(c_strip_icon, &sign_hazard);
        icon = true;
    } else if (ev_now) {
        char d[16];
        fmt_dist(d, sizeof d, ev_now->dist);
        lv_obj_set_style_bg_color(c_strip, th->al_bg, 0);
        lv_obj_set_style_text_color(txt, th->fg, 0);
        snprintf(b, sizeof b, d[0] ? "%s · còn %s" : "%s", alert_name(ev_now->code), d);
        set_alert_icon(c_strip_icon, ev_now);
        icon = true;
    } else {
        lv_obj_set_style_bg_color(c_strip, th->card, 0);
        lv_obj_set_style_text_color(txt, th->mut, 0);
        snprintf(b, sizeof b, "%s", !waze ? "Chưa mở Waze · chỉ có tốc độ GPS" : "Đường thoáng");
    }
    lv_label_set_text(txt, b);
    show(c_strip_icon, icon);
    int tw = text_w(b, &ui_font_txt16), total = tw + (icon ? 28 + 8 : 0);
    int x0 = (UI_W - 2 * PAD - total) / 2;
    lv_obj_set_x(c_strip_icon, x0);
    lv_obj_set_x(txt, x0 + (icon ? 36 : 0));
}

/* ======================= man Canh bao ======================= */
#define A_CELL_H 64
#define A_IMG    112
static void build_alert(void)
{
    lv_obj_t *p = page[SCR_ALERT] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int cw = UI_W - 3 * PAD - SIDE_W, ch = BODY_H - PAD - 10 - A_CELL_H - 12;
    a_card = mk_card(p, PAD, PAD, cw, ch, 12);
    a_img = mk_image(a_card, 10, (ch - 2 - A_IMG) / 2, A_IMG, 48);
    int tx = 10 + A_IMG + 12, tw = cw - 2 - tx - 10;
    a_cap = mk_label_w(a_card, &ui_font_cap12, tx, 22, tw, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(a_cap, 1, 0);
    a_name = mk_label_w(a_card, &ui_font_txt20, tx, 40, tw, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(a_name, LV_LABEL_LONG_WRAP);
    a_dist = mk_label(a_card, &ui_font_num54, tx, 0, "");
    lv_obj_set_style_text_letter_space(a_dist, TIGHT(&ui_font_num54), 0);
    a_unit = mk_label(a_card, &ui_font_unit22, 0, 0, "");
    a_bar = mk_fill(a_card, tx, 0, tw, 6, 3);
    a_fill = mk_fill(a_bar, 0, 0, 0, 6, 3);

    int sx = UI_W - PAD - SIDE_W;
    a_spd = mk_speed(p, &ui_font_speed72, sx, 0, SIDE_W, &ui_font_sm12);
    a_ring = mk_ring(p, sx + (SIDE_W - 64) / 2, 0, 64);
    int cy = BODY_H - 12 - A_CELL_H, cellw = (UI_W - 2 * PAD - 10) / 2;
    for (int i = 0; i < 2; i++) {
        a_cell[i] = mk_fill(p, PAD + i * (cellw + 10), cy, cellw, A_CELL_H, 10);
        a_cell_img[i] = mk_image(a_cell[i], 10, (A_CELL_H - 40) / 2, 40, 48);
        a_cell_t[i] = mk_label_w(a_cell[i], &ui_font_txt14, 60, 12, cellw - 70, LV_TEXT_ALIGN_LEFT);
        a_cell_s[i] = mk_label_w(a_cell[i], &ui_font_sm12, 60, 34, cellw - 70, LV_TEXT_ALIGN_LEFT);
    }
}

static void render_alert(void)
{
    if (!ev_now) return;
    int others = 0;
    for (int i = 0; i < al_n; i++) others += &al_list[i] != ev_now;
    int ch = others ? BODY_H - PAD - 10 - A_CELL_H - 12 : BODY_H - 2 * PAD;   /* khong co o phu: the cao het man */
    lv_obj_set_height(a_card, ch);
    lv_obj_set_y(a_img, (ch - 2 - A_IMG) / 2);
    lv_obj_set_y(a_cap, (ch - 2) / 2 - 64);
    lv_obj_set_y(a_name, (ch - 2) / 2 - 46);
    lv_obj_set_style_bg_color(a_card, th->al_bg, 0);
    lv_obj_set_style_border_color(a_card, th->al_border, 0);
    set_alert_icon(a_img, ev_now);
    lv_label_set_text(a_cap, alert_caption(ev_now->code));
    lv_obj_set_style_text_color(a_cap, th->warn, 0);
    lv_label_set_text(a_name, alert_name(ev_now->code));
    lv_obj_set_style_text_color(a_name, th->fg, 0);
    char num[12];
    const char *unit;
    alert_value(ev_now, num, sizeof num, &unit);
    lv_label_set_text(a_dist, num);
    lv_label_set_text(a_unit, unit);
    lv_obj_set_style_text_color(a_dist, th->al_fg, 0);
    lv_obj_set_style_text_color(a_unit, th->al_fg, 0);
    lv_obj_update_layout(a_name);
    int ny = lv_obj_get_y(a_name) + lv_obj_get_height(a_name) + 6;
    int tx = lv_obj_get_x(a_name);
    lv_obj_set_y(a_dist, ny);
    lv_obj_set_pos(a_unit, tx + text_w_t(num, &ui_font_num54) + 6, ny + baseline(&ui_font_num54) - baseline(&ui_font_unit22));
    lv_obj_set_y(a_bar, ny + baseline(&ui_font_num54) + 10);
    lv_obj_set_style_bg_color(a_bar, th->al_border, 0);
    lv_obj_set_style_bg_color(a_fill, th->al_fg, 0);
    bool bar = ev_now->dist >= 0 && ev_now->code != 8;
    show(a_bar, bar);
    if (bar) set_bar(a_bar, a_fill, progress(ev_start, ev_now->dist));

    /* cot phai: toc do + bien gioi han, can giua theo chieu doc cua the */
    render_speed(&a_spd);
    int colh = baseline(&ui_font_speed72) + 8 + ui_font_sm12.line_height + 10 + 64;
    int y0 = PAD + (ch - colh) / 2;
    lv_obj_set_y(a_spd.num, y0);
    lv_obj_set_y(a_spd.unit, y0 + baseline(&ui_font_speed72) + 8);
    lv_obj_set_y(a_ring.ring, y0 + colh - 64);
    render_ring(&a_ring);

    /* 2 o duoi: cac canh bao / bien khac */
    int k = 0;
    for (int i = 0; i < al_n && k < 2; i++) {
        const ui_alert_t *a = &al_list[i];
        if (a == ev_now) continue;
        lv_obj_set_style_bg_color(a_cell[k], th->card, 0);
        set_alert_icon(a_cell_img[k], a);
        lv_label_set_text(a_cell_t[k], alert_name(a->code));
        lv_obj_set_style_text_color(a_cell_t[k], th->fg, 0);
        char d[16], s[40];
        fmt_dist(d, sizeof d, a->dist);
        if (a->code == 8 && a->value > 0) snprintf(s, sizeof s, "giảm còn %d km/h", a->value);
        else if (d[0]) snprintf(s, sizeof s, "còn %s", d);
        else snprintf(s, sizeof s, "đang hiệu lực");
        lv_label_set_text(a_cell_s[k], s);
        lv_obj_set_style_text_color(a_cell_s[k], th->mut, 0);
        show(a_cell[k], true);
        k++;
    }
    for (; k < 2; k++) show(a_cell[k], false);
}

/* ======================= man Cao toc ======================= */
#define H_CARD_H 80
#define H_CELL_H (BODY_H - 12 - 44 - 10 - (12 + H_CARD_H + 10))   /* 3 o gian kin xuong thanh duoi */
#define H_SIGN   72
static void build_highway(void)
{
    lv_obj_t *p = page[SCR_HIGHWAY] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int lw = UI_W - 3 * PAD - 104;
    h_card = mk_card(p, PAD, 12, lw, H_CARD_H, 10);
    h_img = mk_image(h_card, 12, (H_CARD_H - 2 - 40) / 2, 40, 64);
    lv_obj_set_style_image_recolor(h_img, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(h_img, LV_OPA_COVER, 0);
    lv_image_set_src(h_img, &arrow_straight);
    int tx = 12 + 40 + 12, tw = lw - 2 - tx - 8;
    h_cap = mk_label_w(h_card, &ui_font_cap12, tx, 8, tw, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(h_cap, 1, 0);
    h_name = mk_label_w(h_card, &ui_font_txt20, tx, 24, tw, LV_TEXT_ALIGN_LEFT);
    h_sub = mk_label_w(h_card, &ui_font_txt14, tx, 52, tw, LV_TEXT_ALIGN_LEFT);
    int cy = 12 + H_CARD_H + 10, cw = (lw - 16) / 3;
    static const char *const cap[3] = {"Giới hạn", "Biển báo", "Cảnh báo"};
    for (int i = 0; i < 3; i++) {
        h_cell[i] = mk_fill(p, PAD + i * (cw + 8), cy, cw, H_CELL_H, 10);
        lv_obj_set_style_border_width(h_cell[i], 2, 0);
    }
    int sy = (H_CELL_H - 4 - (H_SIGN + 6 + ui_font_sm12.line_height)) / 2;   /* bien 72 px + chu duoi, giua o */
    for (int i = 0; i < 3; i++) {
        h_cell_l[i] = mk_label_w(h_cell[i], &ui_font_sm12, 0, sy + H_SIGN + 6, cw - 4, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(h_cell_l[i], cap[i]);
    }
    h_ring = mk_ring(h_cell[0], (cw - 4 - H_SIGN) / 2, sy, H_SIGN);
    for (int i = 0; i < 2; i++) {
        h_cell_img[i] = mk_image(h_cell[i + 1], (cw - 4 - H_SIGN) / 2, sy, H_SIGN, 48);
        h_cell_v[i] = mk_label_w(h_cell[i + 1], &ui_font_txt20, 0, sy + (H_SIGN - ui_font_txt20.line_height) / 2, cw - 4,
                                 LV_TEXT_ALIGN_CENTER);
    }
    int sx = UI_W - PAD - 104;
    h_spd = mk_speed(p, &ui_font_speed62, sx, 0, 104, &ui_font_sm12);
    h_min = mk_label_w(p, &ui_font_sm12, sx, 0, 104, LV_TEXT_ALIGN_CENTER);
    h_bar = mk_fill(p, PAD, BODY_H - 12 - 44, UI_W - 2 * PAD, 44, 10);
    int bw = (UI_W - 2 * PAD) / 2;
    h_rest = mk_label_w(h_bar, &ui_font_txt14, 0, (44 - ui_font_txt14.line_height) / 2, bw, LV_TEXT_ALIGN_CENTER);
    h_toll = mk_label_w(h_bar, &ui_font_txt14, bw, (44 - ui_font_txt14.line_height) / 2, bw, LV_TEXT_ALIGN_CENTER);
}

static const ui_alert_t *find_alert(int c1, int c2)
{
    for (int i = 0; i < al_n; i++)
        if (al_list[i].code == c1 || al_list[i].code == c2) return &al_list[i];
    return NULL;
}

static void render_highway(void)
{
    lv_obj_set_style_bg_color(h_card, th->hw_bg, 0);
    lv_obj_set_style_border_color(h_card, th->hw_bd, 0);
    lv_label_set_text(h_cap, "ĐANG ĐI CAO TỐC");
    lv_obj_set_style_text_color(h_cap, th->hw_fg, 0);
    lv_label_set_text(h_name, street_now[0] ? street_now : "Cao tốc");
    lv_obj_set_style_text_color(h_name, lv_color_white(), 0);
    char b[64];
    if (limit > 0) snprintf(b, sizeof b, "Tối đa %d km/h · giữ khoảng cách", limit);
    else snprintf(b, sizeof b, "Giữ khoảng cách an toàn");
    lv_label_set_text(h_sub, b);
    lv_obj_set_style_text_color(h_sub, th->hw_fg, 0);

    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(h_cell[i], th->card, 0);
        lv_obj_set_style_border_color(h_cell[i], i == 0 && level == UI_LEVEL_OVER ? th->over : i == 0 ? th->ok : th->card, 0);
        lv_obj_set_style_text_color(h_cell_l[i], th->mut, 0);
    }
    render_ring(&h_ring);
    const ui_alert_t *it[2] = {sign_now, ev_now};
    for (int i = 0; i < 2; i++) {
        show(h_cell_img[i], it[i] != NULL);
        show(h_cell_v[i], it[i] == NULL);
        lv_label_set_text(h_cell_v[i], "—");
        lv_obj_set_style_text_color(h_cell_v[i], th->mut, 0);
        if (it[i]) {
            set_alert_icon(h_cell_img[i], it[i]);
            char d[16];
            fmt_dist(d, sizeof d, it[i]->dist);
            lv_label_set_text(h_cell_l[i + 1], d[0] ? d : alert_name(it[i]->code));
        } else {
            lv_label_set_text(h_cell_l[i + 1], i == 0 ? "Biển báo" : "Cảnh báo");
        }
    }

    render_speed(&h_spd);
    int colh = baseline(&ui_font_speed62) + 8 + ui_font_sm12.line_height + 10 + ui_font_sm12.line_height;
    int y0 = 12 + (H_CARD_H + 10 + H_CELL_H - colh) / 2;
    lv_obj_set_y(h_spd.num, y0);
    lv_obj_set_y(h_spd.unit, y0 + baseline(&ui_font_speed62) + 8);
    lv_obj_set_y(h_min, y0 + colh - ui_font_sm12.line_height);
    if (limit > 0) snprintf(b, sizeof b, "Tối đa %d", limit);
    else snprintf(b, sizeof b, "Chưa có giới hạn");
    lv_label_set_text(h_min, b);
    lv_obj_set_style_text_color(h_min, th->mut, 0);

    lv_obj_set_style_bg_color(h_bar, th->card, 0);
    const ui_alert_t *rest = find_alert(19, 19), *toll = find_alert(12, 12);
    char d[16];
    fmt_dist(d, sizeof d, rest ? rest->dist : -1);
    snprintf(b, sizeof b, "Trạm dừng nghỉ  %s", d[0] ? d : "—");
    lv_label_set_text(h_rest, b);
    fmt_dist(d, sizeof d, toll ? toll->dist : -1);
    snprintf(b, sizeof b, "Trạm thu phí  %s", d[0] ? d : "—");
    lv_label_set_text(h_toll, b);
    lv_obj_set_style_text_color(h_rest, th->fg, 0);
    lv_obj_set_style_text_color(h_toll, th->fg, 0);
}

/* ======================= man Chi dan (dan duong) ======================= */
#define N_ETA_H 44
#define N_ARROW 84
static void build_nav(void)
{
    lv_obj_t *p = page[SCR_NAV] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int cw = UI_W - 3 * PAD - SIDE_W, ch = BODY_H - PAD - 10 - N_ETA_H - 12;
    n_card = mk_card(p, PAD, PAD, cw, ch, 12);
    n_arrow = mk_image(n_card, 8, 8, N_ARROW, 64);
    lv_obj_set_style_image_recolor(n_arrow, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(n_arrow, LV_OPA_COVER, 0);
    n_exit = mk_label(n_card, &ui_font_txt20, 8 + N_ARROW - 14, 8, "");
    lv_obj_set_style_text_color(n_exit, lv_color_white(), 0);
    int tx = 8 + N_ARROW + 10;
    n_dist = mk_label(n_card, &ui_font_num68, tx, 8, "");
    lv_obj_set_style_text_letter_space(n_dist, TIGHT(&ui_font_num68), 0);
    lv_obj_set_style_text_color(n_dist, lv_color_white(), 0);
    n_unit = mk_label(n_card, &ui_font_unit30, 0, 0, "");
    lv_obj_set_style_text_color(n_unit, lv_color_white(), 0);
    int ty = 8 + baseline(&ui_font_num68) + 6;
    n_turn = mk_label_w(n_card, &ui_font_txt16, tx, ty, cw - tx - 12, LV_TEXT_ALIGN_LEFT);
    int sy = ty + ui_font_txt16.line_height + 10;
    if (sy < 8 + N_ARROW + 10) sy = 8 + N_ARROW + 10;
    n_street = mk_label_w(n_card, &ui_font_txt20, 14, sy, cw - 28, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(n_street, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(n_street, 2 * ui_font_txt20.line_height, 0);
    lv_obj_set_style_text_color(n_street, lv_color_white(), 0);
    n_bar = mk_fill(n_card, 14, ch - 14 - 6, cw - 28 - 92, 6, 3);
    n_fill = mk_fill(n_bar, 0, 0, 0, 6, 3);
    lv_obj_set_style_bg_color(n_fill, lv_color_white(), 0);
    n_next_l = mk_label(n_card, &ui_font_sm12, 0, 0, "Tiếp theo");
    n_next = mk_image(n_card, 0, 0, 30, 64);
    lv_obj_set_style_image_recolor(n_next, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(n_next, LV_OPA_COVER, 0);

    int sx = UI_W - PAD - SIDE_W;
    n_spd = mk_speed(p, &ui_font_speed72, sx, 0, SIDE_W, &ui_font_sm12);
    n_ring = mk_ring(p, sx + (SIDE_W - 64) / 2, 0, 64);
    n_sign = mk_image(p, sx + (SIDE_W - 40) / 2, 0, 40, 48);

    int ey = BODY_H - 12 - N_ETA_H, ew = UI_W - 2 * PAD;
    n_eta = mk_fill(p, PAD, ey, ew, N_ETA_H, 10);
    static const char *const cap[3] = {"Giờ đến", "Còn lại", "Thời gian"};
    for (int i = 0; i < 3; i++) {
        n_eta_v[i] = mk_label_w(n_eta, &ui_font_txt20, i * ew / 3, 0, ew / 3, LV_TEXT_ALIGN_CENTER);
        n_eta_l[i] = mk_label_w(n_eta, &ui_font_sm12, i * ew / 3, 27, ew / 3, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(n_eta_l[i], cap[i]);
    }
    n_band = mk_fill(p, PAD, ey, ew, N_ETA_H, 10);
    n_band_img = mk_image(n_band, 10, (N_ETA_H - 32) / 2, 32, 48);
    n_band_t = mk_label(n_band, &ui_font_txt16, 52, (N_ETA_H - ui_font_txt16.line_height) / 2, "");
    n_band_d = mk_label(n_band, &ui_font_txt20, 0, (N_ETA_H - ui_font_txt20.line_height) / 2, "");
    n_band_bar = mk_fill(n_band, 0, (N_ETA_H - 8) / 2, 60, 8, 4);
    lv_obj_set_style_bg_opa(n_band_bar, LV_OPA_20, 0);
    n_band_fill = mk_fill(n_band_bar, 0, 0, 0, 8, 4);
}

static void render_nav(void)
{
    lv_obj_set_style_bg_color(n_card, th->nav_bg, 0);
    lv_obj_set_style_border_color(n_card, th->nav_bd, 0);
    const lv_image_dsc_t *arr = arrow_for_turn(turn_now);
    if (arr) lv_image_set_src(n_arrow, arr);
    show(n_arrow, arr != NULL);
    show(n_exit, exit_now > 0);
    if (exit_now > 0) lv_label_set_text_fmt(n_exit, "%d", exit_now % 100);

    char num[12];
    const char *unit;
    fmt_dist2(num, sizeof num, &unit, dist_now);
    lv_label_set_text(n_dist, num);
    lv_label_set_text(n_unit, unit);
    int tx = lv_obj_get_x(n_dist);
    lv_obj_set_pos(n_unit, tx + text_w_t(num, &ui_font_num68) + 6, 8 + baseline(&ui_font_num68) - baseline(&ui_font_unit30));
    lv_label_set_text(n_turn, turn_name(turn_now));
    lv_obj_set_style_text_color(n_turn, th->blue_fg, 0);
    lv_label_set_text(n_street, street_nav);

    lv_obj_set_style_bg_color(n_bar, th->blue_dim, 0);
    set_bar(n_bar, n_fill, progress(turn_start, dist_now));
    const lv_image_dsc_t *a2 = turn2_now ? arrow_for_turn(turn2_now) : NULL;
    show(n_next, a2 != NULL);
    show(n_next_l, a2 != NULL);
    if (a2) {
        int cw = lv_obj_get_width(n_card), ch = lv_obj_get_height(n_card);
        lv_image_set_src(n_next, a2);
        lv_obj_set_pos(n_next, cw - 14 - 30, ch - 10 - 30);
        lv_obj_set_pos(n_next_l, cw - 14 - 30 - 6 - text_w("Tiếp theo", &ui_font_sm12), ch - 10 - 15 - ui_font_sm12.line_height / 2);
        lv_obj_set_style_text_color(n_next_l, th->blue_fg, 0);
    }

    /* cot phai */
    render_speed(&n_spd);
    bool sg = sign_now != NULL;
    int ch = lv_obj_get_height(n_card), colh = baseline(&ui_font_speed72) + 8 + ui_font_sm12.line_height + 10 + 64 + (sg ? 8 + 40 : 0);
    int y0 = PAD + (ch - colh) / 2;
    if (y0 < 4) y0 = 4;
    lv_obj_set_y(n_spd.num, y0);
    lv_obj_set_y(n_spd.unit, y0 + baseline(&ui_font_speed72) + 8);
    int ry = y0 + baseline(&ui_font_speed72) + 8 + ui_font_sm12.line_height + 10;
    lv_obj_set_y(n_ring.ring, ry);
    render_ring(&n_ring);
    show(n_sign, sg);
    if (sg) {
        set_alert_icon(n_sign, sign_now);
        lv_obj_set_y(n_sign, ry + 64 + 8);
    }

    /* dai duoi: canh bao (vang) hoac gio den / con lai / thoi gian */
    bool band = ev_now != NULL;
    show(n_band, band);
    show(n_eta, !band && has_route);
    if (band) {
        lv_obj_set_style_bg_color(n_band, th->band, 0);
        lv_color_t ink = lv_color_hex(0x111111);
        set_alert_icon(n_band_img, ev_now);
        lv_label_set_text(n_band_t, alert_name(ev_now->code));
        lv_obj_set_style_text_color(n_band_t, ink, 0);
        char d[16];
        const char *u;
        alert_value(ev_now, num, sizeof num, &u);
        snprintf(d, sizeof d, "%s %s", num, u);
        lv_label_set_text(n_band_d, num[0] ? d : "");
        lv_obj_set_style_text_color(n_band_d, ink, 0);
        int dx = 52 + text_w(alert_name(ev_now->code), &ui_font_txt16) + 12;
        lv_obj_set_x(n_band_d, dx);
        int bx = dx + text_w(d, &ui_font_txt20) + 14, bw = lv_obj_get_width(n_band) - bx - 14;
        bool bar = bw > 40 && ev_now->dist >= 0;
        show(n_band_bar, bar);
        if (bar) {
            lv_obj_set_x(n_band_bar, bx);
            lv_obj_set_width(n_band_bar, bw);
            lv_obj_set_style_bg_color(n_band_bar, ink, 0);
            lv_obj_set_style_bg_color(n_band_fill, ink, 0);
            lv_obj_update_layout(n_band_bar);
            set_bar(n_band_bar, n_band_fill, progress(ev_start, ev_now->dist));
        }
    } else if (has_route) {
        lv_obj_set_style_bg_color(n_eta, th->card, 0);
        char v[3][20];
        snprintf(v[0], sizeof v[0], "%s", eta_txt[0] ? eta_txt : "--:--");
        fmt_dist(v[1], sizeof v[1], eta_rm > 0 ? eta_rm : -1);
        if (!v[1][0]) snprintf(v[1], sizeof v[1], "--");
        if (eta_rmin >= 60) snprintf(v[2], sizeof v[2], "%d giờ %02d", eta_rmin / 60, eta_rmin % 60);
        else if (eta_rmin > 0) snprintf(v[2], sizeof v[2], "%d phút", eta_rmin);
        else snprintf(v[2], sizeof v[2], "--");
        for (int i = 0; i < 3; i++) {
            lv_label_set_text(n_eta_v[i], v[i]);
            lv_obj_set_style_text_color(n_eta_v[i], th->fg, 0);
            lv_obj_set_style_text_color(n_eta_l[i], th->mut, 0);
        }
    }
}

/* ======================= ve tat ca ======================= */
static void render_all(void)
{
    if (!scr || screen < 0) return;
    lv_obj_set_style_bg_color(scr, th->bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    render_top();
    switch (screen) {
    case SCR_CRUISE: render_cruise(); break;
    case SCR_ALERT: render_alert(); break;
    case SCR_HIGHWAY: render_highway(); break;
    case SCR_NAV: render_nav(); break;
    default: break;
    }
    lv_obj_move_foreground(frame);
    if (gofa_dbg && shown(gofa_dbg)) lv_obj_move_foreground(gofa_dbg);
    if (set_scr && shown(set_scr)) lv_obj_move_foreground(set_scr);
    lv_obj_move_foreground(toast);
}

/* ======================= chon man ======================= */
static void set_screen(int s)
{
    if (s == screen) return;
    screen = s;
    for (int i = 0; i < SCR_N; i++) show(page[i], i == s);
    if (level == UI_LEVEL_OVER && s != SCR_STANDBY) lv_timer_resume(blink_tmr);
    else { lv_timer_pause(blink_tmr); show(frame, false); }
    render_all();
    if (s == SCR_STANDBY) render_standby();
}

/* Chua co du lieu -> man cho; dang dan duong -> chi dan; canh bao gan -> canh bao; cao toc; chay thuong. */
static void update_screen(void)
{
    int s;
    if (!sig_ok || (!nav_on && !moving)) s = SCR_STANDBY;
    else if (nav_on) s = SCR_NAV;
    else if (ev_now && ev_now->dist <= ALERT_M) s = SCR_ALERT;
    else if (on_highway()) s = SCR_HIGHWAY;
    else s = SCR_CRUISE;
    set_screen(s);
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
        if (lv == UI_LEVEL_OVER && screen != SCR_STANDBY) {     /* vien do nhap nhay */
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
    update_screen();
    render_all();
}

/* ======================= mau sac ======================= */
static void apply_theme(void)
{
    lv_obj_set_style_bg_color(page[SCR_STANDBY], th->bg, 0);
    lv_obj_set_style_text_color(sb_brand, th->fg, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(flip[i], th->flip_top, 0);
        lv_obj_set_style_border_color(flip[i], th->border, 0);
        lv_obj_set_style_bg_color(flip_bot[i], th->flip_bot, 0);
        lv_obj_set_style_bg_color(flip_line[i], th->flip_line, 0);
    }
    lv_obj_set_style_text_color(lbl_clk[0], th->fg, 0);
    lv_obj_set_style_text_color(lbl_clk[1], th->accent, 0);
    lv_obj_set_style_text_color(dg_h, th->fg, 0);
    lv_obj_set_style_text_color(dg_c, th->accent, 0);
    lv_obj_set_style_text_color(dg_m, th->accent, 0);
    lv_obj_set_style_bg_color(sb_bar, th->card, 0);
    lv_obj_set_style_border_color(sb_bar, th->border, 0);
    lv_obj_set_style_text_color(lbl_date, th->fg, 0);
    lv_obj_set_style_bg_color(toast, th->fg, 0);
    lv_obj_set_style_text_color(lbl_toast, th->bg, 0);
    lv_obj_set_style_border_color(frame, th->over, 0);
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
    int w = text_w(text, &ui_font_txt16) + 48;
    if (w < 200) w = 200;
    if (w > UI_W - 40) w = UI_W - 40;
    lv_obj_set_size(toast, w, 44);
    lv_obj_set_pos(toast, (UI_W - w) / 2, (UI_H - 44) / 2);
    lv_obj_set_width(lbl_toast, w);
    lv_obj_set_pos(lbl_toast, 0, (44 - ui_font_txt16.line_height) / 2);
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
    rot_on = !rot_on;
    ui_toast("Đã xoay màn hình");
    if (set_scr && shown(set_scr)) settings_render();
}

/* Giu tay tren man hinh 5 giay: mo cai dat. */
static uint32_t release_tick;
static bool hold_hint;
static void on_press(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        if (!release_tick || lv_tick_elaps(release_tick) > 600) {
            press_tick = lv_tick_get();
            hold_used = hold_hint = false;
        }
    } else if (c == LV_EVENT_PRESSING) {
        uint32_t held = lv_tick_elaps(press_tick);
        if (held >= 1000 && !hold_hint) {
            hold_hint = hold_used = true;
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

/* ======================= cai dat (mau xanh la) =======================
 * 4 trang: Hien thi (Ngay / Dem / Tu dong, xoay 180 do), Canh bao (loa, am luong), Ket noi (Waze Mod / GOFA, MAC),
 * He thong (phien ban, board). Cham hoac nut BOOT (1 lan = muc ke, 2 lan = chon). Khong cham 10 giay: tu dong.  */
static void style_switch(lv_obj_t *sw, bool on)
{
    lv_obj_set_style_bg_color(sw, on ? S_GREEN : S_TRACK, 0);
    lv_obj_set_style_shadow_width(sw, on ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(sw, S_GREEN, 0);
    lv_obj_set_style_shadow_opa(sw, LV_OPA_40, 0);
    lv_obj_t *knob = lv_obj_get_child(sw, 0);
    lv_obj_set_x(knob, on ? 46 - 3 - 20 : 3);
}

static lv_obj_t *mk_switch(lv_obj_t *p, int x, int y)
{
    lv_obj_t *sw = mk_fill(p, x, y, 46, 26, 13);
    lv_obj_t *k = mk_fill(sw, 3, 3, 20, 20, 10);
    lv_obj_set_style_bg_color(k, lv_color_hex(0xF0FDF4), 0);
    lv_obj_add_flag(k, LV_OBJ_FLAG_EVENT_BUBBLE);
    return sw;
}

static void style_choice(lv_obj_t *o, bool on)
{
    lv_obj_set_style_bg_color(o, on ? S_ON_BG : S_CARD, 0);
    lv_obj_set_style_border_color(o, on ? S_GREEN : S_BORDER, 0);
    lv_obj_set_style_shadow_width(o, on ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(o, S_GREEN, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_30, 0);
}

static lv_obj_t *mk_setting_row(lv_obj_t *p, int y, int h, const char *title, const char *sub, lv_obj_t **sw)
{
    lv_obj_t *r = mk_card(p, 0, y, UI_W - 24, h, 16);
    lv_obj_set_style_bg_color(r, S_CARD, 0);
    lv_obj_set_style_border_color(r, S_BORDER, 0);
    lv_obj_t *dot = mk_fill(r, 12, (h - 2 - 32) / 2, 32, 32, 16);
    lv_obj_set_style_bg_color(dot, S_ON_BG, 0);
    lv_obj_t *dd = mk_fill(dot, 10, 10, 12, 12, 6);
    lv_obj_set_style_bg_color(dd, S_GREEN2, 0);
    lv_obj_t *t = mk_label(r, &ui_font_txt14, 56, h / 2 - 1 - 18, title);
    lv_obj_set_style_text_color(t, S_FG, 0);
    lv_obj_t *s = mk_label(r, &ui_font_sm12, 56, h / 2 - 1 + 2, sub);
    lv_obj_set_style_text_color(s, S_MUT, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    if (sw) *sw = mk_switch(r, UI_W - 24 - 2 - 12 - 46, (h - 2 - 26) / 2);
    if (sw) lv_obj_add_flag(*sw, LV_OBJ_FLAG_EVENT_BUBBLE);
    return r;
}

static int focus_items(lv_obj_t **out)
{
    int n = 0;
    out[n++] = set_tab[set_page];
    switch (set_page) {
    case PG_DISPLAY:
        for (int i = 0; i < 3; i++) out[n++] = mode_card[i];
        out[n++] = rot_row;
        break;
    case PG_ALERT:
        out[n++] = snd_row;
        if (snd_on) { out[n++] = vol_minus; out[n++] = vol_plus; }
        break;
    case PG_LINK:
        out[n++] = src_card[0];
        out[n++] = src_card[1];
        break;
    default:
        break;
    }
    out[n++] = set_close;
    return n;
}

static void focus_render(void)
{
    lv_obj_t *all[] = {set_tab[0], set_tab[1], set_tab[2], set_tab[3], mode_card[0], mode_card[1], mode_card[2], rot_row,
                       snd_row, vol_minus, vol_plus, src_card[0], src_card[1], set_close};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_obj_set_style_outline_width(all[i], 0, 0);
    if (set_focus < 0) return;
    lv_obj_t *it[10];
    int n = focus_items(it);
    if (set_focus >= n) set_focus = n - 1;
    lv_obj_t *o = it[set_focus];
    lv_obj_set_style_outline_width(o, 3, 0);
    lv_obj_set_style_outline_pad(o, -4, 0);                       /* ve vao trong: khong bi cat o mep trang */
    lv_obj_set_style_outline_color(o, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_COVER, 0);
}

static void settings_render(void)
{
    for (int i = 0; i < PG_N; i++) {
        bool cur = set_page == i;
        lv_obj_set_style_bg_color(set_tab[i], cur ? S_ON_BG : S_CARD, 0);
        lv_obj_set_style_border_color(set_tab[i], cur ? S_GREEN : S_BORDER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(set_tab[i], 0), cur ? S_GREEN3 : lv_color_hex(0xA8B5B2), 0);
        show(set_page_obj[i], cur);
    }
    lv_label_set_text(lv_obj_get_child(set_pill_phone, 1), phone ? "Điện thoại" : "Chưa nối");
    lv_obj_set_style_bg_color(lv_obj_get_child(set_pill_phone, 0), phone ? S_GREEN2 : S_MUT, 0);
    lv_label_set_text(lv_obj_get_child(set_pill_src, 1), src == UI_SRC_GOFA ? "GOFA" : "Waze");
    /* Hien thi */
    for (int i = 0; i < 3; i++) {
        int m = i == 0 ? UI_MODE_DAY : i == 1 ? UI_MODE_NIGHT : UI_MODE_AUTO;
        style_choice(mode_card[i], mode == m);
        lv_obj_set_style_text_color(mode_sub[i], mode == m ? S_GREEN3 : S_MUT, 0);
    }
    style_switch(rot_sw, rot_on);
    /* Canh bao */
    style_switch(snd_sw, snd_on);
    show(vol_card, snd_on);
    lv_label_set_text_fmt(lbl_vol, "%d%%", snd_vol);
    set_bar(vol_bar, vol_fill, snd_vol * 10);
    /* Ket noi */
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == (src == UI_SRC_GOFA);
        style_choice(src_card[i], on);
        lv_label_set_text(lbl_src_st[i], on ? "Đang dùng" : "Bấm để chọn");
        lv_obj_set_style_text_color(lbl_src_st[i], on ? S_GREEN3 : S_MUT, 0);
    }
    lv_label_set_text_fmt(lbl_mac, "MAC Bluetooth  %s", mac_txt[0] ? mac_txt : "--");
    /* He thong */
    lv_label_set_text_fmt(lbl_sys[0], "Phiên bản  %s", ver_txt[0] ? ver_txt : "--");
    lv_label_set_text_fmt(lbl_sys[2], "Nguồn dữ liệu  %s", src == UI_SRC_GOFA ? "GOFA" : "Waze Mod");
    focus_render();
}

static void settings_close(void)
{
    lv_timer_pause(set_tmr);
    show(set_scr, false);
    ui_toast("Đã lưu cài đặt");
}

static void set_tmr_cb(lv_timer_t *t)
{
    (void)t;
    if (set_page == PG_LINK || (rb_scr && shown(rb_scr))) return;   /* trang Ket noi (co MAC): khong tu dong dong */
    settings_close();
}

static void sound_changed(void)
{
    if (sound_cb) sound_cb(snd_on, snd_vol);
    settings_render();
    beep(1);                                                       /* nghe thu muc am moi */
}

/* Doi nguon: luu ngay vao NVS roi dung o man MAC Bluetooth; nguoi dung bam RST. Cham / BOOT: huy. */
static void rb_cancel(void)
{
    if (source_cb) source_cb(src);
    show(rb_scr, false);
    lv_timer_reset(set_tmr);
    lv_timer_resume(set_tmr);
}

static void rb_start(int want)
{
    if (source_cb) source_cb(want);
    lv_timer_pause(set_tmr);
    if (rb_tile) lv_obj_delete(rb_tile);
    rb_tile = ui_partner_tile(rb_scr, want, 72);
    lv_obj_set_pos(rb_tile, (UI_W - 72) / 2, 30);
    lv_obj_add_flag(rb_tile, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(lbl_rb_title, want == UI_SRC_GOFA ? "Đã chọn GOFA" : "Đã chọn Waze Mod");
    lv_label_set_text(lbl_rb_mac, mac_txt[0] ? mac_txt : "--");
    show(rb_scr, true);
    lv_obj_move_foreground(rb_scr);
}

static void on_set_btn(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target(e);
    lv_timer_reset(set_tmr);
    if (o == rb_scr) { rb_cancel(); return; }
    if (o == set_close) { settings_close(); return; }
    for (int i = 0; i < PG_N; i++)
        if (o == set_tab[i]) { set_page = i; if (set_focus > 0) set_focus = 0; settings_render(); return; }
    for (int i = 0; i < 3; i++)
        if (o == mode_card[i]) {
            int m = i == 0 ? UI_MODE_DAY : i == 1 ? UI_MODE_NIGHT : UI_MODE_AUTO;
            ui_set_mode(m);
            if (mode_cb) mode_cb(mode);
            settings_render();
            return;
        }
    if (o == rot_row) { ui_rotate(); return; }
    if (o == src_card[0] || o == src_card[1]) {
        int want = o == src_card[1] ? UI_SRC_GOFA : UI_SRC_WAZE;
        if (want != src) rb_start(want);
        return;
    }
    if (o == snd_row) snd_on = !snd_on;
    else if (o == vol_minus) snd_vol = snd_vol > 10 ? snd_vol - 10 : 10;
    else if (o == vol_plus) snd_vol = snd_vol < 100 ? snd_vol + 10 : 100;
    else return;
    sound_changed();
}

static void clickable(lv_obj_t *o)
{
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, on_set_btn, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *mk_pill(lv_obj_t *p, int x, const char *txt)
{
    int w = text_w(txt, &ui_font_sm12) + 34;
    lv_obj_t *o = mk_card(p, x - w, 4, w, 28, 14);
    lv_obj_set_style_bg_color(o, S_ON_BG, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0x1E4D33), 0);
    lv_obj_t *d = mk_fill(o, 10, (26 - 7) / 2, 7, 7, 4);
    lv_obj_set_style_bg_color(d, S_GREEN2, 0);
    lv_obj_t *l = mk_label(o, &ui_font_sm12, 22, (26 - ui_font_sm12.line_height) / 2, txt);
    lv_obj_set_style_text_color(l, S_GREEN3, 0);
    return o;
}

static void build_settings(void)
{
    set_scr = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_color(set_scr, S_BG, 0);
    lv_obj_set_style_bg_opa(set_scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(set_scr, LV_OBJ_FLAG_CLICKABLE);              /* cham nen: khong doi Ngay/Dem */
    lv_obj_add_event_cb(set_scr, on_set_btn, LV_EVENT_CLICKED, NULL);

    /* dau trang */
    lv_obj_t *l = mk_label(set_scr, &ui_font_cap12, 12, 10, "EVCARHUD");
    lv_obj_set_style_text_letter_space(l, 4, 0);
    lv_obj_set_style_text_color(l, S_FG, 0);
    l = mk_label(set_scr, &ui_font_sm12, 12, 26, "Cài đặt màn hình");
    lv_obj_set_style_text_color(l, S_GREEN2, 0);
    set_close = mk_card(set_scr, UI_W - 12 - 36, 6, 36, 32, 16);
    lv_obj_set_style_bg_color(set_close, S_CARD, 0);
    lv_obj_set_style_border_color(set_close, S_BORDER, 0);
    l = mk_label(set_close, &ui_font_txt16, 0, 0, "X");
    lv_obj_set_style_text_color(l, S_FG, 0);
    lv_obj_center(l);
    clickable(set_close);
    int px = UI_W - 12 - 36 - 8;                                   /* hai the trang thai, xep tu phai sang trai */
    set_pill_src = mk_pill(set_scr, px, "GOFA");
    px -= text_w("GOFA", &ui_font_sm12) + 34 + 8;
    set_pill_phone = mk_pill(set_scr, px, "Điện thoại");

    /* tab */
    static const char *const tabs[PG_N] = {"Hiển thị", "Cảnh báo", "Kết nối", "Hệ thống"};
    int tw = (UI_W - 24 - 3 * 6) / 4;
    for (int i = 0; i < PG_N; i++) {
        set_tab[i] = mk_card(set_scr, 12 + i * (tw + 6), 46, tw, 34, 17);
        lv_obj_t *t = mk_label(set_tab[i], &ui_font_txt14, 0, 0, tabs[i]);
        lv_obj_center(t);
        clickable(set_tab[i]);
        set_page_obj[i] = mk_box(set_scr, 12, 90, UI_W - 24, UI_H - 90 - 10);
    }

    /* Hien thi: Ngay / Dem / Tu dong + xoay 180 do */
    lv_obj_t *p = set_page_obj[PG_DISPLAY];
    static const char *const mn[3][2] = {{"Ngày", "Nền sáng"}, {"Đêm", "Nền tối"}, {"Tự động", "Theo giờ"}};
    int mw = (UI_W - 24 - 16) / 3;
    for (int i = 0; i < 3; i++) {
        mode_card[i] = mk_card(p, i * (mw + 8), 0, mw, 92, 14);
        lv_obj_t *t = mk_label_w(mode_card[i], &ui_font_txt20, 0, 22, mw - 2, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(t, mn[i][0]);
        lv_obj_set_style_text_color(t, S_FG, 0);
        mode_sub[i] = mk_label_w(mode_card[i], &ui_font_sm12, 0, 52, mw - 2, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(mode_sub[i], mn[i][1]);
        clickable(mode_card[i]);
    }
    rot_row = mk_setting_row(p, 104, 62, "Xoay màn hình 180°", "Khi lắp board ngược trong xe (hoặc bấm BOOT 2 lần)", &rot_sw);
    lv_obj_add_event_cb(rot_row, on_set_btn, LV_EVENT_CLICKED, NULL);
    l = mk_label_w(p, &ui_font_sm12, 0, 180, UI_W - 24, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(l, "Chạm màn hình ngoài cài đặt cũng đổi Tự động, Ngày, Đêm.");
    lv_obj_set_style_text_color(l, S_MUT, 0);

    /* Canh bao: loa + am luong */
    p = set_page_obj[PG_ALERT];
    snd_row = mk_setting_row(p, 0, 62, "Âm thanh cảnh báo", "Tít khi vượt tốc độ, có cảnh báo mới, còn 100 m tới chỗ rẽ", &snd_sw);
    lv_obj_add_event_cb(snd_row, on_set_btn, LV_EVENT_CLICKED, NULL);
    vol_card = mk_card(p, 0, 74, UI_W - 24, 136, 16);
    lv_obj_set_style_bg_color(vol_card, S_CARD, 0);
    lv_obj_set_style_border_color(vol_card, S_BORDER, 0);
    l = mk_label(vol_card, &ui_font_txt14, 14, 10, "Âm lượng");
    lv_obj_set_style_text_color(l, S_FG, 0);
    int bw = 96, bh = 70, by = 36;
    vol_minus = mk_card(vol_card, 12, by, bw, bh, 16);
    vol_plus = mk_card(vol_card, UI_W - 24 - 2 - 12 - bw, by, bw, bh, 16);
    lv_obj_t *bt[2] = {vol_minus, vol_plus};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(bt[i], S_ON_BG, 0);
        lv_obj_set_style_border_color(bt[i], S_GREEN, 0);
        lv_obj_t *h = mk_fill(bt[i], (bw - 2 - 34) / 2, (bh - 2 - 6) / 2, 34, 6, 3);
        lv_obj_set_style_bg_color(h, S_GREEN3, 0);
        if (i == 1) {
            lv_obj_t *v = mk_fill(bt[i], (bw - 2 - 6) / 2, (bh - 2 - 34) / 2, 6, 34, 3);
            lv_obj_set_style_bg_color(v, S_GREEN3, 0);
        }
        clickable(bt[i]);
    }
    int cx = 12 + bw + 10, cwid = UI_W - 24 - 2 - 2 * cx;
    lbl_vol = mk_label_w(vol_card, &ui_font_num40, cx, by + 2, cwid, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_vol, S_GREEN2, 0);
    vol_bar = mk_fill(vol_card, cx + 6, by + bh - 14, cwid - 12, 8, 4);
    lv_obj_set_style_bg_color(vol_bar, S_TRACK, 0);
    vol_fill = mk_fill(vol_bar, 0, 0, 0, 8, 4);
    lv_obj_set_style_bg_color(vol_fill, S_GREEN, 0);

    /* Ket noi: Waze Mod | GOFA */
    p = set_page_obj[PG_LINK];
    static const char *const names[2] = {"Waze Mod", "GOFA"};
    int cw = (UI_W - 24 - 10) / 2, ch = 160;
    for (int i = 0; i < 2; i++) {
        src_card[i] = mk_card(p, i * (cw + 10), 0, cw, ch, 16);
        clickable(src_card[i]);
        lv_obj_t *t = ui_partner_tile(src_card[i], i, 64);
        lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_pos(t, (cw - 2 - 64) / 2, 16);
        lv_obj_t *n = mk_label_w(src_card[i], &ui_font_txt16, 0, 92, cw - 2, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(n, names[i]);
        lv_obj_set_style_text_color(n, S_FG, 0);
        lbl_src_st[i] = mk_label_w(src_card[i], &ui_font_sm12, 0, 116, cw - 2, LV_TEXT_ALIGN_CENTER);
    }
    lbl_mac = mk_label_w(p, &ui_font_txt14, 0, ch + 10, UI_W - 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_mac, S_MUT, 0);
    lbl_ble = mk_label_w(p, &ui_font_sm12, 0, ch + 30, UI_W - 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_ble, S_MUT, 0);
    lv_label_set_text(lbl_ble, "Đổi nguồn xong bấm RST để khởi động lại");

    /* He thong */
    p = set_page_obj[PG_SYSTEM];
    lv_obj_t *box = mk_card(p, 0, 0, UI_W - 24, 200, 16);
    lv_obj_set_style_bg_color(box, S_CARD, 0);
    lv_obj_set_style_border_color(box, S_BORDER, 0);
    for (int i = 0; i < 4; i++) {
        lbl_sys[i] = mk_label_w(box, &ui_font_txt14, 16, 16 + i * 44, UI_W - 24 - 34, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lbl_sys[i], S_FG, 0);
        if (i) {
            lv_obj_t *ln = mk_fill(box, 16, 6 + i * 44, UI_W - 24 - 34, 1, 0);
            lv_obj_set_style_bg_color(ln, S_BORDER, 0);
        }
    }
    lv_label_set_text(lbl_sys[1], "Board  ESP32-P4 · màn 3.5\" ST7796 480×320");
    lv_label_set_text(lbl_sys[3], "Giữ BOOT 5 giây: bật / tắt chế độ trình diễn");

    /* doi nguon: "Da chon GOFA" + MAC Bluetooth, cho bam RST; cham de huy */
    rb_scr = mk_box(set_scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_color(rb_scr, S_BG, 0);
    lv_obj_set_style_bg_opa(rb_scr, LV_OPA_COVER, 0);
    clickable(rb_scr);
    lbl_rb_title = mk_label_w(rb_scr, &ui_font_txt20, 0, 116, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(lbl_rb_title, S_FG, 0);
    l = mk_label_w(rb_scr, &ui_font_txt14, 0, 152, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "MAC Bluetooth");
    lv_obj_set_style_text_color(l, S_MUT, 0);
    lbl_rb_mac = mk_label_w(rb_scr, &ui_font_num40, 0, 172, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(lbl_rb_mac, &ui_font_dist24, 0);
    lv_obj_set_style_text_color(lbl_rb_mac, S_GREEN2, 0);
    l = mk_label_w(rb_scr, &ui_font_txt16, 0, 222, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Bấm RST để khởi động lại");
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFB020), 0);
    l = mk_label_w(rb_scr, &ui_font_sm12, 0, 270, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Chạm màn hình hoặc bấm BOOT để huỷ");
    lv_obj_set_style_text_color(l, S_MUT, 0);
    show(rb_scr, false);
    show(set_scr, false);
    set_tmr = lv_timer_create(set_tmr_cb, 10000, NULL);
    lv_timer_pause(set_tmr);
}

static void settings_open(void)
{
    set_page = PG_DISPLAY;
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
void ui_open_settings_page(int pg)
{
    settings_open();
    set_page = pg >= 0 && pg < PG_N ? pg : 0;
    settings_render();
}

void ui_set_source(int s) { src = s == UI_SRC_GOFA ? UI_SRC_GOFA : UI_SRC_WAZE; render_standby(); }
void ui_set_source_hook(void (*cb)(int)) { source_cb = cb; }
void ui_test_switch(int want) { rb_start(want); }
void ui_set_version(const char *v) { snprintf(ver_txt, sizeof ver_txt, "%s", v ? v : ""); }
void ui_set_rotated(bool on) { rot_on = on; }

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
        gofa_dbg = mk_box(scr, 0, UI_H - 56, UI_W, 56);
        lv_obj_set_style_bg_color(gofa_dbg, lv_color_hex(0x05080C), 0);
        lv_obj_set_style_bg_opa(gofa_dbg, LV_OPA_COVER, 0);
        lbl_gofa_dbg[0] = mk_label_w(gofa_dbg, &ui_font_sm12, 8, 3, UI_W - 16, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lbl_gofa_dbg[0], lv_color_hex(0x22D37A), 0);
        lbl_gofa_dbg[1] = mk_label_w(gofa_dbg, &ui_font_mono14, 8, 20, UI_W - 16, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lbl_gofa_dbg[1], lv_color_white(), 0);
        lv_label_set_long_mode(lbl_gofa_dbg[1], LV_LABEL_LONG_CLIP);
        lv_obj_set_height(lbl_gofa_dbg[1], 34);
    }
    lv_label_set_text(lbl_gofa_dbg[0], state ? state : "");
    lv_label_set_text(lbl_gofa_dbg[1], last && last[0] ? last : "Chờ app GOFA gửi dữ liệu...");
    show(gofa_dbg, true);
    lv_obj_move_foreground(gofa_dbg);
    if (shown(set_scr)) lv_obj_move_foreground(set_scr);
    lv_obj_move_foreground(toast);
}

void ui_set_mac(const char *mac) { snprintf(mac_txt, sizeof mac_txt, "%s", mac ? mac : ""); }

bool ui_settings_key(int key)
{
    if (!ui_settings_shown()) return false;
    lv_timer_reset(set_tmr);
    if (shown(rb_scr)) { rb_cancel(); ui_toast("Đã huỷ"); return true; }
    lv_obj_t *it[10];
    int n = focus_items(it);
    if (key == UI_KEY_NEXT) {
        set_focus = set_focus < 0 ? 1 : (set_focus + 1) % n;
        settings_render();
        beep(1);
        return true;
    }
    if (set_focus < 0) set_focus = 1;
    lv_obj_t *o = it[set_focus < n ? set_focus : n - 1];
    if (o == set_tab[set_page]) {                                  /* o tab: sang trang ke */
        set_page = (set_page + 1) % PG_N;
        set_focus = 0;
        settings_render();
        beep(1);
        return true;
    }
    lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
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

/* ======================= man cho ======================= */
#define FLIP_W 170
#define FLIP_H 140
#define FLIP_Y 62
static void build_standby(void)
{
    lv_obj_t *p = page[SCR_STANDBY] = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    sb_brand = mk_label(p, &ui_font_cap12, 16, 18, "EVCARHUD");
    lv_obj_set_style_text_letter_space(sb_brand, 4, 0);
    sb_stat = mk_label_w(p, &ui_font_txt14, 140, 16, UI_W - 140 - 16, LV_TEXT_ALIGN_RIGHT);
    int gap = 16, x0 = (UI_W - 2 * FLIP_W - gap) / 2;
    for (int i = 0; i < 2; i++) {
        flip[i] = mk_card(p, x0 + i * (FLIP_W + gap), FLIP_Y, FLIP_W, FLIP_H, 18);
        lv_obj_set_style_clip_corner(flip[i], true, 0);
        flip_bot[i] = mk_box(flip[i], 0, FLIP_H / 2 - 1, FLIP_W - 2, FLIP_H / 2 - 1);
        lv_obj_set_style_bg_opa(flip_bot[i], LV_OPA_COVER, 0);
        lbl_clk[i] = mk_label_w(flip[i], &ui_font_clock88, 0, 0, FLIP_W - 2, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_y(lbl_clk[i], (FLIP_H - 2) / 2 - baseline(&ui_font_clock88) + 32);   /* giua chu so (cao ~64 px) tren khe */
        flip_line[i] = mk_box(flip[i], 0, FLIP_H / 2 - 2, FLIP_W - 2, 3);
        lv_obj_set_style_bg_opa(flip_line[i], LV_OPA_COVER, 0);
    }
    /* chua ket noi dien thoai: dong ho so 24 gio HH:MM, dau ":" nhap nhay */
    const lv_font_t *df = &ui_font_clock120;
    int w2 = text_w("00", df) + 8, wc = text_w(":", df) + 8, dx = (UI_W - 2 * w2 - wc) / 2;
    int dy = (FLIP_Y + FLIP_H / 2) - (baseline(df) - 44);         /* giua chu so (cao ~88 px) */
    dg_h = mk_label_w(p, df, dx, dy, w2, LV_TEXT_ALIGN_CENTER);
    dg_c = mk_label_w(p, df, dx + w2, dy, wc, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(dg_c, ":");
    dg_m = mk_label_w(p, df, dx + w2 + wc, dy, w2, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(dg_h, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(dg_m, LV_LABEL_LONG_CLIP);
    sb_bar = mk_card(p, 16, UI_H - 16 - 48, UI_W - 32, 48, 12);
    lbl_date = mk_label_w(sb_bar, &ui_font_txt16, 14, (46 - ui_font_txt16.line_height) / 2, 260, LV_TEXT_ALIGN_LEFT);
    lbl_gps = mk_label_w(sb_bar, &ui_font_txt14, UI_W - 32 - 2 - 14 - 160, (46 - ui_font_txt14.line_height) / 2, 160,
                         LV_TEXT_ALIGN_RIGHT);
}

static void colon_cb(lv_timer_t *t)
{
    (void)t;
    if (screen != SCR_STANDBY || phone || clk_h < 0) lv_obj_set_style_text_opa(dg_c, LV_OPA_COVER, 0);
    else lv_obj_set_style_text_opa(dg_c, lv_obj_get_style_text_opa(dg_c, 0) ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static void render_standby(void)
{
    if (!page[SCR_STANDBY]) return;
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
    for (int i = 0; i < 2; i++) show(flip[i], phone);              /* da ket noi: dong ho lat; chua: dong ho so */
    show(dg_h, !phone);
    show(dg_c, !phone);
    show(dg_m, !phone);
    if (day > 0 && month > 0 && wday >= 0) snprintf(b, sizeof b, "%s, %d Tháng %d", WDAY_NAME[wday % 7], day, month);
    else if (wday >= 0) snprintf(b, sizeof b, "%s", WDAY_NAME[wday % 7]);
    else snprintf(b, sizeof b, "Chờ giờ từ điện thoại");
    lv_label_set_text(lbl_date, b);
    lv_label_set_text(sb_stat, phone ? "Đã kết nối điện thoại"
                               : src == UI_SRC_GOFA ? "Chờ kết nối GOFA qua điện thoại" : "Chờ kết nối Waze qua điện thoại");
    lv_obj_set_style_text_color(sb_stat, th->accent, 0);
    lv_obj_set_style_text_opa(sb_stat, phone ? LV_OPA_COVER : LV_OPA_80, 0);
    const char *g = phone ? "GPS sẵn sàng" : (clk_h >= 0 && !clk_synced) ? "Giờ chưa chỉnh" : "Chưa có GPS";
    lv_label_set_text(lbl_gps, g);
    lv_obj_set_style_text_color(lbl_gps, phone ? th->accent : th->mut, 0);
}

/* ======================= man khoi dong / ket noi ======================= */
/* Theo bo man mau "Khoi dong · Logo": logo that (vong dong ho vach cyan, 2 vach cuoi do, kim trang, chu EVCar + HUD cyan,
 * ui_logo.c sinh tu tools/logo), nen 0x020617.
 *  - Khoi dong: logo, thanh tien trinh, "Dang khoi dong..."
 *  - Ket noi Waze / GOFA: logo, "KET NOI VOI", o nguon + ten nguon, dong trang thai duoi. */
extern const lv_image_dsc_t ui_logo;
#define SP_BG     0x020617
#define SP_LOGO_Y 46
#define SP_ROW_Y  (SP_LOGO_Y + 136 + 6)

static lv_obj_t *mk_splash_base(lv_obj_t *s)
{
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(SP_BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_t *img = lv_image_create(s);
    lv_image_set_src(img, &ui_logo);
    lv_obj_set_pos(img, (UI_W - (int)ui_logo.header.w) / 2, SP_LOGO_Y);
    return img;
}

/* "KET NOI VOI" + o nguon 44 px + "Waze Mod" / "GOFA" (mot khoi de an/hien cung luc) */
static lv_obj_t *mk_connect_row(lv_obj_t *s, int source)
{
    lv_obj_t *box = mk_box(s, 0, SP_ROW_Y, UI_W, 70);
    lv_obj_t *cap = mk_label_w(box, &ui_font_cap12, 0, 0, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(cap, "KẾT NỐI VỚI");
    lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x94A3B8), 0);
    bool g = source == UI_SRC_GOFA;
    const char *nm = g ? "GOFA" : "Waze Mod";
    int tw = 44 + 12 + text_w(nm, &ui_font_unit22), x0 = (UI_W - tw) / 2;
    lv_obj_t *tile = ui_partner_tile(box, source, 44);
    lv_obj_set_pos(tile, x0, 24);
    lv_obj_t *n = mk_label(box, &ui_font_unit22, x0 + 56, 24 + (44 - ui_font_unit22.line_height) / 2, nm);
    lv_obj_set_style_text_color(n, lv_color_hex(0xE2E8F0), 0);
    return box;
}

/* dong duoi cung: cham mau + chu, can giua */
static void mk_status_line(lv_obj_t *s, bool dot, lv_color_t col, const char *txt)
{
    const lv_font_t *f = &ui_font_sm12;
    int w = text_w(txt, f) + (dot ? 13 : 0), x0 = (UI_W - w) / 2, y = UI_H - 12 - f->line_height;
    if (dot) {
        lv_obj_t *d = mk_fill(s, x0, y + (f->line_height - 7) / 2, 7, 7, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x22C55E), 0);
    }
    lv_obj_t *l = mk_label(s, f, x0 + (dot ? 13 : 0), y, txt);
    lv_obj_set_style_text_color(l, col, 0);
}

static lv_obj_t *sp_boot, *sp_fill, *sp_conn;

static void sp_fill_cb(void *o, int32_t v) { lv_obj_set_width((lv_obj_t *)o, v); }

static void sp_phase2_cb(lv_timer_t *t)                         /* khoi dong xong: hien "Ket noi voi" */
{
    (void)t;
    if (!sp_boot || !sp_conn) return;
    show(sp_boot, false);
    show(sp_conn, true);
    lv_obj_fade_in(sp_conn, 400, 0);
    sp_boot = sp_conn = NULL;
}

static void sp_deleted_cb(lv_event_t *e)
{
    (void)e;
    sp_boot = sp_fill = sp_conn = NULL;
}

/* Man khoi dong (3 giay tren board): 1,6 giay dau thanh tien trinh chay, sau do hien nguon dang chon (Waze / GOFA). */
lv_obj_t *ui_splash_create(const char *version, int source)
{
    lv_obj_t *s = lv_obj_create(NULL);
    mk_splash_base(s);
    lv_obj_add_event_cb(s, sp_deleted_cb, LV_EVENT_DELETE, NULL);
    sp_boot = mk_box(s, 0, SP_ROW_Y + 8, UI_W, 40);
    lv_obj_t *bar = mk_fill(sp_boot, (UI_W - 200) / 2, 0, 200, 6, 3);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E293B), 0);
    sp_fill = mk_fill(bar, 0, 0, 6, 6, 3);
    lv_obj_set_style_bg_color(sp_fill, lv_color_hex(0x22D3EE), 0);
    lv_obj_t *l = mk_label_w(sp_boot, &ui_font_sm12, 0, 14, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Đang khởi động…");
    lv_obj_set_style_text_color(l, lv_color_hex(0x94A3B8), 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sp_fill);
    lv_anim_set_exec_cb(&a, sp_fill_cb);
    lv_anim_set_values(&a, 6, 200);
    lv_anim_set_duration(&a, 1400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    sp_conn = mk_connect_row(s, source);
    show(sp_conn, false);
    lv_timer_t *t = lv_timer_create(sp_phase2_cb, 1600, NULL);
    lv_timer_set_repeat_count(t, 1);
    char b[64];
    if (version && version[0]) snprintf(b, sizeof b, "ESP32-P4 · 3.5 inch · %s", version);
    else snprintf(b, sizeof b, "ESP32-P4 · 3.5 inch");
    mk_status_line(s, false, lv_color_hex(0x64748B), b);
    return s;
}

/* Vua noi dien thoai: hien man "Ket noi voi Waze / GOFA · Da ket noi dien thoai" 2,5 giay (cham de tat). */
static lv_obj_t *conn_ov;
static lv_timer_t *conn_tmr;

static void conn_close(void)
{
    if (conn_tmr) lv_timer_delete(conn_tmr);
    conn_tmr = NULL;
    if (conn_ov) lv_obj_delete(conn_ov);
    conn_ov = NULL;
}

static void conn_tmr_cb(lv_timer_t *t)
{
    (void)t;
    conn_tmr = NULL;                                             /* lan chay cuoi: LVGL tu xoa timer */
    if (conn_ov) lv_obj_delete(conn_ov);
    conn_ov = NULL;
}

static void conn_click_cb(lv_event_t *e)
{
    (void)e;
    conn_close();
}

static void show_connected(void)
{
    conn_close();
    conn_ov = mk_box(lv_layer_top(), 0, 0, UI_W, UI_H);
    lv_obj_add_flag(conn_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(conn_ov, conn_click_cb, LV_EVENT_CLICKED, NULL);
    mk_splash_base(conn_ov);
    mk_connect_row(conn_ov, src);
    mk_status_line(conn_ov, true, lv_color_hex(0x86EFAC), "Đã kết nối điện thoại");
    lv_obj_fade_in(conn_ov, 250, 0);
    conn_tmr = lv_timer_create(conn_tmr_cb, 2500, NULL);
    lv_timer_set_repeat_count(conn_tmr, 1);
}

/* ======================= dung giao dien ======================= */
void dashboard_create(lv_obj_t *screen_obj)
{
    scr = screen_obj;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_click, LV_EVENT_SHORT_CLICKED, NULL);   /* cham: Ngay/Dem; giu 5 giay: cai dat */
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, on_press, LV_EVENT_RELEASED, NULL);

    build_top();
    build_cruise();
    build_alert();
    build_highway();
    build_nav();
    build_standby();
    colon_tmr = lv_timer_create(colon_cb, 500, NULL);

    frame = mk_box(scr, 0, 0, UI_W, UI_H);                         /* vien do nhap nhay khi vuot toc do */
    lv_obj_set_style_border_width(frame, 5, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    show(frame, false);
    blink_tmr = lv_timer_create(blink_cb, 400, NULL);
    lv_timer_pause(blink_tmr);
    over_tmr = lv_timer_create(over_cb, 8000, NULL);
    lv_timer_pause(over_tmr);
    build_settings();

    toast = mk_box(scr, 0, 0, 200, 44);                            /* thong bao nho */
    lv_obj_set_style_radius(toast, 22, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lbl_toast = mk_label_w(toast, &ui_font_txt16, 0, 0, 200, LV_TEXT_ALIGN_CENTER);
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
    if (kmh >= MOVE_KMH) {
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
    if (changed && screen != SCR_STANDBY) render_top();
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
    if (!nav_on) {
        snprintf(street_now, sizeof street_now, "%s", street);
    } else {
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
        exit_now = exit_n;
        turn2_now = exit_n > 0 ? 0 : turn2;
        snprintf(street_nav, sizeof street_nav, "%s", street);
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
    update_screen();
    render_all();
}

void ui_set_chip(int kind, const char *text)
{
    (void)kind; (void)text;                                       /* camera / cam vuot da nam trong canh bao */
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
    if (connected && !phone) show_connected();
    else if (!connected) conn_close();
    phone = connected;
    render_standby();
    if (screen != SCR_STANDBY) render_top();
    if (set_scr && shown(set_scr)) settings_render();
}

void ui_set_waze(bool on)
{
    if (on == waze) return;
    waze = on;
    render_all();
}

void ui_set_gps(int lvl) { (void)lvl; }

void ui_set_hint(const char *text) { (void)text; }

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
