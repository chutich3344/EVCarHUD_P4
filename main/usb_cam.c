/* Camera USB (UVC) - ban thu nghiem: xem usb_cam.h.
 *
 * Luong du lieu:
 *   USB Host (cong OTG 2.0 High Speed) -> driver usb_host_uvc -> frame_cb (xep hang) -> task "usb_cam" (loi 1):
 *   chep khung MJPEG vao bo dem canh hang DMA, chen bang Huffman chuan neu camera bo di (webcam MJPEG hay bo DHT),
 *   giai ma JPEG phan cung ra RGB565 640x480 -> thu nho 1/2 -> 320x240 -> doi bo dem cua lv_canvas (trong khoa LVGL).
 * Camera da thu: RMONCAM G200/A3, chip Realtek VID 0BDA PID 5832 (MJPEG 30 fps o 640x480 / 320x240). */
#include "usb_cam.h"
#include "sdkconfig.h"
#if CONFIG_EVCARHUD_USB_CAM

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "usb/uvc_host.h"
#include "driver/jpeg_decode.h"

#include "adas_feed.h"
#include "board.h"
#include "detect.h"
#include "obstacle_ui.h"
#include "dashboard.h"
#include "ui_fonts.h"

static const char *TAG = "usb_cam";

#define CAM_W 640
#define CAM_H 480
#define PV_W (CAM_W / 2)            /* khung xem truoc 320x240 */
#define PV_H (CAM_H / 2)
#define IN_MAX (256 * 1024)         /* khung MJPEG lon nhat nhan giai ma (640x480 thuong 30-90 KB) */
#define N_FRAMES 3

/* ---------------- bang Huffman chuan (JPEG Annex K.3) cho khung MJPEG khong co DHT ---------------- */
static const uint8_t k_dht[] = {
    0xFF, 0xC4, 0x01, 0xA2,
    /* DC do sang */
    0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    /* AC do sang */
    0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
    /* DC mau */
    0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    /* AC mau */
    0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
};

/* Tim vi tri chen DHT: tra ve vi tri marker SOS (FFDA) neu khung KHONG co DHT, 0 neu da co / loi. */
static size_t dht_insert_pos(const uint8_t *p, size_t n)
{
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 0;
    size_t i = 2;
    while (i + 4 <= n) {
        if (p[i] != 0xFF) return 0;
        uint8_t m = p[i + 1];
        if (m == 0xC4) return 0;                 /* da co bang Huffman */
        if (m == 0xDA) return i;                 /* toi SOS ma chua thay DHT */
        size_t len = ((size_t)p[i + 2] << 8) | p[i + 3];
        i += 2 + len;
    }
    return 0;
}

/* ---------------- trang thai ---------------- */
static QueueHandle_t s_q;                         /* uvc_host_frame_t* tu frame_cb */
static uvc_host_stream_hdl_t s_stream;
static volatile bool s_connected;
static jpeg_decoder_handle_t s_jpeg;
static uint8_t *s_in;  static size_t s_in_cap;    /* bo dem vao bo giai ma (canh hang DMA) */
static uint16_t *s_rgb; static size_t s_rgb_cap;  /* anh RGB565 640x480 */
static uint16_t *s_pv[2];                         /* 2 bo dem xem truoc 320x240 (canvas doi qua lai) */
static int s_pv_back;
static unsigned s_fmt_w, s_fmt_h;

/* giao dien */
static lv_obj_t *s_box, *s_canvas, *s_info;
static volatile bool s_hidden;

/* so do (task usb_cam ghi, cap nhat nhan moi giay) */
static int s_frames, s_decoded, s_shown, s_errs;
static int64_t s_dec_us_sum;
static int s_det_runs, s_det_ms_sum, s_det_last = -1;
static int64_t s_shown_since;                    /* luc khung xem truoc hien (tu an sau PREVIEW_S giay) */
#define PREVIEW_S 20

/* ---------------- USB ---------------- */
static bool frame_cb(const uvc_host_frame_t *frame, void *ctx)
{
    (void)ctx;
    if (xQueueSendToBack(s_q, &frame, 0) != pdPASS) return true;   /* hang day: tra khung lai ngay */
    return false;                                                    /* task usb_cam tra khung sau */
}

static void stream_cb(const uvc_host_stream_event_data_t *ev, void *ctx)
{
    (void)ctx;
    switch (ev->type) {
    case UVC_HOST_TRANSFER_ERROR:
        ESP_LOGW(TAG, "loi truyen USB %d", ev->transfer_error.error);
        break;
    case UVC_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "camera bi rut ra");
        s_connected = false;
        uvc_host_stream_close(ev->device_disconnected.stream_hdl);
        s_stream = NULL;
        break;
    case UVC_HOST_FRAME_BUFFER_OVERFLOW:
        ESP_LOGW(TAG, "khung qua lon so voi bo dem");
        break;
    case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
        break;                                     /* het bo dem trong: task xu ly cham hon camera, bo qua khung */
    default:
        break;
    }
}

static void usb_lib_task(void *arg)
{
    (void)arg;
    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

/* Mo camera: thu MJPEG 640x480@30, roi 320x240, roi dinh dang mac dinh cua camera. */
static bool open_stream(void)
{
    static const uvc_host_stream_format_t tries[] = {
        {CAM_W, CAM_H, 30, UVC_VS_FORMAT_MJPEG},
        {320, 240, 30, UVC_VS_FORMAT_MJPEG},
        {0, 0, 0, UVC_VS_FORMAT_MJPEG},
    };
    for (size_t i = 0; i < sizeof tries / sizeof tries[0]; i++) {
        uvc_host_stream_config_t cfg = {
            .event_cb = stream_cb,
            .frame_cb = frame_cb,
            .user_ctx = NULL,
            .usb = {.dev_addr = UVC_HOST_ANY_DEV_ADDR, .vid = UVC_HOST_ANY_VID, .pid = UVC_HOST_ANY_PID, .uvc_stream_index = 0},
            .vs_format = tries[i],
            .advanced = {
                .number_of_frame_buffers = N_FRAMES,
                .frame_size = 0,                       /* theo dwMaxVideoFrameSize camera bao */
                .frame_heap_caps = MALLOC_CAP_SPIRAM,
                .number_of_urbs = 3,
                .urb_size = 10 * 1024,
            },
        };
        /* lan dau cho lau (doi cam camera); cac dinh dang du phong chi cho ngan */
        esp_err_t err = uvc_host_stream_open(&cfg, pdMS_TO_TICKS(i == 0 ? 5000 : 1500), &s_stream);
        if (err == ESP_OK) {
            uvc_host_stream_format_t f;
            if (uvc_host_stream_format_get(s_stream, &f) == ESP_OK) {
                s_fmt_w = f.h_res;
                s_fmt_h = f.v_res;
            } else {
                s_fmt_w = tries[i].h_res;
                s_fmt_h = tries[i].v_res;
            }
            ESP_LOGI(TAG, "da mo camera MJPEG %ux%u", s_fmt_w, s_fmt_h);
            return true;
        }
        if (i == 0 && (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND)) return false;   /* chua cam camera */
        ESP_LOGW(TAG, "khong mo duoc dinh dang %d (%s)", (int)i, esp_err_to_name(err));
    }
    return false;
}

/* ---------------- giao dien ---------------- */
static void on_box_click(lv_event_t *e)
{
    (void)e;
    s_hidden = true;
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create(void)
{
    s_box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_box);
    lv_obj_set_size(s_box, PV_W + 8, PV_H + 8 + 50);
    lv_obj_align(s_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_hex(0x10131E), 0);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_box, 10, 0);
    lv_obj_set_style_border_color(s_box, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_border_width(s_box, 2, 0);
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_box, on_box_click, LV_EVENT_CLICKED, NULL);

    s_canvas = lv_canvas_create(s_box);
    lv_canvas_set_buffer(s_canvas, s_pv[0], PV_W, PV_H, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_obj_set_pos(s_canvas, 4, 4);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    s_info = lv_label_create(s_box);
    lv_obj_set_width(s_info, PV_W);
    lv_obj_set_pos(s_info, 4, PV_H + 8);
    lv_obj_set_style_text_font(s_info, &ui_font_sm12, 0);
    lv_obj_set_style_text_color(s_info, lv_color_hex(0xE5E7EB), 0);
    lv_label_set_text(s_info, "Camera USB: đang chờ ảnh...\nChạm để ẩn");
    lv_obj_set_style_text_line_space(s_info, -1, 0);
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
}

static void ui_show(bool on)
{
    board_ui_lock();
    if (on) {
        s_hidden = false;
        s_shown_since = esp_timer_get_time();
        lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_box);
    } else {
        lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    }
    board_ui_unlock();
}

static void stats_tick(void)
{
    static int64_t last;
    int64_t now = esp_timer_get_time();
    if (!last) last = now;
    if (now - last < 1000000) return;
    float sec = (now - last) / 1e6f;
    int dec_ms = s_decoded ? (int)(s_dec_us_sum / s_decoded / 1000) : 0;
    char txt[200];
    int det_ms = s_det_runs ? s_det_ms_sum / s_det_runs : 0;
    snprintf(txt, sizeof txt, "Camera %ux%u · nhận %.0f khung/s · giải mã %d ms\nNhận diện %d ms (%.0f lần/s) · %d vật · lỗi %d\nChạm để ẩn",
             s_fmt_w, s_fmt_h, s_frames / sec, dec_ms, det_ms, s_det_runs / sec, s_det_last < 0 ? 0 : s_det_last, s_errs);
    ESP_LOGI(TAG, "%s", txt);
    board_ui_lock();
    lv_label_set_text(s_info, txt);
    if (!s_hidden && s_shown_since && now - s_shown_since > PREVIEW_S * 1000000LL) {   /* dang chay xe: tu an */
        s_hidden = true;
        lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    }
    board_ui_unlock();
    s_frames = s_decoded = s_shown = s_det_runs = s_det_ms_sum = 0;
    s_dec_us_sum = 0;
    last = now;
}

/* RGB565 w x h -> 320x240 (lay mau cach deu; 640x480 la dung 1/2) */
static void downscale(const uint16_t *src, unsigned w, unsigned h, uint16_t *dst)
{
    for (unsigned y = 0; y < PV_H; y++) {
        const uint16_t *row = src + (size_t)(y * h / PV_H) * w;
        uint16_t *out = dst + (size_t)y * PV_W;
        for (unsigned x = 0; x < PV_W; x++) out[x] = row[x * w / PV_W];
    }
}

/* Ve khung chu nhat 2 diem anh len anh xem truoc */
static void draw_rect(uint16_t *img, int x1, int y1, int x2, int y2, uint16_t c)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > PV_W - 1) x2 = PV_W - 1;
    if (y2 > PV_H - 1) y2 = PV_H - 1;
    if (x2 - x1 < 3 || y2 - y1 < 3) return;
    for (int t = 0; t < 2; t++) {
        for (int x = x1; x <= x2; x++) {
            img[(y1 + t) * PV_W + x] = c;
            img[(y2 - t) * PV_W + x] = c;
        }
        for (int y = y1; y <= y2; y++) {
            img[y * PV_W + x1 + t] = c;
            img[y * PV_W + x2 - t] = c;
        }
    }
}

/* Chep khung MJPEG (+ chen DHT) vao bo dem cua bo giai ma; false neu khung hong / qua lon. Sau ham nay tra khung ngay. */
static size_t s_need;
static bool copy_frame(const uvc_host_frame_t *f)
{
    size_t n = f->data_len;
    size_t pos = dht_insert_pos(f->data, n);
    s_need = n + (pos ? sizeof k_dht : 0);
    if (s_need > s_in_cap || n < 4) {
        s_errs++;
        return false;
    }
    if (pos) {
        memcpy(s_in, f->data, pos);
        memcpy(s_in + pos, k_dht, sizeof k_dht);
        memcpy(s_in + pos + sizeof k_dht, f->data + pos, n - pos);
    } else {
        memcpy(s_in, f->data, n);
    }
    return true;
}

/* Giai ma -> thu nho 320x240 -> nhan dien vat can -> canh bao; neu khung xem truoc dang hien: ve khung + doi bo dem canvas. */
static void process_frame(void)
{
    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(s_in, s_need, &info) != ESP_OK || !info.width || !info.height ||
        (size_t)((info.width + 15) & ~15u) * ((info.height + 15) & ~15u) * 2 > s_rgb_cap) {
        s_errs++;
        return;
    }
    jpeg_decode_cfg_t dcfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,     /* RGB565 little-endian = dinh dang LVGL; ESP-DL: RGB565LE */
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_len = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = jpeg_decoder_process(s_jpeg, &dcfg, s_in, s_need, (uint8_t *)s_rgb, s_rgb_cap, &out_len);
    if (err != ESP_OK) {
        s_errs++;
        return;
    }
    s_dec_us_sum += esp_timer_get_time() - t0;
    s_decoded++;

    /* bo giai ma xuat theo khoi 16 diem anh: chieu rong dong lam tron len boi 16 */
    unsigned stride_w = (info.width + 15) & ~15u;
    uint16_t *dst = s_pv[s_pv_back];
    downscale(s_rgb, stride_w, info.height, dst);

#if CONFIG_EVCARHUD_DETECT
    detect_box_t bx[DET_BOX_MAX];
    int ms = 0;
    int nb = detect_run_rgb565((const uint8_t *)dst, PV_W, PV_H, bx, DET_BOX_MAX, &ms);
    s_det_last = nb;
    if (nb >= 0) {
        s_det_runs++;
        s_det_ms_sum += ms;
        adas_feed(bx, nb, PV_W, PV_H);
        if (!s_hidden) {
            for (int i = 0; i < nb; i++) {
                /* vang: vat dang canh bao (xe phia truoc), xanh: vat khac */
                draw_rect(dst, bx[i].x1, bx[i].y1, bx[i].x2, bx[i].y2, bx[i].dist_m > 0 ? 0xFFE0 : 0x07E0);
            }
        }
    }
#endif

    if (s_hidden) return;
    board_ui_lock();
    lv_canvas_set_buffer(s_canvas, dst, PV_W, PV_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(s_canvas);
    board_ui_unlock();
    s_pv_back ^= 1;
    s_shown++;
}

static void cam_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!open_stream()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        s_connected = true;
        xQueueReset(s_q);
        if (uvc_host_stream_start(s_stream) != ESP_OK) {
            ESP_LOGE(TAG, "khong bat duoc luong anh");
            uvc_host_stream_close(s_stream);
            s_stream = NULL;
            s_connected = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        ui_show(true);
        while (s_connected) {
            uvc_host_frame_t *f;
            if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(500)) == pdPASS) {
                s_frames++;
                bool ok = copy_frame(f);
                if (s_stream) uvc_host_frame_return(s_stream, f);   /* tra khung ngay: camera nap tiep trong luc giai ma */
                if (ok) process_frame();
            }
            stats_tick();
        }
        ui_show(false);
        adas_feed_lost();
    }
}

void usb_cam_start(void)
{
    /* bo dem: giai ma ra RGB565 640x480 (dong lam tron 16) + 2 khung xem truoc trong PSRAM */
    jpeg_decode_memory_alloc_cfg_t in_cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    jpeg_decode_memory_alloc_cfg_t out_cfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    s_in = jpeg_alloc_decoder_mem(IN_MAX, &in_cfg, &s_in_cap);
    s_rgb = jpeg_alloc_decoder_mem((size_t)CAM_W * CAM_H * 2, &out_cfg, &s_rgb_cap);
    for (int i = 0; i < 2; i++) s_pv[i] = heap_caps_calloc(PV_W * PV_H, 2, MALLOC_CAP_SPIRAM);
    s_q = xQueueCreate(N_FRAMES, sizeof(uvc_host_frame_t *));
    jpeg_decode_engine_cfg_t eng = {.intr_priority = 0, .timeout_ms = 100};
    if (!s_in || !s_rgb || !s_pv[0] || !s_pv[1] || !s_q || jpeg_new_decoder_engine(&eng, &s_jpeg) != ESP_OK) {
        ESP_LOGE(TAG, "thieu bo nho / khong mo duoc bo giai ma JPEG: tat camera USB");
        return;
    }

    board_ui_lock();
    ui_create();
    obstacle_ui_init();
    board_ui_unlock();

    usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if (usb_host_install(&host_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "khong cai duoc USB Host");
        return;
    }
    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 15, NULL, 1);

    uvc_host_driver_config_t uvc_cfg = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 14,
        .xCoreID = 1,
        .create_background_task = true,
        .event_cb = NULL,
        .user_ctx = NULL,
    };
    if (uvc_host_install(&uvc_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "khong cai duoc driver UVC");
        return;
    }
    xTaskCreatePinnedToCore(cam_task, "usb_cam", 16384, NULL, 5, NULL, 0);   /* stack lon: ESP-DL chay trong task nay */
    ESP_LOGI(TAG, "cho camera USB o cong OTG...");
}
#endif /* CONFIG_EVCARHUD_USB_CAM */
