#include "sdkconfig.h"
#include "ble_link.h"
#include "wifi_link.h"

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_ESP_HOSTED_HOST_FEAT_BT

#include "esp_hosted_bt_host_stack.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "link.h"
#include "board.h"
#include "dashboard.h"
#include "settings.h"
#include "gofa.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "freertos/queue.h"

/* HLP/1 qua BLE GATT (tai lieu Waze Mod, muc 8):
 *   dich vu  8a7e0001-4d6e-4c48-9a9d-484c504c0001
 *   TX       8a7e0002-...  dien thoai -> HUD, ghi co xac nhan, chia khuc tuy y (ghep lai theo LF)
 *   RX       8a7e0003-...  HUD -> dien thoai, notify, moi khuc toi da MTU - 3 byte
 *   CAPS     8a7e0004-...  doc: kha nang cua HUD
 * NimBLE luu UUID 128 bit theo thu tu byte dao nguoc. */
#define HLP_UUID(n) BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a, 0x48, 0x4c, 0x6e, 0x4d, (n), 0x00, 0x7e, 0x8a)
static const ble_uuid128_t UUID_SVC = HLP_UUID(0x01);
static const ble_uuid128_t UUID_TX = HLP_UUID(0x02);
static const ble_uuid128_t UUID_RX = HLP_UUID(0x03);
static const ble_uuid128_t UUID_CAPS = HLP_UUID(0x04);
static const char CAPS[] = "{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}";

#define BT_TRIES 6                       /* so lan bat Bluetooth (C6) truoc khi bo cuoc */
#define START_DELAY_MS 2000              /* de man hinh len truoc va cau noi SDIO toi chip C6 san sang */
#define BLE_LINE_MAX 640                     /* 1 dong HLP toi da 512 byte */
#define EV_RX   (1u << 0)
#define EV_TX   (1u << 1)
#define EV_UP   (1u << 2)
#define EV_DOWN (1u << 3)
#define EV_SNIFF (1u << 4)                /* GOFA: co goi moi trong hang doi */

static const char *TAG = "ble";
static TaskHandle_t s_task;
static StreamBufferHandle_t s_rx_sb;     /* byte tu dien thoai: ham GATT chi chep vao day roi tra ve ngay */
static MessageBufferHandle_t s_tx_mb;    /* cac dong cho gui len dien thoai */
static uint16_t s_rx_handle;             /* handle gia tri cua dac tinh RX (notify) */
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile int s_state;
static uint8_t s_addr_type;
static bool s_linked;                    /* da goi link_up (chi task ble dung) */
static volatile bool s_synced;           /* ngan xep da noi chuyen duoc voi bo dieu khien Bluetooth */
static int s_resets;

static int gap_event(struct ble_gap_event *ev, void *arg);

static void wake(uint32_t bits)
{
    if (s_task) xTaskNotify(s_task, bits, eSetBits);
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    const ble_uuid_t *u = ctxt->chr->uuid;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ble_uuid_cmp(u, &UUID_TX.u) == 0) {
        uint8_t buf[256];
        uint16_t n = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) > sizeof buf) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
        if (n && xStreamBufferSend(s_rx_sb, buf, n, 0) != n) ESP_LOGW(TAG, "bo dem nhan day, mat %u byte", (unsigned)n);
        wake(EV_RX);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && ble_uuid_cmp(u, &UUID_CAPS.u) == 0) {
        return os_mbuf_append(ctxt->om, CAPS, sizeof CAPS - 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ===== Tram sac (app android-tramsac, docs/tram-sac-ble.md): dang ky o ca che do Waze va GOFA, khong quang ba =====
 * UUID 4556d00X-8c2a-4b7e-9d3f-5a1c0e7b2f10. Ghi "T1|ten|met|A/R/L/B/?|goc|kW|cong|so tram" ("T1|" = xoa); doc INFO = "TRAMSAC1".
 * 4556d004: ghi vi tri dien thoai "P1|vi do|kinh do|ten noi" (ten co the bo trong) -> thoi tiet theo GPS. */
#define TS_UUID(n) BLE_UUID128_INIT(0x10, 0x2f, 0x7b, 0x0e, 0x1c, 0x5a, 0x3f, 0x9d, 0x7e, 0x4b, 0x2a, 0x8c, (n), 0xd0, 0x56, 0x45)
static const ble_uuid128_t UUID_TS_SVC = TS_UUID(0x01), UUID_TS_STA = TS_UUID(0x02), UUID_TS_INFO = TS_UUID(0x03),
                           UUID_TS_POS = TS_UUID(0x04);

static int ts_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return os_mbuf_append(ctxt->om, "TRAMSAC1", 8) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    char b[200];
    uint16_t n = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) >= sizeof b) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, b, sizeof b - 1, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
    b[n] = 0;
    ESP_LOGI(TAG, "tram sac: %s", b);
    if (strncmp(b, "T1|", 3) != 0) return 0;                      /* phien ban khac: bo qua */
    char *f[9] = {0};                                              /* ten, met, phia, goc, kW, cong, so tram, che do */
    int k = 0;
    for (char *p = b + 3; k < 8; ) {
        f[k++] = p;
        char *q = strchr(p, '|');
        if (!q) break;
        *q = 0;
        p = q + 1;
    }
    board_ui_lock();
    if (!f[0] || !f[0][0] || k < 3) ui_set_station(NULL, 0, '?', UI_TS_NO_ANGLE, 0, 0, 0, 'N');
    else ui_set_station(f[0], atoi(f[1]), f[2][0] ? f[2][0] : '?', k > 3 && f[3][0] ? atoi(f[3]) : UI_TS_NO_ANGLE,
                        k > 4 ? atoi(f[4]) : 0, k > 5 ? atoi(f[5]) : 0, k > 6 ? atoi(f[6]) : 0,
                        k > 7 && f[7][0] == 'D' ? 'D' : 'N');
    board_ui_unlock();
    return 0;
}

static int pos_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    char b[120];
    uint16_t n = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) >= sizeof b) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(ctxt->om, b, sizeof b - 1, &n) != 0) return BLE_ATT_ERR_UNLIKELY;
    b[n] = 0;
    if (strncmp(b, "P1|", 3) != 0) return 0;
    char *e;
    double lat = strtod(b + 3, &e);
    if (*e != '|') return 0;
    double lon = strtod(e + 1, &e);
    const char *name = *e == '|' ? e + 1 : "";
    ESP_LOGI(TAG, "vi tri dien thoai: %.4f, %.4f %s", lat, lon, name);
    wifi_set_phone_pos(lat, lon, name);
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &UUID_TX.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = &UUID_RX.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_rx_handle},
            {.uuid = &UUID_CAPS.u, .access_cb = chr_access, .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_TS_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &UUID_TS_STA.u, .access_cb = ts_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = &UUID_TS_INFO.u, .access_cb = ts_access, .flags = BLE_GATT_CHR_F_READ},
            {.uuid = &UUID_TS_POS.u, .access_cb = pos_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {0},
        },
    },
    {0},
};


/* ===== Che do GOFA (tai lieu hud.platform.gofa.vn/docs) =====
 * Quang ba: ten "HUD_xxxx" + Service UUID dinh danh 128 bit "<tien to>-<MAC 12 ky tu hex>" (dat dau tien).
 * GATT: dich vu GOFA 0x2A60 (app ghi protobuf vao 0x3030..0x3033, 0x3038, 0x3039, WRITE co phan hoi) va
 * Device Information 0x180A (app doc model / firmware / hardware / ma loi). Giai ma o gofa.c. */
#define U16(x) BLE_UUID16_INIT(x)
static const ble_uuid16_t UUID_GOFA = U16(0x2A60), UUID_DIS = U16(0x180A);
static const ble_uuid16_t UUID_FW = U16(0x2A26), UUID_HW = U16(0x2A27), UUID_MODEL = U16(0x2A24), UUID_ERR = U16(0x2A3F);
static const ble_uuid16_t G_SIGN = U16(0x3030), G_SPEED = U16(0x3031), G_SOUND = U16(0x3032), G_OVER = U16(0x3033),
                          G_NAV = U16(0x3038), G_TIME = U16(0x3039);
static ble_uuid128_t s_gofa_id;           /* Service UUID dinh danh (co MAC) */
static char s_gofa_name[16];
static volatile bool s_gofa;
typedef struct { uint16_t chr, len; uint8_t d[512]; } gofa_pkt_t;
static QueueHandle_t s_gofa_q;
static volatile uint16_t s_mtu = 23;
static uint16_t s_gofa_cnt[4];            /* so goi da nhan: bien bao, toc do, dan duong, gio */
static volatile uint16_t s_gofa_rd, s_gofa_snd, s_gofa_ovr, s_gofa_bad;   /* lan doc 0x180A, goi 0x3032, 0x3033, goi hong/qua dai */
#define EV_GUP   (1u << 5)
#define EV_GDOWN (1u << 6)

static int gofa_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    uint16_t id = ble_uuid_u16(ctxt->chr->uuid);
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const esp_app_desc_t *ad = esp_app_get_description();
        const char *v = id == 0x2A26 ? (ad ? ad->version : "1.0") : id == 0x2A27 ? "ESP32-P4-WIFI6-LCD-3.5" : id == 0x2A24 ? "EVCarHUD 3.5" : NULL;
        ESP_LOGI(TAG, "GOFA doc 0x%04X", id);
        s_gofa_rd++;
        wake(EV_SNIFF);
        if (v) return os_mbuf_append(ctxt->om, v, strlen(v)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        static const uint8_t no_err = 0;                           /* ma loi 0x2A3F: 0 = khong loi */
        return os_mbuf_append(ctxt->om, &no_err, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    static gofa_pkt_t pk;                                          /* chi task host NimBLE goi: khong can khoa */
    pk.chr = id;
    pk.len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, pk.d, sizeof pk.d, &pk.len) != 0 && OS_MBUF_PKTLEN(ctxt->om) > sizeof pk.d) {
        s_gofa_bad++;
        wake(EV_SNIFF);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (id == 0x3032) s_gofa_snd++;
    if (id == 0x3033) s_gofa_ovr++;
    ESP_LOGI(TAG, "GOFA ghi 0x%04X, %u byte", id, pk.len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, pk.d, pk.len, ESP_LOG_INFO);
    if (s_gofa_q && xQueueSend(s_gofa_q, &pk, 0) == pdTRUE) wake(EV_SNIFF);
    return 0;                                                      /* WRITE co phan hoi: tra loi ngay, giai ma sau */
}

#define GOFA_W(id) {.uuid = &(id).u, .access_cb = gofa_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP}
#define GOFA_R(id) {.uuid = &(id).u, .access_cb = gofa_access, .flags = BLE_GATT_CHR_F_READ}
static const struct ble_gatt_svc_def s_gofa_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID_GOFA.u, .characteristics = (struct ble_gatt_chr_def[]) {
        GOFA_W(G_SIGN), GOFA_W(G_SPEED), GOFA_W(G_SOUND), GOFA_W(G_OVER), GOFA_W(G_NAV), GOFA_W(G_TIME), {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &UUID_DIS.u, .characteristics = (struct ble_gatt_chr_def[]) {
        GOFA_R(UUID_FW), GOFA_R(UUID_HW), GOFA_R(UUID_MODEL), GOFA_R(UUID_ERR), {0}}},
    {0},
};

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1; }

/* Ten + UUID dinh danh tu MAC Bluetooth THAT. Board P4: song Bluetooth la cua chip C6 nen MAC phai lay tu NimBLE sau khi
 * dong bo (on_sync), khong phai esp_read_mac(ESP_MAC_BT) cua P4 (P4 khong co Bluetooth, MAC do sai -> GOFA khong nhan HUD).
 * mac[] theo thu tu doc AA:BB:CC:DD:EE:FF. */
static void gofa_setup(const uint8_t mac[6])
{
    snprintf(s_gofa_name, sizeof s_gofa_name, "HUD_%02X%02X", mac[4], mac[5]);
    uint8_t be[16] = {0};                                          /* UUID theo thu tu doc (big endian) */
    const char *p = CONFIG_EVCARHUD_GOFA_UUID_PREFIX;
    int k = 0;
    for (; *p && k < 10; p++) {
        if (*p == '-') continue;
        int hi = hexv(p[0]), lo = p[1] ? hexv(p[1]) : -1;
        if (hi < 0 || lo < 0) break;
        be[k++] = (uint8_t)(hi << 4 | lo);
        p++;
    }
    memcpy(be + 10, mac, 6);
    s_gofa_id.u.type = BLE_UUID_TYPE_128;
    for (int i = 0; i < 16; i++) s_gofa_id.value[i] = be[15 - i];  /* NimBLE luu nguoc */
    char us[BLE_UUID_STR_LEN];
    ble_uuid_to_str(&s_gofa_id.u, us);
    ESP_LOGI(TAG, "GOFA: ten %s, service UUID dinh danh %s", s_gofa_name, us);
}

static void gofa_show(const char *state, const char *last)       /* task ble: dai GOFA duoi man hinh */
{
    (void)state;
    char head[128];
    snprintf(head, sizeof head, "MTU%u đọc%u biển%u tốc%u âm%u quá%u dẫn%u giờ%u%s", s_mtu, s_gofa_rd,
             s_gofa_cnt[0], s_gofa_cnt[1], s_gofa_snd, s_gofa_ovr, s_gofa_cnt[2], s_gofa_cnt[3], s_gofa_bad ? " · LỖI" : "");
    ui_gofa_debug(head, last);
}

static void gofa_drain(void)
{
    static gofa_pkt_t pk;
    while (s_gofa_q && xQueueReceive(s_gofa_q, &pk, 0) == pdTRUE) {
        char sum[96], last[160];
        int ci = pk.chr == 0x3030 ? 0 : pk.chr == 0x3031 ? 1 : pk.chr == 0x3038 ? 2 : pk.chr == 0x3039 ? 3 : -1;
        if (ci >= 0) s_gofa_cnt[ci]++;
        board_ui_lock();
        gofa_handle(pk.chr, pk.d, pk.len, sum, sizeof sum);
        int k = snprintf(last, sizeof last, "%s\n", sum);
        for (int i = 0; i < pk.len && i < 12 && k < (int)sizeof last - 4; i++) k += snprintf(last + k, sizeof last - k, "%02X ", pk.d[i]);
        gofa_show("đang nhận dữ liệu", last);
        board_ui_unlock();
    }
}

static void advertise(void)
{
    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)(s_gofa ? &s_gofa_id : &UUID_SVC);   /* GOFA: UUID dinh danh dat dau tien */
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    f.name = (const uint8_t *)name;
    f.name_len = (uint8_t)strlen(name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);                          /* co + ten + UUID dich vu: vua 31 byte khi ten <= 8 ky tu */
    if (rc != 0) {                                                /* ten dai: UUID o goi quang ba, ten sang goi tra loi quet */
        struct ble_hs_adv_fields r;
        memset(&r, 0, sizeof r);
        r.name = f.name;
        r.name_len = f.name_len;
        r.name_is_complete = 1;
        f.name = NULL;
        f.name_len = 0;
        f.name_is_complete = 0;
        rc = ble_gap_adv_set_fields(&f);
        if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&r);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "khong dat duoc du lieu quang ba (rc=%d)", rc);
        return;
    }
    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "khong bat dau quang ba duoc (rc=%d)", rc);
        return;
    }
    if (s_state != 2) s_state = 1;
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            s_conn = ev->connect.conn_handle;
            ESP_LOGI(TAG, "dien thoai ket noi Bluetooth: cho dien thoai doc dich vu HLP va bat thong bao RX");
            if (s_gofa) {
                /* App Gofa bo ban tin dan duong neu vuot gioi han 1 lan ghi (MTU mac dinh 23 -> 20 byte):
                 * HUD xin MTU lon ngay khi noi. */
                int mrc = ble_gattc_exchange_mtu(s_conn, NULL, NULL);
                ESP_LOGI(TAG, "GOFA: xin MTU %d (rc=%d)", CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU, mrc);
                s_mtu = 23;
                wake(EV_GUP);
            }
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "dien thoai ngat Bluetooth (ly do %d)", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_state = 1;
        wake(EV_DOWN | (s_gofa ? EV_GDOWN : 0));
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "dien thoai %s thong bao handle %u", ev->subscribe.cur_notify || ev->subscribe.cur_indicate ? "bat" : "tat",
                 ev->subscribe.attr_handle);
        if (ev->subscribe.attr_handle == s_rx_handle) {           /* dien thoai bat/tat nhan thong bao tren RX */
            if (ev->subscribe.cur_notify) {
                s_state = 2;
                wake(EV_UP);
            } else {
                wake(EV_DOWN);
            }
        }
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
        s_mtu = ev->mtu.value;
        if (s_gofa) wake(EV_SNIFF);
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    s_synced = true;
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_addr_type) != 0) {
        ESP_LOGW(TAG, "khong co dia chi Bluetooth");
        return;
    }
    uint8_t a[6] = {0};
    ble_hs_id_copy_addr(s_addr_type, a, NULL);
    ESP_LOGI(TAG, "dang quang ba \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X, dich vu HLP RX handle %u): cho Waze Mod ket noi",
             ble_svc_gap_device_name(), a[5], a[4], a[3], a[2], a[1], a[0], (unsigned)s_rx_handle);
    /* Trang nap evcarhud.com nghe cong UART sau khi nap de doc MAC Bluetooth (chip C6) dang ky GOFA: giu dung dinh dang dong nay.
     * NimBLE luu dia chi nguoc thu tu nen in tu a[5] ve a[0]. */
    uint8_t m[6];
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, m, NULL) != 0) memcpy(m, a, sizeof m);
    printf("EVCARHUD_BT_MAC=%02X:%02X:%02X:%02X:%02X:%02X\n", m[5], m[4], m[3], m[2], m[1], m[0]);
    fflush(stdout);
    uint8_t be[6] = {m[5], m[4], m[3], m[2], m[1], m[0]};
    if (s_gofa) {                                                  /* ten HUD_xxxx + UUID dinh danh theo MAC that cua C6 */
        gofa_setup(be);
        ble_svc_gap_device_name_set(s_gofa_name);
    }
    char t[20];
    snprintf(t, sizeof t, "%02X:%02X:%02X:%02X:%02X:%02X", be[0], be[1], be[2], be[3], be[4], be[5]);
    board_ui_lock();
    ui_set_mac(t);                                                 /* trang Ket noi: MAC dung de dang ky GOFA */
    board_ui_unlock();
    advertise();
}

static void on_reset(int reason)
{
    if (++s_resets <= 3) ESP_LOGW(TAG, "ngan xep Bluetooth dat lai (ly do %d)", reason);   /* lap lai mai thi chi bao vai lan */
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();                                            /* chi tra ve khi nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static void ble_tx(const char *line)                              /* link.c goi (dang giu khoa LVGL): chi xep hang */
{
    size_t n = strlen(line);
    if (!s_tx_mb || n == 0 || n >= BLE_LINE_MAX) return;
    if (xMessageBufferSend(s_tx_mb, line, n, 0) == n) wake(EV_TX);
}

static void send_line(char *line, size_t n)                       /* line co cho cho them LF */
{
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    line[n++] = '\n';
    uint16_t mtu = ble_att_mtu(conn);
    size_t chunk = mtu > 23 ? (size_t)mtu - 3 : 20;
    for (size_t off = 0; off < n; off += chunk) {
        size_t len = n - off < chunk ? n - off : chunk;
        int rc = -1;
        for (int k = 0; k < 4 && rc != 0; k++) {                  /* het bo dem tam thoi: thu lai vai lan */
            struct os_mbuf *om = ble_hs_mbuf_from_flat(line + off, (uint16_t)len);
            rc = om ? ble_gatts_notify_custom(conn, s_rx_handle, om) : BLE_HS_ENOMEM;
            if (rc != 0) vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "gui thong bao that bai (rc=%d)", rc);
            return;
        }
    }
}

static bool bt_up(void)
{
    /* Board P4: song Bluetooth nam o chip ESP32-C6 (noi qua SDIO, esp_hosted tu khoi dong luc bat may), ngan xep NimBLE
     * chay tren P4. Cach chinh thuc (esp_hosted 3.x): gui lenh cho C6 bat bo dieu khien Bluetooth roi noi kenh HCI vao NimBLE.
     * Moi lan hoi ma C6 khong tra loi mat 5 s, nen chi thu dung mot lan. (Lay tu ban P4 cu, da chay that 11/10.) */
    esp_err_t e = ESP_FAIL;
#if !CONFIG_EVCARHUD_BLE_DIRECT_HCI
    esp_hosted_bt_host_stack_cfg_t cfg = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
    cfg.controller_ready_timeout_ms = 3000;                       /* sau khi tat han de thu lai, C6 can vai giay */
    e = esp_hosted_bt_host_stack_setup(&cfg);
    if (e != ESP_OK) ESP_LOGW(TAG, "C6 khong tra loi lenh bat Bluetooth (%s): thu dung thang kenh HCI", esp_err_to_name(e));
#endif
    if (e != ESP_OK) {
        /* Firmware C6 xuat xuong cua board (ban 0.0.0) khong hieu lenh do nhung tu bat Bluetooth san luc khoi dong
         * ("HCI over SDIO, BLE only" trong log khoi dong): noi thang kenh HCI vao NimBLE la chay. */
        esp_hosted_bt_host_stack_cfg_t direct = ESP_HOSTED_BT_HOST_STACK_CONFIG_DEFAULT();
        direct.bring_up_controller = false;
        e = esp_hosted_bt_host_stack_setup(&direct);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "khong noi duoc kenh HCI toi C6 (%s): bo qua Bluetooth", esp_err_to_name(e));
        return false;
    }
    e = nimble_port_init();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init: %s", esp_err_to_name(e));
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    s_gofa = settings_get_source() == UI_SRC_GOFA;
    if (s_gofa) {                                                  /* ten tam; on_sync dat lai theo MAC that */
        static const uint8_t z[6] = {0};
        gofa_setup(z);
    }
    if (s_gofa && rc == 0) rc = ble_gatts_count_cfg(s_gofa_svcs);
    if (s_gofa && rc == 0) rc = ble_gatts_add_svcs(s_gofa_svcs);
    if (rc == 0) rc = ble_svc_gap_device_name_set(s_gofa ? s_gofa_name : CONFIG_EVCARHUD_BLE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "dang ky dich vu GATT that bai (rc=%d)", rc);
        return false;
    }
    nimble_port_freertos_init(host_task);
    /* NimBLE tu gui lai lenh HCI Reset moi ~2 s. Luc khoi dong C6 con ban do song / vao Wi-Fi nen bo dieu khien BT co the
     * khong tra loi vai giay dau (log: "HCI wait for ack returned 19") -> cho 20 s thay vi 5 s. */
    for (int i = 0; i < 200 && !s_synced; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!s_synced) {
        ESP_LOGW(TAG, "bo dieu khien Bluetooth khong tra loi 20 s: tat han roi thu lai");
        if (nimble_port_stop() == 0) nimble_port_deinit();
        esp_hosted_bt_host_stack_teardown();                  /* tat + deinit bo dieu khien tren C6 de lan sau bat lai tu dau */
        return false;
    }
    return true;
}

static void ble_task(void *arg)
{
    (void)arg;
    static char line[BLE_LINE_MAX + 2];
    static uint8_t rx[256];
    vTaskDelay(pdMS_TO_TICKS(START_DELAY_MS));
    bool up = false;
    s_resets = 0;
    for (int k = 0; k < BT_TRIES && !(up = bt_up()); k++) {
        ESP_LOGW(TAG, "Bluetooth chua len (lan %d/%d): thu lai sau 10 s", k + 1, BT_TRIES);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    if (!up) {
        ESP_LOGE(TAG, "Bluetooth khong len sau %d lan: bo qua (rut dien HUD de C6 khoi dong lai)", BT_TRIES);
        s_state = -1;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        uint32_t ev = 0;
        xTaskNotifyWait(0, UINT32_MAX, &ev, pdMS_TO_TICKS(1000));
        if ((ev & EV_DOWN) && s_linked) {
            s_linked = false;
            link_down(LINK_BLE);
            xStreamBufferReset(s_rx_sb);
            xMessageBufferReset(s_tx_mb);
        }
        if ((ev & EV_UP) && !s_linked && s_conn != BLE_HS_CONN_HANDLE_NONE) {
            s_linked = true;
            link_up(LINK_BLE, ble_tx);                            /* HUD gui `dev` truoc (qua ble_tx -> hang doi) */
        }
        if (s_gofa && (ev & (EV_GUP | EV_GDOWN))) {
            bool up = s_conn != BLE_HS_CONN_HANDLE_NONE;
            board_ui_lock();
            gofa_connected(up);
            gofa_show("", up ? "Đã kết nối app Gofa" : "Mất kết nối app Gofa");
            board_ui_unlock();
        }
        if (s_gofa && (ev & EV_SNIFF)) {
            if (s_gofa_q && uxQueueMessagesWaiting(s_gofa_q) == 0) {   /* chi doi MTU: cap nhat dai */
                board_ui_lock();
                gofa_show("", "");
                board_ui_unlock();
            }
            gofa_drain();
        }
        size_t n;
        while ((n = xStreamBufferReceive(s_rx_sb, rx, sizeof rx, 0)) > 0) {
            if (s_linked) link_rx(LINK_BLE, rx, n, false);        /* ghep dong theo LF nam trong bo giai ma */
        }
        while ((n = xMessageBufferReceive(s_tx_mb, line, BLE_LINE_MAX, 0)) > 0) send_line(line, n);
    }
}

void ble_link_start(void)
{
    if (s_task) return;
    s_rx_sb = xStreamBufferCreate(2048, 1);
    s_tx_mb = xMessageBufferCreate(2048);
    s_gofa_q = xQueueCreate(12, sizeof(gofa_pkt_t));
    if (!s_rx_sb || !s_tx_mb) return;
    if (xTaskCreate(ble_task, "ble_link", 5120, NULL, 4, &s_task) != pdPASS) s_task = NULL;
}

int ble_link_state(void) { return s_state; }

#else  /* Bluetooth tat trong menuconfig */

void ble_link_start(void) {}
int ble_link_state(void) { return 0; }

#endif
