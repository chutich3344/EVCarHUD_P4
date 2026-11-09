/* Loa cua board P4: codec ES8311 (I2C 0x18, bus dung chung voi cam ung) + I2S + mach khuech dai (bat bang GPIO53).
 * audio_start() tao task rieng khoi tao codec; audio_beep() chi xep lenh, khong chan (goi duoc trong khoa LVGL). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void audio_start(void);              /* goi mot lan sau board_display_init() (can bus I2C dung chung) */
void audio_beep(int times);          /* phat `times` tieng tit ngan */
void audio_set_volume(int percent);  /* 0..100, 0 = tat tieng */

/* Tro ly AI: 16 kHz, 16 bit, 1 kenh (micro va loa cung la ES8311) */
#include <stdbool.h>
#include <stdint.h>
bool audio_ready(void);
int audio_mic_read(int16_t *buf, int n);      /* chan cho du n mau; -1 neu loi */
void audio_play(const int16_t *pcm, int n);   /* chan cho den khi day xong xuong I2S */

#ifdef __cplusplus
}
#endif
