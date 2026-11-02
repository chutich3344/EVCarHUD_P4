/* Chup anh giao dien 320x240 tren PC (khong can board): chay che do trinh dien qua bo giai ma HLP that
 * va luu vai khung hinh ra file BMP. Xem tools/pc_preview/build.sh. */
#include "lvgl.h"
#include "dashboard.h"
#include "demo.h"
#include "gofa.h"
#include <string.h>
#include "dashboard_hlp.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint16_t fb[UI_W * UI_H];
static uint32_t now_ms;
static uint32_t tick_cb(void) { return now_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * UI_W + x] = *src++;
    lv_display_flush_ready(d);
}

static void run(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        now_ms += 10;
        lv_timer_handler();
    }
}

static void save_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = UI_W * 3, size = 54 + row * UI_H;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    int w = UI_W, hh = -UI_H;                       /* chieu cao am: tu tren xuong */
    memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (int i = 0; i < UI_W * UI_H; i++) {
        uint16_t c = fb[i];
        uint8_t bgr[3] = {(uint8_t)((c & 0x1F) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c >> 11) << 3)};
        fwrite(bgr, 1, 3, f);
    }
    fclose(f);
    printf("da luu %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char p[256];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);

    lv_screen_load(ui_splash_create("v1.1", UI_SRC_WAZE));          /* man chao logo */
    run(800);
    snprintf(p, sizeof p, "%s/00_man_chao.bmp", dir); save_bmp(p);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
    dashboard_create(scr);
    hlp_dash_init(NULL, NULL);                     /* tren board: link_init() goi */
    run(400);

    /* Kich ban chay thu giong trang web (che do trinh dien): chup cuoi moi muc, khi chu ten muc da tat */
    demo_start();                                   /* vao thang muc 1 (logo da chup o tren) */
    for (int i = 1; demo_step_ms(i) > 0; i++) {
        run(demo_step_ms(i) - 300);
        snprintf(p, sizeof p, "%s/buoc_%02d.bmp", dir, i); save_bmp(p);
        run(300);
    }

    demo_stop();
    run(400);
    /* Nut BOOT trong cai dat: 1 lan = muc ke (khung vang), 2 lan = chon */
    /* Gia lap app Gofa: ban tin protobuf giong tai lieu (so truong = thu tu trong bang) */
    ui_set_source(UI_SRC_GOFA);
    gofa_connected(true);
    {
        char sum[96];
        static const uint8_t tm[] = {0x0A, 0x06, 0x08, 14, 0x10, 5, 0x18, 9};
        static const uint8_t spd[] = {0x08, 45};
        static const uint8_t sg[] = {0x08, 5, 0x10, 33, 0x18, 0xAC, 0x02};
        uint8_t nav[96];
        size_t n = 0;
        const char *a = "Mai Chí Thọ", *b = "Đồng Văn Cống";
        nav[n++] = 0x08; nav[n++] = 2;
        nav[n++] = 0x12; nav[n++] = (uint8_t)strlen(a); memcpy(nav + n, a, strlen(a)); n += strlen(a);
        nav[n++] = 0x1A; nav[n++] = (uint8_t)strlen(b); memcpy(nav + n, b, strlen(b)); n += strlen(b);
        nav[n++] = 0x20; nav[n++] = 4; nav[n++] = 0x28; nav[n++] = 0x96; nav[n++] = 0x01;
        gofa_handle(GOFA_CHR_TIME, tm, sizeof tm, sum, sizeof sum); printf("%s\n", sum);
        gofa_handle(GOFA_CHR_SPEED, spd, sizeof spd, sum, sizeof sum); printf("%s\n", sum);
        gofa_handle(GOFA_CHR_SIGN, sg, sizeof sg, sum, sizeof sum); printf("%s\n", sum);
        run(300);
        snprintf(p, sizeof p, "%s/gofa_cruise.bmp", dir); save_bmp(p);
        gofa_handle(GOFA_CHR_NAV, nav, n, sum, sizeof sum); printf("%s\n", sum);
        char last[160];
        snprintf(last, sizeof last, "%s\n08 02 12 0E 4D 61 69 20", sum);
        ui_gofa_debug("GOFA: đang nhận dữ liệu", last);
        run(300);
        snprintf(p, sizeof p, "%s/gofa_nav.bmp", dir); save_bmp(p);
        static const uint8_t off[] = {0x08, 0};
        gofa_handle(GOFA_CHR_NAV, off, sizeof off, sum, sizeof sum);
        gofa_connected(false);
        run(7000);
    }
    ui_set_source(UI_SRC_WAZE);
    ui_set_mac("A0:B7:65:2C:4F:A6");
    ui_open_settings();
    static const int keys[] = {UI_KEY_NEXT, UI_KEY_NEXT, UI_KEY_NEXT, UI_KEY_NEXT, UI_KEY_OK, UI_KEY_NEXT, UI_KEY_NEXT};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        ui_settings_key(keys[i]);
        run(200);
        snprintf(p, sizeof p, "%s/nut_%02d.bmp", dir, (int)i); save_bmp(p);
    }
    ui_settings_key(UI_KEY_OK);
    run(200);
    snprintf(p, sizeof p, "%s/nut_99.bmp", dir); save_bmp(p);
    return 0;
}
