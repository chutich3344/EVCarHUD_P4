#include "buttons.h"
#include "board.h"
#include "dashboard.h"
#include "settings.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 20
#define LONG_MS 5000
#define DOUBLE_MS 400                                              /* 2 lan bam cach nhau toi da 0,4 s */
#define SET_MS 1000                                                /* giu 1-5 s roi tha: mo cai dat */

static const char *TAG = "nut";

static void btn_task(void *arg)
{
    (void)arg;
    int held = 0, since = -1;                                      /* since: ms tu lan bam nha truoc, -1 = khong cho */
    bool fired = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        bool down = gpio_get_level(CONFIG_EVCARHUD_PIN_BOOT) == 0;
        if (down) {
            held += POLL_MS;
            if (held >= LONG_MS && !fired) {                       /* giu 5 s: doi che do trinh dien */
                fired = true;
                bool demo = !settings_get_demo();
                settings_set_demo(demo);
                ESP_LOGI(TAG, "che do trinh dien: %s", demo ? "bat" : "tat");
                board_ui_lock();
                ui_toast(demo ? "Trình diễn: BẬT" : "Trình diễn: TẮT");
                board_ui_unlock();
                vTaskDelay(pdMS_TO_TICKS(1200));
                esp_restart();
            }
            continue;
        }
        if (held >= SET_MS && !fired) {                            /* giu 1-5 s roi tha: mo cai dat */
            since = -1;
            board_ui_lock();
            if (!ui_settings_shown()) ui_open_settings();
            board_ui_unlock();
        } else if (held >= 40 && !fired) {                         /* bam nha (loc rung 40 ms) */
            if (since >= 0) {                                      /* lan bam thu 2: cai dat = chon, ngoai = xoay */
                since = -1;
                board_ui_lock();
                if (!ui_settings_key(UI_KEY_OK)) ui_rotate();
                board_ui_unlock();
            } else {
                since = 0;                                         /* doi xem co lan bam thu 2 khong */
            }
        } else if (since >= 0 && (since += POLL_MS) > DOUBLE_MS) { /* chi bam 1 lan: cai dat = muc ke, ngoai = Ngay/Dem */
            since = -1;
            board_ui_lock();
            if (!ui_settings_key(UI_KEY_NEXT)) ui_cycle_mode();
            board_ui_unlock();
        }
        held = 0;
        fired = false;
    }
}

void buttons_start(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << CONFIG_EVCARHUD_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
    xTaskCreate(btn_task, "nut", 3072, NULL, 3, NULL);
}
