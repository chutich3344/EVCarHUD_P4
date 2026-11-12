/* Chan doan Bluetooth qua chip C6 (2026-11-12: NimBLE gui HCI Reset ma khong co tra loi).
 * Boc (--wrap) 2 ham cua esp_hosted de dem: lenh HCI P4 gui sang C6 (va ket qua gui), goi HCI C6 tra ve.
 * In vai goi dau ra UART voi the "btdiag". */
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"

#define ESP_HCI_IF_DIAG 4   /* ESP_HCI_IF trong eh_common_interface.h (enum noi bo) */

static const char *TAG = "btdiag";
static int s_tx, s_tx_err, s_rx;

esp_err_t __real_eh_host_transport_tx(uint8_t if_type, uint8_t if_num, const uint8_t *payload, uint16_t len, uint8_t flags);
esp_err_t __wrap_eh_host_transport_tx(uint8_t if_type, uint8_t if_num, const uint8_t *payload, uint16_t len, uint8_t flags)
{
    esp_err_t e = __real_eh_host_transport_tx(if_type, if_num, payload, len, flags);
    if (if_type == ESP_HCI_IF_DIAG) {
        s_tx++;
        if (e != ESP_OK) s_tx_err++;
        if (s_tx <= 6 || e != ESP_OK)
            ESP_LOGW(TAG, "P4->C6 HCI #%d: %u byte [%02x %02x %02x %02x] -> %s (loi %d, nhan %d)", s_tx, len,
                     len > 0 ? payload[0] : 0, len > 1 ? payload[1] : 0, len > 2 ? payload[2] : 0, len > 3 ? payload[3] : 0,
                     esp_err_to_name(e), s_tx_err, s_rx);
    }
    return e;
}

void __real_eh_host_mcu_hci_rx_deliver(const uint8_t *frame, uint16_t len);
void __wrap_eh_host_mcu_hci_rx_deliver(const uint8_t *frame, uint16_t len)
{
    s_rx++;
    if (s_rx <= 6)
        ESP_LOGW(TAG, "C6->P4 HCI #%d: %u byte [%02x %02x %02x %02x %02x %02x]", s_rx, len, len > 0 ? frame[0] : 0,
                 len > 1 ? frame[1] : 0, len > 2 ? frame[2] : 0, len > 3 ? frame[3] : 0, len > 4 ? frame[4] : 0,
                 len > 5 ? frame[5] : 0);
    __real_eh_host_mcu_hci_rx_deliver(frame, len);
}
