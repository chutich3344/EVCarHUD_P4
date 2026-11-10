#include "wifi_link.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "c6_update.h"
#include "cJSON.h"

#include "board.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "settings.h"
#include "ota.h"
#include "ai.h"

static const char *TAG = "wifi";

enum { CMD_ENABLE, CMD_DISABLE, CMD_SCAN, CMD_JOIN, CMD_RETRY, CMD_FORGET, CMD_SCAN_TIMEOUT, CMD_JOIN_TIMEOUT, CMD_REVERT };
typedef struct {
    int cmd;
    char ssid[33];
    char pass[65];
    bool use_saved;                              /* JOIN: lay mat khau da luu */
} cmd_t;

static QueueHandle_t q;
static bool ready, started, connected, joining, sntp_on;
static bool scanning, scan_auto, scan_user;      /* scan_auto: do de chon mang da luu manh nhat; scan_user: trang Wi-Fi dang cho */
static int retries;
static char cur_ssid[33], cur_pass[65];          /* mang dang thu / dang noi */
static char prev_ssid[33], prev_pass[65];        /* mang dang noi truoc khi doi: noi mang moi hong thi quay ve (nhu dien thoai) */
static volatile bool leaving;                     /* dang tu ngat mang cu de doi mang: su kien mat ket noi do khong tinh la loi */
static SemaphoreHandle_t left_sem;               /* bao da ngat xong mang cu */
static TaskHandle_t wx_task;
static esp_timer_handle_t retry_tmr, scan_tmr, join_tmr;
static char c6_ver[24] = "?";                   /* phien ban firmware esp_hosted tren chip C6 */
const char *wifi_link_c6_ver(void) { return c6_ver; }
static int last_reason;                          /* ma ly do mat ket noi gan nhat (wifi_err_reason_t) */
static bool got_assoc;                           /* lan noi nay da vao duoc diem phat (cho DHCP) */

static void ui_state(int st, const char *ssid)
{
    board_ui_lock();
    ui_wifi_set_state(st, ssid);
    board_ui_unlock();
}

static void post(int c, const char *ssid, const char *pass)
{
    cmd_t m = {.cmd = c};
    if (ssid) snprintf(m.ssid, sizeof m.ssid, "%s", ssid);
    if (pass) snprintf(m.pass, sizeof m.pass, "%s", pass);
    if (q) xQueueSend(q, &m, 0);
}

/* --- hook giao dien (chay trong task LVGL: chi gui lenh, khong goi Wi-Fi truc tiep) --- */
static void h_enable(bool on) { post(on ? CMD_ENABLE : CMD_DISABLE, NULL, NULL); }
static void h_scan(void) { post(CMD_SCAN, NULL, NULL); }
static void h_join(const char *ssid, const char *pass)
{
    cmd_t m = {.cmd = CMD_JOIN, .use_saved = !pass};
    snprintf(m.ssid, sizeof m.ssid, "%s", ssid);
    if (pass) snprintf(m.pass, sizeof m.pass, "%s", pass);
    if (q) xQueueSend(q, &m, 0);
}
static void h_forget(const char *ssid) { post(CMD_FORGET, ssid, NULL); }

/* --- gio NTP --- */
static void on_sntp(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP: da dong bo gio");
    board_ui_lock();
    hlp_dash_set_clock(tv->tv_sec, settings_get_tz(), UI_TSRC_NTP);
    board_ui_unlock();
}

static void sntp_go(void)
{
    if (!sntp_on) {
        esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        c.sync_cb = on_sntp;
        if (esp_netif_sntp_init(&c) == ESP_OK) sntp_on = true;
    } else {
        esp_sntp_restart();
    }
}

/* --- thoi tiet: vi tri GPS dien thoai (neu co, moi trong 3 gio) hoac theo IP (ip-api.com) -> Open-Meteo,
 *     30 phut/lan khi co mang; xe di xa hon 5 km tu cho lay lan truoc thi lay lai ngay --- */
static int http_get(const char *url, char *buf, int cap);
static portMUX_TYPE pos_mux = portMUX_INITIALIZER_UNLOCKED;
static double ph_lat, ph_lon, wx_lat = 999, wx_lon = 999;
static char ph_name[48];
static int64_t ph_us;                               /* luc nhan vi tri (esp_timer), 0 = chua co */
#define PH_FRESH_US (3LL * 3600 * 1000000)

static double km_between(double la1, double lo1, double la2, double lo2)
{
    double dy = (la2 - la1) * 111.2, dx = (lo2 - lo1) * 111.2 * cos(la1 * M_PI / 180);
    return sqrt(dx * dx + dy * dy);
}

void wifi_set_phone_pos(double lat, double lon, const char *name)
{
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) return;
    bool first;
    portENTER_CRITICAL(&pos_mux);
    first = !ph_us;
    ph_lat = lat;
    ph_lon = lon;
    snprintf(ph_name, sizeof ph_name, "%s", name ? name : "");
    ph_us = esp_timer_get_time();
    portEXIT_CRITICAL(&pos_mux);
    if (wx_task && connected && (first || km_between(wx_lat, wx_lon, lat, lon) > 5)) xTaskNotifyGive(wx_task);
}

/* ten noi tu toa do (BigDataCloud, khong can khoa) khi app khong gui ten */
static void place_name(double lat, double lon, char *out, size_t n, char *buf, int cap)
{
    char url[160];
    snprintf(url, sizeof url, "https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.4f&longitude=%.4f"
             "&localityLanguage=vi", lat, lon);
    if (http_get(url, buf, cap) <= 0) return;
    cJSON *j = cJSON_Parse(buf);
    if (!j) return;
    const char *keys[] = {"city", "locality", "principalSubdivision"};
    for (int i = 0; i < 3; i++) {
        cJSON *c = cJSON_GetObjectItem(j, keys[i]);
        if (cJSON_IsString(c) && c->valuestring[0]) { snprintf(out, n, "%s", c->valuestring); break; }
    }
    cJSON_Delete(j);
}

static int http_get(const char *url, char *buf, int cap)
{
    esp_http_client_config_t c = {.url = url, .timeout_ms = 8000, .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return -1;
    int n = -1;
    if (esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        if (esp_http_client_get_status_code(h) == 200) {
            n = 0;
            int r;
            while (n < cap - 1 && (r = esp_http_client_read(h, buf + n, cap - 1 - n)) > 0) n += r;
            buf[n] = 0;
        }
    }
    esp_http_client_cleanup(h);
    return n;
}

static int wx_kind_of(int code, const char **name, bool day)
{
    if (code <= 1) { *name = day ? "Nắng" : "Trời quang"; return day ? UI_WX_SUN : UI_WX_CLOUD; }
    if (code == 2) { *name = "Ít mây"; return day ? UI_WX_SUN : UI_WX_CLOUD; }
    if (code == 3) { *name = "Nhiều mây"; return UI_WX_CLOUD; }
    if (code == 45 || code == 48) { *name = "Sương mù"; return UI_WX_FOG; }
    if (code >= 95) { *name = "Dông"; return UI_WX_STORM; }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) { *name = "Mưa"; return UI_WX_RAIN; }
    *name = "Nhiều mây";
    return UI_WX_CLOUD;
}

static bool weather_once(void)
{
    static char buf[2048];
    char city[48] = "";
    double lat, lon;
    bool ok;
    portENTER_CRITICAL(&pos_mux);
    bool gps = ph_us && esp_timer_get_time() - ph_us < PH_FRESH_US;
    if (gps) { lat = ph_lat; lon = ph_lon; snprintf(city, sizeof city, "%s", ph_name); }
    portEXIT_CRITICAL(&pos_mux);
    cJSON *j;
    if (gps) {                                                   /* GPS dien thoai */
        if (!city[0]) place_name(lat, lon, city, sizeof city, buf, sizeof buf);
    } else {                                                     /* chua co GPS: uoc theo IP (thuong ra thanh pho cua nha mang) */
        if (http_get("http://ip-api.com/json/?fields=status,city,lat,lon", buf, sizeof buf) <= 0) return false;
        j = cJSON_Parse(buf);
        if (!j) return false;
        cJSON *la = cJSON_GetObjectItem(j, "lat"), *lo = cJSON_GetObjectItem(j, "lon"), *ci = cJSON_GetObjectItem(j, "city");
        ok = cJSON_IsNumber(la) && cJSON_IsNumber(lo);
        if (ok) { lat = la->valuedouble; lon = lo->valuedouble; }
        if (cJSON_IsString(ci)) snprintf(city, sizeof city, "%s", ci->valuestring);
        cJSON_Delete(j);
        if (!ok) return false;
    }
    ESP_LOGI(TAG, "thoi tiet theo %s: %.3f, %.3f", gps ? "GPS dien thoai" : "IP", lat, lon);

    char url[200];
    snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,weather_code,is_day&timezone=auto", lat, lon);
    if (http_get(url, buf, sizeof buf) <= 0) return false;
    j = cJSON_Parse(buf);
    if (!j) return false;
    cJSON *cur = cJSON_GetObjectItem(j, "current"), *off = cJSON_GetObjectItem(j, "utc_offset_seconds");
    cJSON *t = cur ? cJSON_GetObjectItem(cur, "temperature_2m") : NULL, *wc = cur ? cJSON_GetObjectItem(cur, "weather_code") : NULL;
    cJSON *dy = cur ? cJSON_GetObjectItem(cur, "is_day") : NULL;
    ok = cJSON_IsNumber(t) && cJSON_IsNumber(wc);
    if (ok) {
        wx_lat = lat;
        wx_lon = lon;
        const char *name;
        int kind = wx_kind_of(wc->valueint, &name, !cJSON_IsNumber(dy) || dy->valueint);
        char txt[96];
        int deg = (int)(t->valuedouble + (t->valuedouble >= 0 ? 0.5 : -0.5));
        if (city[0]) snprintf(txt, sizeof txt, "%s · %d°C · %s", city, deg, name);
        else snprintf(txt, sizeof txt, "%d°C · %s", deg, name);
        ESP_LOGI(TAG, "thoi tiet: %s", txt);
        board_ui_lock();
        ui_set_weather(kind, txt);
        board_ui_unlock();
    }
    if (cJSON_IsNumber(off)) {                                   /* mui gio noi dang dung: luu lai cho dong ho */
        int tz = off->valueint / 60;
        if (tz != settings_get_tz()) {
            settings_set_tz(tz);
            int64_t now;
            int otz;
            board_ui_lock();
            if (hlp_dash_now(&now, &otz)) hlp_dash_set_clock(now, tz, hlp_dash_clock_source());
            board_ui_unlock();
        }
    }
    cJSON_Delete(j);
    return ok;
}

static void weather_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);                 /* co mang: lay ngay */
        while (connected) {
            bool ok = weather_once();
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ok ? 30 * 60 * 1000 : 60 * 1000))) continue;
        }
    }
}

/* --- su kien Wi-Fi --- */
/* Ly do de nguoi dung tu sua (vd hotspot dien thoai chi phat 5 GHz: chip C6 chi bat duoc 2,4 GHz) */
static void fail_hint(void)
{
    char h[96];
    switch (last_reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        snprintf(h, sizeof h, "Không thấy mạng: hotspot phải bật băng tần 2,4 GHz (mã %d)", last_reason); break;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        snprintf(h, sizeof h, "Sai mật khẩu (mã %d)", last_reason); break;
    case 0:
        snprintf(h, sizeof h, got_assoc ? "Đã vào mạng nhưng không được cấp IP (DHCP)" : "Chip Wi-Fi không phản hồi"); break;
    default:
        snprintf(h, sizeof h, "Sóng yếu hoặc điểm phát từ chối (mã %d)", last_reason); break;
    }
    ui_wifi_fail_hint(h);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = 16;
        static wifi_ap_record_t rec[16];
        static ui_wifi_ap_t aps[16];
        static char ss[WIFI_SAVED_MAX][33], sp[WIFI_SAVED_MAX][65];
        scanning = false;
        esp_timer_stop(scan_tmr);
        if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) n = 0;
        ESP_LOGI(TAG, "do song xong: %u mang", n);
        int ns = settings_wifi_list(ss, sp);
        int best = -1, best_rssi = -1000;
        for (int i = 0; i < n; i++) {
            snprintf(aps[i].ssid, sizeof aps[i].ssid, "%s", (const char *)rec[i].ssid);
            aps[i].rssi = rec[i].rssi;
            aps[i].lock = rec[i].authmode != WIFI_AUTH_OPEN;
            aps[i].saved = false;
            for (int k = 0; k < ns; k++)
                if (!strcmp(ss[k], aps[i].ssid)) {
                    aps[i].saved = true;
                    if (rec[i].rssi > best_rssi) { best_rssi = rec[i].rssi; best = k; }
                }
        }
        if (scan_user) {
            scan_user = false;
            board_ui_lock();
            ui_wifi_scan_done(aps, n);
            board_ui_unlock();
        }
        if (scan_auto) {                                         /* chon mang da luu song manh nhat dang co */
            scan_auto = false;
            if (connected || joining) return;
            if (best >= 0) {
                ESP_LOGI(TAG, "mang da luu manh nhat: %s (%d dBm)", ss[best], best_rssi);
                post(CMD_JOIN, ss[best], sp[best]);
            } else {
                ESP_LOGI(TAG, "khong thay mang da luu nao: do lai sau 30 s");
                ui_state(UI_WIFI_IDLE, "");
                esp_timer_start_once(retry_tmr, 30 * 1000000ULL);
            }
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        got_assoc = true;
        ESP_LOGI(TAG, "da vao diem phat %s, cho DHCP cap IP", cur_ssid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        last_reason = d ? d->reason : 0;
        ESP_LOGW(TAG, "mat ket noi %s, ly do %d", cur_ssid, last_reason);
        bool was = connected;
        connected = false;
        if (was) ota_wifi_changed(false, NULL);
        if (was) ai_wifi_changed(false);
        if (leaving) {                                           /* minh tu ngat de doi mang: khong phai loi */
            leaving = false;
            if (left_sem) xSemaphoreGive(left_sem);
            return;
        }
        if (!started) return;
        if (joining) {
            if (++retries < 3) { esp_wifi_connect(); return; }
            joining = false;
            esp_timer_stop(join_tmr);
            ESP_LOGW(TAG, "khong ket noi duoc %s", cur_ssid);
            fail_hint();
            ui_state(UI_WIFI_FAIL, cur_ssid);
            if (prev_ssid[0]) post(CMD_REVERT, NULL, NULL);      /* quay ve mang vua dung */
            else esp_timer_start_once(retry_tmr, 20 * 1000000ULL);  /* 20 s sau: do lai, chon mang da luu manh nhat */
            return;
        }
        if (was) ESP_LOGW(TAG, "mat Wi-Fi %s: thu lai sau 10 s", cur_ssid);
        ui_state(UI_WIFI_IDLE, cur_ssid);
        esp_timer_start_once(retry_tmr, 10 * 1000000ULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        retries = 0;
        esp_timer_stop(join_tmr);
        /* HUD cam nguon xe, khong can tiet kiem dien: tat ngu modem de khong lo goi ARP/ket noi tu dien thoai
         * (trang tai firmware http://IP HUD bao ERR_ADDRESS_UNREACHABLE tren mot so router mesh) */
        esp_wifi_set_ps(WIFI_PS_NONE);
        {
            char ip[16];
            const ip_event_got_ip_t *ev = data;
            snprintf(ip, sizeof ip, IPSTR, IP2STR(&ev->ip_info.ip));
            ota_wifi_changed(true, ip);                          /* cap nhat OTA: lan dau co mang thi hoi ban moi */
            ai_wifi_changed(true);                               /* tro ly Mi Mi duoc phep mo phien */
        }
        if (joining) settings_set_wifi(cur_ssid, cur_pass);      /* chi luu khi ket noi duoc */
        joining = false;
        ESP_LOGI(TAG, "da ket noi %s", cur_ssid);
        ui_state(UI_WIFI_CONNECTED, cur_ssid);
        sntp_go();
        if (wx_task) xTaskNotifyGive(wx_task);
    }
}

static void retry_cb(void *arg) { (void)arg; post(CMD_RETRY, NULL, NULL); }

/* Do song: chu dong (active) tren moi kenh, dung duoc ca khi dang noi mang. Chip C6 (esp_hosted) doi khi khong bao
 * SCAN_DONE (vd lenh noi mang chen ngang): canh gac 12 s, het gio thi huy lan do va bao ket qua rong de trang Wi-Fi khong
 * quay mai, lan bam "Do lai" sau van chay. */
static esp_err_t scan_begin(void)
{
    wifi_scan_config_t sc = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    sc.scan_time.active.min = 80;
    sc.scan_time.active.max = 200;
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    if (e == ESP_OK) {
        scanning = true;
        esp_timer_stop(scan_tmr);
        esp_timer_start_once(scan_tmr, 12 * 1000000ULL);
    } else {
        ESP_LOGW(TAG, "khong do duoc song: %s", esp_err_to_name(e));
    }
    return e;
}

static void scan_tmr_cb(void *arg) { (void)arg; post(CMD_SCAN_TIMEOUT, NULL, NULL); }
/* Het gio noi mang: bao loi len man ngay tai day (luong Wi-Fi co the dang ket trong lenh gui C6 khong tra loi),
 * phan don dep (ngat, hen thu lai) de luong Wi-Fi lam khi ranh. */
static void join_tmr_cb(void *arg)
{
    (void)arg;
    if (joining && !connected) {
        fail_hint();
        ui_state(UI_WIFI_FAIL, cur_ssid);
    }
    post(CMD_JOIN_TIMEOUT, NULL, NULL);
}

static void connect_to(const char *ssid, const char *pass, bool user)
{
    wifi_config_t c = {0};
    snprintf((char *)c.sta.ssid, sizeof c.sta.ssid, "%s", ssid);
    snprintf((char *)c.sta.password, sizeof c.sta.password, "%s", pass ? pass : "");
    c.sta.threshold.authmode = pass && *pass ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    c.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;                       /* hotspot Android WPA3 (SAE H2E) */
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    /* Dang noi mang khac: nho lai de quay ve neu mang moi hong, ngat han roi moi noi (khong chong lenh len chip C6) */
    if (connected && strcmp(cur_ssid, ssid)) {
        snprintf(prev_ssid, sizeof prev_ssid, "%s", cur_ssid);
        snprintf(prev_pass, sizeof prev_pass, "%s", cur_pass);
    } else if (!user) {
        prev_ssid[0] = 0;                                        /* tu dong noi / quay ve: khong quay vong */
    }
    if (connected || joining) {
        joining = false;
        leaving = true;
        xSemaphoreTake(left_sem, 0);
        esp_wifi_disconnect();
        if (xSemaphoreTake(left_sem, pdMS_TO_TICKS(3000)) != pdTRUE) ESP_LOGW(TAG, "ngat mang cu qua 3 s: noi tiep");
        leaving = false;
        connected = false;
    }
    snprintf(cur_ssid, sizeof cur_ssid, "%s", ssid);
    snprintf(cur_pass, sizeof cur_pass, "%s", pass ? pass : "");
    joining = true;
    got_assoc = false;
    last_reason = 0;
    retries = user ? 0 : 1;
    esp_timer_stop(retry_tmr);
    esp_timer_stop(join_tmr);
    esp_timer_start_once(join_tmr, 30 * 1000000ULL);            /* chip C6 doi khi khong bao su kien: khong quay mai */
    esp_wifi_set_config(WIFI_IF_STA, &c);
    ui_state(UI_WIFI_CONNECTING, ssid);
    esp_wifi_connect();
}

/* Tu ket noi: do song roi vao mang da luu manh nhat (vd hotspot 5G va Wi-Fi nha cung luu: mang nao song tot hon thi vao) */
static void connect_saved(void)
{
    char s[WIFI_SAVED_MAX][33], p[WIFI_SAVED_MAX][65];
    int n = settings_wifi_list(s, p);
    if (!n) { ui_state(UI_WIFI_IDLE, ""); return; }
    scan_auto = true;
    if (scanning) return;                                        /* dang do (trang Wi-Fi): ket qua dung chung */
    if (scan_begin() == ESP_OK) return;
    scan_auto = false;
    connect_to(s[0], p[0], false);                               /* khong do duoc: thu mang luu gan nhat */
}

static bool wifi_up(void)
{
    if (!ready) {
        esp_netif_init();
        esp_err_t e = esp_event_loop_create_default();
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        e = esp_wifi_init(&cfg);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "khong khoi dong duoc Wi-Fi tren C6 (%s): can firmware esp_hosted co Wi-Fi", esp_err_to_name(e));
            return false;
        }
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        {   /* C6 phai cung doi esp_hosted voi P4, lech ban thi Wi-Fi chap chon (do song rong, TCP het gio) */
            esp_hosted_coprocessor_fwver_t fw = {0};
            if (esp_hosted_get_coprocessor_fwversion(&fw) == 0)
                snprintf(c6_ver, sizeof c6_ver, "%u.%u.%u", (unsigned)fw.major1, (unsigned)fw.minor1, (unsigned)fw.patch1);
            else
                snprintf(c6_ver, sizeof c6_ver, "không đọc được");
            ESP_LOGI(TAG, "firmware C6 (esp_hosted): %s", c6_ver);
        }
        c6_update_check();                                       /* co anh C6 kem theo (nap USB): nang C6 roi khoi dong lai */
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
        esp_wifi_set_mode(WIFI_MODE_STA);
        ready = true;
    }
    if (!started) {
        if (esp_wifi_start() != ESP_OK) return false;
        started = true;
    }
    return true;
}

static void wifi_task(void *arg)
{
    (void)arg;
    if (settings_get_wifi_on()) {
        if (wifi_up()) connect_saved();
        else ui_state(UI_WIFI_FAIL, "");
    }
    cmd_t m;
    for (;;) {
        if (xQueueReceive(q, &m, portMAX_DELAY) != pdTRUE) continue;
        switch (m.cmd) {
        case CMD_ENABLE:
            settings_set_wifi_on(true);
            if (wifi_up()) connect_saved();
            else ui_state(UI_WIFI_FAIL, "");
            break;
        case CMD_DISABLE:
            settings_set_wifi_on(false);
            esp_timer_stop(retry_tmr);
            if (connected) ota_wifi_changed(false, NULL);
            if (connected) ai_wifi_changed(false);
            joining = connected = scanning = scan_auto = scan_user = false;
            if (started) { started = false; esp_wifi_disconnect(); esp_wifi_stop(); }
            ui_state(UI_WIFI_OFF, "");
            break;
        case CMD_SCAN: {
            scan_user = true;
            if (scanning) break;                                 /* dang do tu dong: cho ket qua (co canh gac 12 s) */
            esp_err_t e = wifi_up() ? scan_begin() : ESP_FAIL;
            if (e == ESP_ERR_WIFI_STATE) {                       /* dang noi mang: do lai sau 3 s */
                vTaskDelay(pdMS_TO_TICKS(3000));
                e = scan_begin();
            }
            if (e != ESP_OK) {
                scan_user = false;
                board_ui_lock();
                ui_wifi_scan_done(NULL, 0);
                board_ui_unlock();
            }
            break;
        }
        case CMD_SCAN_TIMEOUT:
            if (!scanning) break;
            ESP_LOGW(TAG, "do song qua 12 s chua xong: huy");
            esp_wifi_scan_stop();
            scanning = false;
            if (scan_user) {
                scan_user = false;
                board_ui_lock();
                ui_wifi_scan_done(NULL, 0);
                board_ui_unlock();
            }
            if (scan_auto) { scan_auto = false; esp_timer_start_once(retry_tmr, 10 * 1000000ULL); }
            break;
        case CMD_JOIN:
            if (m.use_saved && !settings_wifi_find(m.ssid, m.pass)) m.pass[0] = 0;
            if (wifi_up()) connect_to(m.ssid, m.pass, true);
            else ui_state(UI_WIFI_FAIL, m.ssid);
            break;
        case CMD_FORGET:
            settings_wifi_forget(m.ssid);
            ESP_LOGI(TAG, "quen mang %s", m.ssid);
            break;
        case CMD_JOIN_TIMEOUT:
            if (!joining || connected) break;
            ESP_LOGW(TAG, "noi %s qua 30 s chua co IP: dung", cur_ssid);
            joining = false;
            leaving = true;                                      /* su kien ngat sau day khong hen thu lai 10 s */
            esp_wifi_disconnect();
            last_reason = 0;
            fail_hint();
            ui_state(UI_WIFI_FAIL, cur_ssid);
            if (prev_ssid[0]) post(CMD_REVERT, NULL, NULL);
            else esp_timer_start_once(retry_tmr, 20 * 1000000ULL);
            break;
        case CMD_REVERT: {
            if (!prev_ssid[0] || connected) break;
            char ss[33], pp[65];
            snprintf(ss, sizeof ss, "%s", prev_ssid);
            snprintf(pp, sizeof pp, "%s", prev_pass);
            prev_ssid[0] = 0;
            ESP_LOGI(TAG, "quay ve mang cu %s", ss);
            vTaskDelay(pdMS_TO_TICKS(1500));                     /* de nguoi dung kip doc ly do loi */
            if (!connected && !joining) connect_to(ss, pp, false);
            break;
        }
        case CMD_RETRY:
            if (started && !connected && !joining) connect_saved();
            break;
        }
    }
}

void wifi_link_start(void)
{
    static const ui_wifi_hooks_t h = {.enable = h_enable, .scan = h_scan, .join = h_join, .forget = h_forget};
    q = xQueueCreate(6, sizeof(cmd_t));
    esp_timer_create_args_t ta = {.callback = retry_cb, .name = "wifi_retry"};
    esp_timer_create(&ta, &retry_tmr);
    esp_timer_create_args_t ts = {.callback = scan_tmr_cb, .name = "wifi_scan"};
    esp_timer_create(&ts, &scan_tmr);
    esp_timer_create_args_t tj = {.callback = join_tmr_cb, .name = "wifi_join"};
    esp_timer_create(&tj, &join_tmr);
    left_sem = xSemaphoreCreateBinary();
    bool on = settings_get_wifi_on();
    char s[33], p[65];
    bool saved = settings_get_wifi(s, p);
    board_ui_lock();
    ui_set_wifi_hooks(&h);
    ui_wifi_set_state(!on ? UI_WIFI_OFF : saved ? UI_WIFI_CONNECTING : UI_WIFI_IDLE, saved ? s : "");
    board_ui_unlock();
    xTaskCreate(wifi_task, "wifi", 6144, NULL, 3, NULL);
    xTaskCreate(weather_task, "thoi_tiet", 10240, NULL, 2, &wx_task);
}
