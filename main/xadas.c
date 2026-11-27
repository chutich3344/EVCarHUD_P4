/* xADAS HUD Link - xem xadas.h. Phan giai ma / chon chu thuan C (thu tren PC: tools/xadas_test). */
#include "xadas.h"

#include <stdio.h>
#include <string.h>

#ifndef XADAS_NO_UI
#include "board.h"
#include "dashboard.h"
#include "obstacle_ui.h"
#include "esp_log.h"
static const char *TAG = "xadas";
#endif

bool xadas_parse(const uint8_t *b, size_t n, xadas_frame_t *f)
{
    if (!b || n < 10 || b[0] != 1) return false;
    f->seq = b[1];
    f->state = b[2];
    f->level = b[3];
    f->imperial = b[4] & 0x01;
    f->test = b[4] & 0x02;
    f->speed = b[5];
    f->dist = (uint16_t)(b[6] | b[7] << 8);
    f->min = (uint16_t)(b[8] | b[9] << 8);
    if (f->state > XA_ST_NOIMG) f->state = XA_ST_WAIT;            /* state la: hien nhu Cho */
    return true;
}

int xadas_obs_level(const xadas_frame_t *f)
{
    if (!f || f->state != XA_ST_MEASURE || f->dist == XA_NONE16) return 0;
    return f->level == XA_LV_CLOSE ? 2 : f->level == XA_LV_NEAR ? 1 : 0;   /* OBS_DANGER / OBS_CAUTION */
}

void xadas_chip_text(const xadas_frame_t *f, bool live, char *out, size_t n, int *color)
{
    /* mau: 0 xam, 1 xanh la, 2 vang, 3 do, 4 xanh duong, 5 vang canh bao he thong (= UI_XA_* trong dashboard.h) */
    if (!live) {
        snprintf(out, n, "Mất xADAS");
        *color = 0;
        return;
    }
    const char *u = f->imperial ? "ft" : "m";
    switch (f->state) {
    case XA_ST_MEASURE:
        if (f->dist == XA_NONE16) {
            snprintf(out, n, "— %s", u);
            *color = 0;
        } else {
            snprintf(out, n, "%s%u %s", f->test ? "THỬ " : "", (unsigned)f->dist, u);
            *color = f->level == XA_LV_SAFE ? 1 : f->level == XA_LV_NEAR ? 2 : f->level == XA_LV_CLOSE ? 3
                   : f->level == XA_LV_NOGPS ? 4 : 0;
            if (f->level > XA_LV_NOGPS) *color = 0;
        }
        return;
    case XA_ST_HOT:
        snprintf(out, n, "xADAS nóng");
        *color = 5;
        return;
    case XA_ST_NOIMG:
        snprintf(out, n, "xADAS mất hình");
        *color = 5;
        return;
    case XA_ST_SLOW:
        snprintf(out, n, "xADAS dừng");
        *color = 0;
        return;
    default:
        snprintf(out, n, "xADAS");
        *color = 0;
        return;
    }
}

#ifndef XADAS_NO_UI
static uint32_t s_cnt;

void xadas_on_write(const uint8_t *b, size_t n)
{
    xadas_frame_t f;
    if (!xadas_parse(b, n, &f)) {
        ESP_LOGW(TAG, "bo khung (%u byte, version %u)", (unsigned)n, n ? b[0] : 0);
        return;
    }
    if ((s_cnt++ % 20) == 0 || f.level == XA_LV_CLOSE)
        ESP_LOGI(TAG, "seq %u state %u level %u %s%s speed %u dist %u min %u", f.seq, f.state, f.level, f.imperial ? "ft " : "",
                 f.test ? "THU " : "", f.speed, f.dist, f.min);
    board_ui_lock();
    obstacle_ui_xadas(&f);
    board_ui_unlock();
}

void xadas_link_down(void)
{
    board_ui_lock();
    obstacle_ui_xadas(NULL);
    board_ui_unlock();
}
#endif
