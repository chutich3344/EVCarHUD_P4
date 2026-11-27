/* Gia lap VietMap Live tren giao dien P4 480x320 (vml_uart.c): anh mui ten A8 64 px, bien / camera 48 px.
 * Chay: tools/pc_preview/build.sh voi preview_vml.c thay preview.c (xem preview.c de biet cach build). */
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "obstacle_ui.h"

void board_beep(int times) { (void)times; }
static uint16_t fb[UI_W * UI_H];
static uint32_t now_ms;
static uint32_t tick_cb(void) { return now_ms; }
#include <stdint.h>
#include <stdbool.h>
int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
bool board_ui_lock(void) { return true; }
void board_ui_unlock(void) {}
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * UI_W + x] = *src++;
    lv_display_flush_ready(d);
}
static void run(int ms) { for (int t = 0; t < ms; t += 10) { now_ms += 10; lv_timer_handler(); } }
static const char *outdir;
static void shot(const char *name)
{
    char p[300];
    snprintf(p, sizeof p, "%s/vml_%s.bmp", outdir, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    int row = UI_W * 3, size = 54 + row * UI_H;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    int w = UI_W, hh = -UI_H;
    memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (int i = 0; i < UI_W * UI_H; i++) {
        uint16_t c = fb[i];
        uint8_t bgr[3] = {(uint8_t)((c & 0x1F) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c >> 11) << 3)};
        fwrite(bgr, 1, 3, f);
    }
    fclose(f);
}

static uint8_t arr_px[64 * 64], sign_px[48 * 48 * 3], cam_px[48 * 48 * 3];
static lv_image_dsc_t arr_d, sign_d, cam_d;
static void mkdsc(lv_image_dsc_t *d, void *px, lv_color_format_t cf, int w)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC; d->header.cf = cf; d->header.w = w; d->header.h = w;
    d->header.stride = cf == LV_COLOR_FORMAT_A8 ? w : w * 2;
    d->data_size = cf == LV_COLOR_FORMAT_A8 ? w * w : w * w * 3; d->data = px;
}
static void mk_images(void)
{
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {          /* mui ten re phai (gia anh VML) */
        int sx = x * 60 / 64, sy = y * 60 / 64, a = 0;
        if (sx >= 18 && sx <= 30 && sy >= 26 && sy <= 56) a = 255;
        if (sy >= 20 && sy <= 32 && sx >= 18 && sx <= 40) a = 255;
        if (sx >= 38 && sx <= 54 && abs(sy - 26) <= 54 - sx) a = 255;
        arr_px[y * 64 + x] = a;
    }
    uint16_t *sc = (uint16_t *)sign_px, *cc = (uint16_t *)cam_px;
    uint8_t *sa = sign_px + 48 * 48 * 2, *ca = cam_px + 48 * 48 * 2;
    for (int y = 0; y < 48; y++) for (int x = 0; x < 48; x++) {
        int dx = x - 24, dy = y - 24, r2 = dx * dx + dy * dy;
        sc[y * 48 + x] = r2 >= 18 * 18 ? 0xF800 : 0xFFFF;
        float dd = 24 - sqrtf((x + .5f - 24) * (x + .5f - 24) + (y + .5f - 24) * (y + .5f - 24));
        sa[y * 48 + x] = dd >= 1 ? 255 : dd <= 0 ? 0 : (uint8_t)(dd * 255);
        uint16_t c = 0x0000;
        if (x >= 8 && x <= 35 && y >= 16 && y <= 34) c = 0x2104;
        if (x >= 35 && x <= 43 && y >= 19 && y <= 30) c = 0x2104;
        if ((x - 21) * (x - 21) + (y - 25) * (y - 25) <= 40) c = 0x5DFF;
        if (y >= 37 && y <= 40 && x >= 8 && x <= 40) c = 0xFE60;
        cc[y * 48 + x] = c;
        ca[y * 48 + x] = 255;
    }
    for (int y = 17; y <= 31; y++) for (int x = 14; x <= 34; x++) {     /* so "60" */
        int lx = x < 24 ? x - 14 : x - 25;
        if (lx < 0 || lx > 9) continue;
        int e = lx <= 1 || lx >= 8 || y <= 18 || y >= 30 || (x < 24 && y >= 23 && y <= 25);
        if (x < 24 && lx >= 8 && y < 23) e = 0;
        if (e) sc[y * 48 + x] = 0x0000;
    }
    mkdsc(&arr_d, arr_px, LV_COLOR_FORMAT_A8, 64);
    mkdsc(&sign_d, sign_px, LV_COLOR_FORMAT_RGB565A8, 48);
    mkdsc(&cam_d, cam_px, LV_COLOR_FORMAT_RGB565A8, 48);
}

int main(int argc, char **argv)
{
    mk_images();
    outdir = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    dashboard_create(scr);
    hlp_dash_init(NULL, NULL);
    obstacle_ui_init();
    ui_set_mode(UI_MODE_NIGHT);
    ui_set_time(13, 45, 5);
    ui_set_date(27, 11);
    run(400);

    hlp_dash_vml(true);                                  /* cau noi bao VML da noi */
    run(600); shot("0_vml_da_noi"); run(2600);
    ui_set_speed_limit(50);
    run(500); shot("1_chi_bien_hien_tai");
    ui_alert_t nx = {8, 400, -1}, cam = {3, 250, 0};
    ui_set_alert_icon(&sign_d); ui_set_alerts(&nx, 1);
    run(600); shot("2_bien_tiep_theo");
    ui_set_nav_icon(&arr_d); ui_set_nav(true, 3, 450, 0, "Mai Chí Thọ", 0);
    run(800); shot("3_re_bien_tiep");
    ui_set_alert_icon(&cam_d); ui_set_alerts(&cam, 1);
    run(800); shot("4_re_camera");
    ui_set_alerts(NULL, 0); ui_set_alert_icon(NULL);
    hlp_dash_lane(42);                                   /* app LANE noi BLE cung luc: co toc do */
    run(800); shot("5_re_co_toc_do_LANE");
    ui_set_mode(UI_MODE_DAY);
    ui_set_alert_icon(&sign_d); ui_set_alerts(&nx, 1);
    run(800); shot("6_ban_ngay");
    return 0;
}
