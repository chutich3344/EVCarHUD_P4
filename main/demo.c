/* Che do trinh dien: chay dung kich ban "Chay thu" tren evcarhud.com, moi muc Ngay roi Dem:
 *   logo 3 giay -> 1 chua ket noi dien thoai -> 2 da ket noi dien thoai -> 3 doi Ngay -> Dem -> Tu dong
 *   -> 4 dang chay, chua co Waze -> 5 dang chay, co Waze -> 6 chi duong, chua co Waze -> 7 chi duong co Waze.
 * Du lieu gia la cac dong HLP/1 (giong Waze Mod gui) dua vao chinh bo giai ma that. Moi muc hien ten o giua man
 * hinh 2 giay. Bat/tat: giu BOOT 5 giay (xem buttons.c). Chay trong task LVGL (lv_timer), khong can khoa them. */
#include "demo.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include <stdio.h>
#include <string.h>

enum { K_LOGO, K_NOPHONE, K_PHONE, K_DRIVE };

typedef struct {
    int lim, spd0, spd1;    /* gioi han, toc do dau -> cuoi chang */
    int trn, trn2, dst0;    /* re, re tiep, khoang cach re dau chang (m); trn = 0: khong dan duong */
    int exit_n;
    const char *st, *st2;
    int alr, alr_d0;        /* canh bao gan nhat va khoang cach dau chang */
    int alr2, alr2_d0;
} leg_t;

/* chay chua co Waze: chi toc do GPS; co Waze: ten duong, gioi han, canh bao */
static const leg_t L_GPS      = {0, 34, 46, 0, 0, -1, 0, "", "", 0, 0, 0, 0};
static const leg_t L_GPS2     = {0, 50, 58, 0, 0, -1, 0, "", "", 0, 0, 0, 0};
static const leg_t L_CRUISE   = {50, 36, 44, 0, 0, -1, 0, "Nguyễn Văn Linh", "", 40, 600, 23, 300};
static const leg_t L_CRUISE2  = {50, 44, 48, 0, 0, -1, 0, "Nguyễn Văn Linh", "", 2, 300, 23, 200};
static const leg_t L_NAV_GPS  = {0, 34, 40, 2, 0, 350, 0, "", "Nguyễn Hữu Thọ", 0, 0, 0, 0};
static const leg_t L_NAV_GPS2 = {0, 40, 44, 3, 0, 500, 0, "", "Lê Văn Lương", 0, 0, 0, 0};
static const leg_t L_NAV      = {50, 30, 38, 1, 2, 600, 0, "Nguyễn Trãi", "Nguyễn Trãi", 1, 900, 23, 300};
static const leg_t L_NAV_OVER = {50, 58, 64, 2, 0, 300, 0, "Hậu Giang", "Hậu Giang", 8, 200, 28, 150};

typedef struct {
    int kind, ms, mode;
    const char *name;       /* hien o giua man hinh khi vao muc */
    const leg_t *leg;
} step_t;

static const step_t STEPS[] = {
    {K_LOGO, 3000, UI_MODE_NIGHT, NULL, NULL},
    {K_NOPHONE, 4000, UI_MODE_DAY, "1. Chưa kết nối · Ngày", NULL},
    {K_NOPHONE, 4000, UI_MODE_NIGHT, "1. Chưa kết nối · Đêm", NULL},
    {K_PHONE, 4000, UI_MODE_DAY, "2. Đã kết nối · Ngày", NULL},
    {K_PHONE, 4000, UI_MODE_NIGHT, "2. Đã kết nối · Đêm", NULL},
    {K_PHONE, 3000, UI_MODE_DAY, "3. Chế độ: Ban ngày", NULL},
    {K_PHONE, 3000, UI_MODE_NIGHT, "3. Chế độ: Ban đêm", NULL},
    {K_PHONE, 3000, UI_MODE_AUTO, "3. Chế độ: Tự động", NULL},
    {K_DRIVE, 5000, UI_MODE_DAY, "4. Chạy, chưa có Waze · Ngày", &L_GPS},
    {K_DRIVE, 5000, UI_MODE_NIGHT, "4. Chạy, chưa có Waze · Đêm", &L_GPS2},
    {K_DRIVE, 5000, UI_MODE_DAY, "5. Chạy, có Waze · Ngày", &L_CRUISE},
    {K_DRIVE, 5000, UI_MODE_NIGHT, "5. Chạy, có Waze · Đêm", &L_CRUISE2},
    {K_DRIVE, 5000, UI_MODE_DAY, "6. Chỉ đường, chưa Waze · Ngày", &L_NAV_GPS},
    {K_DRIVE, 5000, UI_MODE_NIGHT, "6. Chỉ đường, chưa Waze · Đêm", &L_NAV_GPS2},
    {K_DRIVE, 5000, UI_MODE_DAY, "7. Chỉ đường, có Waze · Ngày", &L_NAV},
    {K_DRIVE, 5000, UI_MODE_NIGHT, "7. Chỉ đường, có Waze · Đêm", &L_NAV_OVER},
};
#define N_STEPS ((int)(sizeof STEPS / sizeof STEPS[0]))

static lv_timer_t *tmr;
static uint32_t t0;
static uint32_t seq;
static int cur = -1;
static lv_obj_t *dash_scr;            /* man bang dieu khien (de quay lai sau logo) */

static void feed(const char *line)
{
    hlp_dash_feed((const uint8_t *)line, strlen(line));
    hlp_dash_feed((const uint8_t *)"\n", 1);
}

static void feed_clock(void)          /* gio gia: 13:45 thu Nam 30/10 (mui gio +7) */
{
    feed("{\"v\":1,\"t\":\"hi\",\"sess\":1,\"unix\":1761806700,\"tz\":420,\"rate\":4}");
}

static int lerp(int a, int b, int num, int den) { return den <= 0 ? b : a + (b - a) * num / den; }

static void feed_leg(const leg_t *L, int pos, int len)
{
    int spd = lerp(L->spd0, L->spd1, pos, len);
    int dst = L->dst0 < 0 ? -1 : lerp(L->dst0, L->dst0 / 3, pos, len);
    int ad = L->alr ? lerp(L->alr_d0, L->alr_d0 / 3, pos, len) : -1;
    int ad2 = L->alr2 ? lerp(L->alr2_d0, L->alr2_d0 / 2, pos, len) : -1;
    int rm = 6700 - pos / 20;

    char alrs[160] = "";
    if (L->alr && L->alr2) snprintf(alrs, sizeof alrs, ",\"alrs\":[{\"k\":%d,\"d\":%d},{\"k\":%d,\"d\":%d}]", L->alr, ad, L->alr2, ad2);
    else if (L->alr) snprintf(alrs, sizeof alrs, ",\"alrs\":[{\"k\":%d,\"d\":%d,\"v\":%d}]", L->alr, ad, L->alr == 8 ? 40 : -1);
    else snprintf(alrs, sizeof alrs, ",\"alrs\":[]");

    char line[512];
    snprintf(line, sizeof line,
             "{\"v\":1,\"t\":\"s\",\"ts\":%u,\"nav\":%d,\"spd\":%d,\"lim\":%d,\"over\":%d,\"trn\":%d,\"trn2\":%d,"
             "\"dst\":%d,\"exit\":%d,\"st\":\"%s\",\"st2\":\"%s\",\"eta\":\"%s\",\"rm\":%d,\"rmin\":%d,"
             "\"avg\":0,\"avgL\":0,\"alr\":%d,\"alrD\":%d%s}",
             (unsigned)(++seq * 250), L->trn ? 1 : 0, spd, L->lim, L->lim > 0 && spd > L->lim, L->trn, L->trn2,
             dst, L->exit_n, L->st, L->st2, L->trn ? "20:05" : "", L->trn ? rm : 0, L->trn ? rm / 370 + 1 : 0,
             L->alr, ad, alrs);
    feed(line);
}

static void enter(int k)
{
    const step_t *S = &STEPS[k];
    if (S->kind == K_LOGO) {
        hlp_dash_on_disconnect();
        dash_scr = ui_screen();
        lv_screen_load(ui_splash_create("", UI_SRC_WAZE));
        return;
    }
    if (k > 0 && STEPS[k - 1].kind == K_LOGO) {
        if (lv_screen_active() != dash_scr) lv_screen_load_anim(dash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);   /* xoa man logo */
        hlp_dash_on_connect();
        feed_clock();                                     /* dong ho chay tiep ca khi mat ket noi */
    }
    if (S->kind == K_NOPHONE) hlp_dash_on_disconnect();
    else if (S->kind == K_PHONE && STEPS[k - 1].kind == K_NOPHONE) {
        hlp_dash_on_connect();
        feed_clock();
    }
    ui_set_mode(S->mode);
    if (S->name) ui_toast(S->name);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t total = 0;
    for (int i = 0; i < N_STEPS; i++) total += (uint32_t)STEPS[i].ms;
    int pos = (int)(lv_tick_elaps(t0) % total), k = 0;
    while (pos >= STEPS[k].ms) pos -= STEPS[k++].ms;
    if (k != cur) {
        cur = k;
        enter(k);
    }
    if (STEPS[k].kind == K_DRIVE) feed_leg(STEPS[k].leg, pos, STEPS[k].ms);
    else if (STEPS[k].kind == K_PHONE && pos < 500) feed_clock();   /* giu "da ket noi" song */
}

void demo_start(void)
{
    if (tmr) return;
    t0 = lv_tick_get() - (uint32_t)STEPS[0].ms;        /* luc bat may da hien logo: vao thang muc 1, vong sau moi co logo */
    dash_scr = ui_screen();
    seq = 0;
    cur = -1;
    tmr = lv_timer_create(tick_cb, 250, NULL);
    tick_cb(tmr);
}

void demo_stop(void)
{
    if (!tmr) return;
    lv_timer_delete(tmr);
    tmr = NULL;
    if (dash_scr && lv_screen_active() != dash_scr) lv_screen_load_anim(dash_scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    hlp_dash_on_disconnect();
}

bool demo_running(void) { return tmr != NULL; }

int demo_step_ms(int i) { return i >= 0 && i < N_STEPS ? STEPS[i].ms : 0; }

void demo_step(int ms)
{
    if (!tmr) return;
    t0 -= (uint32_t)ms;                                  /* chi dung cho anh xem truoc tren PC: nhay toi thoi diem khac */
    tick_cb(tmr);
}
