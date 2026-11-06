/* Chay lai video tren PC bang dung ma firmware: adas_feed.c (chon xe phia truoc, TTC, muc canh bao, chong keu pho dong,
 * chan troi tu chinh) + obstacle_ui.c (phuong an E) + dashboard.c (giao dien HUD). Xem run.py.
 * Vao (stdin): moi khung "F <ms> <km/h> <n>" roi n dong "<cls> <x1> <y1> <x2> <y2> <diem 0..100>" (anh 320x240).
 * Ra: <dir>/hud.rgb (480x320 RGB888 moi khung) va stdout moi khung:
 *     "R <chan troi y> <lead i hoac -1> <dist_m> <muc OBS_*> <so tieng tit cong don>" */
#include "lvgl.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "obstacle_ui.h"
#include "adas_feed.h"
#include "detect.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint16_t fb[UI_W * UI_H];
static uint32_t now_ms;
static int beeps, last_level;
static uint32_t tick_cb(void) { return now_ms; }
int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
bool board_ui_lock(void) { return true; }
void board_ui_unlock(void) {}
void board_beep(int times) { beeps += times; }

/* obstacle_ui_set bi goi qua adas_feed -> ghi lai muc dang hien */
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * UI_W + x] = *src++;
    lv_display_flush_ready(d);
}

static void run_to(uint32_t t)
{
    while (now_ms + 10 <= t) {
        now_ms += 10;
        lv_timer_handler();
    }
    now_ms = t;
    lv_timer_handler();
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    bool day = argc > 2 && !strcmp(argv[2], "day");
    char p[512];
    snprintf(p, sizeof p, "%s/hud.rgb", dir);
    FILE *out = fopen(p, "wb");
    if (!out) return 1;
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(UI_W, UI_H);
    static uint16_t buf[UI_W * UI_H];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    ui_set_boot_night(!day);
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    dashboard_create(scr);
    hlp_dash_init(NULL, NULL);
    ui_set_mode(day ? UI_MODE_DAY : UI_MODE_NIGHT);
    obstacle_ui_init();
    hlp_dash_set_clock(1793578500LL, 7 * 60, UI_TSRC_NTP);
    ui_set_signal(true);
    ui_set_phone(true);
    ui_set_waze(true);
    ui_set_nav(false, 0, -1, 0, "Video chạy thử", 0);
    ui_set_speed_limit(50);
    ui_set_speed(0);
    run_to(4000);
    uint32_t t0 = 0;
    bool first = true;

    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        unsigned ms;
        int kmh, n;
        if (sscanf(line, "F %u %d %d", &ms, &kmh, &n) != 3) continue;
        if (first) { t0 = ms; first = false; }
        detect_box_t bx[DET_BOX_MAX];
        int k = 0;
        for (int i = 0; i < n && fgets(line, sizeof line, stdin); i++) {
            detect_box_t b = {0};
            if (sscanf(line, "%d %d %d %d %d %d", &b.cls, &b.x1, &b.y1, &b.x2, &b.y2, &b.score) == 6 && k < DET_BOX_MAX) bx[k++] = b;
        }
        run_to(4000 + ms - t0);
        ui_set_speed(kmh);
        ui_set_speed_limit(kmh >= 55 ? 80 : 50);
        adas_feed(bx, k, 320, 240);
        run_to(now_ms + 20);
        int lead = -1, dm = 0;
        for (int i = 0; i < k; i++) if (bx[i].dist_m > 0) { lead = i; dm = bx[i].dist_m; }
        for (int i = 0; i < UI_W * UI_H; i++) {
            uint16_t c = fb[i];
            uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3)};
            fwrite(rgb, 1, 3, out);
        }
        fflush(out);
        printf("R %.1f %d %d %d %d\n", (double)adas_feed_horizon(), lead, dm, obstacle_ui_level(), beeps);
        fflush(stdout);
    }
    fclose(out);
    (void)last_level;
    return 0;
}
