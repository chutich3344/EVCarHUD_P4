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
    lv_color_t accent, dim;                  /* dim: mui ten o khong chon, cham tat */
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_NIGHT = {                /* theo man mau: nen xanh den */
    .bg = C(0x060913), .card = C(0x0E1424), .card2 = C(0x0E1424), .border = C(0x141B2E), .fg = C(0xFFFFFF),
    .mut = C(0xFFFFFF), .ok = C(0x22C55E), .warn = C(0xF5B301), .over = C(0xFF4D4F), .over_bg = C(0x3A0D10),
    .over_fg = C(0xFFB4B4), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x1A3866),
    .al_bg = C(0x1A1606), .al_border = C(0x3A3108), .al_fg = C(0xFFD400), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xFFD400),
    .nav_bg = C(0x10284F), .nav_bd = C(0x2A5BA8), .hw_bg = C(0x0B2E1E), .hw_bd = C(0x1C7A47),
    .flip_top = C(0x1E293B), .flip_bot = C(0x0C111D), .flip_line = C(0x020408), .accent = C(0x22C55E), .dim = C(0x3A4A66),
};
static const theme_t TH_DAY = {                  /* ban ngay: nen sang, cung bo cuc */
    .bg = C(0xF3F3EF), .card = C(0xFFFFFF), .card2 = C(0xE6E9EE), .border = C(0xD9DDE2), .fg = C(0x0B1220),
    .mut = C(0x0B1220), .ok = C(0x15803D), .warn = C(0xB45309), .over = C(0xDC2626), .over_bg = C(0xFDE2E2),
    .over_fg = C(0x991B1B), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x17478F),
    .al_bg = C(0xFFF6D6), .al_border = C(0xF0D264), .al_fg = C(0x92400E), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xF5B400),
    .nav_bg = C(0x1F5FBF), .nav_bd = C(0x1F5FBF), .hw_bg = C(0x0B7A3E), .hw_bd = C(0x0B7A3E),
    .flip_top = C(0xFFFFFF), .flip_bot = C(0xF1F0EC), .flip_line = C(0xDCDCDA), .accent = C(0x15803D), .dim = C(0xB8C2D0),
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
/* Phan tren cac man chay: toc do (so to, khung rong co dinh can phai) + "km/h" | bien gioi han, chia doi man hinh */
typedef struct {
    lv_obj_t *num, *unit, *ring, *rlbl;
    const lv_font_t *f2, *f3, *r2, *r3;     /* f2: 1-2 chu so, f3: 3 chu so */
    int sz2, sz3, rsize, cy;
} head_t;
/* Dai thong bao: bieu tuong + chu + khoang cach + loa, chop 1 Hz khi nguy hiem / camera / CSGT / vuot toc do */
typedef struct {
    lv_obj_t *box, *img, *txt, *dist, *spk, *bar, *fill;
    int h;
    int blink;                              /* 0: khong, 1: mo 35%, 2: do tuoi / do sam (vuot toc do) */
    const lv_font_t *df;
} strip_t;
/* Toc do (cruise) */
static head_t c_hd;
static strip_t c_st;
/* Canh bao */
static head_t a_hd;
static strip_t a_st;
static lv_obj_t *a_cell[2], *a_cell_img[2], *a_cell_t[2], *a_cell_s[2];
/* Cao toc */
static head_t h_hd;
static strip_t h_st;
static lv_obj_t *h_bar, *h_toll, *h_rest;
/* Chi dan */
static head_t n_hd;
static lv_obj_t *n_cell[3], *n_cell_img[3], *n_cell_d[3];
static lv_obj_t *n_eta, *n_eta_v[3], *n_eta_l[3];
static strip_t n_st;
/* man cho */
static lv_obj_t *sb_brand, *sb_dot, *sb_chip, *sb_chip_l, *sb_date, *sb_bar, *sb_icon, *sb_txt, *sb_pill, *sb_pill_dot, *sb_pill_l;
static lv_obj_t *dg_h, *dg_c, *dg_m;
static lv_timer_t *colon_tmr, *strip_tmr;
static bool blink_dim;
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


/* Ten huong re theo ma HLP `trn` */

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

/* ======================= khoi dung chung ======================= */
#define CAP_H(sz) ((sz) * 727 / 1000)                 /* chieu cao chu so Inter */
#define HALF_W    ((UI_W - 2 * PAD) / 2)

static head_t mk_head(lv_obj_t *p, const lv_font_t *f2, int sz2, const lv_font_t *f3, int sz3,
                      int rsize, const lv_font_t *r2, const lv_font_t *r3)
{
    head_t h = {.f2 = f2, .f3 = f3, .r2 = r2, .r3 = r3, .sz2 = sz2, .sz3 = sz3, .rsize = rsize};
    int bw = lv_text_get_width("88", 2, f2, -sz2 * 4 / 100), b3 = lv_text_get_width("188", 3, f3, -sz3 * 4 / 100);
    if (b3 > bw) bw = b3;
    bw += 4;
    int uw = text_w("km/h", &ui_font_txt16), x0 = PAD + (HALF_W - bw - 6 - uw) / 2;
    if (x0 < 4) x0 = 4;
    h.num = mk_label_w(p, f2, x0, 0, bw, LV_TEXT_ALIGN_RIGHT);      /* can phai trong khung co dinh: so khong nhay */
    lv_label_set_long_mode(h.num, LV_LABEL_LONG_CLIP);
    h.unit = mk_label(p, &ui_font_txt16, x0 + bw + 6, 0, "km/h");
    h.ring = mk_fill(p, PAD + HALF_W + (HALF_W - rsize) / 2, 0, rsize, rsize, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(h.ring, lv_color_white(), 0);
    lv_obj_set_style_border_color(h.ring, RED_SIGN, 0);
    lv_obj_set_style_border_opa(h.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(h.ring, rsize >= 150 ? 19 : rsize * 12 / 100, 0);
    h.rlbl = mk_label(h.ring, r2, 0, 0, "-");
    lv_obj_set_style_text_color(h.rlbl, lv_color_hex(0x111111), 0);
    return h;
}

static void head_area(head_t *h, int y, int hgt)                 /* vung (y, cao) cua phan tren trong trang */
{
    h->cy = y + hgt / 2;
    lv_obj_set_y(h->ring, h->cy - h->rsize / 2);
}

static void render_head(head_t *h)
{
    char b[12];
    if (speed < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%d", speed % 1000);
    bool three = speed >= 100;
    const lv_font_t *f = three ? h->f3 : h->f2;
    int sz = three ? h->sz3 : h->sz2;
    lv_obj_set_style_text_font(h->num, f, 0);
    lv_obj_set_style_text_letter_space(h->num, -sz * 4 / 100, 0);
    lv_label_set_text(h->num, b);
    int ny = h->cy + CAP_H(sz) / 2 - baseline(f);
    lv_obj_set_y(h->num, ny);
    lv_obj_set_y(h->unit, ny + baseline(f) - baseline(&ui_font_txt16));
    lv_obj_set_style_text_color(h->num, speed_color(), 0);
    lv_obj_set_style_text_color(h->unit, th->fg, 0);

    if (limit > 0) snprintf(b, sizeof b, "%d", limit % 1000);
    else snprintf(b, sizeof b, "-");
    const lv_font_t *rf = limit >= 100 ? h->r3 : h->r2;
    lv_obj_set_style_text_font(h->rlbl, rf, 0);
    lv_obj_set_style_text_letter_space(h->rlbl, -(int)rf->line_height * 3 / 100, 0);
    lv_label_set_text(h->rlbl, b);
    lv_obj_center(h->rlbl);
}

static strip_t mk_strip(lv_obj_t *p, int x, int y, int w, int h)
{
    strip_t s = {.h = h, .df = h >= 60 ? &ui_font_dist30 : &ui_font_dist26};
    const lv_font_t *tf = h >= 60 ? &ui_font_txt20 : &ui_font_txt16;
    s.box = mk_card(p, x, y, w, h, h >= 50 ? 12 : 10);
    bool bar = h >= 50;
    int row = (h - 2) / 2 - (bar ? 5 : 0);                       /* giua hang chu */
    s.img = mk_image(s.box, 12, row - 15, 30, 48);
    s.txt = mk_label_w(s.box, tf, 52, row - tf->line_height / 2, 200, LV_TEXT_ALIGN_LEFT);
    s.dist = mk_label(s.box, s.df, 0, row + CAP_H(h >= 60 ? 30 : 26) / 2 - baseline(s.df), "");
    lv_obj_set_style_text_letter_space(s.dist, -1, 0);
    s.spk = mk_label(s.box, &lv_font_montserrat_14, 0, row - lv_font_montserrat_14.line_height / 2, LV_SYMBOL_VOLUME_MAX);
    if (bar) {
        s.bar = mk_fill(s.box, 12, h - 2 - 10, w - 2 - 24, 4, 2);
        s.fill = mk_fill(s.bar, 0, 0, 0, 4, 2);
    }
    return s;
}

/* mau dai theo loai: CSGT do, camera phat nguoi cam, con lai (nguy hiem) vang */
static void alert_colors(int code, lv_color_t *bg, lv_color_t *bd, lv_color_t *fg)
{
    bool police = code == 1, cam = code == 2 || code == 3 || (code >= 40 && code <= 46);
    if (night) {
        *bg = lv_color_hex(police ? 0x2A0B10 : cam ? 0x2A1408 : 0x1A1606);
        *bd = lv_color_hex(police ? 0x7F1D1D : cam ? 0x7C2D12 : 0x3A3108);
        *fg = lv_color_hex(police ? 0xFF4D4F : cam ? 0xFF8A3D : 0xFFD400);
    } else {
        *bg = lv_color_hex(police ? 0xFDE2E2 : cam ? 0xFFEDD5 : 0xFFF6D6);
        *bd = lv_color_hex(police ? 0xF5A3A3 : cam ? 0xFDBA74 : 0xF0D264);
        *fg = lv_color_hex(police ? 0xB91C1C : cam ? 0xC2410C : 0x92400E);
    }
}

static bool urgent_alert(int code)                               /* chop + tit: nguy hiem, camera, CSGT */
{
    return code != 6 && code != 12 && code != 19 && !(code >= 20 && code <= 21) && code != 35 && code != 36;
}

/* ve dai: icon, chu, khoang cach (mau fg), loa, thanh tien do (permille < 0: an) */
static void strip_fill(strip_t *s, const lv_image_dsc_t *icon, const char *txt, const char *dist,
                       lv_color_t bg, lv_color_t bd, lv_color_t fg, bool spk, int permille, int blink)
{
    int w = lv_obj_get_width(s->box) - 2;
    lv_obj_set_style_bg_color(s->box, bg, 0);
    lv_obj_set_style_border_color(s->box, bd, 0);
    show(s->img, icon != NULL);
    if (icon) lv_image_set_src(s->img, icon);
    int tx = icon ? 52 : 14;
    show(s->spk, spk);
    int right = w - 12;
    if (spk) {
        lv_obj_set_style_text_color(s->spk, th->fg, 0);
        right -= lv_font_montserrat_14.line_height + 2;
        lv_obj_set_x(s->spk, right + 4);
        right -= 8;
    }
    lv_label_set_text(s->dist, dist ? dist : "");
    lv_obj_set_style_text_color(s->dist, fg, 0);
    int dw = dist && dist[0] ? lv_text_get_width(dist, strlen(dist), s->df, -1) : 0;
    lv_obj_set_x(s->dist, right - dw);
    if (dw) right -= dw + 10;
    lv_obj_set_x(s->txt, tx);
    lv_obj_set_width(s->txt, right - tx > 20 ? right - tx : 20);
    lv_label_set_text(s->txt, txt);
    lv_obj_set_style_text_color(s->txt, th->fg, 0);
    if (s->bar) {
        show(s->bar, permille >= 0);
        if (permille >= 0) {
            lv_obj_set_style_bg_color(s->bar, bd, 0);
            lv_obj_set_style_bg_color(s->fill, fg, 0);
            set_bar(s->bar, s->fill, permille);
        }
    }
    s->blink = blink;
    lv_obj_set_style_opa(s->box, blink == 1 && blink_dim ? 89 : LV_OPA_COVER, 0);
    if (blink == 2) {
        lv_obj_set_style_bg_color(s->box, lv_color_hex(blink_dim ? 0x5A0C12 : 0xE11D2A), 0);
        lv_obj_set_style_border_color(s->box, lv_color_hex(blink_dim ? 0x5A0C12 : 0xE11D2A), 0);
    }
}

static void strip_alert(strip_t *s, const ui_alert_t *a)
{
    lv_color_t bg, bd, fg;
    alert_colors(a->code, &bg, &bd, &fg);
    char num[12], d[20];
    const char *u;
    alert_value(a, num, sizeof num, &u);
    if (num[0]) snprintf(d, sizeof d, "%s %s", num, u);
    else d[0] = 0;
    bool urg = urgent_alert(a->code);
    strip_fill(s, sign_for_alert(a->code), alert_name(a->code), d, bg, bd, fg, urg && snd_on,
               a->dist >= 0 && a->code != 8 ? progress(ev_start, a->dist) : -1, urg);
}

static void strip_over(strip_t *s)                               /* vuot toc do: dai do chop */
{
    char b[40];
    snprintf(b, sizeof b, "Vượt tốc độ %d km/h", speed - limit);
    strip_fill(s, &sign_hazard, b, "", th->over_bg, th->over_bg, th->over, snd_on, -1, 2);
    lv_obj_set_style_text_color(s->txt, lv_color_white(), 0);
    lv_obj_set_style_text_color(s->spk, lv_color_white(), 0);
}

static void strip_blink_cb(lv_timer_t *t)
{
    (void)t;
    blink_dim = !blink_dim;
    strip_t *all[] = {&c_st, &a_st, &n_st};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        if (!all[i]->box || !all[i]->blink) continue;
        else if (all[i]->blink == 1) lv_obj_set_style_opa(all[i]->box, blink_dim ? 89 : LV_OPA_COVER, 0);
        else {
            lv_color_t c = lv_color_hex(blink_dim ? 0x5A0C12 : 0xE11D2A);   /* do tuoi / do sam, chu trang */
            lv_obj_set_style_bg_color(all[i]->box, c, 0);
            lv_obj_set_style_border_color(all[i]->box, c, 0);
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

    const char *st = !waze ? "Chỉ có GPS" : screen == SCR_NAV && street_nav[0] ? street_nav : street_now;
    lv_label_set_text(t_street, st);
    lv_obj_set_style_text_color(t_street, th->fg, 0);
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

/* ======================= man Toc do (chay thuong) =======================
 * Tren: toc do 150 px + km/h | bien gioi han 176 px. Duoi: dai 40 px (vuot toc do: do, chop; canh bao xa; duong thoang). */
#define C_STRIP_H 40
static void build_cruise(void)
{
    lv_obj_t *p = page[SCR_CRUISE] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    c_hd = mk_head(p, &ui_font_d150, 150, &ui_font_d96, 96, 176, &ui_font_r76, &ui_font_sign64);
    head_area(&c_hd, 0, BODY_H - 12 - C_STRIP_H);
    c_st = mk_strip(p, PAD, BODY_H - 12 - C_STRIP_H, UI_W - 2 * PAD, C_STRIP_H);
}

static void render_cruise(void)
{
    render_head(&c_hd);
    if (level == UI_LEVEL_OVER) {
        strip_over(&c_st);
    } else if (ev_now) {
        char d[16];
        fmt_dist(d, sizeof d, ev_now->dist);
        lv_color_t bg, bd, fg;
        alert_colors(ev_now->code, &bg, &bd, &fg);
        strip_fill(&c_st, sign_for_alert(ev_now->code), alert_name(ev_now->code), d, th->card, th->card, fg, false, -1, false);
    } else {
        strip_fill(&c_st, NULL, !waze ? "Chưa mở Waze · chỉ có tốc độ GPS" : "Đường thoáng", "", th->card, th->card, th->fg,
                   false, -1, false);
        lv_obj_set_style_text_align(c_st.txt, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    lv_obj_set_style_text_align(c_st.txt, LV_TEXT_ALIGN_LEFT, 0);
}

/* ======================= man Canh bao =======================
 * Tren: toc do 104 px | bien 100 px. Giua: dai canh bao 54 px chop 1 Hz (mau theo loai) + loa. Duoi: 2 o canh bao / bien khac. */
#define A_CELL_H 64
#define A_ST_H   64
static void build_alert(void)
{
    lv_obj_t *p = page[SCR_ALERT] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    a_hd = mk_head(p, &ui_font_d104, 104, &ui_font_d96, 96, 100, &ui_font_r44, &ui_font_r36);
    a_st = mk_strip(p, PAD, 0, UI_W - 2 * PAD, A_ST_H);
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
    int sy = others ? BODY_H - 12 - A_CELL_H - 10 - A_ST_H : BODY_H - 12 - A_ST_H;   /* khong co o phu: dai xuong day */
    lv_obj_set_y(a_st.box, sy);
    head_area(&a_hd, 8, sy - 8 - 8);
    render_head(&a_hd);
    strip_alert(&a_st, ev_now);

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
        lv_obj_set_style_text_color(a_cell_s[k], th->fg, 0);
        show(a_cell[k], true);
        k++;
    }
    for (; k < 2; k++) show(a_cell[k], false);
}

/* ======================= man Cao toc =======================
 * Tren: toc do 104 px | bien 116 px. Giua: dai xanh la 54 px (bien / canh bao phia truoc, hoac ten cao toc).
 * Duoi: tram dung nghi / tram thu phi. */
#define H_ST_H  54
#define H_BAR_H 44
static void build_highway(void)
{
    lv_obj_t *p = page[SCR_HIGHWAY] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int by = BODY_H - 12 - H_BAR_H, sy = by - 8 - H_ST_H;
    h_hd = mk_head(p, &ui_font_d104, 104, &ui_font_d96, 96, 116, &ui_font_r50, &ui_font_r44);
    head_area(&h_hd, 6, sy - 8 - 6);
    h_st = mk_strip(p, PAD, sy, UI_W - 2 * PAD, H_ST_H);
    h_bar = mk_fill(p, PAD, by, UI_W - 2 * PAD, H_BAR_H, 10);
    int bw = (UI_W - 2 * PAD) / 2;
    h_rest = mk_label_w(h_bar, &ui_font_txt14, 0, (H_BAR_H - ui_font_txt14.line_height) / 2, bw, LV_TEXT_ALIGN_CENTER);
    h_toll = mk_label_w(h_bar, &ui_font_txt14, bw, (H_BAR_H - ui_font_txt14.line_height) / 2, bw, LV_TEXT_ALIGN_CENTER);
}

static const ui_alert_t *find_alert(int c1, int c2)
{
    for (int i = 0; i < al_n; i++)
        if (al_list[i].code == c1 || al_list[i].code == c2) return &al_list[i];
    return NULL;
}

static void render_highway(void)
{
    render_head(&h_hd);
    const ui_alert_t *it = NULL;                                   /* bien / canh bao phia truoc (tru tram nghi, thu phi) */
    for (int i = 0; i < al_n && !it; i++)
        if (al_list[i].code != 12 && al_list[i].code != 19) it = &al_list[i];
    if (it) {
        char d[16];
        fmt_dist(d, sizeof d, it->dist);
        strip_fill(&h_st, sign_for_alert(it->code), alert_name(it->code), d, th->hw_bg, th->hw_bd, th->fg, false, -1, false);
    } else {
        char b[64];
        if (limit > 0) snprintf(b, sizeof b, "Tối đa %d km/h · giữ khoảng cách", limit);
        else snprintf(b, sizeof b, "Giữ khoảng cách an toàn");
        strip_fill(&h_st, &arrow_straight, b, "", th->hw_bg, th->hw_bd, th->fg, false, -1, false);
    }
    lv_obj_set_style_image_recolor(h_st.img, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(h_st.img, it ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (!it) lv_obj_set_style_text_color(h_st.txt, lv_color_white(), 0);

    lv_obj_set_style_bg_color(h_bar, th->card, 0);
    const ui_alert_t *rest = find_alert(19, 19), *toll = find_alert(12, 12);
    char b[64], d[16];
    fmt_dist(d, sizeof d, rest ? rest->dist : -1);
    snprintf(b, sizeof b, "Trạm dừng nghỉ  %s", d[0] ? d : "—");
    lv_label_set_text(h_rest, b);
    fmt_dist(d, sizeof d, toll ? toll->dist : -1);
    snprintf(b, sizeof b, "Trạm thu phí  %s", d[0] ? d : "—");
    lv_label_set_text(h_toll, b);
    lv_obj_set_style_text_color(h_rest, th->fg, 0);
    lv_obj_set_style_text_color(h_toll, th->fg, 0);
}

/* ======================= man Chi dan (dan duong) =======================
 * Tren: toc do 120 px | bien 116 px. Giua: 3 o Trai / Thang / Phai, o dang re sang xanh kem khoang cach.
 * Duoi: gio den / con lai / thoi gian; co canh bao thi thanh dai canh bao chop. */
#define N_CELL_H 54
#define N_ETA_H  44
static int turn_side(int trn)                                    /* 0 trai, 1 thang, 2 phai */
{
    switch (trn) {
    case 2: case 4: case 6: case 8: case 13: case 15: return 0;
    case 3: case 5: case 7: case 9: case 14: case 16: return 2;
    default: return 1;
    }
}

static void build_nav(void)
{
    lv_obj_t *p = page[SCR_NAV] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    int ey = BODY_H - 12 - N_ETA_H, cy = ey - 8 - N_CELL_H, ew = UI_W - 2 * PAD;
    n_hd = mk_head(p, &ui_font_d120, 120, &ui_font_d96, 96, 116, &ui_font_r50, &ui_font_r44);
    head_area(&n_hd, 6, cy - 8 - 6);
    int cw = (ew - 16) / 3;
    for (int i = 0; i < 3; i++) {
        n_cell[i] = mk_card(p, PAD + i * (cw + 8), cy, cw, N_CELL_H, 12);
        n_cell_img[i] = mk_image(n_cell[i], 0, (N_CELL_H - 2 - 26) / 2, 26, 64);
        lv_obj_set_style_image_recolor_opa(n_cell_img[i], LV_OPA_COVER, 0);
        n_cell_d[i] = mk_label(n_cell[i], &ui_font_dist26, 0, (N_CELL_H - 2) / 2 + CAP_H(26) / 2 - baseline(&ui_font_dist26), "");
        lv_obj_set_style_text_letter_space(n_cell_d[i], -1, 0);
    }
    n_eta = mk_fill(p, PAD, ey, ew, N_ETA_H, 10);
    static const char *const cap[3] = {"Giờ đến", "Còn lại", "Thời gian"};
    for (int i = 0; i < 3; i++) {
        n_eta_v[i] = mk_label_w(n_eta, &ui_font_txt20, i * ew / 3, 0, ew / 3, LV_TEXT_ALIGN_CENTER);
        n_eta_l[i] = mk_label_w(n_eta, &ui_font_sm12, i * ew / 3, 27, ew / 3, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(n_eta_l[i], cap[i]);
    }
    n_st = mk_strip(p, PAD, ey, ew, N_ETA_H);
}

static void render_nav(void)
{
    render_head(&n_hd);
    static const lv_image_dsc_t *const base[3] = {&arrow_left, &arrow_straight, &arrow_right};
    int side = turn_side(turn_now);
    const lv_image_dsc_t *arr = arrow_for_turn(turn_now);
    for (int i = 0; i < 3; i++) {
        bool on = i == side;
        int cw = lv_obj_get_width(n_cell[i]) - 2;
        lv_obj_set_style_bg_color(n_cell[i], on ? (night ? lv_color_hex(0x1A4A8F) : th->blue) : th->card, 0);
        lv_obj_set_style_border_color(n_cell[i], on ? lv_color_hex(0x5B8FE0) : th->border, 0);
        lv_image_set_src(n_cell_img[i], on && arr ? arr : base[i]);
        lv_obj_set_style_image_recolor(n_cell_img[i], on ? lv_color_white() : th->dim, 0);
        show(n_cell_d[i], on);
        int gw = 26;
        if (on) {
            char num[12], d[20];
            const char *u;
            fmt_dist2(num, sizeof num, &u, dist_now);
            if (num[0]) snprintf(d, sizeof d, "%s %s", num, u);
            else d[0] = 0;
            lv_label_set_text(n_cell_d[i], d);
            lv_obj_set_style_text_color(n_cell_d[i], lv_color_white(), 0);
            if (d[0]) gw += 8 + lv_text_get_width(d, strlen(d), &ui_font_dist26, -1);
        }
        int x0 = (cw - gw) / 2;
        lv_obj_set_x(n_cell_img[i], x0);
        lv_obj_set_x(n_cell_d[i], x0 + 34);
    }

    /* duoi: canh bao (chop) hoac gio den / con lai / thoi gian */
    bool band = ev_now != NULL;
    show(n_st.box, band);
    show(n_eta, !band && has_route);
    if (band) {
        strip_alert(&n_st, ev_now);
    } else {
        n_st.blink = 0;
        if (has_route) {
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
                lv_obj_set_style_text_color(n_eta_l[i], th->fg, 0);
            }
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
    lv_obj_set_style_text_color(dg_h, th->fg, 0);
    lv_obj_set_style_text_color(dg_c, th->accent, 0);
    lv_obj_set_style_text_color(dg_m, th->accent, 0);
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

/* ======================= man cho =======================
 * Theo man mau "Cho ket noi": dong ho to giua man, ngay thang; duoi la dai "Cho ket noi Waze/GOFA qua dien thoai..."
 * kem o "GPS san sang". Goc phai tren: cham trang thai + o nguon. Dau ":" nhap nhay khi chua noi dien thoai. */
#define SB_BAR_H 54
static void build_standby(void)
{
    lv_obj_t *p = page[SCR_STANDBY] = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    sb_brand = mk_label(p, &ui_font_cap12, PAD, (TOP_H - ui_font_cap12.line_height) / 2, "EVCARHUD");
    lv_obj_set_style_text_letter_space(sb_brand, 3, 0);
    sb_chip = mk_fill(p, 0, (TOP_H - 18) / 2, 40, 18, 5);
    sb_chip_l = mk_label(sb_chip, &ui_font_cap12, 6, (18 - ui_font_cap12.line_height) / 2, "waze");
    sb_dot = mk_fill(p, 0, (TOP_H - 8) / 2, 8, 8, LV_RADIUS_CIRCLE);

    const lv_font_t *df = &ui_font_clock120;
    int by = UI_H - 14 - SB_BAR_H;
    int grp = CAP_H(120) + 18 + ui_font_txt20.line_height, top = TOP_H + (by - TOP_H - grp) / 2;
    int w2 = text_w("00", df) + 4, wc = text_w(":", df) + 4, dx = (UI_W - 2 * w2 - wc) / 2;
    int dy = top + CAP_H(120) - baseline(df);
    dg_h = mk_label_w(p, df, dx, dy, w2, LV_TEXT_ALIGN_RIGHT);
    dg_c = mk_label_w(p, df, dx + w2, dy, wc, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(dg_c, ":");
    dg_m = mk_label_w(p, df, dx + w2 + wc, dy, w2, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(dg_h, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(dg_m, LV_LABEL_LONG_CLIP);
    sb_date = mk_label_w(p, &ui_font_txt20, 0, top + CAP_H(120) + 18, UI_W, LV_TEXT_ALIGN_CENTER);

    int bw = UI_W - 2 * PAD;
    sb_bar = mk_card(p, PAD, by, bw, SB_BAR_H, 12);
    sb_icon = mk_image(sb_bar, 14, (SB_BAR_H - 2 - 22) / 2, 22, 64);
    lv_image_set_src(sb_icon, &arrow_straight);
    lv_obj_set_style_image_recolor_opa(sb_icon, LV_OPA_COVER, 0);
    sb_pill = mk_card(sb_bar, 0, (SB_BAR_H - 2 - 26) / 2, 120, 26, 13);
    sb_pill_dot = mk_fill(sb_pill, 10, (24 - 7) / 2, 7, 7, LV_RADIUS_CIRCLE);
    sb_pill_l = mk_label(sb_pill, &ui_font_sm12, 23, (24 - ui_font_sm12.line_height) / 2, "");
    sb_txt = mk_label_w(sb_bar, &ui_font_txt16, 46, (SB_BAR_H - 2 - ui_font_txt16.line_height) / 2, 200, LV_TEXT_ALIGN_LEFT);
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
    char b[64];
    char hh[4] = "--", mm[4] = "--";
    if (clk_h >= 0) {
        snprintf(hh, sizeof hh, "%02d", clk_h % 24);
        snprintf(mm, sizeof mm, "%02d", clk_m % 60);
    }
    lv_label_set_text(dg_h, hh);
    lv_label_set_text(dg_m, mm);
    if (day > 0 && month > 0 && wday >= 0) snprintf(b, sizeof b, "%s, %02d/%02d", WDAY_NAME[wday % 7], day, month);
    else if (wday >= 0) snprintf(b, sizeof b, "%s", WDAY_NAME[wday % 7]);
    else snprintf(b, sizeof b, "Chờ giờ từ điện thoại");
    lv_label_set_text(sb_date, b);
    lv_obj_set_style_text_color(sb_date, th->fg, 0);

    bool g = src == UI_SRC_GOFA;
    const char *sn = g ? "GOFA" : "waze";
    lv_label_set_text(sb_chip_l, sn);
    lv_obj_set_style_text_color(sb_chip_l, g ? lv_color_white() : lv_color_hex(0x05263A), 0);
    lv_obj_set_style_bg_color(sb_chip, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
    int cw = text_w(sn, &ui_font_cap12) + 12;
    lv_obj_set_width(sb_chip, cw);
    lv_obj_set_x(sb_chip, UI_W - PAD - cw);
    lv_obj_set_x(sb_dot, UI_W - PAD - cw - 6 - 8);
    lv_obj_set_style_bg_color(sb_dot, phone ? th->ok : th->dim, 0);
    lv_obj_set_style_text_color(sb_brand, th->fg, 0);

    lv_obj_set_style_bg_color(sb_bar, night ? lv_color_hex(0x0B1A2E) : th->card, 0);
    lv_obj_set_style_border_color(sb_bar, night ? lv_color_hex(0x1E3A5F) : th->border, 0);
    lv_obj_set_style_image_recolor(sb_icon, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
    const char *gp = phone ? "GPS sẵn sàng" : (clk_h >= 0 && !clk_synced) ? "Giờ chưa chỉnh" : "Chưa có GPS";
    lv_label_set_text(sb_pill_l, gp);
    lv_obj_set_style_text_color(sb_pill_l, th->fg, 0);
    int pw = 23 + text_w(gp, &ui_font_sm12) + 12, bw = lv_obj_get_width(sb_bar) - 2;
    lv_obj_set_width(sb_pill, pw);
    lv_obj_set_x(sb_pill, bw - 14 - pw);
    lv_obj_set_style_bg_color(sb_pill, phone ? lv_color_hex(night ? 0x0F2A1C : 0xDCFCE7) : th->card, 0);
    lv_obj_set_style_border_color(sb_pill, phone ? lv_color_hex(night ? 0x1E4D33 : 0x86EFAC) : th->border, 0);
    lv_obj_set_style_bg_color(sb_pill_dot, phone ? th->ok : th->dim, 0);
    lv_label_set_text(sb_txt, phone ? "Đã kết nối · chờ dữ liệu dẫn đường"
                              : g ? "Chờ kết nối GOFA qua điện thoại…" : "Chờ kết nối Waze qua điện thoại…");
    lv_obj_set_width(sb_txt, bw - 14 - pw - 10 - 46);
    lv_obj_set_style_text_color(sb_txt, th->fg, 0);
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
    strip_tmr = lv_timer_create(strip_blink_cb, 500, NULL);       /* dai canh bao chop 1 Hz: sang 0,5 s, mo 35% 0,5 s */

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
    if (code && code != ev_code) beep(urgent_alert(code) ? 2 : 1);  /* canh bao moi: nguy hiem / camera / CSGT tit tit */
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
