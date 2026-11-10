/* Cap nhat firmware OTA: xem ota.h. Moi viec mang / flash chay trong task "ota" (lenh tu giao dien qua hang doi);
 * trang tai len tu trinh duyet chay trong task cua esp_http_server. Chi mot lan ghi flash tai mot thoi diem (s_busy). */
#include "ota.h"
#include "wifi_link.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "board.h"
#include "dashboard.h"

static const char *TAG = "ota";

#define CONFIRM_US   (30LL * 1000000)       /* ban moi chay on 30 giay sau khi len man chinh: xac nhan */
#define WEB_IDLE_US  (10LL * 60 * 1000000)  /* trang tai len tu tat sau 10 phut khong dung */
#define HEAD_BUF     (0x8000 + 2)           /* du de thay bang phan vung (0xAA50 o 0x8000) trong file gop */
#define APP_OFF_FULL 0x10000                /* file gop: firmware bat dau o 0x10000 */

/* ======================= ghi firmware: nhan file gop hoac app, dung dung do dai anh firmware ======================= */
typedef struct {
    esp_ota_handle_t h;
    const esp_partition_t *part;
    uint8_t *head;                          /* dem dau file de nhan dang (giai phong sau khi biet loai file) */
    size_t head_n;
    bool decided;
    size_t skip;                            /* so byte bo qua dau file (0 hoac 0x10000) */
    size_t in;                              /* byte da nhan (ca phan bo qua) */
    size_t app;                             /* byte firmware da ghi */
    /* do dai anh firmware: doc tieu de anh + tieu de tung doan khi byte di qua */
    uint8_t hb[24];
    size_t hb_n, hdr_at, hdr_need;
    int stage, seg_left;
    bool hash;
    size_t app_len;                         /* 0 = chua biet */
    char err[64];
} sink_t;

static void sink_abort(sink_t *s)
{
    if (s->h) esp_ota_abort(s->h);
    s->h = 0;
    free(s->head);
    s->head = NULL;
}

static bool sink_begin(sink_t *s)
{
    memset(s, 0, sizeof *s);
    s->hdr_need = 24;
    s->part = esp_ota_get_next_update_partition(NULL);
    if (!s->part) { snprintf(s->err, sizeof s->err, "Bảng phân vùng chưa có OTA (nạp USB file gộp)"); return false; }
    s->head = heap_caps_malloc(HEAD_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s->head) { snprintf(s->err, sizeof s->err, "Hết bộ nhớ"); return false; }
    esp_err_t e = esp_ota_begin(s->part, OTA_WITH_SEQUENTIAL_WRITES, &s->h);
    if (e != ESP_OK) {
        snprintf(s->err, sizeof s->err, "Không mở được vùng flash (%s)", esp_err_to_name(e));
        sink_abort(s);
        return false;
    }
    ESP_LOGI(TAG, "ghi vao %s (0x%" PRIx32 ")", s->part->label, s->part->address);
    return true;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Doc tieu de anh firmware (esp_image_header_t 24 byte, roi moi doan 8 byte: dia chi nap + do dai) tu cac byte firmware
 * d[0..n) bat dau o vi tri s->app. Het doan: do dai = lam tron 16 (co 1 byte checksum) + 32 byte SHA-256 neu co. */
static bool sink_parse(sink_t *s, const uint8_t *d, size_t n)
{
    size_t base = s->app;
    while (!s->app_len) {
        size_t at = s->hdr_at + s->hb_n;
        if (at >= base + n) break;                                 /* byte tieu de ke tiep chua toi */
        size_t i = at - base, take = s->hdr_need - s->hb_n;
        if (take > n - i) take = n - i;
        memcpy(s->hb + s->hb_n, d + i, take);
        s->hb_n += take;
        if (s->hb_n < s->hdr_need) break;
        if (s->stage == 0) {
            if (s->hb[0] != 0xE9 || s->hb[1] == 0 || s->hb[1] > 16) {
                snprintf(s->err, sizeof s->err, "File không phải firmware ESP32");
                return false;
            }
            s->seg_left = s->hb[1];
            s->hash = s->hb[23] == 1;
            s->hdr_at = 24;
            s->hdr_need = 8;
            s->stage = 1;
        } else {
            s->hdr_at += 8 + le32(s->hb + 4);
            if (--s->seg_left == 0) {
                s->app_len = (s->hdr_at & ~(size_t)15) + 16 + (s->hash ? 32 : 0);
                ESP_LOGI(TAG, "anh firmware %u byte", (unsigned)s->app_len);
                if (s->app_len > s->part->size) {
                    snprintf(s->err, sizeof s->err, "Firmware lớn hơn vùng OTA (%u KB)", (unsigned)(s->part->size / 1024));
                    return false;
                }
            }
        }
        s->hb_n = 0;
    }
    return true;
}

static int sink_write_app(sink_t *s, const uint8_t *d, size_t n)
{
    if (!sink_parse(s, d, n)) return -1;
    if (s->app_len && s->app + n > s->app_len) n = s->app_len - s->app;
    if (n) {
        esp_err_t e = esp_ota_write(s->h, d, n);
        if (e != ESP_OK) {
            snprintf(s->err, sizeof s->err, "Ghi flash lỗi (%s)", esp_err_to_name(e));
            return -1;
        }
        s->app += n;
    }
    return s->app_len && s->app >= s->app_len ? 1 : 0;
}

/* Nhan tiep n byte cua file. Tra ve 0: can them, 1: du firmware (bo qua phan con lai), -1: loi (s->err). */
static int sink_feed(sink_t *s, const uint8_t *d, size_t n)
{
    if (!s->decided) {                                             /* gom dau file de biet file gop hay file app */
        size_t take = HEAD_BUF - s->head_n < n ? HEAD_BUF - s->head_n : n;
        memcpy(s->head + s->head_n, d, take);
        s->head_n += take;
        s->in += take;
        d += take;
        n -= take;
        if (s->head_n < HEAD_BUF) return 0;
        if (s->head[0] != 0xE9) { snprintf(s->err, sizeof s->err, "File không phải firmware ESP32"); return -1; }
        s->decided = true;
        s->skip = s->head[0x8000] == 0xAA && s->head[0x8001] == 0x50 ? APP_OFF_FULL : 0;
        ESP_LOGI(TAG, "file %s", s->skip ? "gop (nap 0x0): bo qua bootloader + bang phan vung" : "firmware (0x10000)");
        int r = s->skip ? 0 : sink_write_app(s, s->head, s->head_n);
        free(s->head);
        s->head = NULL;
        if (r) return r;
    }
    if (s->in < s->skip) {
        size_t k = s->skip - s->in < n ? s->skip - s->in : n;
        s->in += k;
        d += k;
        n -= k;
    }
    s->in += n;
    return n ? sink_write_app(s, d, n) : 0;
}

/* Kiem tra anh (esp_ota_end: checksum + SHA-256 + dung chip), dung du an evcarhud_p4, roi chon khoi dong tu khe moi. */
static bool sink_finish(sink_t *s, char *ver, size_t vn)
{
    if (!s->app_len || s->app < s->app_len) {
        snprintf(s->err, sizeof s->err, "File thiếu dữ liệu (%u/%u KB)", (unsigned)(s->app / 1024), (unsigned)(s->app_len / 1024));
        sink_abort(s);
        return false;
    }
    esp_err_t e = esp_ota_end(s->h);
    s->h = 0;
    if (e != ESP_OK) {
        snprintf(s->err, sizeof s->err, e == ESP_ERR_OTA_VALIDATE_FAILED ? "Firmware hỏng hoặc sai chip" : "Kiểm tra lỗi (%s)",
                 esp_err_to_name(e));
        return false;
    }
    esp_app_desc_t nd;
    const esp_app_desc_t *cur = esp_app_get_description();
    if (esp_ota_get_partition_description(s->part, &nd) != ESP_OK || strcmp(nd.project_name, cur->project_name)) {
        snprintf(s->err, sizeof s->err, "Không phải firmware HUD P4 (%s)", nd.project_name);
        return false;
    }
    if (ver) snprintf(ver, vn, "%s", nd.version);
    e = esp_ota_set_boot_partition(s->part);
    if (e != ESP_OK) { snprintf(s->err, sizeof s->err, "Không đặt được vùng khởi động (%s)", esp_err_to_name(e)); return false; }
    ESP_LOGI(TAG, "da cai %s vao %s", nd.version, s->part->label);
    return true;
}

static int sink_pct(const sink_t *s, size_t total)
{
    if (s->app_len) return (int)(s->app * 100 / s->app_len);
    return total ? (int)(s->in * 100 / total) : 0;
}

/* ======================= trang thai + giao dien ======================= */
enum { CMD_OPEN, CMD_CHECK, CMD_AUTO_CHECK, CMD_INSTALL, CMD_WEB, CMD_WEB_STOP, CMD_CLOSE };

static QueueHandle_t q;
static volatile bool s_busy;                /* dang ghi flash (may chu hoac trinh duyet) */
static bool s_wifi, s_auto_done, s_open;
static char s_ip[16];
static char s_mac[18];
/* ket qua lan hoi may chu gan nhat */
static bool s_has_new;
static char s_new_ver[24], s_new_sha[65], s_new_notes[160];
static int s_new_size;
static httpd_handle_t s_web;
static char s_code[5];
static esp_timer_handle_t s_web_tmr, s_ok_tmr;
static enum { V_HOME, V_CHECKING, V_LATEST, V_NEW, V_WEB, V_FAIL } s_view;
static char s_fail[192];

static void post(int c)
{
    if (q) xQueueSend(q, &c, 0);
}

static void on_action(int act)                                    /* task LVGL: chi gui lenh */
{
    switch (act) {
    case UI_OTA_ACT_OPEN: post(CMD_OPEN); break;
    case UI_OTA_ACT_CHECK: post(CMD_CHECK); break;
    case UI_OTA_ACT_INSTALL: post(CMD_INSTALL); break;
    case UI_OTA_ACT_WEB: post(CMD_WEB); break;
    case UI_OTA_ACT_WEB_STOP: post(CMD_WEB_STOP); break;
    case UI_OTA_ACT_CLOSE: post(CMD_CLOSE); break;
    default: break;
    }
}

static const char *cur_ver(void) { return esp_app_get_description()->version; }

static void view(const ui_ota_view_t *v)
{
    board_ui_lock();
    ui_ota_view(v);
    board_ui_unlock();
}

/* ve man theo s_view (khi khong dang ghi flash) */
static void render(void)
{
    char t[96], d[512], b[48];
    ui_ota_view_t v = {.tone = UI_OTA_INFO, .title = t, .detail = d, .pct = -1};
    switch (s_view) {
    case V_HOME:
    case V_LATEST:
        if (s_view == V_LATEST) {
            v.tone = UI_OTA_OK;
            snprintf(t, sizeof t, "Đã là bản mới nhất (%s)", cur_ver());
        } else {
            snprintf(t, sizeof t, "Phiên bản đang chạy %s", cur_ver());
        }
        snprintf(b, sizeof b, "Chip Wi-Fi C6: %s", wifi_link_c6_ver());
        snprintf(d, sizeof d, "%s\nHoặc tải file .bin lên từ trình duyệt trên điện thoại / máy tính cùng mạng Wi-Fi.\n%s",
                 s_wifi ? "Kiểm tra bản mới trên máy chủ EVCarHUD qua Wi-Fi."
                        : "Chưa có Wi-Fi: kết nối Wi-Fi (hotspot điện thoại) để kiểm tra bản mới.", b);
        v.btn_a = "Kiểm tra bản mới";
        v.act_a = UI_OTA_ACT_CHECK;
        v.btn_b = "Tải lên từ trình duyệt";
        v.act_b = UI_OTA_ACT_WEB;
        break;
    case V_CHECKING:
        snprintf(t, sizeof t, "Đang kiểm tra bản mới…");
        snprintf(d, sizeof d, "Phiên bản đang chạy %s", cur_ver());
        v.spin = true;
        break;
    case V_NEW:
        v.tone = UI_OTA_NEW;
        snprintf(t, sizeof t, "Có bản mới %s", s_new_ver);
        snprintf(d, sizeof d, "Đang chạy %s · nhanh chậm tùy mạng Wi-Fi của HUD.%s%s\nNên cập nhật khi xe đứng yên, không tắt nguồn HUD.",
                 cur_ver(), s_new_notes[0] ? "\n" : "", s_new_notes);
        v.btn_a = "Cập nhật ngay";
        v.act_a = UI_OTA_ACT_INSTALL;
        v.btn_b = "Để sau";
        v.act_b = UI_OTA_ACT_CLOSE;
        break;
    case V_WEB:
        snprintf(t, sizeof t, "Mở trình duyệt, vào địa chỉ:");
        snprintf(b, sizeof b, "http://%s", s_ip);
        v.big = b;
        snprintf(d, sizeof d, "Mã xác nhận  %s\nĐiện thoại / máy tính phải cùng mạng Wi-Fi với HUD. Chọn file .bin (file gộp hoặc app-0x10000 đều được). Tự tắt sau 10 phút.", s_code);
        v.btn_b = "Dừng";
        v.act_b = UI_OTA_ACT_WEB_STOP;
        break;
    case V_FAIL:
        v.tone = UI_OTA_FAIL;
        snprintf(t, sizeof t, "Không cập nhật được");
        snprintf(d, sizeof d, "%s\nHUD vẫn chạy bản %s · chip Wi-Fi C6 %s.", s_fail, cur_ver(), wifi_link_c6_ver());
        v.btn_a = "Thử lại";
        v.act_a = UI_OTA_ACT_CHECK;
        v.btn_b = "Tải lên từ trình duyệt";
        v.act_b = UI_OTA_ACT_WEB;
        break;
    }
    view(&v);
}

static void fail(const char *why)
{
    ESP_LOGW(TAG, "%s", why);
    snprintf(s_fail, sizeof s_fail, "%s", why);
    s_view = V_FAIL;
    render();
}

static void progress(const char *title, int pct, const char *detail)
{
    ui_ota_view_t v = {.tone = UI_OTA_BUSY, .title = title, .detail = detail, .pct = pct, .lock = true};
    view(&v);
}

static void reboot_after_install(const char *ver)
{
    char t[64];
    snprintf(t, sizeof t, "Đã cài bản %s", ver);
    ui_ota_view_t v = {.tone = UI_OTA_OK, .title = t, .pct = 100, .lock = true,
                       .detail = "Khởi động lại sau 3 giây…\nBản mới lỗi thì HUD tự quay về bản cũ."};
    view(&v);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

/* ======================= kenh 1: may chu evcarhud-web ======================= */
static void server_url(char *u, size_t n, bool download)
{
    char sha[65] = "";                                             /* SHA file da cai qua may chu lan truoc (neu co) */
    size_t sn = sizeof sha;
    nvs_handle_t nh;
    if (nvs_open("ota", NVS_READONLY, &nh) == ESP_OK) {
        if (nvs_get_str(nh, "sha", sha, &sn) != ESP_OK) sha[0] = 0;
        nvs_close(nh);
    }
    snprintf(u, n, "%s?model=%s&mac=%s&v=%s%s%s%s", CONFIG_EVCARHUD_OTA_URL, CONFIG_EVCARHUD_OTA_MODEL, s_mac, cur_ver(),
             sha[0] ? "&sha=" : "", sha, download ? "&download=1" : "");
}

/* Chi tiet loi ket noi (ESP_ERR_HTTP_CONNECT chung chung): loi esp-tls + ma mbedTLS + RAM trong chip con lai */
static void tls_why(esp_http_client_handle_t h, char *out, size_t n)
{
    int code = 0, flags = 0;
    esp_err_t te = esp_http_client_get_and_clear_last_tls_error(h, &code, &flags);
    snprintf(out, n, "%s, -0x%04x/%x, RAM %uK", te ? esp_err_to_name(te) : "-", (unsigned)(-code) & 0xFFFF, (unsigned)flags,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

/* Hoi may chu ban moi nhat. true: hoi duoc (s_has_new co ban moi hay khong); false: loi (why) */
static bool server_check(char *why, size_t wn)
{
    char url[300];
    server_url(url, sizeof url, false);
    esp_http_client_config_t c = {.url = url, .timeout_ms = 10000, .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { snprintf(why, wn, "Hết bộ nhớ"); return false; }
    static char buf[1024];
    int n = -1, st = 0;
    esp_err_t oe = ESP_FAIL;
    for (int i = 0; i < 3; i++) {                                  /* DNS/TLS loi thoang qua (hotspot dien thoai): thu lai */
        if ((oe = esp_http_client_open(h, 0)) == ESP_OK) break;
        ESP_LOGW(TAG, "mo ket noi kiem tra loi %s, thu lai", esp_err_to_name(oe));
        esp_http_client_close(h);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    wifi_link_net_result(oe == ESP_OK);
    if (oe == ESP_OK) {
        esp_http_client_fetch_headers(h);
        st = esp_http_client_get_status_code(h);
        n = 0;
        for (int idle = 0; n < (int)sizeof buf - 1 && idle < 200;) {   /* EAGAIN (TLS chua du goi): doc lai, toi da ~10 s */
            int r = esp_http_client_read(h, buf + n, sizeof buf - 1 - n);
            if (r > 0) { n += r; idle = 0; }
            else if (r == -ESP_ERR_HTTP_EAGAIN && !esp_http_client_is_complete_data_received(h)) { idle++; vTaskDelay(pdMS_TO_TICKS(50)); }
            else break;
        }
        buf[n] = 0;
    }
    char tw[96] = "";
    if (n < 0) tls_why(h, tw, sizeof tw);
    esp_http_client_cleanup(h);
    if (n < 0) { snprintf(why, wn, "Không kết nối được máy chủ (%s; %s)", esp_err_to_name(oe), tw); ESP_LOGW(TAG, "%s", why); return false; }
    cJSON *j = cJSON_Parse(buf);
    cJSON *e = j ? cJSON_GetObjectItem(j, "error") : NULL;
    if (st != 200) {
        if (cJSON_IsString(e)) snprintf(why, wn, "%s", e->valuestring);
        else snprintf(why, wn, "Máy chủ báo lỗi %d", st);
        cJSON_Delete(j);
        return false;
    }
    cJSON *ver = j ? cJSON_GetObjectItem(j, "version") : NULL, *sha = j ? cJSON_GetObjectItem(j, "sha256") : NULL;
    cJSON *sz = j ? cJSON_GetObjectItem(j, "size") : NULL, *no = j ? cJSON_GetObjectItem(j, "notes") : NULL;
    cJSON *up = j ? cJSON_GetObjectItem(j, "update") : NULL;
    bool ok = cJSON_IsString(ver) && cJSON_IsNumber(sz);
    if (ok) {
        snprintf(s_new_ver, sizeof s_new_ver, "%s", ver->valuestring);
        snprintf(s_new_sha, sizeof s_new_sha, "%s", cJSON_IsString(sha) ? sha->valuestring : "");
        snprintf(s_new_notes, sizeof s_new_notes, "%s", cJSON_IsString(no) ? no->valuestring : "");
        s_new_size = sz->valueint;
        /* may chu quyet dinh (khac phien ban, hoac cung phien ban nhung file da bi cai de); khong co truong nay thi so phien ban */
        s_has_new = cJSON_IsBool(up) ? cJSON_IsTrue(up) : strcmp(s_new_ver, cur_ver()) != 0;
        ESP_LOGI(TAG, "may chu: ban %s (%d byte)%s", s_new_ver, s_new_size, s_has_new ? " -> co ban moi" : "");
    } else {
        snprintf(why, wn, "Máy chủ trả dữ liệu lạ");
    }
    cJSON_Delete(j);
    return ok;
}

/* Mo ket noi tai file tu byte 'from' (from > 0: noi tiep bang Range, may chu phai tra 206) */
static esp_http_client_handle_t dl_open(const char *url, size_t from, int64_t *len, char *why, size_t wn)
{
    esp_http_client_config_t c = {.url = url, .timeout_ms = 15000, .buffer_size = 4096,
                                  .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) { snprintf(why, wn, "Hết bộ nhớ"); return NULL; }
    if (from) {
        char r[40];
        snprintf(r, sizeof r, "bytes=%u-", (unsigned)from);
        esp_http_client_set_header(h, "Range", r);
    }
    esp_err_t oe = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        if ((oe = esp_http_client_open(h, 0)) == ESP_OK) break;
        ESP_LOGW(TAG, "mo ket noi tai file loi %s, thu lai", esp_err_to_name(oe));
        esp_http_client_close(h);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    wifi_link_net_result(oe == ESP_OK);
    if (oe != ESP_OK) {
        snprintf(why, wn, "Không kết nối được máy chủ khi tải (%s)", esp_err_to_name(oe));
        esp_http_client_cleanup(h);
        return NULL;
    }
    *len = esp_http_client_fetch_headers(h);
    int st = esp_http_client_get_status_code(h);
    if (st != (from ? 206 : 200)) {
        snprintf(why, wn, from && st == 200 ? "Máy chủ không cho tải nối tiếp" : "Máy chủ báo lỗi %d khi tải file", st);
        esp_http_client_cleanup(h);
        return NULL;
    }
    return h;
}

static void server_install(void)
{
    char url[300], why[192] = "";
    server_url(url, sizeof url, true);
    static sink_t s;
    uint8_t *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_http_client_handle_t h = NULL;
    bool ok = false, begun = false;
    char ver[32] = "";
    int64_t total = 0, len = 0;
    s_busy = true;
    progress("Đang tải bản mới…", 0, "Không tắt nguồn HUD");
    if (!buf) { snprintf(why, sizeof why, "Hết bộ nhớ"); goto out; }
    if (!(h = dl_open(url, 0, &total, why, sizeof why))) goto out;
    if (!sink_begin(&s)) { snprintf(why, sizeof why, "%s", s.err); goto out; }
    begun = true;
    int last = -1, r = 0, tries = 0;
    char code[32] = "";
    int64_t idle = esp_timer_get_time();
    for (;;) {
        int n = esp_http_client_read(h, (char *)buf, 4096);
        if (n > 0) {
            idle = esp_timer_get_time();
            tries = 0;
            r = sink_feed(&s, buf, n);
            if (r < 0) { snprintf(why, sizeof why, "%s", s.err); break; }
            int p = sink_pct(&s, total > 0 ? (size_t)total : 0);
            if (p != last) {
                last = p;
                char d[64];
                snprintf(d, sizeof d, "%u / %u KB · không tắt nguồn HUD", (unsigned)(s.app / 1024),
                         (unsigned)((s.app_len ? s.app_len : (size_t)(total > 0 ? total : 0)) / 1024));
                progress("Đang tải và ghi bản mới…", p, d);
            }
            if (r == 1) break;                                     /* du firmware: phan sau cua file gop (mo hinh...) bo qua */
            continue;
        }
        if (n == 0 && esp_http_client_is_complete_data_received(h)) break;
        /* TLS chua du mot goi (EAGAIN) la binh thuong: doc lai, chi coi la dut khi 20 giay khong co byte nao */
        if ((n == -ESP_ERR_HTTP_EAGAIN || n == 0) && esp_timer_get_time() - idle < 20000000) {
            if (n == 0) vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        /* Dut that: noi lai tu byte da nhan, toi da 5 lan lien tiep */
        int en = esp_http_client_get_errno(h);
        ESP_LOGW(TAG, "tai bi dut o %u byte (read=%d errno=%d), noi lai lan %d", (unsigned)s.in, n, en, tries + 1);
        snprintf(code, sizeof code, "mã %d/%d", n, en);
        esp_http_client_cleanup(h);
        h = NULL;
        while (!h && tries < 5) {
            vTaskDelay(pdMS_TO_TICKS(1000 * ++tries));
            why[0] = 0;
            h = dl_open(url, s.in, &len, why, sizeof why);
            if (!h && strcmp(why, "Máy chủ không cho tải nối tiếp") == 0) break;
        }
        if (!h) {
            if (strcmp(why, "Máy chủ không cho tải nối tiếp") != 0)
                snprintf(why, sizeof why, "Mất kết nối khi tải (%u KB, %s)", (unsigned)(s.in / 1024), code);
            break;
        }
        idle = esp_timer_get_time();
    }
    if (why[0]) { if (begun) sink_abort(&s); goto out; }
    progress("Đang kiểm tra firmware…", 100, "Không tắt nguồn HUD");
    ok = sink_finish(&s, ver, sizeof ver);
    if (!ok) snprintf(why, sizeof why, "%s", s.err);
out:
    if (h) esp_http_client_cleanup(h);
    free(buf);
    if (ok) {
        nvs_handle_t nh;                                           /* SHA file vua cai: may chu biet HUD da chay dung file nao */
        if (nvs_open("ota", NVS_READWRITE, &nh) == ESP_OK) {
            nvs_set_str(nh, "sha", s_new_sha);
            nvs_commit(nh);
            nvs_close(nh);
        }
        reboot_after_install(ver);
    }
    s_busy = false;
    fail(why);
}

/* ======================= kenh 2: trang tai len tu trinh duyet ======================= */
static const char PAGE[] =
"<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>EVCarHUD P4 - cập nhật firmware</title><style>"
"body{font-family:system-ui,sans-serif;background:#0b1a12;color:#e8fff1;margin:0;padding:24px 16px;max-width:520px;margin:auto}"
"h1{font-size:20px;color:#34d399;margin:0 0 4px}p{color:#9fbfae;font-size:14px;line-height:1.5}"
"label{display:block;margin:16px 0 6px;font-size:14px}input{width:100%;box-sizing:border-box;font-size:16px;padding:10px;"
"border-radius:10px;border:1px solid #2f5a43;background:#11261a;color:#fff}"
"button{margin-top:20px;width:100%;padding:14px;font-size:16px;border:0;border-radius:12px;background:#22c55e;color:#04120a;font-weight:700}"
"button:disabled{opacity:.5}#bar{height:10px;background:#11261a;border-radius:5px;margin-top:18px;overflow:hidden}"
"#fill{height:100%;width:0;background:#22c55e}#msg{margin-top:12px;font-size:15px}</style>"
"<h1>Cập nhật firmware HUD</h1><p>Đang chạy bản <b>%VER%</b>. Chọn file .bin (file gộp nạp 0x0 hoặc file app-0x10000 đều được)."
" Không tắt nguồn HUD khi đang tải.</p>"
"<label>Mã xác nhận (hiện trên HUD)</label><input id=code inputmode=numeric maxlength=4>"
"<label>File firmware</label><input id=f type=file accept='.bin,application/octet-stream'>"
"<button id=go>Tải lên và cài</button><div id=bar><div id=fill></div></div><div id=msg></div><script>"
"go.onclick=function(){var f=document.getElementById('f').files[0];if(!f){msg.textContent='Chưa chọn file';return}"
"go.disabled=true;msg.textContent='Đang tải lên…';var x=new XMLHttpRequest();"
"x.open('POST','/update?code='+encodeURIComponent(code.value));"
"x.upload.onprogress=function(e){if(e.lengthComputable)fill.style.width=(e.loaded*100/e.total)+'%'};"
"x.onload=function(){msg.textContent=x.responseText;go.disabled=x.status==200};"
"x.onerror=function(){msg.textContent='Mất kết nối với HUD';go.disabled=false};x.send(f)}</script>";

static esp_err_t web_page(httpd_req_t *r)
{
    esp_timer_stop(s_web_tmr);
    esp_timer_start_once(s_web_tmr, WEB_IDLE_US);
    const char *mark = strstr(PAGE, "%VER%");
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_send_chunk(r, PAGE, mark - PAGE);
    httpd_resp_send_chunk(r, cur_ver(), HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(r, mark + 5, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t web_reply(httpd_req_t *r, const char *status, const char *msg)
{
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(r, msg);
}

static esp_err_t web_update(httpd_req_t *r)
{
    char qs[48], code[8] = "";
    if (httpd_req_get_url_query_str(r, qs, sizeof qs) == ESP_OK) httpd_query_key_value(qs, "code", code, sizeof code);
    if (strcmp(code, s_code)) return web_reply(r, "403 Forbidden", "Sai mã xác nhận (xem trên màn hình HUD)");
    if (s_busy) return web_reply(r, "409 Conflict", "HUD đang cập nhật, đợi xong rồi thử lại");
    if (r->content_len < HEAD_BUF) return web_reply(r, "400 Bad Request", "File quá nhỏ, không phải firmware");
    esp_timer_stop(s_web_tmr);
    s_busy = true;
    static sink_t s;
    static uint8_t buf[4096];
    char why[192] = "", ver[32] = "";
    size_t left = r->content_len;
    int last = -1, res = 0;
    progress("Đang nhận firmware từ trình duyệt…", 0, "Không tắt nguồn HUD");
    if (!sink_begin(&s)) snprintf(why, sizeof why, "%s", s.err);
    while (!why[0] && left > 0) {
        int n = httpd_req_recv(r, (char *)buf, left < sizeof buf ? left : sizeof buf);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { snprintf(why, sizeof why, "Mất kết nối khi tải lên"); break; }
        left -= n;
        if (res == 1) continue;                                    /* du firmware: doc not phan con lai cua file gop */
        res = sink_feed(&s, buf, n);
        if (res < 0) { snprintf(why, sizeof why, "%s", s.err); break; }
        int p = sink_pct(&s, r->content_len);
        if (p != last) {
            last = p;
            char d[64];
            snprintf(d, sizeof d, "%u KB · không tắt nguồn HUD", (unsigned)(s.app / 1024));
            progress("Đang nhận firmware từ trình duyệt…", p, d);
        }
    }
    if (!why[0]) {
        progress("Đang kiểm tra firmware…", 100, "Không tắt nguồn HUD");
        if (!sink_finish(&s, ver, sizeof ver)) snprintf(why, sizeof why, "%s", s.err);
    } else {
        sink_abort(&s);
    }
    if (why[0]) {
        s_busy = false;
        web_reply(r, "400 Bad Request", why);
        esp_timer_start_once(s_web_tmr, WEB_IDLE_US);
        fail(why);
        return ESP_OK;
    }
    char m[96];
    snprintf(m, sizeof m, "Đã cài bản %s. HUD khởi động lại sau 3 giây.", ver);
    web_reply(r, "200 OK", m);
    nvs_handle_t nh;                                               /* file tai len tay: khong biet SHA tren may chu */
    if (nvs_open("ota", NVS_READWRITE, &nh) == ESP_OK) {
        nvs_erase_key(nh, "sha");
        nvs_commit(nh);
        nvs_close(nh);
    }
    reboot_after_install(ver);
    return ESP_OK;
}

static void web_stop(void)
{
    if (s_web && !s_busy) {
        httpd_stop(s_web);
        s_web = NULL;
        ESP_LOGI(TAG, "dung trang tai len");
    }
    esp_timer_stop(s_web_tmr);
}

static void web_idle_cb(void *a)
{
    (void)a;
    post(CMD_WEB_STOP);
}

static bool web_start(char *why, size_t wn)
{
    if (!s_wifi || !s_ip[0]) { snprintf(why, wn, "Chưa có Wi-Fi: kết nối Wi-Fi cùng mạng với điện thoại trước"); return false; }
    snprintf(s_code, sizeof s_code, "%04u", (unsigned)(esp_random() % 10000));
    if (!s_web) {
        httpd_config_t c = HTTPD_DEFAULT_CONFIG();
        c.stack_size = 8192;
        c.recv_wait_timeout = 15;
        c.max_uri_handlers = 4;
        if (httpd_start(&s_web, &c) != ESP_OK) { s_web = NULL; snprintf(why, wn, "Không mở được trang tải lên"); return false; }
        static const httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = web_page};
        static const httpd_uri_t upd = {.uri = "/update", .method = HTTP_POST, .handler = web_update};
        httpd_register_uri_handler(s_web, &page);
        httpd_register_uri_handler(s_web, &upd);
    }
    esp_timer_stop(s_web_tmr);
    esp_timer_start_once(s_web_tmr, WEB_IDLE_US);
    ESP_LOGI(TAG, "trang tai len: http://%s (ma %s)", s_ip, s_code);
    return true;
}

/* ======================= task ======================= */
static void check(bool manual)
{
    char why[192] = "";
    if (!s_wifi) {
        if (manual) fail("Chưa có Wi-Fi. Vào Cài đặt > Kết nối Wi-Fi (hotspot điện thoại) rồi thử lại.");
        return;
    }
    if (manual) { s_view = V_CHECKING; render(); }
    bool ok = server_check(why, sizeof why);
    board_ui_lock();
    ui_ota_badge(ok && s_has_new ? s_new_ver : NULL);
    if (ok && s_has_new && !manual) {
        char t[80];
        snprintf(t, sizeof t, "Có firmware mới %s: Cài đặt > Hệ thống", s_new_ver);
        ui_toast(t);
    }
    board_ui_unlock();
    if (!manual) { if (!ok) ESP_LOGW(TAG, "tu kiem tra: %s", why); return; }
    if (!ok) { fail(why); return; }
    s_view = s_has_new ? V_NEW : V_LATEST;
    render();
}

static void ota_task(void *arg)
{
    (void)arg;
    int c;
    for (;;) {
        if (xQueueReceive(q, &c, portMAX_DELAY) != pdTRUE) continue;
        if (s_busy) continue;                                      /* dang ghi flash tu trinh duyet */
        switch (c) {
        case CMD_OPEN:
            s_open = true;
            if (s_view == V_CHECKING) s_view = V_HOME;
            if (s_view == V_WEB && !s_web) s_view = V_HOME;
            if (s_view == V_FAIL) s_view = V_HOME;
            if (s_has_new && s_view == V_HOME) s_view = V_NEW;
            render();
            break;
        case CMD_CHECK: check(true); break;
        case CMD_AUTO_CHECK: check(false); break;
        case CMD_INSTALL:
            if (!s_has_new) { check(true); break; }
            server_install();
            break;
        case CMD_WEB: {
            char why[192] = "";
            if (web_start(why, sizeof why)) { s_view = V_WEB; render(); }
            else fail(why);
            break;
        }
        case CMD_WEB_STOP:
            web_stop();
            if (s_view == V_WEB) { s_view = V_HOME; if (s_open) render(); }
            break;
        case CMD_CLOSE:
            s_open = false;
            board_ui_lock();
            ui_ota_close();
            board_ui_unlock();
            break;
        }
    }
}

void ota_wifi_changed(bool up, const char *ip)
{
    s_wifi = up;
    snprintf(s_ip, sizeof s_ip, "%s", up && ip ? ip : "");
    if (!up) { post(CMD_WEB_STOP); return; }
    if (!s_auto_done) {                                            /* moi lan bat may hoi may chu 1 lan */
        s_auto_done = true;
        post(CMD_AUTO_CHECK);
    }
}

/* ======================= xac nhan ban moi / bao quay ve ======================= */
static void confirm_cb(void *a)
{
    (void)a;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "ban %s chay on: da xac nhan, khong quay ve ban cu nua", cur_ver());
        char t[64];
        snprintf(t, sizeof t, "Đã cập nhật lên bản %s", cur_ver());
        board_ui_lock();
        ui_toast(t);
        board_ui_unlock();
    }
}

static void report_rollback(void)
{
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t d;
    if (!bad || esp_ota_get_partition_description(bad, &d) != ESP_OK) return;
    nvs_handle_t nh;                                               /* chi bao 1 lan cho moi ban loi */
    char seen[33] = "";
    size_t n = sizeof seen;
    if (nvs_open("ota", NVS_READWRITE, &nh) != ESP_OK) return;
    nvs_get_str(nh, "bad", seen, &n);
    if (strcmp(seen, d.version)) {
        nvs_set_str(nh, "bad", d.version);
        nvs_commit(nh);
        char t[160];
        snprintf(t, sizeof t, "Bản %s lỗi khi khởi động, đã quay về %s", d.version, cur_ver());
        ESP_LOGW(TAG, "%s", t);
        board_ui_lock();
        ui_toast(t);
        board_ui_unlock();
    }
    nvs_close(nh);
}

void ota_start(void)
{
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(s_mac, sizeof s_mac, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    ESP_LOGI(TAG, "dang chay %s tu %s", cur_ver(), run ? run->label : "?");
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "ban moi chay lan dau: xac nhan sau 30 giay (sap truoc do thi quay ve ban cu)");
        esp_timer_create_args_t a = {.callback = confirm_cb, .name = "ota_ok"};
        esp_timer_create(&a, &s_ok_tmr);
        esp_timer_start_once(s_ok_tmr, CONFIRM_US);
    } else {
        report_rollback();
    }
    esp_timer_create_args_t w = {.callback = web_idle_cb, .name = "ota_web"};
    esp_timer_create(&w, &s_web_tmr);
    q = xQueueCreate(6, sizeof(int));
    board_ui_lock();
    ui_set_ota_hook(on_action);
    board_ui_unlock();
    xTaskCreate(ota_task, "ota", 8192, NULL, 3, NULL);
}
