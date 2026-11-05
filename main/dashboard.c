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
#include <stdlib.h>
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
    /* Gradient: nen tren (quang cam), ranh, bong khoi, vien sang tren-trai, cap mau chuyen (so toc do, nhan) */
    lv_color_t bg_top, track, shadow, hi, sub;
    lv_opa_t sh_opa, sign_opa;
    uint32_t g_ok[2], g_near[2], g_over[2], acc[2], clk[2];
} theme_t;

#define C(h) LV_COLOR_MAKE(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)
static const theme_t TH_NIGHT = {                /* Gradient dem: nen #1E2235, the #252A40, nhan cam -> do */
    .bg = C(0x1E2235), .card = C(0x252A40), .card2 = C(0x252A40), .border = C(0x292D3F), .fg = C(0xFFFFFF),
    .mut = C(0xFFFFFF), .ok = C(0x34F5B0), .warn = C(0xFFC23A), .over = C(0xFF4560), .over_bg = C(0x3A0D10),
    .over_fg = C(0xFFB4B4), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x1A3866),
    .al_bg = C(0x1A1606), .al_border = C(0x3A3108), .al_fg = C(0xFFD400), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xFFD400),
    .nav_bg = C(0x10284F), .nav_bd = C(0x2A5BA8), .hw_bg = C(0x25615A), .hw_bd = C(0x28816C),
    .flip_top = C(0x1E293B), .flip_bot = C(0x0C111D), .flip_line = C(0x020408), .accent = C(0xFF6A4D), .dim = C(0x717483),
    .bg_top = C(0x1E2235), .track = C(0x161A2A), .shadow = C(0x000000), .hi = C(0x32374C), .sub = C(0xA3A8BD),
    .sh_opa = 128, .sign_opa = 89,
    .g_ok = {0x34F5B0, 0x22B8FF}, .g_near = {0xFFE066, 0xFFA23A}, .g_over = {0xFFA23A, 0xFF3D5A},
    .acc = {0xFFA23A, 0xFF4560}, .clk = {0xFFA23A, 0xFF4560},
};
static const theme_t TH_DAY = {                  /* Gradient ngay: nen #ECEDF1, the #F4F5F8, nhan do -> cam */
    .bg = C(0xECEDF1), .card = C(0xF4F5F8), .card2 = C(0xE6E8EE), .border = C(0xDFE0E5), .fg = C(0x14112B),
    .mut = C(0x14112B), .ok = C(0x059669), .warn = C(0xB45309), .over = C(0xFF3D3D), .over_bg = C(0xFDE2E2),
    .over_fg = C(0x991B1B), .blue = C(0x1F5FBF), .blue_fg = C(0xD6E4FF), .blue_dim = C(0x17478F),
    .al_bg = C(0xFFF4C2), .al_border = C(0xF2D36B), .al_fg = C(0xB45309), .hw = C(0x0B7A3E), .hw_fg = C(0xCFEBDB), .band = C(0xF5B400),
    .nav_bg = C(0x1F5FBF), .nav_bd = C(0x1F5FBF), .hw_bg = C(0xC2E2E4), .hw_bd = C(0x7FDDBF),
    .flip_top = C(0xFFFFFF), .flip_bot = C(0xF1F0EC), .flip_line = C(0xDCDCDA), .accent = C(0xFF5A3D), .dim = C(0xA6A5B0),
    .bg_top = C(0xECEDF1), .track = C(0xD3D4DB), .shadow = C(0x96A0B8), .hi = C(0xFFFFFF), .sub = C(0x6B7085),
    .sh_opa = 140, .sign_opa = 64,
    .g_ok = {0x059669, 0x0284C7}, .g_near = {0xF59E0B, 0xEA580C}, .g_over = {0xFFA23A, 0xFF3D3D},
    .acc = {0xFF3D3D, 0xFFA23A}, .clk = {0xFF3D3D, 0xFFA23A},
};
/* Cai dat: luon nen toi Gradient (#1E2235), nhan cam (S_GREEN* giu ten cu) */
#define S_BG      lv_color_hex(0x1E2235)
#define S_CARD    lv_color_hex(0x252A40)
#define S_BORDER  lv_color_hex(0x32374C)
#define S_ON_BG   lv_color_hex(0x4C3841)
#define S_GREEN   lv_color_hex(0xFF6A4D)
#define S_GREEN2  lv_color_hex(0xFFB067)
#define S_GREEN3  lv_color_hex(0xFF8A70)
#define S_FG      lv_color_hex(0xFFFFFF)
#define S_MUT     lv_color_hex(0xA3A8BD)
#define S_TRACK   lv_color_hex(0x161A2A)
#define RED_SIGN  lv_color_hex(0xE11D48)

static const char *const MODE_NAME[3] = {"Tự động", "Ban ngày", "Ban đêm"};
static const char *const WDAY_NAME[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
enum { SCR_STANDBY = 0, SCR_CRUISE, SCR_ALERT, SCR_HIGHWAY, SCR_NAV, SCR_N };
static const theme_t *th = &TH_NIGHT;

/* ---------------- doi tuong ---------------- */
static lv_obj_t *scr, *page[SCR_N];
/* thanh tren (dung chung cac man dang chay) */
static lv_obj_t *t_bolt;
static lv_obj_t *top, *top_line, *t_clock, *t_street, *t_badge, *t_gps_dot, *t_src, *t_src_l;
/* Phan tren cac man chay: toc do (so to, khung rong co dinh can phai) + "km/h" | bien gioi han, chia doi man hinh */
typedef struct {
    lv_obj_t *cv;
    lv_draw_buf_t *db;
    char txt[16];
    const lv_font_t *f;
    int ls, aw, x0, xw, y0, yh;
    uint32_t c1, c2;
    lv_text_align_t al;
} gtext_t;
#define HLV 3                                 /* so trang thai cua phan tren (vd co dai / an dai) */
/* Phan tren cac man chay: so toc do (mau chuyen, "km/h" nam duoi canh giua) | bien gioi han.
 * Moi trang thai chi la mot vung (y, cao); co so / bien tu tinh tu vung do (head_geo). */
typedef struct {
    int n;
    int ay[HLV], ah[HLV];
    lv_obj_t *unit, *ring, *rlbl;
    gtext_t gt;
} head_t;
/* Dai thong bao: bieu tuong + chu + khoang cach + loa, chop 1 Hz khi nguy hiem / camera / CSGT / vuot toc do */
typedef struct {
    lv_obj_t *box, *img, *txt, *dist, *spk, *bar, *fill;
    int h;
    int blink;                              /* 0: khong, 1: mo 35%, 2: do tuoi / do sam (vuot toc do) */
    const lv_font_t *df;
} strip_t;
/* Toc do + Canh bao: mot man, 2 trang thai (co canh bao gan: dai + o phu, so nho; khong: so to) */
static head_t d_hd;
static strip_t c_st;
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
static lv_obj_t *sb_chip, *sb_chip_l, *sb_date, *sb_bar, *sb_txt, *sb_pill, *sb_pill_dot, *sb_pill_l;
static gtext_t sg[3];                         /* dong ho man cho: gio, hai cham, phut (mau chuyen) */
static int sg_off[3], sg_w[3], sg_tw;
static lv_timer_t *colon_tmr, *strip_tmr;
static bool blink_dim;
/* Chuyen trang thai co dai <-> an dai (0: co dai, 1024: an dai, so + bien to ra), animation 0,4 s */
#define T_MAX 1024
static int d_t, c_t, n_t, h_t;               /* d: canh bao gan <-> khong; c: man Toc do co dai do <-> khong */
static int d_want = -1, c_want = -1, n_want = -1, h_want = -1;
static bool scr_fresh;                       /* vua doi man: dat ngay, khong animation */
static lv_obj_t *frame, *toast, *lbl_toast;
static lv_timer_t *blink_tmr, *toast_tmr;

/* ---------------- trang thai ---------------- */
static int speed = -2, limit = -1, level = -1;
static bool nav_is_ts;                                       /* man dan duong dang dan toi tram sac */
static bool nav_on, has_route, wz_on, wz_hold;   /* wz_hold: giu dan duong khi dien thoai tam ngung gui */                        /* wz_on: Waze dang co lenh re */
static void apply_nav(void);
static int clk_h = -1, clk_m = -1, day = -1, month = -1, wday = -1;
static int screen = -1;
static bool sig_ok, phone, waze = true, moving;
static uint32_t stop_tick;
static ui_alert_t al_list[4];
static int al_n;
static const ui_alert_t *ev_now;            /* canh bao (su kien) gan nhat */
static const ui_alert_t *sign_now;          /* bien bao tren duong gan nhat */
static int ev_code, ev_start;
static uint32_t ev_new_tick;                                     /* lan cuoi co canh bao MOI (da tit) */
#define ALERT_BLINK_MS 8000                                      /* chi chop 8 giay dau, sau do dung yen */
static bool ev_fresh(void) { return ev_new_tick && lv_tick_elaps(ev_new_tick) < ALERT_BLINK_MS; }
static int turn_now = -1, dist_now = -1, turn_start, exit_now, turn2_now;
static char eta_txt[8], street_now[64], street_nav[96];
static int eta_rm, eta_rmin;
static int mode = UI_MODE_AUTO, ambient = -1;
static bool night, boot_night = true;
void ui_set_boot_night(bool n) { boot_night = n; }
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

/* ---------- Gradient: the co bong khoi + chu to mau chuyen ----------
 * st_card: nen the, bong toi duoi-phai, vien sang tren-trai; doi theo theme (theme_styles). */
static lv_style_t st_card, st_card_b;                       /* st_card_b: them vien mo ca 4 canh (o re, dai) */
static void theme_styles(void);
static void card_style(lv_obj_t *o, bool full_border)
{
    lv_obj_add_style(o, full_border ? &st_card_b : &st_card, 0);
}

static lv_color_t mix_hex(uint32_t a, uint32_t b, int t)   /* t 0..255: a -> b */
{
    return lv_color_mix(lv_color_hex(b), lv_color_hex(a), (uint8_t)t);
}

static void theme_styles(void)
{
    static bool init;
    if (!init) {
        lv_style_init(&st_card);
        lv_style_init(&st_card_b);
        init = true;
    }
    if (!th) return;
    lv_style_t *all[2] = {&st_card, &st_card_b};
    for (int i = 0; i < 2; i++) {
        lv_style_t *st = all[i];
        lv_style_set_bg_opa(st, LV_OPA_COVER);
        lv_style_set_bg_color(st, th->card);
        lv_style_set_shadow_width(st, 14);
        lv_style_set_shadow_offset_x(st, 6);
        lv_style_set_shadow_offset_y(st, 6);
        lv_style_set_shadow_color(st, th->shadow);
        lv_style_set_shadow_opa(st, th->sh_opa);
        lv_style_set_border_width(st, 1);
        lv_style_set_border_opa(st, LV_OPA_COVER);
        lv_style_set_border_color(st, th->hi);
        lv_style_set_border_side(st, i ? LV_BORDER_SIDE_FULL : (LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT));
    }
    lv_obj_report_style_change(&st_card);
    lv_obj_report_style_change(&st_card_b);
}

/* nen man: quang cam nhe phia tren (dem), chuyen doc xuong mau nen */
static void bg_grad(lv_obj_t *o)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, th->bg_top, 0);
    lv_obj_set_style_bg_grad_color(o, th->bg, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(o, 0, 0);
    lv_obj_set_style_bg_grad_stop(o, 150, 0);
}

/* nen the chuyen ngang c1 -> c2 (dai cao toc, tram sac, o re dang chon) */
static void hgrad(lv_obj_t *o, lv_color_t c1, lv_color_t c2)
{
    lv_obj_set_style_bg_color(o, c1, 0);
    lv_obj_set_style_bg_grad_color(o, c2, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
}

/* Chu mau chuyen 135 do: ve chu trang len canvas ARGB8888 trong suot, roi to lai RGB theo vi tri.
 * Canvas la lv_image nen phong to / thu nho bang lv_image_set_scale (khong can lop ve rieng). */

static void gtext_init(gtext_t *g, lv_obj_t *p, int x, int y, int w, int h)
{
    memset(g, 0, sizeof *g);
    g->db = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    g->cv = lv_canvas_create(p);
    lv_canvas_set_draw_buf(g->cv, g->db);
    lv_draw_buf_clear(g->db, NULL);
    lv_obj_set_pos(g->cv, x, y);
    lv_obj_remove_flag(g->cv, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_antialias(g->cv, true);
}

/* aw: be rong vung chu (can theo al); mau chuyen tu (x0, y0) toi (x0 + xw, y0 + yh) */
static void gtext_set(gtext_t *g, const char *txt, const lv_font_t *f, int ls, lv_text_align_t al, int aw,
                      uint32_t c1, uint32_t c2, int x0, int xw, int y0, int yh)
{
    if (!g->db) return;
    if (!strcmp(g->txt, txt) && g->f == f && g->ls == ls && g->al == al && g->aw == aw && g->c1 == c1 && g->c2 == c2
        && g->x0 == x0 && g->xw == xw && g->y0 == y0 && g->yh == yh) return;
    snprintf(g->txt, sizeof g->txt, "%s", txt);
    g->f = f; g->ls = ls; g->al = al; g->aw = aw; g->c1 = c1; g->c2 = c2;
    g->x0 = x0; g->xw = xw > 0 ? xw : 1; g->y0 = y0; g->yh = yh > 0 ? yh : 1;
    lv_draw_buf_clear(g->db, NULL);
    lv_layer_t layer;
    lv_canvas_init_layer(g->cv, &layer);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = f;
    d.color = lv_color_white();
    d.letter_space = ls;
    d.align = al;
    d.text = g->txt;
    lv_area_t a = {0, 0, aw - 1, f->line_height - 1};
    lv_draw_label(&layer, &d, &a);
    lv_canvas_finish_layer(g->cv, &layer);
    int w = g->db->header.w, h = g->db->header.h, st = g->db->header.stride;
    uint8_t r1 = c1 >> 16, g1 = c1 >> 8, b1 = c1, r2 = c2 >> 16, g2 = c2 >> 8, b2 = c2;
    for (int y = 0; y < h; y++) {
        uint8_t *px = g->db->data + y * st;
        int ty = (y - g->y0) * 256 / g->yh;
        for (int x = 0; x < w; x++, px += 4) {
            if (!px[3]) continue;
            int t = ((x - g->x0) * 256 / g->xw + ty) / 2;
            if (t < 0) t = 0;
            if (t > 255) t = 255;
            px[0] = (uint8_t)((b1 * (255 - t) + b2 * t) / 255);
            px[1] = (uint8_t)((g1 * (255 - t) + g2 * t) / 255);
            px[2] = (uint8_t)((r1 * (255 - t) + r2 * t) / 255);
        }
    }
    lv_image_cache_drop(g->db);
    lv_obj_invalidate(g->cv);
}

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

/* So toc do ve tu font 190 px (Inter 800) roi thu nho: net min o moi co. */
#define NF      (&ui_font_d190)
#define NCAP    CAP_H(190)
#define NLS     (-190 * 4 / 100)
#define UNIT_DY 9                                  /* chan chu so -> dinh chu "km/h" */
static int w88, w188;                              /* be rong goc "88" / "188" o font 190 */

typedef struct { int rs, bd, cap, ncx, ncy, rcx, rcy; } hgeo_t;

/* Vung (y, cao) -> co bien, co chu so, tam so / tam bien. Chu so cao gan bang bien;
 * 3 chu so: so giu co to (toi da theo be ngang), bien nho lai ~72%. */
static hgeo_t head_geo(int y, int hgt, bool three)
{
    hgeo_t g;
    int W = UI_W - 2 * PAD;
    int rs = hgt - 10;
    if (rs > 170) rs = 170;
    int cap = hgt - 34;                            /* chua cho "km/h" ben duoi */
    if (cap > rs - 6) cap = rs - 6;
    int avail;
    if (three) {
        rs = rs * 72 / 100;
        avail = W - rs - 10;
        g.rcx = UI_W - PAD - rs / 2;
        int cw = NCAP * avail / (w188 ? w188 : 1);
        if (cap > cw) cap = cw;
    } else {
        int colR = W * 45 / 100;
        if (colR < rs) colR = rs;
        avail = W - colR - 8;
        g.rcx = UI_W - PAD - colR / 2;
        int cw = NCAP * avail / (w88 ? w88 : 1);
        if (cap > cw) cap = cw;
    }
    g.ncx = PAD + avail / 2;
    g.rs = rs;
    g.bd = rs * 115 / 1000;
    g.cap = cap;
    g.rcy = y + hgt / 2;
    g.ncy = g.rcy - (UNIT_DY + CAP_H(16)) / 2 - 2;  /* ca khoi so + km/h can giua theo bien */
    return g;
}

static head_t mk_head(lv_obj_t *p, int n)
{
    head_t h = {.n = n};
    if (!w88) {
        int md = 0;                                          /* chu so rong nhat (Inter: 0 rong hon 8) */
        for (char c = '0'; c <= '9'; c++) {
            int cw = lv_text_get_width(&c, 1, NF, 0);
            if (cw > md) md = cw;
        }
        w88 = 2 * md + NLS;
        w188 = lv_text_get_width("1", 1, NF, 0) + 2 * md + 2 * NLS;
    }
    h.ring = mk_fill(p, 0, 0, 100, 100, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(h.ring, lv_color_white(), 0);
    lv_obj_set_style_border_color(h.ring, RED_SIGN, 0);
    lv_obj_set_style_border_opa(h.ring, LV_OPA_COVER, 0);
    h.rlbl = mk_label(h.ring, &ui_font_r92, 0, 0, "-");
    lv_obj_set_style_text_color(h.rlbl, lv_color_hex(0x111111), 0);
    gtext_init(&h.gt, p, 0, 0, w188 * 3 / 2, NF->line_height);       /* du rong cho moi so 3 chu so, khong xuong dong */
    h.unit = mk_label(p, &ui_font_txt16, 0, 0, "km/h");
    lv_obj_set_style_shadow_color(h.ring, RED_SIGN, 0);
    lv_obj_set_style_shadow_offset_y(h.ring, 4, 0);
    lv_obj_set_style_shadow_width(h.ring, 18, 0);
    return h;
}

static void head_area(head_t *h, int i, int y, int hgt)          /* vung (y, cao) cua trang thai i trong trang */
{
    h->ay[i] = y;
    h->ah[i] = hgt;
}

/* w[i]: trong so tung trang thai (tong T_MAX): so, bien, vi tri doi dan muot trong 0,4 s. */
static void render_head(head_t *h, const int *w)
{
    char b[12];
    if (speed < 0) snprintf(b, sizeof b, "--");
    else snprintf(b, sizeof b, "%02d", speed % 1000);          /* 7 -> "07": so khong nhay ra vao */
    size_t bl = strlen(b);
    bool three = speed >= 100;
    hgeo_t g = {0};
    for (int i = 0; i < h->n; i++) {
        hgeo_t k = head_geo(h->ay[i], h->ah[i], three);
        g.rs += k.rs * w[i]; g.bd += k.bd * w[i]; g.cap += k.cap * w[i];
        g.ncx += k.ncx * w[i]; g.ncy += k.ncy * w[i]; g.rcx += k.rcx * w[i]; g.rcy += k.rcy * w[i];
    }
    g.rs /= T_MAX; g.bd /= T_MAX; g.cap /= T_MAX; g.ncx /= T_MAX; g.ncy /= T_MAX; g.rcx /= T_MAX; g.rcy /= T_MAX;

    int aw = h->gt.db ? (int)h->gt.db->header.w : 0;
    int tw = lv_text_get_width(b, bl, NF, NLS);
    const uint32_t *gc = speed < 0 ? NULL : level == UI_LEVEL_OVER ? th->g_over : level == UI_LEVEL_NEAR ? th->g_near : th->g_ok;
    uint32_t m = lv_color_to_u32(th->mut) & 0xFFFFFF;
    int ctop = baseline(NF) - NCAP;
    gtext_set(&h->gt, b, NF, NLS, LV_TEXT_ALIGN_CENTER, aw, gc ? gc[0] : m, gc ? gc[1] : m, (aw - tw) / 2, tw, ctop, NCAP);
    int px = aw / 2, py = baseline(NF) - NCAP / 2;                /* tam chu so trong canvas */
    lv_obj_t *n = h->gt.cv;
    lv_image_set_pivot(n, px, py);
    lv_image_set_scale(n, (uint32_t)(g.cap * 256 / NCAP));
    lv_obj_set_pos(n, g.ncx - px, g.ncy - py);
    lv_obj_t *u = h->unit;                                         /* km/h: duoi so, canh giua theo truc doc cua so */
    lv_obj_set_style_text_color(u, th->fg, 0);
    lv_obj_set_pos(u, g.ncx - text_w("km/h", &ui_font_txt16) / 2,
                   g.ncy + g.cap / 2 + UNIT_DY - (baseline(&ui_font_txt16) - CAP_H(16)));

    lv_obj_set_size(h->ring, g.rs, g.rs);
    lv_obj_set_pos(h->ring, g.rcx - g.rs / 2, g.rcy - g.rs / 2);
    lv_obj_set_style_border_width(h->ring, g.bd, 0);
    lv_obj_set_style_shadow_opa(h->ring, th->sign_opa, 0);
    if (limit > 0) snprintf(b, sizeof b, "%d", limit % 1000);
    else snprintf(b, sizeof b, "-");
    const lv_font_t *rf = limit >= 100 ? &ui_font_r76 : &ui_font_r92;   /* thiet ke cho bien 202 px */
    lv_obj_set_style_text_font(h->rlbl, rf, 0);
    lv_obj_set_style_text_letter_space(h->rlbl, -(int)rf->line_height * 3 / 100, 0);
    lv_label_set_text(h->rlbl, b);
    lv_obj_set_style_transform_pivot_x(h->rlbl, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(h->rlbl, lv_pct(50), 0);
    lv_obj_set_style_transform_scale(h->rlbl, g.rs * 256 / 202, 0);
    lv_obj_center(h->rlbl);
}

static void render_head2(head_t *h, int t)                       /* 2 co: t = 0 nho .. T_MAX to */
{
    int w[HLV] = {T_MAX - t, t, 0};
    render_head(h, w);
}

/* phan chi hien khi co dai: vis = T_MAX hien ro; giam dan thi mo + truot xuong 16 px */
static void fade_part(lv_obj_t *o, int vis)
{
    lv_obj_set_style_opa(o, (lv_opa_t)(vis * 255 / T_MAX), 0);
    lv_obj_set_style_translate_y(o, 16 * (T_MAX - vis) / T_MAX, 0);
    show(o, vis > 0);
}

static void render_cruise(void);
static void render_nav(void);
static void render_highway(void);

static void morph_cb(void *var, int32_t v)
{
    *(int *)var = v;
    if ((var == &d_t || var == &c_t) && screen == SCR_CRUISE) render_cruise();
    else if (var == &n_t && screen == SCR_NAV) render_nav();
    else if (var == &h_t && screen == SCR_HIGHWAY) render_highway();
}

/* dat trang thai muon co (big: an dai, so to). Vua doi man thi dat ngay; khong thi animation 0,4 s */
static void morph_to(int *t, int *want, bool big)
{
    int target = big ? T_MAX : 0;
    if (*want == target && !scr_fresh) return;
    *want = target;
    lv_anim_delete(t, NULL);
    if (scr_fresh || *t == target) { *t = target; return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, t);
    lv_anim_set_exec_cb(&a, morph_cb);
    lv_anim_set_values(&a, *t, target);
    lv_anim_set_duration(&a, 400 * (uint32_t)abs(target - *t) / T_MAX);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static strip_t mk_strip(lv_obj_t *p, int x, int y, int w, int h)
{
    strip_t s = {.h = h, .df = h >= 60 ? &ui_font_dist30 : &ui_font_dist26};
    const lv_font_t *tf = h >= 60 ? &ui_font_txt20 : &ui_font_txt16;
    s.box = mk_card(p, x, y, w, h, h >= 50 ? 12 : 10);
    card_style(s.box, true);
    bool bar = h >= 50;
    int row = (h - 2) / 2 - (bar ? 5 : 0);                       /* giua hang chu */
    s.img = mk_image(s.box, 12, row - 15, 30, 48);
    s.txt = mk_label_w(s.box, tf, 52, row - tf->line_height / 2, 200, LV_TEXT_ALIGN_LEFT);
    s.dist = mk_label(s.box, s.df, 0, row + CAP_H(h >= 60 ? 30 : 26) / 2 - baseline(s.df), "");
    lv_obj_set_style_text_letter_space(s.dist, -1, 0);
    s.spk = mk_label(s.box, &lv_font_montserrat_14, 0, row - lv_font_montserrat_14.line_height / 2, LV_SYMBOL_VOLUME_MAX);
    if (bar) {
        s.bar = mk_fill(s.box, 12, h - 2 - 11, w - 2 - 24, 5, 3);     /* ranh lom */
        s.fill = mk_fill(s.bar, 0, 0, 0, 5, 3);
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
        *bg = lv_color_hex(police ? 0xFFE0E4 : cam ? 0xFFE6D3 : 0xFFF4C2);
        *bd = lv_color_hex(police ? 0xF5A3AB : cam ? 0xF7B58A : 0xF2D36B);
        *fg = lv_color_hex(police ? 0xBE123C : cam ? 0xC2410C : 0xB45309);
    }
}

static bool urgent_alert(int code)                               /* chop + tit: nguy hiem, camera, CSGT */
{
    return code != 6 && code != 12 && code != 19 && !(code >= 20 && code <= 21) && code != 35 && code != 36;
}

/* dai vuot toc do: dem do tuoi / do sam; ngay nen chuyen do -> cam, chop bang do mo */
static void over_paint(strip_t *s)
{
    if (night) {
        lv_color_t c = lv_color_hex(blink_dim ? 0x5A0C12 : 0xE11D2A);
        lv_obj_set_style_bg_color(s->box, c, 0);
        lv_obj_set_style_bg_grad_dir(s->box, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_color(s->box, c, 0);
        lv_obj_set_style_opa(s->box, LV_OPA_COVER, 0);
    } else {
        hgrad(s->box, lv_color_hex(0xFF3D3D), lv_color_hex(0xFFA23A));
        lv_obj_set_style_border_color(s->box, lv_color_hex(0xFF6A3D), 0);
        lv_obj_set_style_opa(s->box, blink_dim ? 150 : LV_OPA_COVER, 0);
    }
}

/* ve dai: icon, chu, khoang cach (mau fg), loa, thanh tien do (permille < 0: an) */
static void strip_fill(strip_t *s, const lv_image_dsc_t *icon, const char *txt, const char *dist,
                       lv_color_t bg, lv_color_t bd, lv_color_t fg, bool spk, int permille, int blink)
{
    int w = lv_obj_get_width(s->box) - 2;
    lv_obj_set_style_bg_color(s->box, bg, 0);
    lv_obj_set_style_bg_grad_dir(s->box, LV_GRAD_DIR_NONE, 0);
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
            lv_obj_set_style_bg_color(s->bar, night ? th->track : lv_color_mix(lv_color_hex(0x23253A), bg, 31), 0);
            lv_obj_set_style_bg_color(s->fill, fg, 0);
            set_bar(s->bar, s->fill, permille);
        }
    }
    s->blink = blink;
    lv_obj_set_style_opa(s->box, blink == 1 && blink_dim ? 89 : LV_OPA_COVER, 0);
    if (blink == 2) over_paint(s);
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
               a->dist >= 0 && a->code != 8 ? progress(ev_start, a->dist) : -1, urg && ev_fresh());
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
        else if (all[i]->blink == 1 && !ev_fresh()) {              /* het 8 giay: thoi chop */
            all[i]->blink = 0;
            lv_obj_set_style_opa(all[i]->box, LV_OPA_COVER, 0);
        } else if (all[i]->blink == 1) lv_obj_set_style_opa(all[i]->box, blink_dim ? 89 : LV_OPA_COVER, 0);
        else over_paint(all[i]);                                   /* vuot toc do, chu trang */
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
    t_bolt = mk_label(top, &lv_font_montserrat_14, 0, (TOP_H - lv_font_montserrat_14.line_height) / 2, LV_SYMBOL_CHARGE);
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

    bool tsd = screen == SCR_NAV && nav_on && nav_is_ts;              /* dan toi tram sac: luon hien ten tram */
    const char *st = tsd ? street_nav : !waze ? "Chỉ có GPS" : screen == SCR_NAV && street_nav[0] ? street_nav : street_now;
    lv_label_set_text(t_street, st);
    lv_obj_set_style_text_color(t_street, th->fg, 0);
    bool hw = screen == SCR_HIGHWAY;
    show(t_badge, hw);
    show(t_bolt, tsd);
    lv_obj_set_style_bg_color(t_badge, th->hw, 0);
    lv_obj_set_style_text_color(t_bolt, lv_color_hex(night ? 0x5EEAD4 : 0x0F766E), 0);
    int pre = hw ? lv_obj_get_width(t_badge) + 6 : tsd ? text_w(LV_SYMBOL_CHARGE, &lv_font_montserrat_14) + 6 : 0;
    int sw = text_w(st, &ui_font_txt14);
    int maxw = 300 - pre;
    if (sw > maxw) sw = maxw;
    int total = sw + pre, x0 = (UI_W - total) / 2;
    if (hw) lv_obj_set_x(t_badge, x0);
    if (tsd) lv_obj_set_x(t_bolt, x0);
    lv_obj_set_x(t_street, x0 + pre);
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
    if (phone) hgrad(t_gps_dot, lv_color_hex(th->acc[0]), lv_color_hex(th->acc[1]));
    else {
        lv_obj_set_style_bg_color(t_gps_dot, th->dim, 0);
        lv_obj_set_style_bg_grad_dir(t_gps_dot, LV_GRAD_DIR_NONE, 0);
    }
}

/* ======================= man Toc do + Canh bao =======================
 * Mot man, 3 co cua phan tren (chuyen muot 0,4 s):
 *  0. canh bao trong 800 m: so 104 px | bien 100 px, dai canh bao 64 px chop 1 Hz (mau theo loai) + loa, 2 o phu
 *  1. vuot toc do / canh bao xa: so 150 px | bien 176 px, dai 40 px (vuot toc do: do tuoi / do sam)
 *  2. duong thoang: an dai, so 150 px (canh giua man) | bien 202 px */
#define C_STRIP_H 40
#define A_CELL_H  64
#define A_ST_H    64
static void build_cruise(void)
{
    lv_obj_t *p = page[SCR_CRUISE] = mk_box(scr, 0, TOP_H, UI_W, BODY_H);
    d_hd = mk_head(p, 3);
    head_area(&d_hd, 1, 0, BODY_H - 12 - C_STRIP_H);
    head_area(&d_hd, 2, 0, BODY_H);
    c_st = mk_strip(p, PAD, BODY_H - 12 - C_STRIP_H, UI_W - 2 * PAD, C_STRIP_H);
    a_st = mk_strip(p, PAD, 0, UI_W - 2 * PAD, A_ST_H);
    int cy = BODY_H - 12 - A_CELL_H, cellw = (UI_W - 2 * PAD - 10) / 2;
    for (int i = 0; i < 2; i++) {
        a_cell[i] = mk_fill(p, PAD + i * (cellw + 10), cy, cellw, A_CELL_H, 10);
        card_style(a_cell[i], false);
        a_cell_img[i] = mk_image(a_cell[i], 10, (A_CELL_H - 40) / 2, 40, 48);
        a_cell_t[i] = mk_label_w(a_cell[i], &ui_font_txt14, 60, 12, cellw - 70, LV_TEXT_ALIGN_LEFT);
        a_cell_s[i] = mk_label_w(a_cell[i], &ui_font_sm12, 60, 34, cellw - 70, LV_TEXT_ALIGN_LEFT);
    }
}

static int a_cells_n;

static void render_alert_parts(void)                             /* dai canh bao + o phu (chi khi co canh bao gan) */
{
    int others = 0;
    for (int i = 0; i < al_n; i++) others += &al_list[i] != ev_now;
    int sy = others ? BODY_H - 12 - A_CELL_H - 10 - A_ST_H : BODY_H - 12 - A_ST_H;   /* khong co o phu: dai xuong day */
    lv_obj_set_y(a_st.box, sy);
    head_area(&d_hd, 0, 8, sy - 8 - 8);
    strip_alert(&a_st, ev_now);
    int k = 0;
    for (int i = 0; i < al_n && k < 2; i++) {
        const ui_alert_t *a = &al_list[i];
        if (a == ev_now) continue;
        lv_obj_set_style_bg_color(a_cell[k], th->card, 0);
        set_alert_icon(a_cell_img[k], a);
        lv_label_set_text(a_cell_t[k], alert_name(a->code));
        lv_obj_set_style_text_color(a_cell_t[k], th->fg, 0);
        char d[16], s2[40];
        fmt_dist(d, sizeof d, a->dist);
        if (a->code == 8 && a->value > 0) snprintf(s2, sizeof s2, "giảm còn %d km/h", a->value);
        else if (d[0]) snprintf(s2, sizeof s2, "còn %s", d);
        else snprintf(s2, sizeof s2, "đang hiệu lực");
        lv_label_set_text(a_cell_s[k], s2);
        lv_obj_set_style_text_color(a_cell_s[k], th->fg, 0);
        k++;
    }
    a_cells_n = k;
}

/* Tram sac gan (app Tram sac): dai xanh ngoc o man Bien toc do khi khong co canh bao / vuot toc do. */
#define TS_HOLD_MS 90000
#define TS_NO_ANGLE (-999)
static char ts_name[64];
static int ts_dist, ts_kw, ts_ports, ts_ang = TS_NO_ANGLE;
static char ts_side = '?', ts_mode = 'N';
static char ts_done[64];                                          /* tram dich nguoi dung da bam "Ket thuc" */
static uint32_t ts_tick;
static lv_timer_t *ts_tmr;

static bool station_on(void) { return ts_name[0] && lv_tick_elaps(ts_tick) < TS_HOLD_MS; }
static bool ts_dest_on(void) { return ts_mode == 'D' && station_on(); }

static int ts_turn(void)                                          /* goc lech -> ma re HLP (mui ten) */
{
    if (ts_ang == TS_NO_ANGLE) return 1;
    int a = ts_ang < 0 ? -ts_ang : ts_ang;
    bool r = ts_ang > 0;
    if (a <= 20) return 1;
    if (a <= 60) return r ? 5 : 4;
    if (a <= 120) return r ? 3 : 2;
    if (a <= 160) return r ? 7 : 6;
    return 8;
}

static void ts_title(char *b, size_t n)                           /* ten tram + kW / cong */
{
    int k = snprintf(b, n, "%s", ts_name);
    if (ts_kw > 0 && k < (int)n) k += snprintf(b + k, n - k, " · %d kW", ts_kw);
    if (ts_ports > 0 && k < (int)n) snprintf(b + k, n - k, " · %d cổng", ts_ports);
}

static void station_fill(strip_t *s)
{
    char d[16], t[96];
    fmt_dist(d, sizeof d, ts_dist);
    const char *sd = ts_side == 'R' ? " · bên phải" : ts_side == 'L' ? " · bên trái" : ts_side == 'B' ? " · phía sau" : "";
    snprintf(t, sizeof t, "%s%s", ts_name, sd);
    const lv_image_dsc_t *ic = ts_side == 'R' ? &arrow_right : ts_side == 'L' ? &arrow_left
                             : ts_side == 'A' ? &arrow_straight : ts_side == 'B' ? &arrow_uturn : NULL;
    strip_fill(s, ic, t, d, lv_color_hex(night ? 0x1B5259 : 0xC4E2E8), lv_color_hex(night ? 0x3E8685 : 0x7FD8CC),
               lv_color_hex(night ? 0x5EEAD4 : 0x0F766E), false, -1, 0);
    if (night) hgrad(s->box, lv_color_hex(0x1B5259), lv_color_hex(0x1F4062));
    if (ic) {
        lv_image_set_scale(s->img, 30 * 256 / 64);                /* mui ten 64 px (bien bao 48 px) */
        lv_obj_set_style_image_recolor(s->img, lv_color_hex(night ? 0x5EEAD4 : 0x0F766E), 0);
        lv_obj_set_style_image_recolor_opa(s->img, LV_OPA_COVER, 0);
    }
}

static void render_cruise(void)
{
    bool near = ev_now && ev_now->dist <= ALERT_M;
    bool over = level == UI_LEVEL_OVER;
    bool st = station_on() && ts_mode != 'D';
    bool strip = !near && (over || ev_now || st);                  /* dai 40 px: vuot toc do / canh bao xa / tram sac */
    morph_to(&d_t, &d_want, !near);
    if (!near && !strip && d_t < T_MAX) {                          /* het canh bao, duong thoang: di thang toi co to */
        lv_anim_delete(&c_t, NULL);
        c_t = c_want = T_MAX;
    }
    morph_to(&c_t, &c_want, !near && !strip);

    if (near) render_alert_parts();
    if (strip) {
        lv_obj_set_style_image_recolor_opa(c_st.img, LV_OPA_TRANSP, 0);
        lv_image_set_scale(c_st.img, 30 * 256 / 48);
        if (over) {
            strip_over(&c_st);
            lv_obj_set_style_text_align(c_st.txt, LV_TEXT_ALIGN_LEFT, 0);
        } else if (!ev_now) {
            station_fill(&c_st);
        } else {
            char d[16];
            fmt_dist(d, sizeof d, ev_now->dist);
            lv_color_t bg, bd, fg;
            alert_colors(ev_now->code, &bg, &bd, &fg);
            strip_fill(&c_st, sign_for_alert(ev_now->code), alert_name(ev_now->code), d, th->card, th->card, fg, false, -1, 0);
        }
    } else if (!over) {
        c_st.blink = 0;
    }
    if (!near) a_st.blink = a_st.blink && d_t < T_MAX;            /* het chop khi da an */

    int w[HLV] = {T_MAX - d_t, d_t * (T_MAX - c_t) / T_MAX, d_t * c_t / T_MAX};
    render_head(&d_hd, w);
    fade_part(a_st.box, w[0]);
    for (int k = 0; k < 2; k++) fade_part(a_cell[k], k < a_cells_n ? w[0] : 0);
    fade_part(c_st.box, w[1]);
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
    h_hd = mk_head(p, 2);
    head_area(&h_hd, 0, 6, sy - 8 - 6);
    head_area(&h_hd, 1, 0, by - 8);                                /* an dai: so to ra, hang tram nghi / thu phi van giu */
    h_st = mk_strip(p, PAD, sy, UI_W - 2 * PAD, H_ST_H);
    h_bar = mk_fill(p, PAD, by, UI_W - 2 * PAD, H_BAR_H, 10);
    card_style(h_bar, false);
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
    bool item = false;
    for (int i = 0; i < al_n; i++) item |= al_list[i].code != 12 && al_list[i].code != 19;
    morph_to(&h_t, &h_want, !item);                                /* qua bien / canh bao: an dai, so to */
    render_head2(&h_hd, h_t);
    fade_part(h_st.box, T_MAX - h_t);
    if (!item && h_t > 0) goto bar;                            /* dang an: giu noi dung dai cu */
    const ui_alert_t *it = NULL;                                   /* bien / canh bao phia truoc (tru tram nghi, thu phi) */
    for (int i = 0; i < al_n && !it; i++)
        if (al_list[i].code != 12 && al_list[i].code != 19) it = &al_list[i];
    if (it) {
        char d[16];
        fmt_dist(d, sizeof d, it->dist);
        lv_color_t abg, abd, afg;                                  /* canh bao: mau theo loai (xanh ngoc chi cho loi ra) */
        alert_colors(it->code, &abg, &abd, &afg);
        strip_fill(&h_st, sign_for_alert(it->code), alert_name(it->code), d, abg, abd, afg, false, -1, false);
    } else {
        char b[64];
        if (limit > 0) snprintf(b, sizeof b, "Tối đa %d km/h · giữ khoảng cách", limit);
        else snprintf(b, sizeof b, "Giữ khoảng cách an toàn");
        strip_fill(&h_st, &arrow_straight, b, "", th->hw_bg, th->hw_bd, th->fg, false, -1, false);
    }
    if (night && !it) hgrad(h_st.box, th->hw_bg, lv_color_hex(0x1F4361));   /* ban ngay: mau don (RGB565 de bi soc) */
    lv_obj_set_style_image_recolor(h_st.img, th->fg, 0);
    lv_obj_set_style_image_recolor_opa(h_st.img, it ? LV_OPA_TRANSP : LV_OPA_COVER, 0);

bar:
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
#define N_ROW_H   56                       /* hang duoi: o re + gio den / dai canh bao */
#define N_TURN_W  150
#define N_ROW_LO  (BODY_H - 12 - N_ROW_H)              /* binh thuong: hang nam day */
#define N_ROW_HI  (N_ROW_LO - 8 - N_ROW_H)             /* co canh bao: hang day len, dai vao cho day */
static lv_obj_t *n_alt_st, *n_alt_clk;
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
    int ew = UI_W - 2 * PAD;
    n_hd = mk_head(p, 2);
    head_area(&n_hd, 0, 4, N_ROW_HI - 8 - 4);                      /* co canh bao: phan tren nho lai */
    head_area(&n_hd, 1, 0, N_ROW_LO - 8);                          /* binh thuong: so + bien to */
    n_st = mk_strip(p, PAD, N_ROW_LO, ew, N_ROW_H);
    n_cell[0] = mk_card(p, PAD, N_ROW_LO, N_TURN_W, N_ROW_H, 12);
    card_style(n_cell[0], true);
    n_cell_img[0] = mk_image(n_cell[0], 0, (N_ROW_H - 2 - 26) / 2, 26, 64);
    lv_obj_set_style_image_recolor_opa(n_cell_img[0], LV_OPA_COVER, 0);
    n_cell_d[0] = mk_label(n_cell[0], &ui_font_dist26, 0, (N_ROW_H - 2) / 2 + CAP_H(26) / 2 - baseline(&ui_font_dist26), "");
    lv_obj_set_style_text_letter_space(n_cell_d[0], -1, 0);
    int bx = PAD + N_TURN_W + 8, bw = ew - N_TURN_W - 8;
    n_eta = mk_fill(p, bx, N_ROW_LO, bw, N_ROW_H, 12);
    card_style(n_eta, false);
    static const char *const cap[3] = {"Giờ đến", "Còn lại", "Thời gian"};
    int iw = bw - 2;
    for (int i = 0; i < 3; i++) {
        n_eta_v[i] = mk_label_w(n_eta, &ui_font_txt20, i * iw / 3, 7, iw / 3, LV_TEXT_ALIGN_CENTER);
        n_eta_l[i] = mk_label_w(n_eta, &ui_font_sm12, i * iw / 3, 33, iw / 3, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(n_eta_l[i], cap[i]);
    }
    /* chua co lo trinh: o nay hien ten duong + dong ho, hang duoi khong bao gio trong */
    n_alt_st = mk_label_w(n_eta, &ui_font_txt16, 12, (N_ROW_H - 2 - ui_font_txt16.line_height) / 2, iw - 24 - 70, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(n_alt_st, LV_LABEL_LONG_DOT);
    n_alt_clk = mk_label_w(n_eta, &ui_font_txt20, iw - 12 - 70, (N_ROW_H - 2 - ui_font_txt20.line_height) / 2, 70, LV_TEXT_ALIGN_RIGHT);
}

/* Kieu C: hang (o re + gio den) nam day. Co canh bao: hang truot len, dai canh bao truot vao cho day,
 * phan tren nho lai; qua canh bao: dai truot xuong bien mat, hang ve lai cho cu. */
static void render_nav(void)
{
    morph_to(&n_t, &n_want, ev_now == NULL);
    int t = n_t;
    render_head2(&n_hd, t);
    int ry = N_ROW_HI + (N_ROW_LO - N_ROW_HI) * t / T_MAX;
    lv_obj_set_y(n_cell[0], ry);
    lv_obj_set_y(n_eta, ry);

    /* o re dang toi */
    const lv_image_dsc_t *arr = arrow_for_turn(turn_now);
    static const lv_image_dsc_t *const base[3] = {&arrow_left, &arrow_straight, &arrow_right};
    lv_obj_t *c = n_cell[0];
    hgrad(c, lv_color_hex(th->acc[0]), lv_color_hex(th->acc[1]));
    lv_obj_set_style_border_color(c, mix_hex(th->acc[0], 0xFFFFFF, 64), 0);
    lv_obj_set_style_shadow_color(c, lv_color_hex(0xFF5A46), 0);
    lv_obj_set_style_shadow_opa(c, night ? 77 : 60, 0);
    lv_obj_set_style_shadow_offset_x(c, 0, 0);
    lv_obj_set_style_shadow_offset_y(c, 4, 0);
    lv_image_set_src(n_cell_img[0], arr ? arr : base[turn_side(turn_now)]);
    lv_obj_set_style_image_recolor(n_cell_img[0], lv_color_white(), 0);
    char num[12], d[20];
    const char *u;
    fmt_dist2(num, sizeof num, &u, dist_now);
    if (num[0]) snprintf(d, sizeof d, "%s %s", num, u);
    else d[0] = 0;
    lv_label_set_text(n_cell_d[0], d);
    lv_obj_set_style_text_color(n_cell_d[0], lv_color_white(), 0);
    int gw = 26 + (d[0] ? 8 + lv_text_get_width(d, strlen(d), &ui_font_dist26, -1) : 0);
    int x0 = (N_TURN_W - 2 - gw) / 2;
    lv_obj_set_x(n_cell_img[0], x0);
    lv_obj_set_x(n_cell_d[0], x0 + 34);

    /* o gio den / con lai / thoi gian, hoac ten duong + gio khi chua co lo trinh */
    lv_obj_set_style_bg_color(n_eta, th->card, 0);
    for (int i = 0; i < 3; i++) {
        show(n_eta_v[i], has_route);
        show(n_eta_l[i], has_route);
    }
    show(n_alt_st, !has_route);
    show(n_alt_clk, !has_route);
    if (has_route) {
        char v[3][20];
        snprintf(v[0], sizeof v[0], "%s", eta_txt[0] ? eta_txt : "--:--");
        fmt_dist(v[1], sizeof v[1], eta_rm > 0 ? eta_rm : -1);
        if (!v[1][0]) snprintf(v[1], sizeof v[1], "--");
        if (eta_rmin >= 60) snprintf(v[2], sizeof v[2], "%dg%02d", eta_rmin / 60, eta_rmin % 60);
        else if (eta_rmin > 0) snprintf(v[2], sizeof v[2], "%d phút", eta_rmin);
        else snprintf(v[2], sizeof v[2], "--");
        for (int i = 0; i < 3; i++) {
            lv_label_set_text(n_eta_v[i], v[i]);
            lv_obj_set_style_text_color(n_eta_v[i], th->fg, 0);
            lv_obj_set_style_text_color(n_eta_l[i], th->mut, 0);
        }
    } else {
        const char *st = street_nav[0] ? street_nav : street_now[0] ? street_now : "Đang dẫn đường";
        lv_label_set_text(n_alt_st, st);
        lv_obj_set_style_text_color(n_alt_st, th->fg, 0);
        char b[8];
        if (clk_h >= 0) snprintf(b, sizeof b, "%02d:%02d", clk_h % 24, clk_m % 60);
        else snprintf(b, sizeof b, "--:--");
        lv_label_set_text(n_alt_clk, b);
        lv_obj_set_style_text_color(n_alt_clk, th->fg, 0);
    }

    /* dai canh bao o cho day: truot vao khi co, truot xuong bien mat khi qua (giu noi dung cu luc truot ra) */
    if (ev_now) strip_alert(&n_st, ev_now);
    else n_st.blink = 0;
    int vis = T_MAX - t;
    lv_obj_set_style_translate_y(n_st.box, 70 * t / T_MAX, 0);
    if (!n_st.blink || vis < T_MAX) lv_obj_set_style_opa(n_st.box, (lv_opa_t)(vis * 255 / T_MAX), 0);
    show(n_st.box, vis > 0);
}

/* ======================= ve tat ca ======================= */
static void render_all(void)
{
    if (!scr || screen < 0) return;
    bg_grad(scr);
    render_top();
    switch (screen) {
    case SCR_CRUISE: render_cruise(); break;
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
    for (int i = 0; i < SCR_N; i++) if (page[i]) show(page[i], i == s);
    scr_fresh = true;
    if (level == UI_LEVEL_OVER && s != SCR_STANDBY) lv_timer_resume(blink_tmr);
    else { lv_timer_pause(blink_tmr); show(frame, false); }
    render_all();
    scr_fresh = false;
    if (s == SCR_STANDBY) render_standby();
}

/* Chua co du lieu -> man cho; dang dan duong -> chi dan; canh bao gan -> canh bao; cao toc; chay thuong. */
static void update_screen(void)
{
    int s;
    if (nav_on && (nav_is_ts || wz_hold)) s = SCR_NAV;              /* dan toi tram sac / dang giu dan duong: ca khi mat tin hieu */
    else if (!sig_ok || (!nav_on && !moving)) s = SCR_STANDBY;
    else if (nav_on) s = SCR_NAV;
    else if (ev_now && ev_now->dist <= ALERT_M) s = SCR_CRUISE;   /* man Toc do o co nho + dai canh bao */
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
    theme_styles();
    bg_grad(page[SCR_STANDBY]);
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
    else if (clk_h >= 0) n = clk_h < 6 || clk_h >= 18;
    else n = true;                                                 /* Tu dong ma chua co gio: mac dinh Dem (khong doan gio) */
    const theme_t *want = n ? &TH_NIGHT : &TH_DAY;
    if (want == th) return;
    lv_color_t old_bg = th ? th->bg : want->bg;
    bool fade = th && scr;
    night = n;
    th = want;
    apply_theme();
    if (fade) {                                                    /* doi theme khi dang chay: phu mau nen cu roi mo dan 0,5 s */
        lv_obj_t *v = mk_box(lv_layer_top(), 0, 0, UI_W, UI_H);
        lv_obj_set_style_bg_color(v, old_bg, 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
        lv_obj_move_background(v);
        lv_obj_fade_out(v, 500, 0);
        lv_obj_delete_delayed(v, 520);
    }
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
    lv_obj_set_style_bg_color(k, lv_color_white(), 0);
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
        lv_obj_set_style_text_color(lv_obj_get_child(set_tab[i], 0), cur ? S_GREEN3 : lv_color_hex(0xA3A8BD), 0);
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
    lv_obj_set_style_border_color(o, lv_color_hex(0x7A4535), 0);
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
 * Theo man mau "Man cho · dong ho · Wi-Fi": thanh tren = thoi tiet (trai), Wi-Fi + Bluetooth + o nguon (phai); dong ho to
 * giua man + ngay thang; dai duoi. Khong doan gio: chua co nguon gio nao thi hien "--:--", "Chua co gio" va dai vang
 * "Ket noi Wi-Fi hoac dien thoai de lay gio" (cham de mo trang Wi-Fi). Co gio: dai cho ket noi + o "Gio: Wi-Fi / dien thoai". */
#define SB_BAR_H 54
#define C_YEL    0xFACC15
static lv_obj_t *sb_wx_dot, *sb_wx, *sb_wifi_hit, *sb_wifi, *sb_bt, *sb_bar_wifi, *sb_bar_bt;
static int tsrc, wifi_st = UI_WIFI_OFF, wx_kind;
static char wx_txt[64], wifi_ssid[33];

/* Bieu tuong Wi-Fi: 3 cung + 1 cham (font khong co), gach cheo do khi chua ket noi. */
static lv_obj_t *mk_wifi_icon(lv_obj_t *p, int x, int y, int s)
{
    lv_obj_t *box = mk_box(p, x, y, s, s);
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
    int cx = s / 2, cy = s * 82 / 100, w = s >= 30 ? 4 : 2;
    static const int R[3] = {78, 54, 30};
    for (int i = 0; i < 3; i++) {
        int r = s * R[i] / 100;
        lv_obj_t *a = lv_arc_create(box);
        lv_obj_remove_style_all(a);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(a, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_pos(a, cx - r, cy - r);
        lv_obj_set_size(a, 2 * r, 2 * r);
        lv_arc_set_bg_angles(a, 225, 315);
        lv_obj_set_style_arc_width(a, w, 0);
        lv_obj_set_style_arc_rounded(a, true, 0);
        lv_obj_set_style_arc_opa(a, LV_OPA_COVER, 0);
    }
    int d = w + 2;
    mk_fill(box, cx - d / 2, cy - d / 2, d, d, LV_RADIUS_CIRCLE);
    lv_point_precise_t *pt = lv_malloc(2 * sizeof(lv_point_precise_t));
    pt[0].x = 1; pt[0].y = 1; pt[1].x = s - 1; pt[1].y = s - 1;
    lv_obj_t *ln = lv_line_create(box);
    lv_line_set_points(ln, pt, 2);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_obj_set_style_line_color(ln, lv_color_hex(0xFF4D4F), 0);
    return box;
}

static void wifi_icon_color(lv_obj_t *box, lv_color_t c, bool slash)
{
    for (int i = 0; i < 3; i++) lv_obj_set_style_arc_color(lv_obj_get_child(box, i), c, 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(box, 3), c, 0);
    show(lv_obj_get_child(box, 4), slash);
}

static lv_obj_t *mk_lock(lv_obj_t *p, int x, int y, lv_color_t c)
{
    lv_obj_t *b = mk_box(p, x, y, 12, 14);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *a = lv_arc_create(b);
    lv_obj_remove_style_all(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(a, 2, 0);
    lv_obj_set_size(a, 8, 10);
    lv_arc_set_bg_angles(a, 180, 360);
    lv_obj_set_style_arc_width(a, 2, 0);
    lv_obj_set_style_arc_opa(a, LV_OPA_COVER, 0);
    lv_obj_set_style_arc_color(a, c, 0);
    lv_obj_t *body = mk_fill(b, 0, 5, 12, 9, 2);
    lv_obj_set_style_bg_color(body, c, 0);
    return b;
}

static lv_color_t wx_color(int k)
{
    switch (k) {
    case UI_WX_SUN: return lv_color_hex(C_YEL);
    case UI_WX_RAIN: return lv_color_hex(0x38BDF8);
    case UI_WX_STORM: return lv_color_hex(0xA78BFA);
    default: return lv_color_hex(0xCBD5E1);
    }
}

static void sb_wifi_cb(lv_event_t *e) { (void)e; ui_open_wifi(); }

static void build_standby(void)
{
    lv_obj_t *p = page[SCR_STANDBY] = mk_box(scr, 0, 0, UI_W, UI_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    sb_wx_dot = mk_fill(p, PAD, (TOP_H - 10) / 2, 10, 10, LV_RADIUS_CIRCLE);
    sb_wx = mk_label_w(p, &ui_font_txt14, PAD, (TOP_H - ui_font_txt14.line_height) / 2, 300, LV_TEXT_ALIGN_LEFT);
    sb_chip = mk_fill(p, 0, (TOP_H - 18) / 2, 40, 18, 5);
    sb_chip_l = mk_label(sb_chip, &ui_font_cap12, 6, (18 - ui_font_cap12.line_height) / 2, "waze");
    sb_bt = mk_label(p, &lv_font_montserrat_14, 0, (TOP_H - lv_font_montserrat_14.line_height) / 2, LV_SYMBOL_BLUETOOTH);
    sb_wifi_hit = mk_box(p, 0, 0, 40, TOP_H);                       /* vung cham rong hon bieu tuong 18 px */
    lv_obj_add_flag(sb_wifi_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sb_wifi_hit, sb_wifi_cb, LV_EVENT_CLICKED, NULL);
    sb_wifi = mk_wifi_icon(sb_wifi_hit, 11, (TOP_H - 18) / 2, 18);

    const lv_font_t *df = &ui_font_clock120;
    int by = UI_H - 14 - SB_BAR_H;
    int grp = CAP_H(120) + 18 + ui_font_txt20.line_height, top = TOP_H + (by - TOP_H - grp) / 2;
    int w2 = text_w("00", df) + 4, wc = text_w(":", df) + 4, dx = (UI_W - 2 * w2 - wc) / 2;
    int dy = top + CAP_H(120) - baseline(df);
    sg_w[0] = w2; sg_w[1] = wc; sg_w[2] = w2;
    sg_off[0] = 0; sg_off[1] = w2; sg_off[2] = w2 + wc;
    sg_tw = 2 * w2 + wc;
    for (int i = 0; i < 3; i++) gtext_init(&sg[i], p, dx + sg_off[i], dy, sg_w[i], df->line_height);
    sb_date = mk_label_w(p, &ui_font_txt20, 0, top + CAP_H(120) + 18, UI_W, LV_TEXT_ALIGN_CENTER);

    int bw = UI_W - 2 * PAD;
    sb_bar = mk_card(p, PAD, by, bw, SB_BAR_H, 12);
    card_style(sb_bar, true);
    lv_obj_add_flag(sb_bar, LV_OBJ_FLAG_CLICKABLE);                  /* cham dai: mo trang Wi-Fi */
    lv_obj_add_event_cb(sb_bar, sb_wifi_cb, LV_EVENT_CLICKED, NULL);
    sb_bar_wifi = mk_wifi_icon(sb_bar, 14, (SB_BAR_H - 2 - 20) / 2, 20);
    sb_bar_bt = mk_label(sb_bar, &lv_font_montserrat_14, 18, (SB_BAR_H - 2 - lv_font_montserrat_14.line_height) / 2,
                         LV_SYMBOL_BLUETOOTH);
    sb_pill = mk_card(sb_bar, 0, (SB_BAR_H - 2 - 28) / 2, 120, 28, 14);
    lv_obj_add_flag(sb_pill, LV_OBJ_FLAG_EVENT_BUBBLE);
    sb_pill_dot = mk_fill(sb_pill, 10, (26 - 7) / 2, 7, 7, LV_RADIUS_CIRCLE);
    sb_pill_l = mk_label(sb_pill, &ui_font_sm12, 23, (26 - ui_font_sm12.line_height) / 2, "");
    sb_txt = mk_label_w(sb_bar, &ui_font_txt16, 46, (SB_BAR_H - 2 - ui_font_txt16.line_height) / 2, 200, LV_TEXT_ALIGN_LEFT);
}

static void colon_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_t *c = sg[1].cv;
    if (screen != SCR_STANDBY || phone || clk_h < 0) lv_obj_set_style_image_opa(c, LV_OPA_COVER, 0);
    else lv_obj_set_style_image_opa(c, lv_obj_get_style_image_opa(c, 0) ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
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
    {
        const lv_font_t *df = &ui_font_clock120;
        const char *pc[3] = {hh, ":", mm};
        const lv_text_align_t al[3] = {LV_TEXT_ALIGN_RIGHT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_LEFT};
        int ct = baseline(df) - CAP_H(120);
        for (int i = 0; i < 3; i++)
            gtext_set(&sg[i], pc[i], df, 0, al[i], sg_w[i], th->clk[0], th->clk[1], -sg_off[i], sg_tw, ct, CAP_H(120));
    }
    if (clk_h < 0) snprintf(b, sizeof b, "Chưa có giờ");
    else if (day > 0 && month > 0 && wday >= 0) snprintf(b, sizeof b, "%s, %02d/%02d", WDAY_NAME[wday % 7], day, month);
    else snprintf(b, sizeof b, "Chưa có ngày");
    lv_label_set_text(sb_date, b);
    lv_obj_set_style_text_color(sb_date, th->fg, 0);

    /* thanh tren: thoi tiet | Wi-Fi, Bluetooth, o nguon */
    show(sb_wx_dot, wx_kind != UI_WX_NONE);
    lv_obj_set_style_bg_color(sb_wx_dot, wx_color(wx_kind), 0);
    lv_obj_set_x(sb_wx, wx_kind != UI_WX_NONE ? PAD + 16 : PAD);
    lv_label_set_text(sb_wx, wx_kind != UI_WX_NONE ? wx_txt : "Chưa có thời tiết");
    lv_obj_set_style_text_color(sb_wx, th->fg, 0);
    bool g = src == UI_SRC_GOFA;
    const char *sn = g ? "GOFA" : "waze";
    lv_label_set_text(sb_chip_l, sn);
    lv_obj_set_style_text_color(sb_chip_l, g ? lv_color_white() : lv_color_hex(0x05263A), 0);
    lv_obj_set_style_bg_color(sb_chip, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
    int cw = text_w(sn, &ui_font_cap12) + 12, cx = UI_W - PAD - cw;
    lv_obj_set_width(sb_chip, cw);
    lv_obj_set_x(sb_chip, cx);
    int btx = cx - 10 - text_w(LV_SYMBOL_BLUETOOTH, &lv_font_montserrat_14);
    lv_obj_set_x(sb_bt, btx);
    lv_obj_set_style_text_color(sb_bt, phone ? lv_color_hex(0x33CCFF) : th->fg, 0);
    lv_obj_set_x(sb_wifi_hit, btx - 8 - 29);
    bool wok = wifi_st == UI_WIFI_CONNECTED, wcon = wifi_st == UI_WIFI_CONNECTING;
    wifi_icon_color(sb_wifi, wok ? lv_color_hex(0x22C55E) : wcon ? lv_color_hex(C_YEL) : th->fg, !wok && !wcon);
    lv_obj_set_width(sb_wx, btx - 8 - 29 - 8 - lv_obj_get_x(sb_wx));

    /* dai duoi */
    int bw = lv_obj_get_width(sb_bar) - 2;
    bool none = clk_h < 0;
    show(sb_bar_wifi, none);
    show(sb_bar_bt, !none);
    show(sb_pill_dot, !none);
    const char *pl, *tx;
    if (none) {
        lv_obj_set_style_bg_color(sb_bar, lv_color_hex(night ? 0x2A1E06 : 0xFEF9C3), 0);
        lv_obj_set_style_border_color(sb_bar, lv_color_hex(night ? 0x7A5A12 : 0xCA8A04), 0);
        wifi_icon_color(sb_bar_wifi, lv_color_hex(night ? C_YEL : 0xA16207), false);
        pl = "Wi-Fi ›";
        tx = wcon ? "Đang tự kết nối Wi-Fi đã lưu…" : wok ? "Đã kết nối Wi-Fi · đang lấy giờ…"
                  : "Kết nối Wi-Fi hoặc điện thoại để lấy giờ";
        lv_obj_set_style_bg_color(sb_pill, lv_color_hex(C_YEL), 0);
        lv_obj_set_style_border_color(sb_pill, lv_color_hex(C_YEL), 0);
        lv_obj_set_style_text_color(sb_pill_l, lv_color_hex(0x1A1606), 0);
        lv_obj_set_style_text_font(sb_pill_l, &ui_font_cap12, 0);
        lv_obj_set_x(sb_pill_l, 12);
    } else {
        lv_obj_set_style_bg_color(sb_bar, th->card, 0);
        lv_obj_set_style_border_color(sb_bar, th->hi, 0);
        lv_obj_set_style_text_color(sb_bar_bt, g ? lv_color_hex(0x0B8F4D) : lv_color_hex(0x33CCFF), 0);
        pl = tsrc == UI_TSRC_NTP ? "Giờ: Wi-Fi" : tsrc == UI_TSRC_RTC ? "Giờ: RTC" : "Giờ: điện thoại";
        tx = phone ? "Đã kết nối · chờ dữ liệu dẫn đường"
                   : g ? "Chờ kết nối GOFA qua điện thoại…" : "Chờ kết nối Waze qua điện thoại…";
        lv_obj_set_style_bg_color(sb_pill, lv_color_hex(night ? 0x4C3841 : 0xF6DFDE), 0);
        lv_obj_set_style_border_color(sb_pill, th->hi, 0);
        hgrad(sb_pill_dot, lv_color_hex(th->acc[0]), lv_color_hex(th->acc[1]));
        lv_obj_set_style_text_color(sb_pill_l, th->fg, 0);
        lv_obj_set_style_text_font(sb_pill_l, &ui_font_sm12, 0);
        lv_obj_set_x(sb_pill_l, 23);
    }
    lv_label_set_text(sb_pill_l, pl);
    int pw = lv_obj_get_x(sb_pill_l) + text_w(pl, lv_obj_get_style_text_font(sb_pill_l, 0)) + 12;
    lv_obj_set_width(sb_pill, pw);
    lv_obj_set_x(sb_pill, bw - 14 - pw);
    lv_label_set_text(sb_txt, tx);
    lv_obj_set_width(sb_txt, bw - 14 - pw - 10 - 46);
    lv_obj_set_style_text_color(sb_txt, th->fg, 0);
}

void ui_set_time_source(int s)
{
    tsrc = s;
    render_standby();
}

void ui_set_weather(int kind, const char *text)
{
    wx_kind = text && *text ? kind : UI_WX_NONE;
    snprintf(wx_txt, sizeof wx_txt, "%s", text ? text : "");
    render_standby();
}

#include "dashboard_wifi.inc"

/* ======================= man khoi dong / ket noi ======================= */
/* Theo bo man Gradient "Khoi dong": nen #1E2235 (dem) / #ECEDF1 (ngay), bieu tuong logo that 96 px (ui_logo_icon.c sinh tu
 * tools/logo), chu "EVCar" + "HUD" to mau chuyen cam -> do.
 *  - Khoi dong: logo, thanh tien trinh mau chuyen trong ranh, "Dang khoi dong..."
 *  - Ket noi Waze / GOFA: logo, "KET NOI VOI", o nguon + ten nguon, dong trang thai duoi.
 * Man khoi dong hien truoc khi biet che do Ngay/Dem: dung ban dem; man "Da ket noi" theo che do dang chay. */
extern const lv_image_dsc_t ui_logo_icon;
#define SP_ICON   96
#define SP_LOGO_Y 46
#define SP_WORD_Y (SP_LOGO_Y + SP_ICON + 10)
#define SP_ROW_Y  (SP_WORD_Y + 52)

static bool sp_night(void) { return scr ? night : boot_night; }            /* chua co bang dieu khien: ban dem */
static lv_color_t sp_fg(void) { return sp_night() ? lv_color_white() : lv_color_hex(0x14112B); }
static const uint32_t *sp_acc(void) { static const uint32_t n[2] = {0xFFA23A, 0xFF4560}, d[2] = {0xFF3D3D, 0xFFA23A}; return sp_night() ? n : d; }

static void sp_buf_free_cb(lv_event_t *e) { lv_draw_buf_destroy((lv_draw_buf_t *)lv_event_get_user_data(e)); }

static lv_obj_t *mk_splash_base(lv_obj_t *s)
{
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, lv_color_hex(sp_night() ? 0x1E2235 : 0xECEDF1), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_NONE, 0);
    lv_obj_t *img = lv_image_create(s);
    lv_image_set_src(img, &ui_logo_icon);
    lv_obj_set_pos(img, (UI_W - SP_ICON) / 2, SP_LOGO_Y);
    /* chu EVCar (mau chu) + HUD (mau chuyen) */
    const lv_font_t *f = &ui_font_logo34;
    int ls = -1, w1 = lv_text_get_width("EVCar", 5, f, ls), w2 = lv_text_get_width("HUD", 3, f, ls) + 2;
    int x0 = (UI_W - w1 - w2) / 2;
    lv_obj_t *l = mk_label(s, f, x0, SP_WORD_Y, "EVCar");
    lv_obj_set_style_text_letter_space(l, ls, 0);
    lv_obj_set_style_text_color(l, sp_fg(), 0);
    gtext_t g;
    gtext_init(&g, s, x0 + w1 + ls, SP_WORD_Y, w2, f->line_height);
    lv_obj_add_event_cb(g.cv, sp_buf_free_cb, LV_EVENT_DELETE, g.db);
    const uint32_t *c = sp_acc();
    int ct = baseline(f) - CAP_H(34);                              /* dinh chu hoa */
    gtext_set(&g, "HUD", f, ls, LV_TEXT_ALIGN_LEFT, w2, c[0], c[1], 0, w2, ct, CAP_H(34));
    return img;
}

/* "KET NOI VOI" + o nguon 44 px + "Waze Mod" / "GOFA" (mot khoi de an/hien cung luc) */
static lv_obj_t *mk_connect_row(lv_obj_t *s, int source)
{
    lv_obj_t *box = mk_box(s, 0, SP_ROW_Y, UI_W, 70);
    lv_obj_t *cap = mk_label_w(box, &ui_font_cap12, 0, 0, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(cap, "KẾT NỐI VỚI");
    lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, sp_fg(), 0);
    bool g = source == UI_SRC_GOFA;
    const char *nm = g ? "GOFA" : "Waze Mod";
    int tw = 44 + 12 + text_w(nm, &ui_font_unit22), x0 = (UI_W - tw) / 2;
    lv_obj_t *tile = ui_partner_tile(box, source, 44);
    lv_obj_set_pos(tile, x0, 24);
    lv_obj_t *n = mk_label(box, &ui_font_unit22, x0 + 56, 24 + (44 - ui_font_unit22.line_height) / 2, nm);
    lv_obj_set_style_text_color(n, sp_fg(), 0);
    return box;
}

/* dong duoi cung: cham mau chuyen + chu, can giua */
static void mk_status_line(lv_obj_t *s, bool dot, lv_color_t col, const char *txt)
{
    const lv_font_t *f = &ui_font_sm12;
    int w = text_w(txt, f) + (dot ? 13 : 0), x0 = (UI_W - w) / 2, y = UI_H - 12 - f->line_height;
    if (dot) {
        lv_obj_t *d = mk_fill(s, x0, y + (f->line_height - 7) / 2, 7, 7, LV_RADIUS_CIRCLE);
        const uint32_t *c = sp_acc();
        hgrad(d, lv_color_hex(c[0]), lv_color_hex(c[1]));
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
    lv_obj_set_style_bg_color(bar, lv_color_hex(sp_night() ? 0x161A2A : 0x23253A), 0);
    sp_fill = mk_fill(bar, 0, 0, 6, 6, 3);
    const uint32_t *c = sp_acc();
    hgrad(sp_fill, lv_color_hex(c[0]), lv_color_hex(c[1]));
    lv_obj_t *l = mk_label_w(sp_boot, &ui_font_sm12, 0, 14, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(l, "Đang khởi động…");
    lv_obj_set_style_text_color(l, sp_fg(), 0);
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
    mk_status_line(s, false, sp_fg(), b);
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
    mk_status_line(conn_ov, true, lv_color_hex(sp_night() ? 0xFF8A70 : 0xC2410C), "Đã kết nối điện thoại");
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

    theme_styles();
    build_top();
    build_cruise();
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
bool ui_is_night(void) { return night; }

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
/* Dan duong: Waze (HLP) uu tien; khong co lenh re Waze thi tram sac dich (app Tram sac, che do D) dung man nay. */
static int wz_turn, wz_dist, wz_exit, wz_turn2;
static char wz_street[64];
static bool ts_dest_on(void);
static int ts_turn(void);
static void ts_title(char *b, size_t n);

static void apply_nav(void)
{
    int turn, dist, exit_n = 0, turn2 = 0;
    char st[96];
    nav_is_ts = false;
    if (wz_on && !(wz_hold && ts_dest_on())) {                    /* Waze dang dan (giu tam thi nhuong tram sac) */
        turn = wz_turn; dist = wz_dist; exit_n = wz_exit; turn2 = wz_turn2;
        snprintf(st, sizeof st, "%s", wz_street);
    } else if (ts_dest_on()) {
        turn = ts_turn(); dist = ts_dist;
        ts_title(st, sizeof st);
        nav_is_ts = true;
    } else {
        nav_on = false;
        snprintf(street_now, sizeof street_now, "%s", wz_street);
        update_screen();
        render_all();
        return;
    }
    nav_on = true;
    if (turn != turn_now || strcmp(st, street_nav) != 0 || dist > turn_start) {
        turn_start = dist;
        turn_beeped = false;
    }
    if (!turn_beeped && dist >= 0 && dist <= 100 && turn_start > 150) {   /* con 100 m toi cho re: tit */
        turn_beeped = true;
        beep(1);
    }
    turn_now = turn;
    dist_now = dist;
    exit_now = exit_n;
    turn2_now = exit_n > 0 ? 0 : turn2;
    snprintf(street_nav, sizeof street_nav, "%s", st);
    update_screen();
    render_all();
}

/* Giu dan duong khi dien thoai tam ngung gui (tat man hinh, Waze chay nen, mat Bluetooth): giu lenh re cuoi 2 phut.
 * Toi noi (lenh re 17 "den noi" con <= 30 m, hoac het dan duong ngay sau lenh den noi): bang "Ban da den noi" nam yen
 * cho toi khi nguoi dung cham "Ket thuc" (hoac Waze bat dau lo trinh moi). */
#define NAV_HOLD_MS 120000                                       /* mat tin hieu dien thoai */
#define NAV_GLITCH_MS 3000                                       /* Waze bao het dan duong khi van noi */
#define ARRIVE_M 30
static uint32_t wz_lost_tick, wz_hold_ms;
static lv_obj_t *arr_box, *arr_sub;
static lv_timer_t *nav_tmr;

static void arrive_close(void)
{
    if (arr_box) { lv_obj_delete(arr_box); arr_box = NULL; }
}

static void arrive_end_cb(lv_event_t *e)
{
    (void)e;
    arrive_close();
    wz_on = wz_hold = false;
    if (ts_dest_on()) {                                           /* nho tram da ket thuc: app gui lai thi khong dan nua */
        snprintf(ts_done, sizeof ts_done, "%s", ts_name);
        ts_name[0] = 0;
    }
    apply_nav();
}

static void arrive_show(const char *title, const char *sub)
{
    if (arr_box) { lv_label_set_text(arr_sub, sub); return; }
    beep(1);
    arr_box = mk_fill(lv_layer_top(), 0, 0, UI_W, UI_H, 0);
    lv_obj_set_style_bg_color(arr_box, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(arr_box, LV_OPA_90, 0);
    lv_obj_add_flag(arr_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *im = mk_image(arr_box, (UI_W - 72) / 2, 44, 72, 64);
    lv_image_set_src(im, &arrow_arrive);
    lv_obj_set_style_image_recolor(im, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_t *t = mk_label_w(arr_box, &ui_font_txt20, 0, 132, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    arr_sub = mk_label_w(arr_box, &ui_font_txt16, 16, 164, UI_W - 32, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(arr_sub, sub);
    lv_obj_set_style_text_color(arr_sub, lv_color_hex(0x86EFAC), 0);
    lv_obj_t *b = mk_fill(arr_box, (UI_W - 240) / 2, 220, 240, 52, 26);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x16A34A), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x22C55E), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 12);
    lv_obj_add_event_cb(b, arrive_end_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = mk_label(b, &ui_font_txt16, 0, 0, "Kết thúc dẫn đường");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);
}

static const char *arrive_side(char side)
{
    return side == 'R' ? "Điểm đến ở bên tay phải" : side == 'L' ? "Điểm đến ở bên tay trái"
         : side == 'B' ? "Điểm đến ở phía sau" : "Điểm đến ở ngay phía trước";
}

static void nav_hold_cb(lv_timer_t *t)
{
    (void)t;
    if (wz_hold && !sig_ok) wz_hold_ms = NAV_HOLD_MS;             /* mat han tin hieu: giu 2 phut */
    if (wz_hold && !arr_box && lv_tick_elaps(wz_lost_tick) > wz_hold_ms) {   /* het thoi gian giu: bo */
        wz_on = wz_hold = false;
        apply_nav();
    }
}

void ui_set_nav(bool active, int turn, int dist_m, int exit_n, const char *street, int turn2)
{
    if (!nav_tmr) nav_tmr = lv_timer_create(nav_hold_cb, 1000, NULL);
    bool a = active && arrow_for_turn(turn);
    if (a) {
        if (arr_box && turn != 17) arrive_close();                /* lo trinh moi */
        wz_on = true;
        wz_hold = false;
        wz_turn = turn; wz_dist = dist_m; wz_exit = exit_n; wz_turn2 = turn2;
        snprintf(wz_street, sizeof wz_street, "%s", street ? street : "");
        if (turn == 17 && dist_m >= 0 && dist_m <= ARRIVE_M) arrive_show("Bạn đã đến nơi", arrive_side('A'));
    } else {
        if (street && street[0]) snprintf(wz_street, sizeof wz_street, "%s", street);
        if (wz_on && !wz_hold) {
            if (wz_turn == 17 && wz_dist <= 300) {                /* het dan duong ngay khi sap toi: da toi noi */
                wz_dist = 0;
                arrive_show("Bạn đã đến nơi", arrive_side('A'));
            }
            wz_hold = true;                                       /* giu man dan duong, cho dien thoai gui lai */
            wz_lost_tick = lv_tick_get();
            /* van co tin hieu ma Waze bao het dan duong (huy lo trinh / chop tat): chi giu 3 s; mat tin hieu: 2 phut */
            wz_hold_ms = sig_ok ? NAV_GLITCH_MS : NAV_HOLD_MS;
        } else if (wz_hold && sig_ok && wz_hold_ms == NAV_HOLD_MS) {   /* dien thoai gui lai, xac nhan het dan duong */
            wz_hold_ms = NAV_GLITCH_MS;
            wz_lost_tick = lv_tick_get();
        }
        if (!wz_on) snprintf(wz_street, sizeof wz_street, "%s", street ? street : "");
    }
    apply_nav();
}

void ui_set_route(const char *eta, int remaining_m, int remaining_min)
{
    has_route = (eta && eta[0]) || remaining_m > 0;
    snprintf(eta_txt, sizeof eta_txt, "%s", eta ? eta : "");
    eta_rm = remaining_m;
    eta_rmin = remaining_min;
    if (screen == SCR_NAV) render_all();
}

/* Nho cac canh bao da tit: cung loai, khoang cach khong tang qua 200 m, trong 2 phut = cung mot canh bao.
 * Tranh tit lai khi danh sach Waze dao thu tu / mat 1 nhip / hai canh bao xen nhau. */
static struct { int code, dist; uint32_t tick; } al_seen[8];
static bool alert_seen(int code, int dist)
{
    int old = 0;
    for (int i = 0; i < 8; i++) {
        if (al_seen[i].code == code && lv_tick_elaps(al_seen[i].tick) < 120000 &&
            (dist < 0 || al_seen[i].dist < 0 || dist <= al_seen[i].dist + 200)) {
            al_seen[i].dist = dist;
            al_seen[i].tick = lv_tick_get();
            return true;
        }
        if (al_seen[i].tick < al_seen[old].tick) old = i;
    }
    al_seen[old].code = code;
    al_seen[old].dist = dist;
    al_seen[old].tick = lv_tick_get() | 1;
    return false;
}

void ui_set_alerts(const ui_alert_t *alerts, int n)
{
    al_n = 0;
    for (int i = 0; alerts && i < n && al_n < (int)(sizeof al_list / sizeof al_list[0]); i++) {
        bool dup = false;                                         /* cung loai, cach nhau < 150 m: trung */
        for (int k = 0; k < al_n; k++)
            if (al_list[k].code == alerts[i].code && abs(al_list[k].dist - alerts[i].dist) < 150) dup = true;
        if (!dup) al_list[al_n++] = alerts[i];
    }
    ev_now = sign_now = NULL;
    for (int i = 0; i < al_n; i++) {
        if (is_road_sign(al_list[i].code)) {
            if (!sign_now) sign_now = &al_list[i];
        } else if (!ev_now) {
            ev_now = &al_list[i];
        }
    }
    int code = ev_now ? ev_now->code : 0;
    if (code && !alert_seen(code, ev_now->dist)) {                /* canh bao MOI that su: tit + chop 8 giay */
        beep(urgent_alert(code) ? 2 : 1);
        ev_new_tick = lv_tick_get() | 1;
    }
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

static void ts_cb(lv_timer_t *t)
{
    (void)t;
    if (ts_name[0] && !station_on()) {                            /* 90 giay khong co ban tin: an */
        bool dest = ts_mode == 'D';
        ts_name[0] = 0;
        if (dest) apply_nav();
        else if (screen == SCR_CRUISE) render_cruise();
    }
}

void ui_set_station(const char *name, int dist_m, char side, int angle, int kw, int ports, int count, char mode)
{
    (void)count;
    bool was_dest = ts_dest_on();
    if (!name || !name[0]) ts_done[0] = 0;                        /* "T1|": quen tram da ket thuc */
    else if (mode == 'D' && !strcmp(name, ts_done)) mode = 'N';   /* da toi + bam Ket thuc: chi con dai tram gan */
    snprintf(ts_name, sizeof ts_name, "%s", name ? name : "");
    ts_dist = dist_m;
    ts_side = side;
    ts_ang = angle;
    ts_kw = kw;
    ts_ports = ports;
    ts_mode = mode == 'D' ? 'D' : 'N';
    ts_tick = lv_tick_get();
    if (!ts_tmr) ts_tmr = lv_timer_create(ts_cb, 2000, NULL);
    if (ts_dest_on() && dist_m >= 0 && dist_m <= ARRIVE_M && (!wz_on || wz_hold)) {
        char t[96];
        snprintf(t, sizeof t, "Đã tới %s", ts_name);
        arrive_show(t, arrive_side(side));
    }
    if (was_dest || ts_dest_on()) apply_nav();
    else if (screen == SCR_CRUISE) render_cruise();
}
