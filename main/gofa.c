#include "gofa.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include <stdio.h>
#include <string.h>

/* ===== protobuf toi gian: chi lay truong 1..8, kieu varint / 32 bit / chuoi ===== */
#define PB_MAX 9
typedef struct {
    bool has[PB_MAX];
    uint32_t v[PB_MAX];
    const uint8_t *s[PB_MAX];     /* truong chieu dai (chuoi / ban tin con) */
    uint16_t sl[PB_MAX];
} pb_t;

static bool pb_varint(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
    uint64_t v = 0;
    for (int sh = 0; *p < end && sh < 64; sh += 7) {
        uint8_t b = *(*p)++;
        v |= (uint64_t)(b & 0x7F) << sh;
        if (!(b & 0x80)) { *out = v; return true; }
    }
    return false;
}

static void pb_parse(const uint8_t *d, size_t n, pb_t *m)
{
    memset(m, 0, sizeof *m);
    const uint8_t *p = d, *end = d + n;
    while (p < end) {
        uint64_t key, v;
        if (!pb_varint(&p, end, &key)) return;
        unsigned f = (unsigned)(key >> 3), wt = (unsigned)(key & 7);
        bool keep = f > 0 && f < PB_MAX;
        switch (wt) {
        case 0:
            if (!pb_varint(&p, end, &v)) return;
            if (keep) { m->has[f] = true; m->v[f] = (uint32_t)v; }
            break;
        case 1:                                                    /* 64 bit: lay 32 bit thap */
            if (end - p < 8) return;
            if (keep) { m->has[f] = true; m->v[f] = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
            p += 8;
            break;
        case 2:
            if (!pb_varint(&p, end, &v) || v > (uint64_t)(end - p)) return;
            if (keep) { m->has[f] = true; m->s[f] = p; m->sl[f] = (uint16_t)v; }
            p += v;
            break;
        case 5:
            if (end - p < 4) return;
            if (keep) { m->has[f] = true; m->v[f] = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
            p += 4;
            break;
        default:
            return;
        }
    }
}

static void pb_str(const pb_t *m, int f, char *out, size_t n)
{
    size_t l = m->s[f] ? m->sl[f] : 0;
    if (l >= n) l = n - 1;
    if (l) memcpy(out, m->s[f], l);
    out[l] = 0;
}

/* ===== bang tra theo tai lieu GOFA ===== */
static int limit_for_sign(uint32_t id)                             /* ID 3..11: bien gioi han toc do */
{
    static const int kmh[] = {30, 40, 50, 60, 70, 80, 90, 100, 120};
    return id >= 3 && id <= 11 ? kmh[id - 3] : 0;
}

static int alert_for_sign(uint32_t id)                             /* ID bien GOFA -> ma canh bao HLP (icon san co) */
{
    switch (id) {
    case 12: return 23;                                            /* vao khu dan cu */
    case 13: return 24;                                            /* ra khoi khu dan cu */
    case 14: return 9;                                             /* cam vuot */
    case 15: return 10;                                            /* het cam vuot */
    case 17: return 19;                                            /* tram dung nghi */
    case 18: return 12;                                            /* tram thu phi */
    case 21: case 23: return 28;                                   /* cam re trai (va quay dau) */
    case 22: case 24: return 29;                                   /* cam re phai (va quay dau) */
    case 25: return 30;                                            /* cam quay dau sang trai */
    case 26: return 74;                                            /* cam quay dau sang phai */
    case 27: return 31;                                            /* cam di thang */
    case 32: return 40;                                            /* camera giao thong */
    case 33: return 2;                                             /* camera phat (toc do) */
    case 34: return 3;                                             /* camera den do */
    case 36: return 41;                                            /* camera AI */
    default: return id ? 4 : 0;                                    /* ID la: bien nguy hiem chung */
    }
}

static int turn_for_maneuver(uint32_t mv)                          /* NavigationManeuver -> ma re HLP `trn` */
{
    /* theo app GOFA 4.2.2: 0 chua ro, 1 thang, 2 quay dau, 3 trai, 4 phai, 5 cheo trai, 6 cheo phai, 7 gat trai, 8 gat phai,
     * 9 vong xuyen cung chieu kim dong ho, 10 vong xuyen nguoc chieu (VN di ben phai: hay gap 10) */
    static const int trn[] = {0, 1, 8, 2, 3, 4, 5, 6, 7, 10, 10};
    return mv < sizeof trn / sizeof trn[0] ? trn[mv] : 0;
}

/* ===== trang thai ===== */
static char road_now[64], road_next[64];
static bool nav_on;
static int nav_trn, nav_dst = -1;
static void (*beep_cb)(int);

void gofa_set_beep(void (*cb)(int)) { beep_cb = cb; }

void gofa_connected(bool on)
{
    if (on) {
        road_now[0] = road_next[0] = 0;
        nav_on = false;
        nav_dst = -1;
    }
    hlp_dash_external(on);
    if (on) ui_set_waze(true);                                     /* app Gofa co ban do: gioi han, bien, dan duong */
}

static void push_nav(void)
{
    ui_set_nav(nav_on, nav_trn, nav_dst, 0, nav_on && road_next[0] ? road_next : road_now, 0);
}

static void on_sign(const pb_t *m, char *sum, size_t sn)
{
    /* position_1 (1) = bien gioi han; position_2 (2) + distance_2 (3); position_3 (4) + distance_3 (5) */
    int lim = limit_for_sign(m->v[1]);
    ui_set_speed_limit(lim);
    ui_alert_t al[2];
    int n = 0;
    for (int k = 0; k < 2; k++) {
        uint32_t id = m->v[2 + 2 * k];
        if (!id) continue;
        int l = limit_for_sign(id);
        al[n].code = l ? 8 : alert_for_sign(id);                    /* bien gioi han phia truoc: "giam toc" + so */
        al[n].value = l ? l : -1;
        al[n].dist = m->has[3 + 2 * k] ? (int)m->v[3 + 2 * k] : -1;
        n++;
    }
    if (n == 2 && al[1].dist >= 0 && (al[0].dist < 0 || al[1].dist < al[0].dist)) {   /* gan truoc */
        ui_alert_t t = al[0]; al[0] = al[1]; al[1] = t;
    }
    ui_set_alerts(al, n);
    snprintf(sum, sn, "biển: %u (%d) · %u/%dm · %u/%dm", (unsigned)m->v[1], lim, (unsigned)m->v[2], (int)m->v[3],
             (unsigned)m->v[4], (int)m->v[5]);
}

static void on_nav(const pb_t *m, char *sum, size_t sn)
{
    /* state (1), current_road_name (2), next_road_name (3), maneuver (4), distance_m (5); state 2 = dang dan duong */
    pb_str(m, 2, road_now, sizeof road_now);
    pb_str(m, 3, road_next, sizeof road_next);
    /* state 2 = dang dan duong; app co the bo truong state (proto3 bo gia tri 0) nen co huong re
     * hoac khoang cach > 0 cung coi la dang dan duong */
    uint32_t mv = m->has[4] ? m->v[4] : 0;
    int dst = m->has[5] ? (int)m->v[5] : -1;
    /* HUDNavigationState: 0 chua xac dinh, 1 khong dan duong, 2 dang dan duong, 3 da den dich, 4 dang tinh lai tuyen */
    uint32_t st = m->v[1];
    if (st == 4) {                                                 /* tinh lai tuyen: giu man dan duong, bao dang tim duong */
        nav_on = true;
        if (!nav_trn) nav_trn = 1;
        nav_dst = -1;
        snprintf(road_next, sizeof road_next, "Đang tìm lại đường…");
        push_nav();
        snprintf(sum, sn, "dẫn đường: tính lại tuyến");
        return;
    }
    bool arrived = st == 3;
    nav_on = st == 2 || arrived || (st != 1 && (mv > 0 || dst > 0));
    nav_trn = arrived ? 17 : nav_on ? turn_for_maneuver(mv) : 0;     /* 17: co dich (mui ten "da den") */
    nav_dst = nav_on && !arrived ? dst : -1;
    if (nav_on && !nav_trn) nav_trn = 1;                           /* chua xac dinh huong: hien di thang */
    push_nav();
    /* De nghi GOFA (13/11): them truong 6 = quang duong con lai (m), 7 = thoi gian con lai (giay) vao NavigationMessage.
     * App chua gui thi 0 -> HUD hien gio hien tai; co thi HUD tu tinh gio den = gio hien tai + thoi gian con lai. */
    int rm = nav_on && !arrived && m->has[6] ? (int)m->v[6] : 0;
    int rs = nav_on && !arrived && m->has[7] ? (int)m->v[7] : 0;
    ui_set_route("", rm, rs > 0 ? (rs + 59) / 60 : 0);
    snprintf(sum, sn, "dẫn đường: st=%u hướng=%u %dm · %s", (unsigned)m->v[1], (unsigned)m->v[4], nav_dst,
             nav_on ? road_next : road_now);
}

static void on_time(const pb_t *m, char *sum, size_t sn)
{
    /* TimeMessage { ClockTime time = 1 { hour = 1, minute = 2, second = 3 } } */
    pb_t t;
    if (m->s[1]) pb_parse(m->s[1], m->sl[1], &t);
    else t = *m;                                                   /* phong khi app gui gio, phut, giay o cap ngoai */
    if (t.v[1] < 24 && t.v[2] < 60 && t.v[3] < 60) hlp_dash_set_local_time((int)t.v[1], (int)t.v[2], (int)t.v[3]);
    snprintf(sum, sn, "giờ: %02u:%02u:%02u", (unsigned)t.v[1], (unsigned)t.v[2], (unsigned)t.v[3]);
}

void gofa_handle(uint16_t chr, const uint8_t *data, size_t len, char *sum, size_t sn)
{
    pb_t m;
    pb_parse(data, len, &m);
    if (sum && sn) sum[0] = 0;
    char tmp[2];
    if (!sum || !sn) { sum = tmp; sn = sizeof tmp; }
    switch (chr) {
    case GOFA_CHR_SIGN:
        on_sign(&m, sum, sn);
        break;
    case GOFA_CHR_SPEED:
        ui_set_speed((int)m.v[1]);
        snprintf(sum, sn, "tốc độ: %u", (unsigned)m.v[1]);
        break;
    case GOFA_CHR_SOUND:
        if (m.v[1] && beep_cb) beep_cb(1);
        snprintf(sum, sn, "âm thanh: %u", (unsigned)m.v[1]);
        break;
    case GOFA_CHR_OVER:                                            /* giao dien tu so toc do voi gioi han: chi ghi lai */
        snprintf(sum, sn, "quá tốc độ: %u loại %u", (unsigned)m.v[1], (unsigned)m.v[2]);
        break;
    case GOFA_CHR_NAV:
        on_nav(&m, sum, sn);
        break;
    case GOFA_CHR_TIME:
        on_time(&m, sum, sn);
        break;
    default:
        snprintf(sum, sn, "0x%04X: %u byte", chr, (unsigned)len);
        break;
    }
    hlp_dash_external_touch();
}
