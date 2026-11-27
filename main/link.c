#include "link.h"
#include "board.h"
#include "dashboard_hlp.h"
#include "esp_log.h"

static const char *TAG = "link";
static const char *const NAME[LINK_N] = {"Wi-Fi", "Bluetooth"};
static link_tx_fn s_tx[LINK_N];
static bool s_up[LINK_N];
static int s_active = -1;                              /* moi bien o day chi doc/ghi khi dang giu khoa LVGL */

static void tx_cb(const char *line, void *user)        /* dashboard_hlp goi (dang giu khoa LVGL) */
{
    (void)user;
    if (s_active >= 0 && s_tx[s_active]) s_tx[s_active](line);
}

void link_init(void)
{
    board_ui_lock();
    hlp_dash_init(tx_cb, NULL);
    board_ui_unlock();
}

void link_up(int id, link_tx_fn tx)
{
    if (id < 0 || id >= LINK_N) return;
    board_ui_lock();
    s_tx[id] = tx;
    s_up[id] = true;
    if (s_active < 0 || s_active == id) {              /* chua co duong nao (hoac chinh duong nay noi lai): dung no */
        s_active = id;
        hlp_dash_on_connect();                         /* gui `dev` xin day du truong */
        ESP_LOGI(TAG, "dung duong %s", NAME[id]);
    } else {
        ESP_LOGI(TAG, "%s cung vua noi nhung dang dung %s: de du phong", NAME[id], NAME[s_active]);
    }
    board_ui_unlock();
}

void link_down(int id)
{
    if (id < 0 || id >= LINK_N) return;
    board_ui_lock();
    s_up[id] = false;
    s_tx[id] = NULL;
    if (s_active == id) {
        hlp_dash_on_disconnect();
        s_active = -1;
        for (int k = 0; k < LINK_N; k++) {
            if (s_up[k]) {                             /* duong kia van noi: chuyen sang */
                s_active = k;
                hlp_dash_on_connect();
                ESP_LOGI(TAG, "%s rot, chuyen sang %s", NAME[id], NAME[k]);
                break;
            }
        }
    }
    board_ui_unlock();
}

void link_rx(int id, const uint8_t *data, size_t len, bool whole_line)
{
    if (!data || len == 0) return;
    board_ui_lock();
    if (id == s_active) {
        hlp_dash_feed(data, len);
        if (whole_line) hlp_dash_feed((const uint8_t *)"\n", 1);
    }
    board_ui_unlock();
}

int link_active(void)
{
    board_ui_lock();
    int a = s_active;
    board_ui_unlock();
    return a;
}
