/* Tro ly giong noi "Mi Mi" (ban 1: cham de noi, chua co tu danh thuc).
 * Cham nut micro goc tren phai: HUD hoi evcarhud.com/api/ai/ota (Device-Id = MAC) lay dia chi WebSocket + token,
 * noi wss, gui micro dang Opus 16 kHz / 60 ms, phat giong tra loi (Opus) ra loa ES8311. Giao thuc giong Xiaozhi.
 * Dai tim o day man hien chu: DANG NGHE / ANH HOI / MI MI. Cham dai luc Mi Mi dang noi: ngat loi, nghe tiep;
 * cham luc dang nghe / dang nghi: ket thuc. May chu dong phien sau 30 giay im lang. */
#pragma once
#include <stdbool.h>

void ai_start(void);                 /* goi sau dashboard_create + audio_start (ngoai khoa LVGL) */
void ai_wifi_changed(bool up);
void ai_station_cancel(void);        /* app Charge gui tram qua BLE: Mi Mi thoi giu dai tram cua minh */
void ai_wake_sens_changed(void);     /* Cai dat doi do nhay goi "Mi Mi": ap nguong moi ngay */       /* wifi_link.c: co / mat mang */
