/* Goi A1 tu app EVCarHUD Lan - xem phone_adas.h. */
#include "phone_adas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool phone_adas_parse(const char *s, size_t n, phone_adas_t *p)
{
    char b[96];
    if (n < 3 || n >= sizeof b) return false;
    memcpy(b, s, n);
    b[n] = 0;
    if (strncmp(b, "A1|", 3) != 0) return false;
    memset(p, 0, sizeof *p);
    p->ldw = '-';
    int v[10] = {0};
    int k = 0;
    char *f = b + 3;
    for (int i = 0; f && i < 11; i++) {
        char *q = strchr(f, '|');
        if (q) *q = 0;
        if (i == 0) p->ldw = (f[0] == 'L' || f[0] == 'R') ? f[0] : '-';
        else v[k++] = atoi(f);
        f = q ? q + 1 : NULL;
    }
    p->off_cm = v[0]; p->pl = v[1]; p->pr = v[2]; p->lead_m = v[3]; p->dv_kmh = v[4];
    p->ttc10 = v[5]; p->level = v[6]; p->kmh = v[7]; p->state = v[8];
    if (p->level < 0) p->level = 0;
    if (p->level > 3) p->level = 3;
    return true;
}

void phone_adas_to_xadas(const phone_adas_t *p, int legal_m, xadas_frame_t *f)
{
    memset(f, 0, sizeof *f);
    f->state = p->state == 1 ? XA_ST_SLOW : p->state == 3 ? XA_ST_HOT : p->state == 4 ? XA_ST_WAIT : XA_ST_MEASURE;
    f->speed = p->kmh >= 0 && p->kmh < 255 ? (uint8_t)p->kmh : XA_NONE8;
    if (p->lead_m > 0 && f->state == XA_ST_MEASURE) {
        f->dist = (uint16_t)(p->lead_m > 9999 ? 9999 : p->lead_m);
        f->level = p->level >= 2 ? XA_LV_CLOSE : p->level == 1 ? XA_LV_NEAR : XA_LV_SAFE;
    } else {
        f->dist = XA_NONE16;
        f->level = XA_LV_NONE;
    }
    /* duoi 55 km/h luat khong ghi so: app dung 1 giay (toi thieu 8 m) */
    int mn = legal_m > 0 ? legal_m : (p->kmh * 10 / 36 > 8 ? p->kmh * 10 / 36 : 8);
    f->min = f->level >= XA_LV_NEAR ? (uint16_t)mn : XA_NONE16;
}

bool phone_nav_parse(const char *s, size_t n, phone_nav_t *p)
{
    char b[200];
    if (n < 3 || n >= sizeof b) return false;
    memcpy(b, s, n);
    b[n] = 0;
    if (strncmp(b, "D1|", 3) != 0) return false;
    memset(p, 0, sizeof *p);
    p->dist_m = -1;
    char *f = b + 3;
    for (int i = 0; f && i < 7; i++) {
        char *q = i < 6 ? strchr(f, '|') : NULL;                  /* ten duong (truong cuoi) giu nguyen */
        if (q) *q = 0;
        switch (i) {
        case 0: p->turn = atoi(f); break;
        case 1: p->dist_m = f[0] ? atoi(f) : -1; break;
        case 2: p->exit_n = atoi(f); break;
        case 3: p->remain_m = atoi(f); break;
        case 4: p->remain_min = atoi(f); break;
        case 5: snprintf(p->eta, sizeof p->eta, "%.5s", f); break;
        case 6: snprintf(p->street, sizeof p->street, "%.95s", f); break;
        }
        f = q ? q + 1 : NULL;
    }
    p->on = p->turn > 0;
    return true;
}

#ifndef XADAS_NO_UI
#include "esp_log.h"
#include "dashboard.h"
#include "board.h"
#include "obstacle_ui.h"
#include "adas_feed.h"

static const char *TAG = "lan_dt";
static unsigned s_cnt;

void phone_adas_on_write(const char *s, size_t n)
{
    phone_nav_t nv;
    if (phone_nav_parse(s, n, &nv)) {
        static int d_turn = -1;
        if (nv.turn != d_turn) { d_turn = nv.turn; ESP_LOGI(TAG, "%.*s", (int)n, s); }
        board_ui_lock();
        ui_phone_nav(nv.on, nv.turn, nv.dist_m, nv.exit_n, nv.remain_m, nv.remain_min, nv.eta, nv.street);
        board_ui_unlock();
        return;
    }
    phone_adas_t p;
    if (!phone_adas_parse(s, n, &p)) return;
    if ((s_cnt++ % 50) == 0 || p.level >= 2 || p.ldw != '-') ESP_LOGI(TAG, "%.*s", (int)n, s);
    xadas_frame_t f;
    phone_adas_to_xadas(&p, adas_feed_legal_m(p.kmh), &f);
    board_ui_lock();
    obstacle_ui_phone(&f, p.ldw, p.pl > 50 && p.pr > 50, p.level >= 3);
    board_ui_unlock();
}
#endif
