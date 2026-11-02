#include "wifi_link.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "cJSON.h"

#include "board.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "settings.h"

static const char *TAG = "wifi";

enum { CMD_ENABLE, CMD_DISABLE, CMD_SCAN, CMD_JOIN, CMD_RETRY };
typedef struct {
    int cmd;
    char ssid[33];
    char pass[65];
} cmd_t;

static QueueHandle_t q;
static bool ready, started, connected, joining, sntp_on;
static int retries;
static char cur_ssid[33], cur_pass[65];          /* mang dang thu / dang noi */
static TaskHandle_t wx_task;
static esp_timer_handle_t retry_tmr;

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
static void h_join(const char *ssid, const char *pass) { post(CMD_JOIN, ssid, pass); }

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

/* --- thoi tiet: vi tri theo IP (ip-api.com) -> Open-Meteo, 30 phut/lan khi co mang --- */
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
    if (http_get("http://ip-api.com/json/?fields=status,city,lat,lon", buf, sizeof buf) <= 0) return false;
    cJSON *j = cJSON_Parse(buf);
    if (!j) return false;
    cJSON *la = cJSON_GetObjectItem(j, "lat"), *lo = cJSON_GetObjectItem(j, "lon"), *ci = cJSON_GetObjectItem(j, "city");
    bool ok = cJSON_IsNumber(la) && cJSON_IsNumber(lo);
    if (ok) { lat = la->valuedouble; lon = lo->valuedouble; }
    if (cJSON_IsString(ci)) snprintf(city, sizeof city, "%s", ci->valuestring);
    cJSON_Delete(j);
    if (!ok) return false;

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
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = 16;
        static wifi_ap_record_t rec[16];
        static ui_wifi_ap_t aps[16];
        if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) n = 0;
        for (int i = 0; i < n; i++) {
            snprintf(aps[i].ssid, sizeof aps[i].ssid, "%s", (const char *)rec[i].ssid);
            aps[i].rssi = rec[i].rssi;
            aps[i].lock = rec[i].authmode != WIFI_AUTH_OPEN;
        }
        board_ui_lock();
        ui_wifi_scan_done(aps, n);
        board_ui_unlock();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        bool was = connected;
        connected = false;
        if (!started) return;
        if (joining) {
            if (++retries < 3) { esp_wifi_connect(); return; }
            joining = false;
            ESP_LOGW(TAG, "khong ket noi duoc %s", cur_ssid);
            ui_state(UI_WIFI_FAIL, cur_ssid);
            char s[33], p[65];                                   /* quay lai mang da luu (neu co) sau 20 s */
            if (settings_get_wifi(s, p) && strcmp(s, cur_ssid)) esp_timer_start_once(retry_tmr, 20 * 1000000ULL);
            return;
        }
        if (was) ESP_LOGW(TAG, "mat Wi-Fi %s: thu lai sau 10 s", cur_ssid);
        ui_state(UI_WIFI_IDLE, cur_ssid);
        esp_timer_start_once(retry_tmr, 10 * 1000000ULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        retries = 0;
        if (joining) settings_set_wifi(cur_ssid, cur_pass);      /* chi luu khi ket noi duoc */
        joining = false;
        ESP_LOGI(TAG, "da ket noi %s", cur_ssid);
        ui_state(UI_WIFI_CONNECTED, cur_ssid);
        sntp_go();
        if (wx_task) xTaskNotifyGive(wx_task);
    }
}

static void retry_cb(void *arg) { (void)arg; post(CMD_RETRY, NULL, NULL); }

static void connect_to(const char *ssid, const char *pass, bool user)
{
    wifi_config_t c = {0};
    snprintf((char *)c.sta.ssid, sizeof c.sta.ssid, "%s", ssid);
    snprintf((char *)c.sta.password, sizeof c.sta.password, "%s", pass ? pass : "");
    c.sta.threshold.authmode = pass && *pass ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    snprintf(cur_ssid, sizeof cur_ssid, "%s", ssid);
    snprintf(cur_pass, sizeof cur_pass, "%s", pass ? pass : "");
    joining = true;
    retries = user ? 0 : 1;
    esp_timer_stop(retry_tmr);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &c);
    ui_state(UI_WIFI_CONNECTING, ssid);
    esp_wifi_connect();
}

static void connect_saved(void)
{
    char s[33], p[65];
    if (settings_get_wifi(s, p)) connect_to(s, p, false);
    else ui_state(UI_WIFI_IDLE, "");
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
            joining = connected = false;
            if (started) { started = false; esp_wifi_disconnect(); esp_wifi_stop(); }
            ui_state(UI_WIFI_OFF, "");
            break;
        case CMD_SCAN: {
            wifi_scan_config_t sc = {.show_hidden = false};
            if (!wifi_up() || esp_wifi_scan_start(&sc, false) != ESP_OK) {
                board_ui_lock();
                ui_wifi_scan_done(NULL, 0);
                board_ui_unlock();
            }
            break;
        }
        case CMD_JOIN:
            if (wifi_up()) connect_to(m.ssid, m.pass, true);
            else ui_state(UI_WIFI_FAIL, m.ssid);
            break;
        case CMD_RETRY:
            if (started && !connected && !joining) connect_saved();
            break;
        }
    }
}

void wifi_link_start(void)
{
    static const ui_wifi_hooks_t h = {.enable = h_enable, .scan = h_scan, .join = h_join};
    q = xQueueCreate(6, sizeof(cmd_t));
    esp_timer_create_args_t ta = {.callback = retry_cb, .name = "wifi_retry"};
    esp_timer_create(&ta, &retry_tmr);
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
