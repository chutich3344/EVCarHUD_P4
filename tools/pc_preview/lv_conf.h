/* LVGL cho anh xem truoc tren PC: giong cau hinh tren board (16 bit mau, ve phan mem). */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64 * 1024U)
#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_COMPLEX 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#endif
