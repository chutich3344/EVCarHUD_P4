#include "dashboard_hlp.h"
#include "hlp.h"
#include <stdio.h>
#include <string.h>

#define STALE_MS 3000                 /* theo spec: 3s khong co tin -> mat tin hieu */
#define WEAK_MS  1500                 /* khong co tin `s` qua 1,5 s (binh thuong 4 tin/giay) -> chu GPS vang */

static hlp_rx_t rx;
static hlp_tx_cb_t tx_cb;
static void *tx_user;

static int64_t cur_sess = -1;
static uint32_t last_ts;
static bool have_ts;
static uint32_t last_rx_tick;
static bool signal_ok;
static uint32_t last_state_tick;      /* lan cuoi nhan tin `s` (vi tri/toc do) */
static bool have_state;
static int gps_level = -1;

static bool has_clock;
static int64_t clock_unix;            /* giay UTC tai thoi diem clock_tick */
static int clock_tz;                  /* phut */
static uint32_t clock_tick;
static int shown_minute = -1;
static bool clock_synced;             /* gio da chinh theo dien thoai (khong phai gio uoc tinh) */
static void (*clock_hook)(int64_t, int);

static lv_timer_t *watch_tmr;
#define WAZE_HOLD_MS 5000             /* mat du lieu Waze (ten duong, gioi han, canh bao) qua 5 s: coi nhu chi con GPS */
static uint32_t last_waze_tick;
static bool waze_seen;
static bool ext_on;                  /* nguon ngoai (app GOFA) dang noi: chi gui khi doi, khong tinh la mat tin hieu */

static void send_line(const char *line)
{
    if (tx_cb) tx_cb(line, tx_user);
}

/* HLP/1 khong co truong chat luong GPS, nen mau chu GPS dua vao do tuoi cua du lieu vi tri:
 * xanh = dang nhan tin `s` deu; vang = con ket noi nhung tin `s` tre > 1,5 s (hoac chua co tin dau tien);
 * do = mat ket noi voi dien thoai. */
static void update_gps(void)
{
    int lvl = !signal_ok ? UI_GPS_LOST
            : ext_on ? UI_GPS_GOOD
            : (!have_state || lv_tick_elaps(last_state_tick) > WEAK_MS) ? UI_GPS_WEAK : UI_GPS_GOOD;
    if (lvl == gps_level) return;
    gps_level = lvl;
    ui_set_gps(lvl);
}

static void set_signal(bool ok)
{
    if (ok != signal_ok) {
        signal_ok = ok;
        ui_set_signal(ok);
    }
    update_gps();
}

static bool is_camera(int code)
{
    return code == 2 || code == 3 || (code >= 40 && code <= 46);
}

/* "Mai Chi Tho (Lan hon hop)" -> "Mai Chi Tho": phan trong ngoac la loai lan, qua dai de hien tren HUD */
static void strip_paren(char *s)
{
    char *p = strchr(s, '(');
    if (p && p != s) {
        while (p > s && p[-1] == ' ') p--;
        *p = 0;
    }
}

/* Ung dung doi khi gui `st2` rong giua chung mot chang re du Waze van hien ten duong. Giu lai ten vua nhan
 * chung nao van la CUNG chang re: cung kieu re va khoang cach khong tang (qua cho re thi `dst` nhay len). */
#define HOLD_SLACK_M 30
static char hold_st2[HLP_STR_MAX];
static int hold_trn, hold_dst;

static const char *held_st2(const hlp_state_t *s)
{
    if (!s->nav || s->trn == 0 || s->dst < 0) {       /* het dan duong: quen ten cu */
        hold_st2[0] = 0;
        return s->st2;
    }
    if (s->st2[0]) {                                  /* co ten moi: nho lai */
        strncpy(hold_st2, s->st2, sizeof hold_st2 - 1);
        hold_st2[sizeof hold_st2 - 1] = 0;
        hold_trn = s->trn;
        hold_dst = s->dst;
        return s->st2;
    }
    if (hold_st2[0] && s->trn == hold_trn && s->dst <= hold_dst + HOLD_SLACK_M) {
        hold_dst = s->dst;                            /* bam theo khoang cach dang giam */
        return hold_st2;
    }
    hold_st2[0] = 0;                                  /* chang re khac: khong dung ten cu */
    return s->st2;
}

static void apply_state(const hlp_state_t *s)
{
    /* canh bao: alrs (gan->xa); neu khong co thi dung alr/alrD/alrV */
    ui_alert_t al[HLP_MAX_ALERTS];
    int n = 0;
    if (s->has_alrs && s->alrs_n > 0) {
        for (int i = 0; i < s->alrs_n && i < HLP_MAX_ALERTS; i++) {
            al[n].code = s->alrs[i].k;
            al[n].dist = s->alrs[i].d;
            al[n].value = s->alrs[i].v;
            n++;
        }
    } else if (s->alr != 0) {
        al[0].code = s->alr;
        al[0].dist = s->alrD;
        al[0].value = s->alrV;
        n = 1;
    }

    /* HLP khong co co "Waze dang mo": co ten duong, gioi han, canh bao hay dang dan duong la co du lieu Waze;
     * chi co toc do la toc do GPS cua dien thoai. Giu 5 s de duong khong ten khong lam nhay giao dien. */
    if (s->nav || s->lim > 0 || s->st[0] || n > 0) {
        waze_seen = true;
        last_waze_tick = lv_tick_get();
    }
    ui_set_waze(waze_seen && lv_tick_elaps(last_waze_tick) < WAZE_HOLD_MS);

    ui_set_speed(s->spd);
    ui_set_speed_limit(s->lim);

    /* ten duong: dang re -> duong SAU khi re (st2); khong thi duong hien tai (st) */
    char tmp[HLP_STR_MAX], street[HLP_STR_MAX];
    const char *st2 = held_st2(s);
    const char *src = (s->nav && s->trn != 0 && st2[0]) ? st2 : s->st;
    strncpy(tmp, src, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    strip_paren(tmp);                                /* bo "(Lan hon hop)" */
    snprintf(street, sizeof street, "%s", tmp);     /* giu chu hoa/thuong nhu Waze (giong mau) */
    ui_set_nav(s->nav != 0, s->trn, s->dst, s->exit_n, street, s->trn2);

    int rm = s->rm > 0 ? s->rm : (int)(s->rkm * 1000.0 + 0.5);
    ui_set_route(s->eta, rm, s->rmin);

    ui_set_alerts(al, n);

    /* chip: camera gan nhat > vung cam vuot */
    char chip[24];
    int kind = UI_CHIP_NONE;
    for (int i = 0; i < n; i++) {
        if (is_camera(al[i].code)) {
            if (al[i].dist >= 0) snprintf(chip, sizeof chip, "CAM %d m", al[i].dist);
            else snprintf(chip, sizeof chip, "CAM");
            kind = UI_CHIP_CAMERA;
            break;
        }
    }
    if (kind == UI_CHIP_NONE && s->avg) {
        snprintf(chip, sizeof chip, "CẤM VƯỢT %d m", s->avgL);
        kind = UI_CHIP_NOPASS;
    }
    ui_set_chip(kind, kind == UI_CHIP_NONE ? "" : chip);
}

static void on_line(const char *line, size_t len, void *user)
{
    (void)len; (void)user;
    hlp_msg_t m;
    if (!hlp_parse_line(line, &m)) return;           /* hong / v khac 1: bo qua */

    switch (m.type) {
    case HLP_MSG_HI:
        if (m.hi.sess != cur_sess) {                 /* session moi: xoa bo nho tin cu */
            cur_sess = m.hi.sess;
            have_ts = false;
        }
        if (m.hi.has_unix) {
            has_clock = true;
            clock_unix = m.hi.unix_time;
            clock_tz = m.hi.tz;
            clock_tick = lv_tick_get();
            shown_minute = -1;
            clock_synced = true;
            ui_set_clock_synced(true);
        }
        break;
    case HLP_MSG_PING:
        send_line("{\"v\":1,\"t\":\"pong\"}");       /* tra loi ngay, truoc moi viec ve */
        break;
    case HLP_MSG_STATE:
        if (have_ts && m.s.ts < last_ts) return;     /* tin cu hon: bo (cung sess) */
        have_ts = true;
        last_ts = m.s.ts;
        apply_state(&m.s);
        have_state = true;
        last_state_tick = lv_tick_get();
        break;
    case HLP_MSG_BYE:
        set_signal(false);
        return;
    default:
        break;                                       /* pong / error / loai la: chi tinh la con song */
    }
    last_rx_tick = lv_tick_get();
    set_signal(true);
}

/* so ngay tu 1/1/1970 -> ngay, thang (thuat toan civil_from_days cua H. Hinnant) */
static void civil_from_days(int64_t z, int *day, int *month)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
}

static void watch_cb(lv_timer_t *t)
{
    (void)t;
    if (signal_ok && !ext_on && lv_tick_elaps(last_rx_tick) > STALE_MS) set_signal(false);
    update_gps();

    if (has_clock) {                                 /* dong ho: moc tu `hi`, tu dem tiep */
        int64_t now = clock_unix + (int64_t)lv_tick_elaps(clock_tick) / 1000 + (int64_t)clock_tz * 60;
        int minute = (int)((now / 60) % 1440);
        if (minute != shown_minute) {
            shown_minute = minute;
            if (clock_synced && clock_hook) clock_hook(now - (int64_t)clock_tz * 60, clock_tz);
            int64_t days = now / 86400;
            int wday = (int)((days + 4) % 7);              /* 1/1/1970 la thu nam; 0 = Chu nhat */
            ui_set_time(minute / 60, minute % 60, wday);
            int d, m;
            civil_from_days(days, &d, &m);
            ui_set_date(d, m);
        }
    }
}

void hlp_dash_set_clock_guess(int64_t unix_s, int tz_min)
{
    if (clock_synced) return;
    has_clock = true;
    clock_unix = unix_s;
    clock_tz = tz_min;
    clock_tick = lv_tick_get();
    shown_minute = -1;
    ui_set_clock_synced(false);
    watch_cb(NULL);
}

void hlp_dash_set_clock_hook(void (*cb)(int64_t, int)) { clock_hook = cb; }

void hlp_dash_init(hlp_tx_cb_t tx, void *user)
{
    tx_cb = tx;
    tx_user = user;
    hlp_rx_init(&rx, on_line, NULL);
    cur_sess = -1;
    have_ts = false;
    has_clock = false;
    clock_synced = false;
    have_state = false;
    gps_level = -1;
    signal_ok = true;                                /* de set_signal(false) ben duoi chay that */
    set_signal(false);
    if (!watch_tmr) watch_tmr = lv_timer_create(watch_cb, 500, NULL);
}

void hlp_dash_on_connect(void)
{
    hold_st2[0] = 0;
    waze_seen = false;
    ui_set_phone(true);
    hlp_rx_init(&rx, on_line, NULL);                 /* bo dong dang do tu ket noi truoc */
    have_ts = false;
    have_state = false;                              /* ket noi moi: GPS vang cho den khi co tin `s` dau tien */
    /* Xin day du truong, ke ca `alrs` (opt-in). Ket thuc bang LF do transport them. */
    send_line("{\"v\":1,\"t\":\"dev\",\"name\":\"EVCarHUD\",\"fw\":\"0.1.0\",\"proto\":[1],"
              "\"disp\":{\"w\":320,\"h\":240,\"color\":1},"
              "\"can\":[\"speed\",\"limit\",\"turn\",\"street\",\"eta\",\"avgzone\",\"alerts\"],"
              "\"want\":{\"rate\":4,\"fields\":[\"nav\",\"spd\",\"lim\",\"over\",\"trn\",\"trn2\",\"dst\","
              "\"exit\",\"st\",\"st2\",\"eta\",\"rmin\",\"rm\",\"rkm\",\"avg\",\"avgL\",\"avgR\",\"avgP\","
              "\"alr\",\"alrD\",\"alrV\",\"alrS\",\"alrM\",\"alrs\"]}}");
}

void hlp_dash_on_disconnect(void)
{
    ui_set_phone(false);
    set_signal(false);
}

void hlp_dash_feed(const uint8_t *data, size_t len)
{
    hlp_rx_feed(&rx, data, len);
}

void hlp_dash_external(bool on)
{
    ext_on = on;
    if (on) {
        ui_set_phone(true);
        last_rx_tick = lv_tick_get();
        set_signal(true);
    } else {
        hlp_dash_on_disconnect();
    }
}

void hlp_dash_external_touch(void) { last_rx_tick = lv_tick_get(); }

/* App GOFA chi gui gio dia phuong (gio, phut, giay), khong co ngay: giu ngay cua dong ho dang chay (gio da luu /
 * gio build), chon ngay gan nhat de qua nua dem khong nhay sai 1 ngay. */
void hlp_dash_set_local_time(int h, int m, int sec)
{
    int64_t now = has_clock ? clock_unix + (int64_t)lv_tick_elaps(clock_tick) / 1000 + (int64_t)clock_tz * 60 : 0;
    int64_t day = now / 86400;
    int64_t t = day * 86400 + h * 3600 + m * 60 + sec;
    if (t - now > 43200) t -= 86400;
    else if (now - t > 43200) t += 86400;
    if (!has_clock) clock_tz = 7 * 60;                             /* Viet Nam UTC+7 */
    has_clock = true;
    clock_unix = t - (int64_t)clock_tz * 60;
    clock_tick = lv_tick_get();
    shown_minute = -1;
    clock_synced = true;
    ui_set_clock_synced(true);
    watch_cb(NULL);
}
