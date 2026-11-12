#include "cloud_log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define RING (64 * 1024)
#define CHUNK (24 * 1024)
#define LOG_URL "https://evcarhud.com/api/log"

static char *s_ring;                 /* PSRAM */
static size_t s_head, s_len;         /* vi tri ghi tiep, so byte dang giu */
static uint32_t s_drop;              /* byte bi de khi day */
static SemaphoreHandle_t s_mux;
static vprintf_like_t s_orig;
static TaskHandle_t s_task;
static volatile bool s_wifi, s_urgent;
static char s_line[384];
static char s_mac[18];
static uint32_t s_boot, s_seq;

static void ring_put(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (c == '\033') {                       /* bo ma mau ANSI: ESC [ ... m */
            while (i < n && p[i] != 'm') i++;
            continue;
        }
        s_ring[s_head] = c;
        s_head = (s_head + 1) % RING;
        if (s_len < RING) s_len++; else s_drop++;
    }
}

static int hook(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int r = s_orig ? s_orig(fmt, ap) : vprintf(fmt, ap);
    if (s_ring && s_mux && !xPortInIsrContext() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        xSemaphoreTake(s_mux, 0) == pdTRUE) {
        int n = vsnprintf(s_line, sizeof s_line, fmt, ap2);
        if (n > 0) {
            if (n >= (int)sizeof s_line) n = sizeof s_line - 1;
            ring_put(s_line, n);
            /* dong loi: "E (" (co the sau ma mau) */
            const char *e = s_line;
            if (*e == '\033') { e = strchr(e, 'm'); e = e ? e + 1 : s_line; }
            if (e[0] == 'E' && e[1] == ' ' && e[2] == '(') s_urgent = true;
        }
        xSemaphoreGive(s_mux);
    }
    va_end(ap2);
    return r;
}

/* Lay toi da n byte cu nhat ra buf (khong xoa); tra so byte */
static size_t ring_peek(char *buf, size_t n)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    size_t k = s_len < n ? s_len : n;
    size_t start = (s_head + RING - s_len) % RING;
    for (size_t i = 0; i < k; i++) buf[i] = s_ring[(start + i) % RING];
    xSemaphoreGive(s_mux);
    return k;
}

static void ring_drop(size_t k)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
    if (k > s_len) k = s_len;
    s_len -= k;
    xSemaphoreGive(s_mux);
}

static bool log_send(const char *body, size_t n)
{
    char url[200];
    snprintf(url, sizeof url, "%s?model=%s&mac=%s&v=%s&boot=%08lx&seq=%lu", LOG_URL, CONFIG_EVCARHUD_OTA_MODEL, s_mac,
             esp_app_get_description()->version, (unsigned long)s_boot, (unsigned long)s_seq);
    esp_http_client_config_t c = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 10000,
                                  .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return false;
    esp_http_client_set_header(h, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_post_field(h, body, (int)n);
    esp_err_t e = esp_http_client_perform(h);
    int st = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if (e == ESP_OK && st == 200) { s_seq++; return true; }
    return false;
}

static void up_task(void *a)
{
    (void)a;
    char *buf = heap_caps_malloc(CHUNK + 128, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) vTaskDelete(NULL);
    int64_t last = 0;
    int fails = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!s_wifi || !s_len) continue;
        int64_t now = esp_timer_get_time() / 1000;
        int wait = fails ? (fails > 5 ? 300000 : 60000 * fails) : 60000;
        if (!(s_urgent && now - last >= 5000 && !fails) && now - last < wait) continue;
        s_urgent = false;
        last = now;
        size_t pre = 0;
        if (s_seq == 0)
            pre = snprintf(buf, 128, "=== khoi dong, ly do reset %d, RAM chip %u, PSRAM %u ===\n", (int)esp_reset_reason(),
                           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        else if (s_drop) {
            pre = snprintf(buf, 128, "=== mat %lu byte log (vong dem day) ===\n", (unsigned long)s_drop);
            s_drop = 0;
        }
        size_t k = ring_peek(buf + pre, CHUNK);
        if (log_send(buf, pre + k)) {
            ring_drop(k);
            fails = 0;
            if (s_len) s_urgent = true;          /* con nua: gui tiep sau 5 s */
        } else {
            fails++;
        }
    }
}

void cloud_log_init(void)
{
    s_ring = heap_caps_malloc(RING, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mux = xSemaphoreCreateMutex();
    if (!s_ring || !s_mux) return;
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(s_mac, sizeof s_mac, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    s_boot = esp_random();
    s_orig = esp_log_set_vprintf(hook);
    /* ngan xep PSRAM: task nay khong doc/ghi flash (chi HTTP), tiet kiem RAM chip */
    xTaskCreateWithCaps(up_task, "cloud_log", 8192, NULL, 1, &s_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void cloud_log_wifi(bool up)
{
    s_wifi = up;
    if (up) s_urgent = true;
}
