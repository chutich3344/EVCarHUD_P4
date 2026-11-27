/* VietMap Live (VML) tren HUD P4 qua mach cau noi ESP32 DevKit (vietmap-hud-test/VML_BRIDGE).
 *
 * P4 chi co BLE (chip C6) nen VML (Bluetooth thuong SPP) noi vao mach cau noi; mach do tu tra loi goi hoi thong tin
 * va chuyen nguyen khung VML sang day UART: cau noi TX -> P4 GPIO5 (RX), cau noi RX <- P4 GPIO4 (TX), 921600.
 * Giao thuc giai tu goi that (ban 2.8" EVCarHUD_S3 main/vml_link.c):
 *   khung: A5 5A 37 C3 | do dai BE32 (tong = do dai + 8) | ... | lenh o byte 12-13 | ... | 8 byte duoi
 *   01 01, 29 byte: byte 20 = bien gioi han hien tai (VML chi gui khi doi). Loai 25 byte "86 A0 xx": chua ro (toc do?), chi log.
 *   40 FE: chi dan. BE32 do dai anh o byte 14 (=1: khong anh), JPEG 60x60; sau anh: met BE32 + 1 byte do dai nhan + nhan UTF-8.
 *          Nhan "Tốc độ tiếp theo" = bien ke tiep; anh xanh duong = mui ten re (nhan = ten duong); anh khac = canh bao.
 *   FF 03 (khung rieng cua mach cau noi, moi 1 s): byte 14 = 1 VML dang noi.
 * VML chi gui khi co thay doi va hay ngat / noi lai ma khong gui lai -> giu chi dan lau, chi xoa khi mat ket noi 10 phut.
 * Toc do: VML khong gui; neu app LANE / Waze noi BLE cung luc thi toc do lay tu do (dashboard_hlp: hlp_dash_vml). */
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/jpeg_decode.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "dashboard.h"
#include "dashboard_hlp.h"
#include "vml_uart.h"

static const char *TAG = "vml";

#define VML_UART UART_NUM_1
#define VML_RX 5
#define VML_TX 4
#define VML_BAUD 921600
#define PKT_MAX 16384
#define IMG 60                       /* anh VML 60x60 */
#define DEC 64                       /* bo giai ma phan cung xuat theo khoi 16 -> 64x64 */
#define ARW 64                       /* mui ten giao dien P4: anh 64 px */
#define SGN 48                       /* bien / canh bao giao dien P4: anh 48 px */
#define TURN_MS 600000
#define SIGN_MS 300000
#define WARN_MS 20000
#define GRACE_MS 600000              /* VML ngat qua 10 phut moi xoa */
#define BRIDGE_MS 3000               /* khong co khung trang thai 3 s: mat mach cau noi */

static const char NEXT_LIMIT[] = "Tốc độ tiếp theo";

/* ---------- anh ---------- */
static jpeg_decoder_handle_t s_jpeg;
static uint8_t *s_jin;
static size_t s_jin_cap;
static uint16_t *s_work;             /* RGB565 64x64 (vung 60x60 o goc tren trai) */
static size_t s_work_cap;
static uint8_t *s_nav_px;            /* A8 64x64 */
static uint8_t *s_sign_px, *s_warn_px;   /* RGB565A8 48x48 */
static lv_image_dsc_t s_nav_dsc, s_sign_dsc, s_warn_dsc;

static bool decode(const uint8_t *jpg, size_t n)
{
    if (!s_jpeg || n > s_jin_cap) return false;
    memcpy(s_jin, jpg, n);
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(s_jin, n, &info) != ESP_OK || info.width != IMG || info.height != IMG) {
        ESP_LOGW(TAG, "JPEG khong phai 60x60 (%ux%u)", (unsigned)info.width, (unsigned)info.height);
        return false;
    }
    jpeg_decode_cfg_t c = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out = 0;
    esp_err_t e = jpeg_decoder_process(s_jpeg, &c, s_jin, n, (uint8_t *)s_work, s_work_cap, &out);
    if (e != ESP_OK) ESP_LOGW(TAG, "giai ma JPEG loi %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static inline void rgb(uint16_t c, int *r, int *g, int *b)
{
    *r = ((c >> 11) & 31) << 3;
    *g = ((c >> 5) & 63) << 2;
    *b = (c & 31) << 3;
}

static uint16_t px_at(int x, int y) { return s_work[y * DEC + x]; }   /* x, y trong 0..59 */

static bool is_arrow(void)
{
    unsigned blue = 0, red = 0;
    for (int y = 0; y < IMG; y++)
        for (int x = 0; x < IMG; x++) {
            int r, g, b;
            rgb(px_at(x, y), &r, &g, &b);
            if (b > 120 && b > r + 60 && b > g + 16) ++blue;
            if (r > 120 && r > b + 60 && r > g + 60) ++red;
        }
    return blue >= 60 && red < 30;
}

/* Mui ten -> mat na A8 64x64 (giao dien to theo mau chu); phong 60 -> 64 theo diem gan nhat */
static void to_mask(void)
{
    for (int y = 0; y < ARW; y++)
        for (int x = 0; x < ARW; x++) {
            int r, g, b;
            rgb(px_at(x * IMG / ARW, y * IMG / ARW), &r, &g, &b);
            int a = (b - (r > g ? r : g)) * 2;
            s_nav_px[y * ARW + x] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
        }
}

/* Bien / canh bao -> RGB565A8 48x48 (thu 60 -> 48 lay trung binh 2x2 gan dung); round: mat na tron cho bien */
static void to_sign(uint8_t *dst, bool round)
{
    uint16_t *c = (uint16_t *)dst;
    uint8_t *a = dst + SGN * SGN * 2;
    for (int y = 0; y < SGN; y++)
        for (int x = 0; x < SGN; x++) {
            int sx = x * IMG / SGN, sy = y * IMG / SGN, sx2 = sx + 1 < IMG ? sx + 1 : sx, sy2 = sy + 1 < IMG ? sy + 1 : sy;
            int R = 0, G = 0, B = 0;
            uint16_t q[4] = {px_at(sx, sy), px_at(sx2, sy), px_at(sx, sy2), px_at(sx2, sy2)};
            for (int i = 0; i < 4; i++) {
                int r, g, b;
                rgb(q[i], &r, &g, &b);
                R += r; G += g; B += b;
            }
            R /= 4; G /= 4; B /= 4;
            c[y * SGN + x] = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
            uint8_t al = 255;
            if (round) {
                float dx = x + 0.5f - SGN / 2.0f, dy = y + 0.5f - SGN / 2.0f;
                float d = SGN / 2.0f - __builtin_sqrtf(dx * dx + dy * dy);
                al = d >= 1 ? 255 : d <= 0 ? 0 : (uint8_t)(d * 255);
            }
            a[y * SGN + x] = al;
        }
}

static void dsc_init(lv_image_dsc_t *d, void *px, lv_color_format_t cf, int w)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = cf;
    d->header.w = w;
    d->header.h = w;
    d->header.stride = cf == LV_COLOR_FORMAT_A8 ? w : w * 2;
    d->data_size = cf == LV_COLOR_FORMAT_A8 ? w * w : w * w * 3;
    d->data = px;
}

/* ---------- trang thai ---------- */
static bool s_on;                    /* VML dang noi qua cau noi */
static bool s_turn, s_sign, s_warn;
static uint32_t s_turn_m, s_sign_m, s_warn_m;
static int64_t s_turn_at, s_sign_at, s_warn_at, s_lost_at, s_bridge_at;
static int s_last_lim = -1;
static char s_street[96], s_last_text[96];
static bool s_bridge;                /* da thay mach cau noi */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void push_alerts(void)        /* trong khoa UI */
{
    ui_alert_t a;
    if (s_warn) {
        a = (ui_alert_t){3, (int)s_warn_m, 0};
        ui_set_alert_icon(&s_warn_dsc);
        ui_set_alerts(&a, 1);
    } else if (s_sign) {
        a = (ui_alert_t){8, (int)s_sign_m, -1};
        ui_set_alert_icon(&s_sign_dsc);
        ui_set_alerts(&a, 1);
    } else {
        ui_set_alerts(NULL, 0);
        ui_set_alert_icon(NULL);
    }
}

static void set_on(bool on)          /* trong khoa UI */
{
    if (on == s_on) return;
    s_on = on;
    ESP_LOGI(TAG, "VML %s", on ? "da noi (qua cau noi)" : "het (xoa du lieu)");
    hlp_dash_vml(on);
    if (on) {
        ui_toast("VietMap Live đã nối");
    } else {
        s_turn = s_sign = s_warn = false;
        s_last_lim = -1;
        s_last_text[0] = 0;
        ui_set_nav_icon(NULL);
        ui_set_alert_icon(NULL);
    }
}

static void on_guidance(const uint8_t *p, size_t size)
{
    if (size < 32) return;
    size_t end = size - 8, meta = 19;
    uint32_t len = be32(p + 14);
    const uint8_t *jpg = NULL;
    if (len != 1) {
        if (len < 4 || len > end - 18) return;
        meta = 18 + len;
        if (p[18] != 0xFF || p[19] != 0xD8 || p[meta - 2] != 0xFF || p[meta - 1] != 0xD9) return;
        jpg = p + 18;
    } else if (p[18] != 1) {
        return;
    }
    if (end - meta < 5) return;
    uint32_t m = be32(p + meta);
    size_t ln = p[meta + 4];
    if (m == UINT32_MAX || !ln || ln > end - meta - 5) return;
    char label[96];
    size_t cl = ln < sizeof label - 1 ? ln : sizeof label - 1;
    memcpy(label, p + meta + 5, cl);
    label[cl] = 0;
    bool next_limit = ln == sizeof NEXT_LIMIT - 1 && memcmp(label, NEXT_LIMIT, ln) == 0;
    bool ok_img = jpg && decode(jpg, len);
    int64_t t = now_ms();

    board_ui_lock();
    set_on(true);
    if (next_limit) {
        ESP_LOGI(TAG, "BIEN tiep %u m", (unsigned)m);
        if (ok_img) to_sign(s_sign_px, true);
        if (ok_img || s_sign) {
            s_sign = true;
            s_sign_m = m;
            s_sign_at = t;
        }
    } else if (ok_img && is_arrow()) {
        ESP_LOGI(TAG, "RE %u m | %s", (unsigned)m, label);
        to_mask();
        snprintf(s_street, sizeof s_street, "%s", label);
        s_turn = true;
        s_turn_m = m;
        s_turn_at = t;
        ui_set_nav_icon(&s_nav_dsc);
        ui_set_nav(true, 3, (int)m, 0, s_street, 0);
    } else if (ok_img) {
        ESP_LOGI(TAG, "CANH BAO %u m | %s", (unsigned)m, label);
        to_sign(s_warn_px, false);
        ui_set_alert_label(label);
        s_warn = true;
        s_warn_m = m;
        s_warn_at = t;
        if (strcmp(label, s_last_text)) {
            snprintf(s_last_text, sizeof s_last_text, "%s", label);
            ui_toast(label);
        }
    } else if (jpg) {                                          /* anh khong giai duoc: van hien re + ten duong */
        ESP_LOGI(TAG, "RE (anh loi) %u m | %s", (unsigned)m, label);
        snprintf(s_street, sizeof s_street, "%s", label);
        s_turn = true;
        s_turn_m = m;
        s_turn_at = t;
        ui_set_nav_icon(NULL);
        ui_set_nav(true, 1, (int)m, 0, s_street, 0);
    } else if (s_turn && !strcmp(label, s_street)) {           /* chi co chu, cung duong: cap nhat met */
        s_turn_m = m;
        s_turn_at = t;
        ui_set_nav(true, 3, (int)m, 0, s_street, 0);
    } else {
        ESP_LOGI(TAG, "CHU %u m | %s", (unsigned)m, label);
        if (strcmp(label, s_last_text)) {
            snprintf(s_last_text, sizeof s_last_text, "%s", label);
            ui_toast(label);
        }
    }
    push_alerts();
    board_ui_unlock();
}

static void on_frame(const uint8_t *p, size_t n)
{
    int cmd = p[12] << 8 | p[13];
    if (cmd == 0xFF03 && n >= 22) {                            /* trang thai mach cau noi */
        s_bridge_at = now_ms();
        if (!s_bridge) {
            s_bridge = true;
            ESP_LOGI(TAG, "thay mach cau noi VML (UART GPIO%d/%d)", VML_RX, VML_TX);
        }
        bool c = p[14] != 0;
        if (c) {
            s_lost_at = 0;
            board_ui_lock();
            set_on(true);
            board_ui_unlock();
        } else if (s_on && !s_lost_at) {
            s_lost_at = now_ms();
        }
    } else if (cmd == 0x0101) {
        if (n == 29) {
            int v = p[20];
            if (v != s_last_lim) {
                s_last_lim = v;
                ESP_LOGI(TAG, "BIEN hien tai %d", v);
                board_ui_lock();
                set_on(true);
                if (v >= 5 && v <= 150 && v % 5 == 0) ui_set_speed_limit(v);   /* 0: VML chua biet -> giu bien cu */
                board_ui_unlock();
            }
        } else {
            char h[64];
            int o = 0;
            for (size_t i = 14; i + 8 < n && o < (int)sizeof h - 4; i++) o += snprintf(h + o, sizeof h - o, "%02X ", p[i]);
            ESP_LOGI(TAG, "0101 (%u byte): %s", (unsigned)n, h);
        }
    } else if (cmd == 0x40FE) {
        on_guidance(p, n);
    } else {
        ESP_LOGI(TAG, "goi la %04X, %u byte", cmd, (unsigned)n);
    }
}

static void expire(void)
{
    int64_t t = now_ms();
    if (s_bridge && t - s_bridge_at > BRIDGE_MS) {             /* mat mach cau noi (rut day / mat nguon) */
        s_bridge = false;
        ESP_LOGW(TAG, "mat mach cau noi VML");
        if (s_on && !s_lost_at) s_lost_at = t;
    }
    bool turn = s_turn && t - s_turn_at > TURN_MS, sign = s_sign && t - s_sign_at > SIGN_MS,
         warn = s_warn && t - s_warn_at > WARN_MS, lost = s_lost_at && t - s_lost_at > GRACE_MS;
    if (!turn && !sign && !warn && !lost) return;
    board_ui_lock();
    if (lost) {
        s_lost_at = 0;
        set_on(false);
    } else {
        if (turn) {
            s_turn = false;
            ui_set_nav(false, 0, -1, 0, s_street, 0);
            ui_set_nav_icon(NULL);
        }
        if (sign) s_sign = false;
        if (warn) s_warn = false;
        push_alerts();
    }
    board_ui_unlock();
}

/* ---------- ghep khung ---------- */
static const uint8_t MAGIC[4] = {0xA5, 0x5A, 0x37, 0xC3};
static uint8_t *s_pkt;
static size_t s_used, s_expect;

static void feed(uint8_t v)
{
    if (s_used < 4 && v != MAGIC[s_used]) {
        s_used = v == MAGIC[0] ? 1 : 0;
        if (s_used) s_pkt[0] = v;
        return;
    }
    s_pkt[s_used++] = v;
    if (s_used == 8) {
        uint32_t len = be32(s_pkt + 4);
        if (len < 6 || len > PKT_MAX - 8) {
            s_used = s_expect = 0;
            return;
        }
        s_expect = len + 8;
    }
    if (!s_expect || s_used != s_expect) return;
    on_frame(s_pkt, s_used);
    s_used = s_expect = 0;
}

static void vml_task(void *arg)
{
    (void)arg;
    static uint8_t buf[512];
    int64_t last_rx = 0;
    for (;;) {
        int n = uart_read_bytes(VML_UART, buf, sizeof buf, pdMS_TO_TICKS(250));
        if (n > 0) {
            if (s_used && now_ms() - last_rx > 1000) s_used = s_expect = 0;   /* khung dut giua chung: bo */
            last_rx = now_ms();
            for (int i = 0; i < n; i++) feed(buf[i]);
        }
        expire();
    }
}

void vml_uart_start(void)
{
    s_pkt = heap_caps_malloc(PKT_MAX, MALLOC_CAP_SPIRAM);
    s_nav_px = heap_caps_calloc(1, ARW * ARW, MALLOC_CAP_SPIRAM);
    s_sign_px = heap_caps_calloc(1, SGN * SGN * 3, MALLOC_CAP_SPIRAM);
    s_warn_px = heap_caps_calloc(1, SGN * SGN * 3, MALLOC_CAP_SPIRAM);
    jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    s_jin = jpeg_alloc_decoder_mem(8192, &in_cfg, &s_jin_cap);
    s_work = jpeg_alloc_decoder_mem(DEC * DEC * 2, &out_cfg, &s_work_cap);
    if (!s_pkt || !s_nav_px || !s_sign_px || !s_warn_px || !s_jin || !s_work) {
        ESP_LOGE(TAG, "thieu RAM");
        return;
    }
    jpeg_decode_engine_cfg_t eng = {.intr_priority = 0, .timeout_ms = 100};
    esp_err_t e = jpeg_new_decoder_engine(&eng, &s_jpeg);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "khong mo duoc bo giai ma JPEG (%s): mui ten VML se la mui ten thang", esp_err_to_name(e));
        s_jpeg = NULL;
    }
    dsc_init(&s_nav_dsc, s_nav_px, LV_COLOR_FORMAT_A8, ARW);
    dsc_init(&s_sign_dsc, s_sign_px, LV_COLOR_FORMAT_RGB565A8, SGN);
    dsc_init(&s_warn_dsc, s_warn_px, LV_COLOR_FORMAT_RGB565A8, SGN);

    uart_config_t uc = {
        .baud_rate = VML_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    e = uart_driver_install(VML_UART, 16384, 0, 0, NULL, 0);
    if (e == ESP_OK) e = uart_param_config(VML_UART, &uc);
    if (e == ESP_OK) e = uart_set_pin(VML_UART, VML_TX, VML_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e == ESP_OK) gpio_pullup_en(VML_RX);                   /* chua cam cau noi: chan RX khong troi */
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "UART loi %s", esp_err_to_name(e));
        return;
    }
    xTaskCreate(vml_task, "vml", 6144, NULL, 4, NULL);
    ESP_LOGI(TAG, "cho mach cau noi VietMap Live tren UART1 RX GPIO%d / TX GPIO%d, %d baud", VML_RX, VML_TX, VML_BAUD);
}
