/* Lidar UART: xem lidar.h. Task "lidar" doc UART, giai ma 2 loai khung, loc trung vi, va khi camera khong chay thi
 * goi adas_feed_lidar() de canh bao chi bang Lidar (xem adas_feed.c). */
#include "lidar.h"
#include "sdkconfig.h"

#if CONFIG_EVCARHUD_LIDAR

#include <string.h>
#include "adas_feed.h"
#include "board.h"
#include "dashboard.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lidar";

#define PORT ((uart_port_t)CONFIG_EVCARHUD_LIDAR_UART)
#define LOST_MS 3000
#define MED_N 5

static const int k_baud[] = {921600, 115200, 460800, 230400, 256000};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_mm;                 /* khoang cach da loc (mm) */
static uint32_t s_ms;            /* luc doc duoc mau hop le (ms) */
static uint32_t s_frame_ms;      /* luc nhan khung dung tong kiem cuoi cung (ke ca "khong co vat") */
static int s_state = LIDAR_OFF;
static const char *s_model = "";
static int s_med[MED_N], s_med_n, s_med_i;
static int s_ui_state = -1;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool lidar_get(float *dist_m, uint32_t max_age_ms)
{
    portENTER_CRITICAL(&s_mux);
    int mm = s_mm;
    uint32_t t = s_ms;
    portEXIT_CRITICAL(&s_mux);
    if (mm <= 0 || t == 0 || now_ms() - t > max_age_ms) return false;
    if (dist_m) *dist_m = (float)mm / 1000.0f;
    return true;
}

int lidar_state(void) { return s_state; }
const char *lidar_model(void) { return s_model; }

/* ---- giai ma ---- */
static uint8_t s_buf[16];
static int s_len;
static int s_kind;               /* 1 = Nooploop (57 00, 16 byte), 2 = Benewake (59 59, 9 byte) */
static int s_last_status = -1, s_last_strength;

static void put_sample(int mm)
{
    uint32_t t = now_ms();
    if (mm <= 0) {                                            /* khung hop le nhung khong co vat trong tam */
        s_med_n = 0;
        portENTER_CRITICAL(&s_mux);
        s_frame_ms = t;
        s_mm = 0;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_med[s_med_i] = mm;
    s_med_i = (s_med_i + 1) % MED_N;
    if (s_med_n < MED_N) s_med_n++;
    int tmp[MED_N];
    memcpy(tmp, s_med, sizeof tmp);
    int n = s_med_n;
    if (n < MED_N) {                                          /* chua du mau: lay cac mau moi nhat */
        for (int i = 0; i < n; i++) tmp[i] = s_med[(s_med_i - 1 - i + MED_N) % MED_N];
    }
    for (int i = 1; i < n; i++) {                             /* sap xep chen */
        int v = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = v;
    }
    portENTER_CRITICAL(&s_mux);
    s_mm = tmp[n / 2];
    s_ms = t;
    s_frame_ms = t;
    portEXIT_CRITICAL(&s_mux);
}

/* tra ve true khi nhan xong mot khung dung tong kiem */
static bool feed_byte(uint8_t b)
{
    if (s_len == 0) {
        if (b == 0x57 || b == 0x59) s_buf[s_len++] = b;
        return false;
    }
    if (s_len == 1) {
        if (s_buf[0] == 0x57 && b == 0x00) s_kind = 1;
        else if (s_buf[0] == 0x59 && b == 0x59) s_kind = 2;
        else {
            s_len = 0;
            return feed_byte(b);
        }
        s_buf[s_len++] = b;
        return false;
    }
    s_buf[s_len++] = b;
    int need = s_kind == 1 ? 16 : 9;
    if (s_len < need) return false;
    s_len = 0;
    uint8_t sum = 0;
    for (int i = 0; i < need - 1; i++) sum += s_buf[i];
    if (sum != s_buf[need - 1]) return false;
    if (s_kind == 1) {
        /* [8..10] khoang cach (mm, 24 bit), [11] dis_status, [12..13] cuong do tin hieu */
        int mm = s_buf[8] | (s_buf[9] << 8) | (s_buf[10] << 16);
        int strength = s_buf[12] | (s_buf[13] << 8);
        s_last_status = s_buf[11];
        s_last_strength = strength;
        if (mm > 200000 || strength == 0) mm = 0;
        s_model = "TOFSense";
        put_sample(mm);
    } else {
        /* [2..3] khoang cach (cm), [4..5] cuong do. 0 hoac >= 180 m: ngoai tam */
        int cm = s_buf[2] | (s_buf[3] << 8);
        int strength = s_buf[4] | (s_buf[5] << 8);
        s_last_strength = strength;
        s_last_status = -1;
        if (cm >= 18000 || strength == 0) cm = 0;
        s_model = "Benewake";
        put_sample(cm * 10);
    }
    return true;
}

static uint8_t s_rx[256];

/* doc trong ms mili giay, tra so khung dung tong kiem */
static int pump(int ms)
{
    int good = 0;
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < end) {
        int n = uart_read_bytes(PORT, s_rx, sizeof s_rx, pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) good += feed_byte(s_rx[i]);
    }
    return good;
}

static void set_ui(int st)
{
    if (st == s_ui_state) return;
    s_ui_state = st;
    board_ui_lock();
    ui_lidar_status(st == LIDAR_OK ? UI_CAM_OK : st == LIDAR_LOST ? UI_CAM_ERR : UI_CAM_OFF);
    board_ui_unlock();
}

static void lidar_task(void *arg)
{
    (void)arg;
    const int pins[2][2] = {{CONFIG_EVCARHUD_LIDAR_RX, CONFIG_EVCARHUD_LIDAR_TX},
                            {CONFIG_EVCARHUD_LIDAR_TX, CONFIG_EVCARHUD_LIDAR_RX}};
    int scans = 0;
    for (;;) {
        /* ---- do tim: toc do baud x thu tu chan ---- */
        bool found = false;
        for (int p = 0; p < 2 && !found; p++) {
            uart_set_pin(PORT, pins[p][1], pins[p][0], UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            for (size_t b = 0; b < sizeof k_baud / sizeof k_baud[0] && !found; b++) {
                uart_set_baudrate(PORT, k_baud[b]);
                uart_flush_input(PORT);
                s_len = 0;
                if (pump(250) >= 3) {
                    found = true;
                    ESP_LOGI(TAG, "thay %s: %d baud, RX=GPIO%d TX=GPIO%d", s_model, k_baud[b], pins[p][0], pins[p][1]);
                }
            }
        }
        if (!found) {
            if (s_state == LIDAR_OK) s_state = LIDAR_LOST;
            set_ui(s_state);
            if (++scans % 20 == 1) ESP_LOGI(TAG, "chua thay Lidar tren GPIO%d/%d (do lai sau 3 s)",
                                            CONFIG_EVCARHUD_LIDAR_RX, CONFIG_EVCARHUD_LIDAR_TX);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        s_state = LIDAR_OK;
        set_ui(s_state);

        /* ---- chay: moi 100 ms mot nhip ---- */
        int64_t log_us = 0;
        for (;;) {
            pump(100);
            uint32_t t = now_ms();
            uint32_t last;
            portENTER_CRITICAL(&s_mux);
            last = s_frame_ms;
            portEXIT_CRITICAL(&s_mux);
            if (t - last > LOST_MS) {
                ESP_LOGW(TAG, "mat tin hieu Lidar, do lai");
                s_state = LIDAR_LOST;
                set_ui(s_state);
                portENTER_CRITICAL(&s_mux);
                s_mm = 0;
                portEXIT_CRITICAL(&s_mux);
                break;
            }
            adas_feed_lidar();                                /* camera khong chay: canh bao chi bang Lidar */
            int64_t us = esp_timer_get_time();
            if (us - log_us >= 3000000) {
                log_us = us;
                float d;
                if (lidar_get(&d, 300)) ESP_LOGI(TAG, "%.2f m (cuong do %d, trang thai %d)", (double)d, s_last_strength, s_last_status);
                else ESP_LOGI(TAG, "khong co vat trong tam (cuong do %d, trang thai %d)", s_last_strength, s_last_status);
            }
        }
    }
}

void lidar_start(void)
{
    uart_config_t c = {
        .baud_rate = k_baud[0],
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(PORT, 2048, 0, 0, NULL, 0) != ESP_OK || uart_param_config(PORT, &c) != ESP_OK) {
        ESP_LOGE(TAG, "khong mo duoc UART%d", (int)PORT);
        return;
    }
    uart_set_pin(PORT, CONFIG_EVCARHUD_LIDAR_TX, CONFIG_EVCARHUD_LIDAR_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(lidar_task, "lidar", 4096, NULL, 4, NULL);
}

#else

void lidar_start(void) {}
bool lidar_get(float *dist_m, uint32_t max_age_ms) { (void)dist_m; (void)max_age_ms; return false; }
int lidar_state(void) { return LIDAR_OFF; }
const char *lidar_model(void) { return ""; }

#endif
