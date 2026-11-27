/*
 * Firmware chip C6 (esp_hosted 3.1.0, mau bleprph_wifi_coex/cp) + cau HCI tu viet.
 *
 * 2026-11-12: tu khi C6 len 3.1.0, P4 gui HCI Reset qua SDIO (ESP_OK) nhung C6 khong tra loi goi nao.
 * Cau nay thay duong HCI cua esp_hosted bang ban tu lam:
 *  - nhan goi HCI tu P4, tu dung khung H4 (byte loai + than) roi dua vao VHCI cua bo dieu khien BLE;
 *  - tu dang ky callback VHCI, goi tra loi cua bo dieu khien ve P4 qua hang doi BT;
 *  - 8 goi dau: gui them 1 su kien HCI rieng (0xFF, 'E6') cho P4 bao so dem + trang thai bo dieu khien
 *    -> bt_diag tren P4 in ra, biet ngay C6 ket o dau.
 */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "eh_cp.h"
#include "eh_transport.h"
#include "eh_transport_cp.h"
#include "eh_interface.h"
#include "eh_frame.h"

static const char *TAG = "c6_hci";

static volatile uint32_t s_rx, s_tx, s_tx_err;

static int to_host(const uint8_t *data, uint16_t len)
{
    uint8_t *buf = malloc(len);
    if (!buf) { s_tx_err++; return -1; }
    memcpy(buf, data, len);
    interface_buffer_handle_t bh;
    memset(&bh, 0, sizeof(bh));
    bh.if_type = ESP_HCI_IF;
    bh.if_num = 0;
    bh.payload_len = len;
    bh.payload = buf;
    bh.priv_buffer_handle = buf;
    bh.free_buf_handle = free;
    if (send_to_host_queue(&bh, PRIO_Q_BT)) {
        free(buf);
        s_tx_err++;
        return -1;
    }
    return 0;
}

static void ctrl_send_ready(void) { }

static int ctrl_to_host(uint8_t *data, uint16_t len)
{
    s_tx++;
    return to_host(data, len);
}

static esp_vhci_host_callback_t s_vhci_cb = {
    .notify_host_send_available = ctrl_send_ready,
    .notify_host_recv = ctrl_to_host,
};

static esp_err_t s_e_init = -1, s_e_en = -1;

/* 1.0.35 cho thay bo dieu khien BLE cua C6 van IDLE (=0) du P4 bao RPC bat BT OK -> tu bat lay */
static int ctrl_up(void)
{
    int st = esp_bt_controller_get_status();
    if (st == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        s_e_init = esp_bt_controller_init(&cfg);
        ESP_LOGW(TAG, "tu khoi tao bo dieu khien BLE: %s", esp_err_to_name(s_e_init));
        st = esp_bt_controller_get_status();
    }
    if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
        s_e_en = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        ESP_LOGW(TAG, "tu bat bo dieu khien BLE: %s", esp_err_to_name(s_e_en));
        st = esp_bt_controller_get_status();
    }
    return st;
}

static void diag_evt(uint8_t type, uint16_t len, int st, int reg, int avail)
{
    uint8_t e[3 + 16] = { 0x04, 0xFF, 16, 0xE6, 'H',
        (uint8_t)s_rx, (uint8_t)s_tx, (uint8_t)s_tx_err,
        (uint8_t)st, type, (uint8_t)len, (uint8_t)reg, (uint8_t)avail,
        (uint8_t)(esp_get_free_heap_size() / 1024), 0x02 /* phien ban cau */,
        (uint8_t)(s_e_init >> 8), (uint8_t)s_e_init, (uint8_t)(s_e_en >> 8), (uint8_t)s_e_en };
    to_host(e, sizeof(e));
}

static esp_err_t hci_from_host(void *buf, uint16_t len, void *eb)
{
    (void)eb;
    uint8_t *body = buf;
    uint8_t type = body[-1];   /* byte H4 nam o o hci_pkt_type ngay truoc than */
    s_rx++;

    int st = ctrl_up();
    int reg = -1;
    if (st == ESP_BT_CONTROLLER_STATUS_ENABLED && type == 0x01)
        reg = esp_vhci_host_register_callback(&s_vhci_cb);
    int avail = esp_vhci_host_check_send_available();

    if (s_rx <= 8) {
        ESP_LOGW(TAG, "HCI tu P4 #%u loai %u %u byte, ctrl=%d reg=%d avail=%d",
                 (unsigned)s_rx, type, len, st, reg, avail);
        diag_evt(type, len, st, reg, avail);
    }

    if (st == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        uint8_t *f = malloc(len + 1);
        if (f) {
            f[0] = type;
            memcpy(f + 1, body, len);
            esp_vhci_host_send_packet(f, len + 1);
            free(f);
        }
    }
    return ESP_OK;
}

static void hook_task(void *arg)
{
    /* cho esp_hosted tu khoi tao xong tinh nang BT (no dang ky duong HCI cua no), roi thay bang cau nay */
    for (int i = 0; i < 100 && !eh_cp_get_traffic_rx_cb(ESP_HCI_IF); i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    eh_cp_set_traffic_rx_cb(ESP_HCI_IF, NULL);
    esp_err_t e = eh_cp_set_traffic_rx_cb(ESP_HCI_IF, hci_from_host);
    ESP_LOGW(TAG, "cau HCI rieng: %s", esp_err_to_name(e));
    int st = ctrl_up();
    if (st == ESP_BT_CONTROLLER_STATUS_ENABLED)
        esp_vhci_host_register_callback(&s_vhci_cb);
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    xTaskCreate(hook_task, "c6_hci_hook", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "C6 Wi-Fi + BLE (cau HCI rieng) dang chay");
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
}
