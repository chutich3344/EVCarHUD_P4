/* Canh bao vat can - xem obstacle_ui.h. */
#include "obstacle_ui.h"

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "dashboard.h"
#include "detect.h"
#include "ui_fonts.h"
#include "adas_feed.h"

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
static uint32_t s_beep_tick, s_off_tick, s_end_tick, s_set_tick;
#define STALE_MS 1500                      /* khong con ai cap nhat (camera rut / dung): tu tat canh bao */
static bool s_city;
static bool s_xa_box;                        /* o canh bao dang ve theo xADAS */
/* xADAS: khung moi nhat + luc nhan; muc canh bao rieng, ghep voi camera trong paint() (lay muc cao hon) */
static xadas_frame_t s_xf;
static bool s_xa_on, s_xa_live, s_xa_city;   /* da tung nhan khung; con khung trong 3 s; xe < 25 km/h */
static int s_xa_level;
static bool s_cam;                          /* dang cam camera USB: camera uu tien, bo xADAS (chip CAM thay chip xADAS) */
static int s_cam_m, s_cam_cls;              /* xe truoc camera dang theo (m, 0 = chua thay) - hien o o khoang cach ca khi an toan */
static uint32_t s_xa_tick, s_xa_lost_tick;
#define XA_LOST_HIDE_MS 60000                /* mat tin hieu qua 1 phut: an chip (nguoi dung da tat xADAS) */
/* O xADAS bo cuc 2 cot (chot 16/11): nam duoi the bien, chi so met to (chip tren cung da doi mau nen bo chu "xADAS");
 * gan / qua gan them dong "TOI THIEU N m"; mat tin hieu: "--". */
static lv_obj_t *s_xc, *s_xc_lab, *s_xc_num, *s_xc_unit;
static char s_xc_sig[96];                    /* lan ve truoc: khong doi thi khong ve lai */
static bool s_xc_on;
/* 17/11: app EVCarHUD Lan (dien thoai, mo hinh openpilot) di chung duong xADAS (phone_adas.c): chip "LAN" thay chu xADAS,
 * vach do chop o mep man khi lan lan, PHANH toan man khi app bao muc 3 (TTC < 1,5 s). */
static bool s_ph, s_ph_next, s_ph_lanes, s_ph_crit_next;
static char s_ph_ldw = '-';
static uint32_t s_ph_ldw_tick;
static lv_obj_t *s_ldw_l, *s_ldw_r;
#define LDW_HOLD_MS 1200

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
    case DET_CLS_TWO: return "Xe 2 bánh phía trước";
    case DET_CLS_BIG: return "Xe lớn phía trước";
    default: return "Xe phía trước";
    }
}

static const char *short_for(int cls)
{
    switch (cls) {
    case DET_CLS_PERSON: return "Người đi bộ";
    case DET_CLS_TWO: return "Xe 2 bánh";
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
    if (show && !slot) ui_sign_slot(&a, &lim, false);         /* khong co cho: tra bien lai */
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
    int nx = ui_speed_right() + 2;                           /* khong de len so toc do: co o lai cho vua khoang trong */
    if (nx > x) {
        x = nx;
        if (x + bw > UI_W - 14) bw = UI_W - 14 - x;
    }
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
    unsigned xmin = s_xf.min;
    if (s_xa_box && xmin != XA_NONE16)                       /* xADAS: muc toi thieu dung so tren app */
        snprintf(sub, sizeof sub, "Tối thiểu %u %s", xmin, s_xf.imperial ? "ft" : "m");
    else if (s_xa_box) snprintf(sub, sizeof sub, "%s", s_xf.level == XA_LV_CLOSE ? "Quá gần" : "Gần");
    else if (bw < 170) snprintf(sub, sizeof sub, "%s", t[0] ? t : short_for(s_cls));
    else if (t[0]) snprintf(sub, sizeof sub, "Va chạm sau %s", t);
    else if (adas_feed_legal_m(ui_get_speed()) > 0 && s_cls != DET_CLS_PERSON)   /* cao toc: nhac khoang cach theo luat */
        snprintf(sub, sizeof sub, "Luật: tối thiểu %d m", adas_feed_legal_m(ui_get_speed()));
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

static int eff_level(void) { return s_xa_level > s_level ? s_xa_level : s_level; }

static lv_obj_t *s_tl;
static bool s_tl_cell;                       /* den dang de len o duoi bien (an o xADAS) */
static bool tl_shown(void) { return s_tl && s_tl_cell && !lv_obj_has_flag(s_tl, LV_OBJ_FLAG_HIDDEN); }

static bool xa_card_paint(void)
{
    lv_area_t a;
    if (!(s_xa_on || s_cam) || !ui_xa_slot(&a)) {
        if (s_xc_on) { vis(s_xc, false); s_xc_on = false; s_xc_sig[0] = 0; }
        return false;
    }
    bool night = ui_is_night();
    bool cam = s_level > s_xa_level && s_level < OBS_CRITICAL;   /* camera thay vat can gan hon: hien so cua camera */
    int kind;                                                    /* 0 an toan, 1 gan, 2 qua gan, 3 khong co so */
    char num[12], lab[40];
    const char *unit = "m";
    lab[0] = 0;
    if (s_cam) {                                                 /* 16/11: camera USB thay xADAS, cung khung */
        int lg = adas_feed_legal_m(ui_get_speed());
        if (!s_cam_m) {
            kind = 3;
            snprintf(num, sizeof num, "--");
            unit = "";
            snprintf(lab, sizeof lab, "CHƯA THẤY XE");
        } else {
            kind = s_level >= OBS_DANGER ? 2 : s_level == OBS_CAUTION ? 1 : 0;
            snprintf(num, sizeof num, "%d", s_cam_m % 1000);
            if (kind && lg > 0 && s_cam_cls != DET_CLS_PERSON) snprintf(lab, sizeof lab, "TỐI THIỂU %d m", lg);
            else if (kind) snprintf(lab, sizeof lab, "%s", s_cam_cls == DET_CLS_PERSON ? "NGƯỜI ĐI BỘ"
                                    : s_cam_cls == DET_CLS_TWO ? "XE 2 BÁNH" : s_cam_cls == DET_CLS_BIG ? "XE LỚN" : "XE PHÍA TRƯỚC");
        }
    } else if (cam) {
        kind = s_level >= OBS_DANGER ? 2 : 1;
        snprintf(num, sizeof num, "%d", s_dist);
        snprintf(lab, sizeof lab, "%s", short_for(s_cls));
    } else if (!s_xa_live || s_xf.state != XA_ST_MEASURE || s_xf.dist == XA_NONE16) {
        kind = 3;
        snprintf(num, sizeof num, "--");
        unit = "";
        /* 16/11: noi ro vi sao chua co so (app da noi nhung chua do / khong thay xe / mat tin hieu) */
        snprintf(lab, sizeof lab, "%s", !s_xa_live ? "MẤT TÍN HIỆU" : s_xf.state == XA_ST_MEASURE ? "CHƯA THẤY XE"
                 : s_xf.state == XA_ST_SLOW ? "XE DỪNG" : s_xf.state == XA_ST_HOT ? "MÁY NÓNG"
                 : s_xf.state == XA_ST_NOIMG ? "MẤT HÌNH" : "CHỜ ĐO");
    } else {
        kind = s_xa_level == OBS_DANGER ? 2 : s_xa_level == OBS_CAUTION ? 1 : 0;
        snprintf(num, sizeof num, "%u", (unsigned)(s_xf.dist % 10000));
        unit = s_xf.imperial ? "ft" : "m";
        if (kind && s_xf.min != XA_NONE16) snprintf(lab, sizeof lab, "TỐI THIỂU %u %s", (unsigned)s_xf.min, unit);
    }
    bool dim = kind == 2 && s_dim;
    char sig[96];
    snprintf(sig, sizeof sig, "%d,%d,%d,%d|%d%d%d|%s|%s|%s", (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2, kind, night, dim, num, unit, lab);
    s_xc_on = true;
    if (!strcmp(sig, s_xc_sig) && (!lv_obj_has_flag(s_xc, LV_OBJ_FLAG_HIDDEN) || tl_shown())) return true;
    snprintf(s_xc_sig, sizeof s_xc_sig, "%s", sig);

    static const uint32_t bgN[4] = {0x252A40, 0x3A300A, 0x4A0F16, 0x252A40}, bgD[4] = {0xF4F5F8, 0xFFF4C2, 0xFFE0E4, 0xF4F5F8};
    static const uint32_t bdN[4] = {0x343A58, 0xFFD400, 0xFF4D4F, 0x4A5068}, bdD[4] = {0xDADDE5, 0xB45309, 0xBE123C, 0xC4C8D2};
    static const uint32_t nmN[4] = {0x34D399, 0xFFD400, 0xFF6B6B, 0x6B7280}, nmD[4] = {0x047857, 0xB45309, 0xBE123C, 0x9CA3AF};
    int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    lv_obj_set_pos(s_xc, a.x1, a.y1);
    lv_obj_set_size(s_xc, w, h);
    lv_obj_set_style_bg_color(s_xc, lv_color_hex((night ? bgN : bgD)[kind]), 0);
    lv_obj_set_style_border_color(s_xc, lv_color_hex((night ? bdN : bdD)[kind]), 0);
    lv_obj_set_style_shadow_color(s_xc, night ? lv_color_black() : lv_color_hex(0x96A0B8), 0);
    lv_obj_set_style_shadow_opa(s_xc, night ? 128 : 140, 0);
    lv_obj_set_style_opa(s_xc, dim ? 140 : LV_OPA_COVER, 0);
    lv_color_t nc = lv_color_hex((night ? nmN : nmD)[kind]);
    lv_color_t lc = kind == 0 ? nc : kind == 3 ? lv_color_hex(night ? 0xA3ACB5 : 0x4B5563) : night ? lv_color_white() : lv_color_hex(0x14112B);

    bool compact = h < 110;                                      /* man dan duong: the thap */
    const lv_font_t *lf = compact ? &ui_font_txt14 : &ui_font_txt16;
    const lv_font_t *nf = &ui_font_d96;
    int px = lab[0] ? (compact ? 60 : 84) : (compact ? 74 : 100);
    int tw = lv_text_get_width(num, strlen(num), nf, -2);
    int uw = unit[0] ? lv_text_get_width(unit, strlen(unit), &ui_font_unit22, 0) : 0;
    int gap = unit[0] ? 4 : 0;
    int sc = px * 256 / 96;
    int room = w - 20 - uw - gap;
    if (tw * sc / 256 > room) sc = room * 256 / (tw ? tw : 1);
    int labH0 = lab[0] ? (int)lf->line_height + 2 : 0, hroom = h - 18 - labH0;   /* the thap: chu so khong tran khoi o */
    if ((96 * 727 / 1000) * sc / 256 > hroom) sc = hroom * 256 / (96 * 727 / 1000);
    int capS = (96 * 727 / 1000) * sc / 256, numW = tw * sc / 256;
    int labH = lab[0] ? (int)lf->line_height + 2 : 0;
    int top = h / 2 - (labH + capS) / 2;
    int gx0 = w / 2 - (numW + gap + uw) / 2;

    vis(s_xc_lab, lab[0] != 0);
    if (lab[0]) {
        lv_obj_set_style_text_font(s_xc_lab, lf, 0);
        set_txt(s_xc_lab, lab);
        lv_obj_set_style_text_color(s_xc_lab, lc, 0);
        lv_obj_set_width(s_xc_lab, w - 8);
        lv_obj_set_pos(s_xc_lab, 2, top);
    }
    set_txt(s_xc_num, num);
    lv_obj_set_style_text_color(s_xc_num, nc, 0);
    int bl = (int)nf->line_height - (int)nf->base_line;            /* dinh label -> chan chu */
    int pvx = tw / 2, pvy = bl - (96 * 727 / 1000) / 2;
    lv_obj_set_style_transform_pivot_x(s_xc_num, pvx, 0);
    lv_obj_set_style_transform_pivot_y(s_xc_num, pvy, 0);
    lv_obj_set_style_transform_scale(s_xc_num, sc, 0);
    lv_obj_set_pos(s_xc_num, gx0 + numW / 2 - pvx - 2, top + labH + capS / 2 - pvy - 2);   /* -2: vien o */
    vis(s_xc_unit, unit[0] != 0);
    if (unit[0]) {
        set_txt(s_xc_unit, unit);
        lv_obj_set_style_text_color(s_xc_unit, nc, 0);
        int ub = (int)ui_font_unit22.line_height - (int)ui_font_unit22.base_line;
        lv_obj_set_pos(s_xc_unit, gx0 + numW + gap - 2, top + labH + capS - ub - 2);
    }
    vis(s_xc, !tl_shown());
    return true;
}

static void paint(void)
{
    bool night = ui_is_night();
    int lvl = eff_level();
    bool xa = s_xa_level > s_level;                          /* o canh bao dang theo xADAS (muc cao hon camera) */
    bool mid = lvl >= OBS_CAUTION && lvl < OBS_CRITICAL;
    bool danger = lvl == OBS_DANGER;

    char d[16], t[16], sub[40];
    float ttc = xa ? 0.0f : s_ttc;
    if (xa) {
        s_cls = s_level == OBS_NONE ? DET_CLS_CAR : s_cls;
        snprintf(d, sizeof d, "%u %s", (unsigned)s_xf.dist, s_xf.imperial ? "ft" : "m");
    } else {
        snprintf(d, sizeof d, "%d m", s_dist);
    }
    s_xa_box = xa;
    if (ttc > 0.0f) {
        fmt_ttc(t, sizeof t, ttc);
        snprintf(sub, sizeof sub, "Va chạm sau %s", t);
    } else {
        snprintf(sub, sizeof sub, "Giữ khoảng cách");
        t[0] = 0;
    }
    if (xa && s_xf.min != XA_NONE16) snprintf(sub, sizeof sub, "Tối thiểu %u %s", (unsigned)s_xf.min, s_xf.imperial ? "ft" : "m");

    bool card = xa_card_paint();                             /* bo cuc 2 cot: so met nam o duoi the bien, khong can o de len bien */
    bool in_box = paint_box(mid && !card, danger, night, d, t) || card;
    bool show_strip = mid && !in_box;
    vis(s_strip, show_strip);
    vis(s_frame, danger);
    vis(s_crit, lvl == OBS_CRITICAL);

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
    if (lvl == OBS_CRITICAL) {
        lv_obj_set_style_bg_color(s_crit, lv_color_hex(s_dim ? 0x7A0A12 : 0xE11D2A), 0);
        lv_image_set_src(s_cicon, icon_for(s_cls, true));
        char cd[40];
        int cdm = xa ? (int)s_xf.dist : s_dist;
        if (t[0]) snprintf(cd, sizeof cd, "%d m · %s", cdm, t);
        else snprintf(cd, sizeof cd, "%d m", cdm);
        lv_label_set_text(s_cdist, cd);
        lv_obj_move_foreground(s_crit);
    }
}

static void xa_chip(void)
{
    char b[24];
    int c = 0;
    if (!s_xa_on) b[0] = 0;
    else if (s_ph) {                                         /* app dien thoai: chip LAN xanh / xam / do */
        snprintf(b, sizeof b, "LÀN");
        c = !s_xa_live ? UI_XA_GREY : s_ph_ldw != '-' ? UI_XA_RED : s_ph_lanes ? UI_XA_GREEN : UI_XA_GREY;
        ui_xadas_chip(b, c);
        return;
    }
    else xadas_chip_text(&s_xf, s_xa_live, b, sizeof b, &c);
    if (s_xa_on && s_xa_live && s_xf.state == XA_ST_MEASURE && s_xf.dist != XA_NONE16 && c >= 1 && c <= 4)
        snprintf(b, sizeof b, "%s", s_xf.test ? "xADAS THỬ" : "xADAS");   /* so met da o o duoi bien: chip chi doi mau */
    ui_xadas_chip(b, c);
}

static void xa_tick(void)
{
    if (!s_xa_on) return;
    if (s_xa_live && lv_tick_elaps(s_xa_tick) >= XA_STALE_MS) {   /* 3 s khong co khung: bo so cu, bao mat tin hieu */
        s_xa_live = false;
        s_xa_lost_tick = lv_tick_get();
        if (s_xa_level != OBS_NONE) { s_xa_level = OBS_NONE; paint(); }
        xa_chip();
    } else if (!s_xa_live && lv_tick_elaps(s_xa_lost_tick) >= XA_LOST_HIDE_MS) {
        s_xa_on = false;                                     /* tat xADAS lau roi: an chip */
        xa_chip();
    }
}

/* vach do doc o mep bi lan, chop 4 Hz, giu LDW_HOLD_MS sau goi lan lan cuoi */
static void ldw_paint(void)
{
    if (!s_ldw_l) return;
    bool on = s_ph && s_xa_live && s_ph_ldw != '-' && lv_tick_elaps(s_ph_ldw_tick) < LDW_HOLD_MS && !ui_settings_shown();
    if (!on && s_ph_ldw != '-' && lv_tick_elaps(s_ph_ldw_tick) >= LDW_HOLD_MS) { s_ph_ldw = '-'; xa_chip(); }
    bool blink = (lv_tick_get() / 125) % 2 == 0;
    vis(s_ldw_l, on && s_ph_ldw == 'L' && blink);
    vis(s_ldw_r, on && s_ph_ldw == 'R' && blink);
    if (on) { lv_obj_move_foreground(s_ldw_l); lv_obj_move_foreground(s_ldw_r); }
}

static bool tl_paint(void);

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    xa_tick();
    if (s_cam_m && lv_tick_elaps(s_set_tick) >= STALE_MS) s_cam_m = 0;   /* camera ngung nhan dien: bo so cu */
    xa_card_paint();                                         /* bam theo the bien khi doi man / doi co (chi ve lai khi doi) */
    tl_paint();                                              /* den giao thong (app LANE) de len o duoi bien */
    ldw_paint();
    if (eff_level() == OBS_NONE) return;
    if (s_level != OBS_NONE &&
        ((s_off_tick && lv_tick_elaps(s_off_tick) >= HOLD_MS) || lv_tick_elaps(s_set_tick) >= STALE_MS)) {
        s_set_tick = lv_tick_get();                          /* camera: het thoi gian giu / mat nguon cap nhat: tat han */
        obstacle_ui_clear();
        return;
    }
    if (++s_tick >= 250 / TICK_MS) {                         /* chop 2 Hz + nhac tieng */
        s_tick = 0;
        s_dim = !s_dim;
        int lvl = eff_level();
        bool city = s_xa_level > s_level ? s_xa_city : s_city;
        uint32_t every = lvl == OBS_CRITICAL ? 1000 : (lvl == OBS_DANGER && !city) ? 4000 : 0;
        if (every && lv_tick_elaps(s_beep_tick) >= every) {
            s_beep_tick = lv_tick_get();
            board_beep(lvl == OBS_CRITICAL ? 3 : 2);
        }
        paint();
        return;
    }
    if (eff_level() == OBS_CRITICAL || lv_obj_has_flag(s_box, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;                                             /* giua 2 lan chop: chi ve lai khi bien doi cho / doi man */
    int lim = 0;
    if (!ui_sign_slot(&a, &lim, true) || lim != s_last_lim || a.x1 != s_last_a.x1 || a.y1 != s_last_a.y1 || a.x2 != s_last_a.x2 ||
        a.y2 != s_last_a.y2)
        paint();
}

void obstacle_ui_init(void)
{
    if (s_strip) return;                                     /* main.c va usb_cam.c deu goi */
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

    s_xc = box(top);                                         /* o xADAS bo cuc 2 cot */
    lv_obj_set_style_radius(s_xc, 14, 0);
    lv_obj_set_style_bg_opa(s_xc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_xc, 2, 0);
    lv_obj_set_style_shadow_width(s_xc, 14, 0);
    lv_obj_set_style_shadow_offset_x(s_xc, 6, 0);
    lv_obj_set_style_shadow_offset_y(s_xc, 6, 0);
    s_xc_lab = label(s_xc, &ui_font_txt16);
    lv_obj_set_style_text_align(s_xc_lab, LV_TEXT_ALIGN_CENTER, 0);
    s_xc_num = label(s_xc, &ui_font_d96);
    lv_obj_set_style_text_letter_space(s_xc_num, -2, 0);
    s_xc_unit = label(s_xc, &ui_font_unit22);
    lv_obj_add_flag(s_xc, LV_OBJ_FLAG_HIDDEN);

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
    for (int i = 0; i < 2; i++) {                            /* vach lan lan (app Lan) */
        lv_obj_t *o = box(top);
        lv_obj_set_size(o, 16, UI_H);
        lv_obj_set_pos(o, i ? UI_W - 16 : 0, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(0xF0323C), 0);
        lv_obj_set_style_radius(o, 0, 0);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (i) s_ldw_r = o; else s_ldw_l = o;
    }
    s_tmr = lv_timer_create(tick_cb, TICK_MS, NULL);
}

int obstacle_ui_level(void) { return s_level; }

void obstacle_ui_touch(void) { s_set_tick = lv_tick_get(); }

void obstacle_ui_clear(void)
{
    if (!s_strip) return;
    if (s_level != OBS_NONE) s_end_tick = lv_tick_get();
    s_level = OBS_NONE;
    s_off_tick = 0;
    s_cam_m = 0;
    paint();
}

void obstacle_ui_set(int level, int cls, int dist_m, float ttc_s, bool city)
{
    s_city = city;
    if (!s_strip) return;
    s_set_tick = lv_tick_get();
    s_cam_m = dist_m > 0 ? dist_m : 0;                       /* o khoang cach: xe truoc ca khi chua canh bao */
    s_cam_cls = cls;
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

void obstacle_ui_cam(bool on)
{
    s_cam = on;
    s_cam_m = 0;
    if (!s_strip) return;
    if (!on) { xa_card_paint(); return; }                    /* rut camera: khung xADAS ke tiep tu hien lai */                             /* rut camera: khung xADAS ke tiep tu hien lai */
    if (s_xa_on || s_xa_level != OBS_NONE) {
        s_xa_on = s_xa_live = false;
        s_xa_level = OBS_NONE;
        xa_chip();
        paint();
    }
}

void obstacle_ui_xadas(const xadas_frame_t *f)
{
    bool from_phone = s_ph_next, crit = s_ph_crit_next;
    s_ph_next = s_ph_crit_next = false;
    if (!s_strip || s_cam) return;                           /* 16/11: cam camera USB thi bo qua xADAS hoan toan */
    if (f) s_ph = from_phone;                                /* khung moi quyet dinh nguon: app xADAS hay app Lan */
    int old = s_xa_level;
    if (!f) {                                                /* mat BLE: bao mat tin hieu ngay, khong giu so cu */
        if (s_xa_on && s_xa_live && s_xf.state != XA_ST_WAIT) {
            s_xa_live = false;
            s_xa_lost_tick = lv_tick_get();
        } else {
            s_xa_on = s_xa_live = false;                     /* xADAS da bao dung do (state 0) roi moi ngat: an chip */
        }
        s_xa_level = OBS_NONE;
    } else {
        s_xf = *f;
        s_xa_on = s_xa_live = true;
        s_xa_tick = lv_tick_get();
        int sp = f->speed != XA_NONE8 ? f->speed : ui_get_speed();
        if (f->imperial && f->speed != XA_NONE8) sp = sp * 16 / 10;
        s_xa_city = sp < 25;
        int lv = xadas_obs_level(f);
        s_xa_level = lv == 2 ? OBS_DANGER : lv == 1 ? OBS_CAUTION : OBS_NONE;
        if (crit && s_xa_level == OBS_DANGER) s_xa_level = OBS_CRITICAL;   /* app Lan: TTC < 1,5 s -> PHANH */
        if (ui_settings_shown()) s_xa_level = OBS_NONE;       /* dang mo cai dat: khong de len */
    }
    if (s_xa_level > old && s_xa_level > s_level && s_xa_level == OBS_DANGER && !s_xa_city) {
        bool again = s_end_tick && lv_tick_elaps(s_end_tick) < 5000;   /* vua het canh bao < 5 s: khong tit lai */
        if (!again) {
            s_beep_tick = lv_tick_get();
            board_beep(2);
        }
    }
    if (old != OBS_NONE && s_xa_level == OBS_NONE && s_level == OBS_NONE) s_end_tick = lv_tick_get();
    xa_chip();
    paint();
}

void obstacle_ui_phone(const xadas_frame_t *f, char ldw, bool lanes, bool crit)
{
    if (!s_strip || s_cam) return;
    if (ldw == 'L' || ldw == 'R') {
        if (s_ph_ldw != ldw || lv_tick_elaps(s_ph_ldw_tick) >= LDW_HOLD_MS) board_beep(1);   /* moi lan lan: tit 1 tieng */
        s_ph_ldw = ldw;
        s_ph_ldw_tick = lv_tick_get();
    }
    s_ph_lanes = lanes;
    s_ph_next = true;
    s_ph_crit_next = crit;
    obstacle_ui_xadas(f);
    ldw_paint();
}

/* ===================== Den giao thong (app LANE, goi T1) =====================
 * Kieu 1 (mac dinh): o duoi bien (cung khung xADAS / LAN) doi thanh: hop den doc 3 bong ben trai (bong dang sang phat sang),
 * ben phai nhan "DEN DO / DEN VANG / DEN XANH" + so giay to cung mau. Khong doc duoc so: chi nhan to.
 * Kieu 2: them - xe dung (< 3 km/h) thi den + so giay to de len cot so toc do (so "00" luc dung vo ich).
 * xADAS / camera bao gan, qua gan: o canh bao giu nguyen (an toan truoc). */
#define TL_STALE_MS 3000
static char s_tl_col;                        /* 'R' 'Y' 'G', 0 = khong co */
static int s_tl_sec = -1;
static uint32_t s_tl_tick;
static int s_tl_style = 1;
static lv_obj_t *s_tl_house, *s_tl_lamp[3], *s_tl_lab, *s_tl_num, *s_tl_unit;
static char s_tl_sig[96];

static void tl_create(void)
{
    if (s_tl) return;
    s_tl = box(lv_layer_top());
    lv_obj_set_style_radius(s_tl, 14, 0);
    lv_obj_set_style_bg_opa(s_tl, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_offset_x(s_tl, 6, 0);
    lv_obj_set_style_shadow_offset_y(s_tl, 6, 0);
    s_tl_house = box(s_tl);
    lv_obj_set_style_bg_opa(s_tl_house, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_tl_house, lv_color_hex(0x0E1020), 0);
    lv_obj_set_style_border_color(s_tl_house, lv_color_hex(0x3A4060), 0);
    lv_obj_set_style_border_width(s_tl_house, 1, 0);
    for (int i = 0; i < 3; i++) {
        s_tl_lamp[i] = box(s_tl_house);
        lv_obj_set_style_radius(s_tl_lamp[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_tl_lamp[i], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_spread(s_tl_lamp[i], 1, 0);
    }
    s_tl_lab = label(s_tl, &ui_font_txt16);
    s_tl_num = label(s_tl, &ui_font_d96);
    lv_obj_set_style_text_letter_space(s_tl_num, -2, 0);
    s_tl_unit = label(s_tl, &ui_font_unit22);
    lv_label_set_text(s_tl_unit, "s");
    lv_obj_add_flag(s_tl, LV_OBJ_FLAG_HIDDEN);
}

static bool tl_live(void) { return s_tl_col && lv_tick_elaps(s_tl_tick) < TL_STALE_MS; }

/* Dat chu so font so (fs: co font 96 / 190) vao (x, y dinh chu, cao chu cap) co gian theo scale sc/256. */
static void tl_place_num(const lv_font_t *nf, int fs, int x_left, int y_cap_top, int sc, int tw)
{
    int capF = fs * 727 / 1000;
    int bl = (int)nf->line_height - (int)nf->base_line;
    int pvx = tw / 2, pvy = bl - capF / 2;
    lv_obj_set_style_transform_pivot_x(s_tl_num, pvx, 0);
    lv_obj_set_style_transform_pivot_y(s_tl_num, pvy, 0);
    lv_obj_set_style_transform_scale(s_tl_num, sc, 0);
    lv_obj_set_pos(s_tl_num, x_left + tw * sc / 256 / 2 - pvx, y_cap_top + capF * sc / 256 / 2 - pvy);
}

static bool tl_paint(void)
{
    if (!s_tl) return false;
    lv_area_t a;
    bool live = tl_live();
    if (!live && s_tl_col) { s_tl_col = 0; s_tl_sec = -1; }
    ui_hold_drive(live);                                         /* do den lau: khong ve man cho */
    bool warn = eff_level() >= OBS_CAUTION;                      /* xe truoc gan / qua gan: o canh bao giu cho */
    if (!live || warn || !ui_xa_slot(&a)) {
        if (!lv_obj_has_flag(s_tl, LV_OBJ_FLAG_HIDDEN)) { vis(s_tl, false); s_tl_sig[0] = 0; s_tl_cell = false; vis(s_xc, s_xc_on); }
        return false;
    }
    bool night = ui_is_night();
    bool big = s_tl_style == 2 && ui_get_speed() >= 0 && ui_get_speed() < 3;
    int cellH = a.y2 - a.y1 + 1;
    if (big) {                                                   /* cot trai: duoi hang km/h + dong ho, toi day o duoi bien */
        a.x2 = a.x1 - 10;
        a.x1 = 6;
        a.y1 = a.y1 - cellH - 10 + 24;
    }
    int ci = s_tl_col == 'R' ? 0 : s_tl_col == 'Y' ? 1 : 2;
    int sec = s_tl_sec;
    bool soon = (ci == 0 && sec >= 0 && sec <= 3);               /* do sap het: nhac chuan bi */
    bool blink = ci == 2 && sec >= 0 && sec <= 3 && (lv_tick_get() / 250) % 2;   /* xanh sap het: so chop 2 Hz */
    char sig[96];
    snprintf(sig, sizeof sig, "%d,%d,%d,%d|%d%d%d|%c%d|%d", (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2, night, big, blink,
             s_tl_col, sec, s_tl_style);
    if (!strcmp(sig, s_tl_sig)) return true;
    snprintf(s_tl_sig, sizeof s_tl_sig, "%s", sig);

    static const uint32_t lit[3] = {0xFF3B30, 0xFFC400, 0x22D36B};
    static const uint32_t off[3] = {0x3A1A1E, 0x3A3214, 0x14321F};
    static const uint32_t fgN[3] = {0xFF5A52, 0xFFD400, 0x34D399}, fgD[3] = {0xD7262B, 0xB45309, 0x047857};
    static const uint32_t bgN[3] = {0x32161C, 0x2E2A12, 0x123024}, bgD[3] = {0xFFE4E4, 0xFFF4C2, 0xDDF7E9};
    int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    lv_color_t fg = lv_color_hex((night ? fgN : fgD)[ci]);
    lv_obj_set_pos(s_tl, a.x1, a.y1);
    lv_obj_set_size(s_tl, w, h);
    if (big) {                                                   /* de len nen trang: khong vien, cung mau nen */
        lv_obj_set_style_bg_color(s_tl, lv_color_hex(night ? 0x1E2235 : 0xECEDF1), 0);
        lv_obj_set_style_border_width(s_tl, 0, 0);
        lv_obj_set_style_shadow_width(s_tl, 0, 0);
    } else {
        lv_obj_set_style_bg_color(s_tl, lv_color_hex((night ? bgN : bgD)[ci]), 0);
        lv_obj_set_style_border_width(s_tl, 2, 0);
        lv_obj_set_style_border_color(s_tl, lv_color_hex(lit[ci]), 0);
        lv_obj_set_style_shadow_width(s_tl, 14, 0);
        lv_obj_set_style_shadow_color(s_tl, night ? lv_color_black() : lv_color_hex(0x96A0B8), 0);
        lv_obj_set_style_shadow_opa(s_tl, night ? 128 : 140, 0);
    }

    /* hop den doc 3 bong ben trai */
    int gap = big ? 8 : 4, pad = big ? 7 : 5;
    int dl = (h - 2 * (big ? 14 : 10) - 2 * pad - 2 * gap) / 3;
    if (dl > (big ? 46 : 28)) dl = big ? 46 : 28;
    int hw = dl + 2 * pad, hh = 3 * dl + 2 * gap + 2 * pad;
    int hx = big ? 8 : 10;
    lv_obj_set_size(s_tl_house, hw, hh);
    lv_obj_set_pos(s_tl_house, hx, (h - hh) / 2 - 2);
    lv_obj_set_style_radius(s_tl_house, hw / 2 < 14 ? hw / 2 : 14, 0);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *l = s_tl_lamp[i];
        lv_obj_set_size(l, dl, dl);
        lv_obj_set_pos(l, pad - 1, pad - 1 + i * (dl + gap));
        lv_obj_set_style_bg_color(l, lv_color_hex(i == ci ? lit[i] : off[i]), 0);
        lv_obj_set_style_shadow_color(l, lv_color_hex(lit[i]), 0);
        lv_obj_set_style_shadow_width(l, i == ci ? dl / 2 + 4 : 0, 0);
        lv_obj_set_style_shadow_opa(l, i == ci ? 200 : 0, 0);
    }

    /* phan chu + so ben phai hop den */
    static const char *const nm[3] = {"ĐÈN ĐỎ", "ĐÈN VÀNG", "ĐÈN XANH"};
    const char *lab = soon ? "CHUẨN BỊ" : nm[ci];
    int rx = hx + hw + (big ? 14 : 8), rw = w - rx - (big ? 8 : 8);
    const lv_font_t *lf = big ? &ui_font_txt20 : (sec >= 0 ? &ui_font_txt14 : &ui_font_txt16);
    lv_obj_set_style_text_font(s_tl_lab, lf, 0);
    set_txt(s_tl_lab, lab);
    lv_obj_set_style_text_color(s_tl_lab, sec >= 0 ? (night ? lv_color_white() : lv_color_hex(0x14112B)) : fg, 0);
    lv_obj_set_style_text_align(s_tl_lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_tl_lab, rw);
    vis(s_tl_num, sec >= 0 && !blink);
    vis(s_tl_unit, sec >= 0 && !blink);
    if (sec < 0) {                                               /* khong co so: nhan to giua */
        if (!big) lv_obj_set_style_text_font(s_tl_lab, &ui_font_txt20, 0);
        lv_obj_set_pos(s_tl_lab, rx, h / 2 - (int)lv_obj_get_style_text_font(s_tl_lab, 0)->line_height / 2);
    } else {
        char num[12];
        snprintf(num, sizeof num, "%d", sec > 999 ? 999 : sec);
        const lv_font_t *nf = big ? &ui_font_d190 : &ui_font_d96;
        int fs = big ? 190 : 96, capF = fs * 727 / 1000;
        int tw = lv_text_get_width(num, strlen(num), nf, -2);
        int uw = lv_text_get_width("s", 1, &ui_font_unit22, 0), ug = 3;
        int labH = (int)lf->line_height + (big ? 6 : 2);
        int hroom = h - (big ? 24 : 22) - labH, room = rw - uw - ug;
        int sc = 256;
        if (capF * sc / 256 > hroom) sc = hroom * 256 / capF;
        if (tw * sc / 256 > room) sc = room * 256 / (tw ? tw : 1);
        int capS = capF * sc / 256, numW = tw * sc / 256;
        int top = h / 2 - (labH + capS) / 2;
        lv_obj_set_pos(s_tl_lab, rx, top - 2);
        lv_obj_set_style_text_font(s_tl_num, nf, 0);
        set_txt(s_tl_num, num);
        lv_obj_set_style_text_color(s_tl_num, fg, 0);
        int gx = rx + (rw - (numW + ug + uw)) / 2;
        tl_place_num(nf, fs, gx, top + labH, sc, tw);
        lv_obj_set_style_text_color(s_tl_unit, fg, 0);
        int ub = (int)ui_font_unit22.line_height - (int)ui_font_unit22.base_line;
        lv_obj_set_pos(s_tl_unit, gx + numW + ug, top + labH + capS - ub);
    }
    vis(s_tl, true);
    s_tl_cell = !big;
    vis(s_xc, big && s_xc_on);
    lv_obj_move_foreground(s_tl);
    if (!lv_obj_has_flag(s_crit, LV_OBJ_FLAG_HIDDEN)) lv_obj_move_foreground(s_crit);
    return true;
}

void obstacle_ui_tlight(char col, int sec)
{
    tl_create();
    if (col != 'R' && col != 'Y' && col != 'G') col = 0;
    s_tl_col = col;
    s_tl_sec = col ? sec : -1;
    s_tl_tick = lv_tick_get();
    tl_paint();
}

void obstacle_ui_tlight_style(int style) { s_tl_style = style == 2 ? 2 : 1; s_tl_sig[0] = 0; }
