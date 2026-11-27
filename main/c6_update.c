/* Nang firmware chip Wi-Fi/Bluetooth ESP32-C6 tu P4 qua SDIO (esp_hosted OTA: begin -> write -> end -> activate).
 * C6 xuat xuong chay esp_hosted 0.0.x, lech the he voi P4 (esp_hosted 3.1.0): Wi-Fi chap chon (do song rong, TCP het gio).
 * Anh firmware C6 (CI build-c6.yml, esp_hosted 3.1.0) nam ngay dau khe ota_1: file "p4-c6-*-app-0x10000.bin" = app P4
 * dem toi 6 MB + anh C6, nap USB o 0x10000 (giu NVS / Wi-Fi da luu). Nap xong xoa sector dau ota_1 de khong nap lai;
 * khe ota_1 van dung binh thuong cho OTA sau nay. */
#include "c6_update.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "board.h"
#include "dashboard.h"

static const char *TAG = "c6";

#define C6_CHIP_ID 0x000D                          /* esp_chip_id_t ESP32-C6 trong tieu de anh */
#define CHUNK 1024
#define MAX_TRIES 2

static void toast(const char *t)
{
    board_ui_lock();
    ui_toast(t);
    board_ui_unlock();
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Do dai anh C6 o dau phan vung (0 = khong co anh C6 hop le) */
static size_t c6_image_len(const esp_partition_t *p)
{
    uint8_t h[24];
    if (esp_partition_read(p, 0, h, sizeof h) != ESP_OK) return 0;
    if (h[0] != 0xE9 || h[1] == 0 || h[1] > 16 || (h[12] | h[13] << 8) != C6_CHIP_ID) return 0;
    size_t at = 24;
    for (int i = 0; i < h[1]; i++) {
        uint8_t s[8];
        if (esp_partition_read(p, at, s, sizeof s) != ESP_OK) return 0;
        at += 8 + le32(s + 4);
        if (at > p->size) return 0;
    }
    size_t len = (at & ~(size_t)15) + 16 + (h[23] == 1 ? 32 : 0);
    return len <= p->size ? len : 0;
}

static int tries_bump(void)
{
    nvs_handle_t nh;
    uint8_t n = 0;
    if (nvs_open("c6", NVS_READWRITE, &nh) != ESP_OK) return 0;
    nvs_get_u8(nh, "tries", &n);
    nvs_set_u8(nh, "tries", n + 1);
    nvs_commit(nh);
    nvs_close(nh);
    return n + 1;
}

static void tries_clear(void)
{
    nvs_handle_t nh;
    if (nvs_open("c6", NVS_READWRITE, &nh) != ESP_OK) return;
    nvs_erase_key(nh, "tries");
    nvs_commit(nh);
    nvs_close(nh);
}

bool c6_update_check(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (!p || !run || run->address == p->address) return false;
    size_t len = c6_image_len(p);
    if (!len) return false;

    int t = tries_bump();
    ESP_LOGW(TAG, "co anh firmware C6 %u byte trong %s (lan thu %d)", (unsigned)len, p->label, t);
    if (t > MAX_TRIES) {
        ESP_LOGE(TAG, "nap C6 hong %d lan: bo anh C6", MAX_TRIES);
        esp_partition_erase_range(p, 0, 4096);
        tries_clear();
        toast("Không nâng được chip Wi-Fi C6: đã thử 2 lần, bỏ qua");
        return false;
    }
    toast("Đang nâng firmware chip Wi-Fi C6… không tắt nguồn HUD (khoảng 1 phút)");
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_8BIT);
    if (!buf) return false;
    char msg[96];
    esp_err_t e = esp_hosted_slave_ota_begin();
    if (e != ESP_OK) {
        snprintf(msg, sizeof msg, "Chip C6 không nhận lệnh nâng cấp (%s)", esp_err_to_name(e));
        goto fail;
    }
    int last = -1;
    for (size_t off = 0; off < len; off += CHUNK) {
        size_t n = len - off < CHUNK ? len - off : CHUNK;
        if ((e = esp_partition_read(p, off, buf, n)) != ESP_OK) { snprintf(msg, sizeof msg, "Đọc flash lỗi (%s)", esp_err_to_name(e)); goto fail; }
        if ((e = esp_hosted_slave_ota_write(buf, n)) != ESP_OK) {
            snprintf(msg, sizeof msg, "Gửi firmware cho C6 lỗi ở %u KB (%s)", (unsigned)(off / 1024), esp_err_to_name(e));
            goto fail;
        }
        int pct = (int)((off + n) * 100 / len);
        if (pct / 25 != last / 25 && pct < 100) {
            last = pct;
            snprintf(msg, sizeof msg, "Đang nâng chip Wi-Fi C6: %d%%", pct);
            toast(msg);
            ESP_LOGI(TAG, "%s", msg);
        }
    }
    /* Da gui het: bo anh C6 truoc khi C6 khoi dong lai (lenh end/activate co the het gio vi C6 reset ngay) */
    esp_partition_erase_range(p, 0, 4096);
    tries_clear();
    e = esp_hosted_slave_ota_end();
    ESP_LOGI(TAG, "ota end: %s", esp_err_to_name(e));
    if (e == ESP_OK) {
        esp_err_t a = esp_hosted_slave_ota_activate();               /* C6 >= 2.6 can; ban cu tu dat vung khoi dong o end */
        ESP_LOGI(TAG, "ota activate: %s", esp_err_to_name(a));
    }
    free(buf);
    snprintf(msg, sizeof msg, e == ESP_OK ? "Đã nâng chip Wi-Fi C6. HUD khởi động lại…"
                                          : "C6 báo lỗi khi kết thúc (%s). HUD khởi động lại…", esp_err_to_name(e));
    toast(msg);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return true;

fail:
    free(buf);
    ESP_LOGE(TAG, "%s", msg);
    toast(msg);
    return false;
}
