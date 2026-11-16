/* Phan cung Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5: man hinh ST7796 (SPI) 3.5 inch xoay ngang 480x320, cam ung FT6336 (I2C),
 * den nen, loa (codec ES8311). Board khong co LED RGB va quang tro: cac ham do la ham rong de main.c giong ban S3. */
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "driver/i2c_master.h"

lv_display_t *board_display_init(void);      /* LVGL + man hinh ngang 480x320 + cam ung + loa; den nen van tat */
bool board_ui_lock(void);                    /* moi thao tac LVGL ngoai task LVGL phai nam trong lock/unlock */
void board_ui_unlock(void);
void board_set_rotated(bool on);             /* xoay hinh + cam ung 180 do (khong luu: main.c luu NVS) */
bool board_get_rotated(void);
const char *board_lcd_name(void);            /* "ST7796" */
void board_set_backlight(int percent);       /* 0..100 */
void board_set_led(bool r, bool g, bool b);  /* board P4 khong co LED RGB: ham rong */
void board_set_volume(int percent);          /* am luong loa 0..100 (0 = tat tieng) */
void board_beep(int times);                  /* tit ngan qua loa (khong chan: xep cho task rieng) */
int board_light_raw(void);                   /* khong co quang tro: luon -1 */
i2c_master_bus_handle_t board_i2c_bus(void); /* bus I2C dung chung (cam ung, codec ES8311); NULL neu chua tao */
