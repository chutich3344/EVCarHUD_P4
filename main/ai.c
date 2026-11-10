/* Tro ly giong noi "Mi Mi" tren HUD P4 (xem ai.h). Giao thuc WebSocket kieu Xiaozhi, may chu evcarhud-web (/api/ai/...).
 *  - Luong dieu khien (ai_ctl): mo / dong phien, xu ly JSON tu may chu (stt, tts, mcp), cap nhat giao dien.
 *  - Luong micro (ai_mic): khi DANG NGHE: doc 60 ms tu ES8311 -> Opus -> gui nhi phan. May chu tu nhan biet het cau (VAD
 *    theo kich thuoc goi Opus: bat DTX nen im lang thi goi rat nho).
 *  - Luong loa (ai_spk): goi Opus tu may chu -> giai ma 16 kHz -> loa. Phat het cau cuoi (tts stop) thi nghe tiep. */
#include "ai.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_websocket_client.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "audio.h"
#include "board.h"
#include "dashboard.h"
#include "settings.h"
#include "wifi_link.h"
#include "ui_fonts.h"

#define FRAME_MS   60
#define RATE       16000
#define FRAME      (RATE * FRAME_MS / 1000)           /* 960 mau moi goi */
#define PLAY_Q     200                                /* ~12 giay giong noi cho phat */
#define LISTEN_MAX_MS 17000                           /* luoi an toan sau VAD_MAX_FR (15 s): VAD luon bao truoc */
#define THINK_MAX_MS  20000                           /* cho may chu tra loi */

static const char *TAG = "ai";

enum { ST_IDLE = 0, ST_CONN, ST_LISTEN, ST_THINK, ST_SPEAK };
static float s_tts_peak = 8000.0f;                  /* dinh tieng Mi Mi gan day (luong loa) */
static volatile bool s_heard;                         /* VAD da nghe thay anh noi trong luot nghe nay */
static volatile float s_vad_peak, s_vad_thr;         /* luot nghe gan nhat: nang luong to nhat / nguong */
static volatile int s_close_code, s_sock_errno;       /* ly do mat ket noi thoai: ma dong tu may chu / errno socket */
static volatile char s_close_ev;
static volatile int s_rx_txt, s_rx_bin;               /* so tin chu / goi tieng nhan duoc trong phien (chan doan) */
static volatile bool s_greet;                         /* dang cho cau chao dau phien (de bao loi ro rang) */
enum { M_TAP = 1, M_WAKE, M_JSON, M_CLOSED, M_DRAINED, M_WIFI_DOWN, M_VAD_END, M_VAD_NONE, M_VAD_HEARD };
typedef struct {
    int type;
    int gen;                                          /* phien nao gui (bo tin cua phien cu) */
    cJSON *json;
} ai_msg_t;
typedef struct {
    int len;
    uint8_t data[];
} pkt_t;

#define BIT_CONNECTED BIT0
#define BIT_HELLO     BIT1

static QueueHandle_t s_ctl, s_play;
static EventGroupHandle_t s_ev;
static esp_websocket_client_handle_t s_ws;
static volatile int s_st = ST_IDLE;
static volatile int s_gen;
static volatile bool s_wifi;
static volatile bool s_drop;                          /* da ngat loi: bo goi giong con lai */
static volatile bool s_tts_stop;                      /* may chu bao het cau: phat xong thi nghe tiep */
static bool s_tasks;
static char s_mac[18], s_uuid[37], s_session[64];
static char *s_txt;                                   /* gom tin chu bi chia nhieu doan */
static int s_txt_len;
static TickType_t s_since;
static volatile bool s_wake_ok;
static volatile int s_ws_http, s_ws_tls;              /* loi bat tay WebSocket gan nhat: ma HTTP, loi TLS */                       /* co mo hinh tu danh thuc (vung flash "model") */
static char s_ws_url[256], s_ws_tok[256];             /* nho dia chi + token: phien sau khoi hoi lai may chu */

/* VAD tren chip (do nang luong): biet anh noi xong de gui "listen stop" ngay, khong cho may chu doan */
#define VAD_SKIP_FR   5                               /* 300 ms dau bo qua: tieng tit + vang loa */
#define VAD_FLOOR_FR  5                               /* 300 ms tiep: lay nen on (min) */
#define VAD_START_FR  3                               /* 180 ms co tieng: bat dau noi */
#define VAD_END_FR    14                              /* 840 ms im: noi xong */
#define VAD_NONE_FR   134                             /* ~8 s khong noi gi: dong phien */
#define VAD_MAX_FR    250                             /* cau dai nhat 15 s */

/* ---------------- giao dien ---------------- */
static lv_obj_t *g_btn, *g_strip, *g_orb, *g_cap, *g_text;

static void orb_anim_cb(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static void on_ui_tap(lv_event_t *e)
{
    (void)e;
    ai_msg_t m = {.type = M_TAP};
    if (s_ctl) xQueueSend(s_ctl, &m, 0);
}

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void ui_build(void)
{
    lv_obj_t *scr = ui_screen();
    if (!scr) return;
    /* nut micro: tron 34 px, ve micro bang hinh (font khong co bieu tuong micro) */
    g_btn = box(scr, UI_W - 44, 40, 34, 34);
    lv_obj_set_style_radius(g_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_btn, lv_color_hex(0x2A2550), 0);
    lv_obj_set_style_bg_opa(g_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_color(g_btn, lv_color_hex(0x7C5CFF), 0);
    lv_obj_set_style_border_width(g_btn, 1, 0);
    lv_obj_set_ext_click_area(g_btn, 10);
    lv_obj_add_flag(g_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_btn, on_ui_tap, LV_EVENT_CLICKED, NULL);
    lv_color_t mc = lv_color_hex(0xC9BCFF);
    lv_obj_t *cap = box(g_btn, 13, 7, 8, 13);         /* than micro */
    lv_obj_set_style_radius(cap, 4, 0);
    lv_obj_set_style_bg_color(cap, mc, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_t *u = box(g_btn, 10, 13, 14, 11);         /* vong chu U */
    lv_obj_set_style_radius(u, 7, 0);
    lv_obj_set_style_border_color(u, mc, 0);
    lv_obj_set_style_border_width(u, 2, 0);
    lv_obj_set_style_border_side(u, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_t *st = box(g_btn, 16, 24, 2, 4);          /* chan */
    lv_obj_set_style_bg_color(st, mc, 0);
    lv_obj_set_style_bg_opa(st, LV_OPA_COVER, 0);
    ui_below_settings(g_btn);

    /* dai tim o day man */
    g_strip = box(scr, 12, UI_H - 62, UI_W - 24, 50);
    lv_obj_set_style_radius(g_strip, 12, 0);
    lv_obj_set_style_bg_color(g_strip, lv_color_hex(0x2A2550), 0);
    lv_obj_set_style_bg_grad_color(g_strip, lv_color_hex(0x1B3550), 0);
    lv_obj_set_style_bg_grad_dir(g_strip, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(g_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_strip, lv_color_hex(0x7C5CFF), 0);
    lv_obj_set_style_border_width(g_strip, 1, 0);
    lv_obj_add_flag(g_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_strip, on_ui_tap, LV_EVENT_CLICKED, NULL);
    g_orb = box(g_strip, 8, 8, 34, 34);
    lv_obj_set_style_radius(g_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_orb, lv_color_hex(0x7C5CFF), 0);
    lv_obj_set_style_bg_grad_color(g_orb, lv_color_hex(0x22B8FF), 0);
    lv_obj_set_style_bg_grad_dir(g_orb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(g_orb, LV_OPA_COVER, 0);
    g_cap = lv_label_create(g_strip);
    lv_obj_set_style_text_font(g_cap, &ui_font_sm12, 0);
    lv_obj_set_style_text_color(g_cap, lv_color_hex(0xC9BCFF), 0);
    lv_obj_set_pos(g_cap, 52, 5);
    g_text = lv_label_create(g_strip);
    lv_obj_set_style_text_font(g_text, &ui_font_txt16, 0);
    lv_obj_set_style_text_color(g_text, lv_color_white(), 0);
    lv_obj_set_pos(g_text, 52, 22);
    lv_obj_set_width(g_text, UI_W - 24 - 52 - 10);
    lv_label_set_long_mode(g_text, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(g_text, "");
    lv_obj_add_flag(g_strip, LV_OBJ_FLAG_HIDDEN);
    ui_below_settings(g_strip);
}

/* cap = NULL: an dai; text = NULL: giu chu cu */
static void ui_show(const char *cap, const char *text, bool pulse)
{
    if (!g_strip) return;
    board_ui_lock();
    if (!cap) {
        lv_anim_delete(g_orb, orb_anim_cb);
        lv_obj_add_flag(g_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(g_btn, lv_color_hex(0x2A2550), 0);
    } else {
        lv_obj_remove_flag(g_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_strip);
        ui_below_settings(g_strip);
        lv_obj_set_style_bg_color(g_btn, lv_color_hex(0x7C5CFF), 0);
        lv_label_set_text(g_cap, cap);
        if (text) lv_label_set_text(g_text, text);
        lv_anim_delete(g_orb, orb_anim_cb);
        if (pulse) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, g_orb);
            lv_anim_set_exec_cb(&a, orb_anim_cb);
            lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
            lv_anim_set_duration(&a, 500);
            lv_anim_set_playback_duration(&a, 500);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        } else {
            lv_obj_set_style_bg_opa(g_orb, LV_OPA_COVER, 0);
        }
    }
    board_ui_unlock();
}

static void toast(const char *t)
{
    board_ui_lock();
    ui_toast(t);
    board_ui_unlock();
}

/* ---------------- WebSocket ---------------- */
static void send_text(const char *s)
{
    if (s_ws && esp_websocket_client_is_connected(s_ws))
        esp_websocket_client_send_text(s_ws, s, (int)strlen(s), pdMS_TO_TICKS(1000));
}

static void send_json(cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    if (s) {
        send_text(s);
        cJSON_free(s);
    }
    cJSON_Delete(j);
}

static void send_listen(const char *state, const char *extra)
{
    char b[160];
    snprintf(b, sizeof b, "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"%s\"%s}", s_session, state, extra ? extra : "");
    send_text(b);
}

static void flush_play(void)
{
    pkt_t *p;
    while (s_play && xQueueReceive(s_play, &p, 0) == pdTRUE) free(p);
}

static void post(int type, int gen, cJSON *j)
{
    ai_msg_t m = {.type = type, .gen = gen, .json = j};
    if (xQueueSend(s_ctl, &m, pdMS_TO_TICKS(500)) != pdTRUE && j) cJSON_Delete(j);
}

static void on_text(int gen, const char *s, int n)
{
    cJSON *j = cJSON_ParseWithLength(s, (size_t)n);
    if (!j) return;
    const cJSON *t = cJSON_GetObjectItem(j, "type");
    if (cJSON_IsString(t) && strcmp(t->valuestring, "hello") == 0) {
        const cJSON *sid = cJSON_GetObjectItem(j, "session_id");
        if (cJSON_IsString(sid)) snprintf(s_session, sizeof s_session, "%s", sid->valuestring);
        cJSON_Delete(j);
        xEventGroupSetBits(s_ev, BIT_HELLO);
        return;
    }
    post(M_JSON, gen, j);
}

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    int gen = (int)(intptr_t)arg;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(s_ev, BIT_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        if (d && d->error_handle.esp_ws_handshake_status_code) s_ws_http = d->error_handle.esp_ws_handshake_status_code;
        if (d && d->error_handle.esp_tls_last_esp_err) s_ws_tls = d->error_handle.esp_tls_last_esp_err;
        if (!s_close_ev) s_close_ev = id == WEBSOCKET_EVENT_CLOSED ? 'C' : id == WEBSOCKET_EVENT_ERROR ? 'E' : 'D';
        if (d && d->error_handle.esp_transport_sock_errno && !s_sock_errno) s_sock_errno = d->error_handle.esp_transport_sock_errno;
        post(M_CLOSED, gen, NULL);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d && d->op_code == 0x8 && d->data_len >= 2) s_close_code = ((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1];
        if (d && d->data_len > 0 && d->payload_offset == 0) {
            if (d->op_code == 0x1) s_rx_txt++;
            else if (d->op_code == 0x2) s_rx_bin++;
        }
        if (gen != s_gen || !d || d->data_len <= 0) break;
        if (d->op_code == 0x2) {                      /* giong noi: mot goi Opus */
            if (s_drop || d->payload_len > d->data_len || d->data_len > 4000) break;
            pkt_t *p = malloc(sizeof(pkt_t) + (size_t)d->data_len);
            if (!p) break;
            p->len = d->data_len;
            memcpy(p->data, d->data_ptr, (size_t)d->data_len);
            if (xQueueSend(s_play, &p, pdMS_TO_TICKS(2000)) != pdTRUE) free(p);
        } else if (d->op_code == 0x1 || (d->op_code == 0x0 && s_txt)) {
            if (d->payload_len <= d->data_len && d->payload_offset == 0) {
                on_text(gen, d->data_ptr, d->data_len);
                break;
            }
            if (d->payload_offset == 0) {             /* tin dai bi chia: gom lai */
                free(s_txt);
                s_txt = d->payload_len < 32768 ? malloc((size_t)d->payload_len) : NULL;
                s_txt_len = d->payload_len;
            }
            if (!s_txt || d->payload_offset + d->data_len > s_txt_len) break;
            memcpy(s_txt + d->payload_offset, d->data_ptr, (size_t)d->data_len);
            if (d->payload_offset + d->data_len == s_txt_len) {
                on_text(gen, s_txt, s_txt_len);
                free(s_txt);
                s_txt = NULL;
            }
        }
        break;
    default:
        break;
    }
}

/* Chi tiet loi ket noi (ESP_ERR_HTTP_CONNECT chung chung): loi esp-tls + ma mbedTLS + RAM trong chip con lai */
static void tls_why(esp_http_client_handle_t h, char *out, size_t n)
{
    int code = 0, flags = 0;
    esp_err_t te = esp_http_client_get_and_clear_last_tls_error(h, &code, &flags);
    const char *tn = te ? esp_err_to_name(te) : "-";
    if (strncmp(tn, "ESP_ERR_", 8) == 0) tn += 8;      /* rut gon: thong bao tren HUD bi cat neu qua dai */
    if (strncmp(tn, "ESP_TLS_", 8) == 0) tn += 8;
    snprintf(out, n, "%s -%04x RAM %uK", tn, (unsigned)(-code) & 0xFFFF,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

/* Hoi may chu dia chi WebSocket + token. Tra ve false va dat loi (thong bao cho nguoi dung). */
static bool fetch_ws(char *url, size_t url_n, char *token, size_t tok_n, char *why, size_t why_n)
{
    if (s_ws_url[0]) {                                /* token theo MAC, khong het han: dung lai */
        snprintf(url, url_n, "%s", s_ws_url);
        snprintf(token, tok_n, "%s", s_ws_tok);
        return true;
    }
    char body[200];
    snprintf(body, sizeof body,
             "{\"application\":{\"name\":\"evcarhud-p4\",\"version\":\"%s\"},\"board\":{\"type\":\"evcarhud-p4\",\"mac\":\"%s\"}}",
             esp_app_get_description()->version, s_mac);
    esp_http_client_config_t c = {.url = CONFIG_EVCARHUD_AI_URL, .method = HTTP_METHOD_POST, .timeout_ms = 10000,
                                  .crt_bundle_attach = esp_crt_bundle_attach};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) {
        snprintf(why, why_n, "Mi Mi: thiếu bộ nhớ");
        return false;
    }
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Device-Id", s_mac);
    esp_http_client_set_header(h, "Client-Id", s_uuid);
    bool ok = false;
    char *buf = malloc(2048);
    esp_err_t e = buf ? esp_http_client_open(h, (int)strlen(body)) : ESP_ERR_NO_MEM;
    if (buf) wifi_link_net_result(e == ESP_OK);
    if (e == ESP_OK && esp_http_client_write(h, body, (int)strlen(body)) == (int)strlen(body)) {
        esp_http_client_fetch_headers(h);
        int st = esp_http_client_get_status_code(h), n = 0, r;
        while (n < 2047 && (r = esp_http_client_read(h, buf + n, 2047 - n)) > 0) n += r;
        buf[n] = 0;
        cJSON *j = st == 200 ? cJSON_Parse(buf) : NULL;
        const cJSON *ws = cJSON_GetObjectItem(j, "websocket");
        const cJSON *u = cJSON_GetObjectItem(ws, "url"), *t = cJSON_GetObjectItem(ws, "token");
        const cJSON *act = cJSON_GetObjectItem(j, "activation");
        if (cJSON_IsString(u) && u->valuestring[0]) {
            snprintf(url, url_n, "%s", u->valuestring);
            snprintf(token, tok_n, "%s", cJSON_IsString(t) ? t->valuestring : "");
            snprintf(s_ws_url, sizeof s_ws_url, "%s", url);
            snprintf(s_ws_tok, sizeof s_ws_tok, "%s", token);
            ok = true;
        } else if (act) {
            const cJSON *msg = cJSON_GetObjectItem(act, "message");
            snprintf(why, why_n, "%s", cJSON_IsString(msg) ? msg->valuestring : "HUD này chưa được bật Mi Mi");
        } else {
            if (st == 200) snprintf(why, why_n, "Mi Mi: máy chủ chưa có trợ lý (trả sai dữ liệu)");
            else snprintf(why, why_n, "Mi Mi: máy chủ trả lỗi %d", st);
        }
        cJSON_Delete(j);
    } else {
        char tw[96];
        tls_why(h, tw, sizeof tw);
        snprintf(why, why_n, "Mi Mi lỗi mạng: %s", tw);   /* chi tiet truoc, de khong bi cat */
        ESP_LOGW(TAG, "POST %s: %s", CONFIG_EVCARHUD_AI_URL, esp_err_to_name(e));
    }
    free(buf);
    esp_http_client_cleanup(h);
    return ok;
}

static void ws_drop(void)
{
    if (s_ws) {
        esp_websocket_client_handle_t ws = s_ws;
        s_ws = NULL;
        s_gen++;
        if (esp_websocket_client_is_connected(ws)) esp_websocket_client_close(ws, pdMS_TO_TICKS(1000));
        esp_websocket_client_destroy(ws);
    }
}

static void session_close(void)
{
    int was = s_st;
    s_st = ST_IDLE;
    s_gen++;                                          /* tin con lai cua phien cu bi bo qua */
    s_drop = true;
    s_tts_stop = false;
    flush_play();
    ws_drop();
    audio_set_volume(settings_get_volume());          /* tra am luong tieng tit canh bao */
    if (was != ST_IDLE) ESP_LOGI(TAG, "ket thuc phien");
    ui_show(NULL, NULL, false);
}

static void go_listen(void)
{
    audio_set_volume(settings_get_volume());          /* het noi: tra am luong tieng tit */
    s_drop = false;
    s_tts_stop = false;
    s_heard = false;
    send_listen("start", ",\"mode\":\"manual\"");   /* VAD tren chip bao noi xong (listen stop) */
    s_st = ST_LISTEN;
    s_since = xTaskGetTickCount();
    ui_show("ĐANG NGHE", "Anh nói đi…", true);
}

static bool session_connect(void);

static void session_open(void)
{
    if (!s_wifi) {
        toast("Mi Mi cần Wi-Fi");
        return;
    }
    if (!settings_get_sound() || settings_get_volume() <= 0) toast("Âm thanh đang tắt: bật để nghe Mi Mi");
    s_st = ST_CONN;
    ui_show("ĐANG KẾT NỐI", "Mi Mi đang tới…", true);
    bool cached = s_ws_url[0] != 0;
    if (session_connect()) return;
    if (cached && s_st == ST_CONN) {                  /* token cu bi tu choi / doi dia chi: hoi lai may chu */
        s_ws_url[0] = 0;
        session_connect();
    }
}

/* true: da vao phien; false: loi (da dong, s_st van ST_CONN neu nen thu lai voi token moi) */
static bool session_connect(void)
{
    bool cached_try = s_ws_url[0] != 0;
    static char url[256], token[256], why[200];
    why[0] = 0;
    ui_show("ĐANG KẾT NỐI", s_ws_url[0] ? "Mi Mi đang tới… (1/3 dùng lại token)" : "Mi Mi đang tới… (1/3 hỏi máy chủ)", true);
    bool got = fetch_ws(url, sizeof url, token, sizeof token, why, sizeof why);
    if (!got && strncmp(why, "Mi Mi lỗi mạng", strlen("Mi Mi lỗi mạng")) == 0 && s_st == ST_CONN) {
        ESP_LOGW(TAG, "%s: thu lai", why);            /* loi mang thoang qua (vua doi mang, TLS het gio): thu them 1 lan */
        vTaskDelay(pdMS_TO_TICKS(1500));
        why[0] = 0;
        got = fetch_ws(url, sizeof url, token, sizeof token, why, sizeof why);
    }
    if (!got) {
        session_close();
        toast(why);
        return true;                                  /* loi tu may chu: khong thu lai */
    }
    char *hdr = malloc(600);
    if (!hdr) {
        session_close();
        return true;
    }
    snprintf(hdr, 600, "Authorization: Bearer %s\r\nDevice-Id: %s\r\nClient-Id: %s\r\nProtocol-Version: 1\r\n", token, s_mac, s_uuid);
    s_session[0] = 0;
    xEventGroupClearBits(s_ev, BIT_CONNECTED | BIT_HELLO);
    s_ws_http = 0;
    s_ws_tls = 0;
    s_close_code = s_sock_errno = 0;
    s_rx_txt = s_rx_bin = 0;
    s_close_ev = 0;
    int gen = ++s_gen;
    esp_websocket_client_config_t wc = {
        .uri = url,
        .headers = hdr,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_reconnect = true,
        .buffer_size = 4096,
        .task_stack = 6144,
        .network_timeout_ms = 10000,
    };
    s_ws = esp_websocket_client_init(&wc);
    free(hdr);                                        /* thu vien da chep header */
    if (!s_ws) {
        session_close();
        toast("Mi Mi: thiếu bộ nhớ");
        return true;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, (void *)(intptr_t)gen);
    ui_show("ĐANG KẾT NỐI", "Mi Mi đang tới… (2/3 mở kết nối thoại)", true);
    if (esp_websocket_client_start(s_ws) != ESP_OK ||
        !(xEventGroupWaitBits(s_ev, BIT_CONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000)) & BIT_CONNECTED)) {
        ws_drop();
        if (s_ws_url[0] && cached_try) return false; /* thu lai voi token moi */
        session_close();
        static char m[96];
        if (s_ws_http == 401) snprintf(m, sizeof m, "Mi Mi: máy chủ từ chối token (401)");
        else if (s_ws_http == 503) snprintf(m, sizeof m, "Mi Mi: máy chủ thiếu khoá AI / giọng đọc (503)");
        else if (s_ws_http) snprintf(m, sizeof m, "Mi Mi: mở kết nối thoại lỗi HTTP %d", s_ws_http);
        else if (s_ws_tls) snprintf(m, sizeof m, "Mi Mi: lỗi TLS kết nối thoại (%s)", esp_err_to_name(s_ws_tls));
        else snprintf(m, sizeof m, "Mi Mi: không mở được kết nối thoại");
        ESP_LOGW(TAG, "WebSocket that bai: HTTP %d, TLS %s", s_ws_http, esp_err_to_name(s_ws_tls));
        toast(m);
        return true;
    }
    send_text("{\"type\":\"hello\",\"version\":1,\"features\":{\"mcp\":true},\"transport\":\"websocket\","
              "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}");
    ui_show("ĐANG KẾT NỐI", "Mi Mi đang tới… (3/3 chờ máy chủ chào)", true);
    if (!(xEventGroupWaitBits(s_ev, BIT_HELLO, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000)) & BIT_HELLO)) {
        session_close();
        toast("Mi Mi: máy chủ không trả lời");
        return true;
    }
    ESP_LOGI(TAG, "da noi phien %s", s_session);
    s_drop = false;
    s_tts_stop = false;
    send_listen("detect", ",\"text\":\"Mi Mi\"");    /* may chu chao "Da, Mi Mi nghe a" roi moi nghe */
    s_st = ST_THINK;
    s_since = xTaskGetTickCount();
    s_greet = true;
    ui_show("MI MI", "Đã kết nối, chờ Mi Mi chào…", true);
    return true;
}

/* ---------------- MCP: cong cu cho tro ly hoi trang thai HUD ---------------- */
static void mcp_reply(const cJSON *id, cJSON *result, int err, const char *emsg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "session_id", s_session);
    cJSON_AddStringToObject(r, "type", "mcp");
    cJSON *p = cJSON_AddObjectToObject(r, "payload");
    cJSON_AddStringToObject(p, "jsonrpc", "2.0");
    cJSON_AddItemToObject(p, "id", cJSON_Duplicate(id, true));
    if (result) {
        cJSON_AddItemToObject(p, "result", result);
    } else {
        cJSON *e = cJSON_AddObjectToObject(p, "error");
        cJSON_AddNumberToObject(e, "code", err);
        cJSON_AddStringToObject(e, "message", emsg);
    }
    send_json(r);
}

static void on_mcp(const cJSON *pl)
{
    const cJSON *id = cJSON_GetObjectItem(pl, "id"), *m = cJSON_GetObjectItem(pl, "method");
    if (!id || !cJSON_IsString(m)) return;            /* thong bao (khong co id): bo qua */
    const char *meth = m->valuestring;
    if (strcmp(meth, "initialize") == 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "protocolVersion", "2024-11-05");
        cJSON *cap = cJSON_AddObjectToObject(res, "capabilities");
        cJSON_AddObjectToObject(cap, "tools");
        cJSON *si = cJSON_AddObjectToObject(res, "serverInfo");
        cJSON_AddStringToObject(si, "name", "evcarhud-p4");
        cJSON_AddStringToObject(si, "version", esp_app_get_description()->version);
        mcp_reply(id, res, 0, NULL);
    } else if (strcmp(meth, "tools/list") == 0) {
        cJSON *res = cJSON_CreateObject();
        cJSON *tools = cJSON_AddArrayToObject(res, "tools");
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "self.get_device_status");
        cJSON_AddStringToObject(t, "description",
                                "Trạng thái HUD trên xe: tốc độ xe hiện tại (km/h, -1 nếu chưa có GPS), âm lượng loa (%), phiên bản firmware.");
        cJSON *sc = cJSON_AddObjectToObject(t, "inputSchema");
        cJSON_AddStringToObject(sc, "type", "object");
        cJSON_AddObjectToObject(sc, "properties");
        cJSON_AddItemToArray(tools, t);
        t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", "self.set_voice_volume");
        cJSON_AddStringToObject(t, "description",
                                "Chỉnh âm lượng giọng nói của trợ lý (30 đến 100 phần trăm), riêng với tiếng tít cảnh báo. Gọi khi người dùng "
                                "bảo nói to lên / nhỏ lại: to lên thì cộng 20, nhỏ lại thì trừ 20 so với ai_volume hiện tại.");
        sc = cJSON_AddObjectToObject(t, "inputSchema");
        cJSON_AddStringToObject(sc, "type", "object");
        cJSON *pr = cJSON_AddObjectToObject(sc, "properties");
        cJSON *vv = cJSON_AddObjectToObject(pr, "volume");
        cJSON_AddStringToObject(vv, "type", "integer");
        cJSON_AddNumberToObject(vv, "minimum", 30);
        cJSON_AddNumberToObject(vv, "maximum", 100);
        cJSON *rq = cJSON_AddArrayToObject(sc, "required");
        cJSON_AddItemToArray(rq, cJSON_CreateString("volume"));
        cJSON_AddItemToArray(tools, t);
        mcp_reply(id, res, 0, NULL);
    } else if (strcmp(meth, "tools/call") == 0) {
        const cJSON *prm = cJSON_GetObjectItem(pl, "params");
        const cJSON *nm = cJSON_GetObjectItem(prm, "name");
        char txt[200];
        if (cJSON_IsString(nm) && strcmp(nm->valuestring, "self.set_voice_volume") == 0) {
            const cJSON *v = cJSON_GetObjectItem(cJSON_GetObjectItem(prm, "arguments"), "volume");
            int nv = cJSON_IsNumber(v) ? v->valueint : settings_get_ai_volume();
            settings_set_ai_volume(nv);
            if (s_st == ST_SPEAK || s_st == ST_THINK) audio_set_volume(settings_get_ai_volume());
            snprintf(txt, sizeof txt, "{\"ai_volume\":%d}", settings_get_ai_volume());
        } else if (cJSON_IsString(nm) && strcmp(nm->valuestring, "self.get_device_status") == 0) {
            snprintf(txt, sizeof txt, "{\"speed_kmh\":%d,\"volume\":%d,\"ai_volume\":%d,\"sound_on\":%s,\"firmware\":\"%s\"}",
                     ui_get_speed(), settings_get_volume(), settings_get_ai_volume(), settings_get_sound() ? "true" : "false",
                     esp_app_get_description()->version);
        } else {
            mcp_reply(id, NULL, -32601, "Không có công cụ này");
            return;
        }
        cJSON *res = cJSON_CreateObject();
        cJSON *ct = cJSON_AddArrayToObject(res, "content");
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "type", "text");
        cJSON_AddStringToObject(c, "text", txt);
        cJSON_AddItemToArray(ct, c);
        cJSON_AddBoolToObject(res, "isError", false);
        mcp_reply(id, res, 0, NULL);
    } else {
        mcp_reply(id, NULL, -32601, "Method not found");
    }
}

static void on_json(cJSON *j)
{
    const cJSON *t = cJSON_GetObjectItem(j, "type");
    if (!cJSON_IsString(t)) return;
    const char *type = t->valuestring;
    const cJSON *txt = cJSON_GetObjectItem(j, "text");
    const char *text = cJSON_IsString(txt) ? txt->valuestring : NULL;
    if (strcmp(type, "stt") == 0) {
        if (s_st == ST_LISTEN || s_st == ST_THINK) {
            s_st = ST_THINK;
            s_since = xTaskGetTickCount();
            ui_show("ANH HỎI", text ? text : "", false);
        }
    } else if (strcmp(type, "tts") == 0) {
        const cJSON *st = cJSON_GetObjectItem(j, "state");
        const char *state = cJSON_IsString(st) ? st->valuestring : "";
        if (strcmp(state, "start") == 0) {
            if (s_st == ST_THINK || s_st == ST_LISTEN) {
                s_st = ST_SPEAK;
                s_drop = false;
                s_tts_stop = false;
                audio_set_volume(settings_get_ai_volume());   /* giong Mi Mi to hon tieng tit (am luong rieng) */
                s_greet = false;
                ui_show("MI MI", NULL, true);
            }
        } else if (strcmp(state, "sentence_start") == 0) {
            if (s_st == ST_SPEAK && text) ui_show("MI MI", text, true);
        } else if (strcmp(state, "stop") == 0) {
            if (s_st == ST_SPEAK) s_tts_stop = true;  /* luong loa phat het hang doi thi bao M_DRAINED */
        }
    } else if (strcmp(type, "mcp") == 0) {
        on_mcp(cJSON_GetObjectItem(j, "payload"));
    } else if (strcmp(type, "alert") == 0 || strcmp(type, "error") == 0) {
        if (text) toast(text);
    }
}

static void on_tap(void)
{
    switch (s_st) {
    case ST_IDLE:
        session_open();
        break;
    case ST_SPEAK:                                    /* ngat loi Mi Mi, nghe tiep */
        s_drop = true;
        flush_play();
        send_text("{\"type\":\"abort\"}");
        go_listen();
        break;
    case ST_LISTEN:                                   /* da noi: cham = "noi xong", gui ngay; chua noi: dong */
        send_listen("stop", NULL);
        if (s_heard) {
            s_st = ST_THINK;
            s_since = xTaskGetTickCount();
            ui_show("MI MI", "Đang nghĩ…", true);
        } else {
            session_close();
        }
        break;
    case ST_THINK:
        send_listen("stop", NULL);
        session_close();
        break;
    default:
        break;
    }
}

/* ---------------- luong ---------------- */
static void ctl_task(void *arg)
{
    (void)arg;
    for (;;) {
        ai_msg_t m;
        if (xQueueReceive(s_ctl, &m, pdMS_TO_TICKS(500)) == pdTRUE) {
            switch (m.type) {
            case M_TAP:
                on_tap();
                break;
            case M_WAKE:                              /* "Mi Mi" bang giong noi */
                if (s_st == ST_IDLE) {
                    ESP_LOGI(TAG, "nghe thay tu danh thuc");
                    session_open();
                }
                break;
            case M_VAD_END:                           /* anh noi xong: gui ngay, khong cho may chu doan */
                if (s_st == ST_LISTEN) {
                    send_listen("stop", NULL);
                    s_st = ST_THINK;
                    s_since = xTaskGetTickCount();
                    ui_show("MI MI", "Đang nghĩ…", true);
                }
                break;
            case M_VAD_HEARD:                         /* bat dau nghe thay tieng: cho anh biet HUD dang nghe */
                if (m.gen == s_gen && s_st == ST_LISTEN) ui_show("ĐANG NGHE", "Mi Mi đang nghe anh…", true);
                break;
            case M_VAD_NONE:                          /* khong ai noi: ket thuc */
                if (s_st == ST_LISTEN && s_vad_peak > s_vad_thr) {   /* co tieng vuot nguong nhung VAD chua chac: van gui, Whisper quyet */
                    send_listen("stop", NULL);
                    s_st = ST_THINK;
                    s_since = xTaskGetTickCount();
                    ui_show("MI MI", "Đang nghĩ…", true);
                } else if (s_st == ST_LISTEN) {
                    session_close();
                    static char m[96];                /* so do de chinh nguong: to nhat nghe duoc / nguong can vuot */
                    snprintf(m, sizeof m, "Mi Mi không nghe thấy anh (to nhất %d, cần %d)", (int)s_vad_peak, (int)s_vad_thr);
                    toast(m);
                }
                break;
            case M_JSON:
                if (m.gen == s_gen && s_st != ST_IDLE) on_json(m.json);
                break;
            case M_CLOSED:
                if (m.gen == s_gen && s_st != ST_IDLE && s_st != ST_CONN) {
                    static char m[110];
                    snprintf(m, sizeof m, "Mi Mi mất kết nối: %c mã %d errno %d TLS %x RAM %uK", s_close_ev ? s_close_ev : '?', s_close_code,
                             s_sock_errno, (unsigned)s_ws_tls, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
                    ESP_LOGW(TAG, "%s", m);
                    session_close();
                    toast(m);
                }
                break;
            case M_DRAINED:
                if (m.gen == s_gen && s_st == ST_SPEAK) go_listen();
                break;
            case M_WIFI_DOWN:
                if (s_st != ST_IDLE && s_st != ST_CONN) session_close();
                break;
            }
            if (m.json) cJSON_Delete(m.json);
        }
        TickType_t el = xTaskGetTickCount() - s_since;
        if ((s_st == ST_LISTEN && el > pdMS_TO_TICKS(LISTEN_MAX_MS)) || (s_st == ST_THINK && el > pdMS_TO_TICKS(THINK_MAX_MS))) {
            ESP_LOGI(TAG, "het gio cho (%s)", s_st == ST_LISTEN ? "nghe" : "tra loi");
            send_listen("stop", NULL);
            if (s_st == ST_LISTEN && s_heard) {       /* da noi: van gui cho Mi Mi tra loi */
                s_st = ST_THINK;
                s_since = xTaskGetTickCount();
                ui_show("MI MI", "Đang nghĩ…", true);
            } else {
                bool think = s_st == ST_THINK, greet = s_greet;
                session_close();
                if (think) {
                    static char m[110];
                    snprintf(m, sizeof m, "Mi Mi: %s sau 20 s (nhận %d tin, %d gói tiếng, RAM %uK)",
                             greet ? "máy chủ không chào lại" : "máy chủ không trả lời", s_rx_txt, s_rx_bin,
                             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
                    ESP_LOGW(TAG, "%s", m);
                    toast(m);
                    if (greet && s_rx_txt <= 1) wifi_link_net_result(false);   /* chi co loi chao "hello": mang/C6 nghen */
                }
            }
        }
    }
}

/* Tu danh thuc "Mi Mi": MultiNet7 tieng Trung (lenh go bang pinyin, giong Xiaozhi CUSTOM_WAKE_WORD), chay thang tren
 * micro khi dang ranh. Mo hinh nam o vung flash "model" (nap bang file gop). Khong co mo hinh: chi cham nut. */
static esp_mn_iface_t *s_mn;
static model_iface_data_t *s_mnd;
static int16_t *s_wbuf;
static int s_wchunk, s_wfill;

static void wake_init(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    char *name = models ? esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE) : NULL;
    if (!name) {
        ESP_LOGW(TAG, "khong co mo hinh tu danh thuc (vung flash \"model\"): chi cham nut micro");
        return;
    }
    s_mn = esp_mn_handle_from_name(name);
    s_mnd = s_mn ? s_mn->create(name, 3000) : NULL;
    if (!s_mnd) return;
    s_mn->set_det_threshold(s_mnd, CONFIG_EVCARHUD_WAKE_THRESHOLD / 100.0f);
    esp_mn_commands_alloc(s_mn, s_mnd);
    esp_mn_commands_clear();
    esp_mn_commands_add(1, CONFIG_EVCARHUD_WAKE_WORD);
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err && err->num > 0) {
        ESP_LOGW(TAG, "tu danh thuc \"%s\" khong hop le (pinyin?)", CONFIG_EVCARHUD_WAKE_WORD);
        return;
    }
    s_wchunk = s_mn->get_samp_chunksize(s_mnd);
    s_wbuf = malloc((size_t)s_wchunk * sizeof(int16_t));
    if (!s_wbuf || s_wchunk <= 0) return;
    s_wake_ok = true;
    ESP_LOGI(TAG, "tu danh thuc \"%s\" (%s, nguong %d%%)", CONFIG_EVCARHUD_WAKE_WORD, name, CONFIG_EVCARHUD_WAKE_THRESHOLD);
}

static void wake_feed(const int16_t *pcm, int n)
{
    while (n > 0) {
        int k = s_wchunk - s_wfill < n ? s_wchunk - s_wfill : n;
        memcpy(s_wbuf + s_wfill, pcm, (size_t)k * sizeof(int16_t));
        s_wfill += k;
        pcm += k;
        n -= k;
        if (s_wfill < s_wchunk) break;
        s_wfill = 0;
        esp_mn_state_t st = s_mn->detect(s_mnd, s_wbuf);
        if (st == ESP_MN_STATE_DETECTED) {
            s_mn->clean(s_mnd);
            post(M_WAKE, 0, NULL);
            return;
        }
        if (st == ESP_MN_STATE_TIMEOUT) s_mn->clean(s_mnd);
    }
}

static void mic_task(void *arg)
{
    (void)arg;
    wake_init();
    esp_opus_enc_config_t cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = RATE;
    cfg.channel = 1;
    cfg.bits_per_sample = 16;
    cfg.bitrate = ESP_OPUS_BITRATE_AUTO;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    cfg.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    cfg.complexity = 0;
    cfg.enable_dtx = true;
    cfg.enable_vbr = true;
    void *enc = NULL;
    int in_sz = 0, out_sz = 0;
    if (esp_opus_enc_open(&cfg, sizeof cfg, &enc) != ESP_AUDIO_ERR_OK || !enc ||
        esp_opus_enc_get_frame_size(enc, &in_sz, &out_sz) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "khong mo duoc bo ma hoa Opus");
        vTaskDelete(NULL);
        return;
    }
    int16_t *pcm = malloc(FRAME * sizeof(int16_t));
    uint8_t *out = malloc((size_t)(out_sz > 0 ? out_sz : 1500));
    if (!pcm || !out || in_sz != FRAME * (int)sizeof(int16_t)) {
        ESP_LOGE(TAG, "Opus: khung %d byte (can %d)", in_sz, FRAME * (int)sizeof(int16_t));
        vTaskDelete(NULL);
        return;
    }
    bool was = false;
    int fr = 0, run = 0, quiet = 0;
    uint8_t hist = 0;
    bool started = false, sent_end = false;
    float floor_e = 0;
    int last_listen_gen = -1;
    TickType_t last_since = 0;
    for (;;) {
        int st = s_st;
        bool wake = st == ST_IDLE && s_wake_ok && s_wifi;
        if (st != ST_LISTEN && !wake) {
            if (was) esp_opus_enc_reset(enc);
            was = false;
            s_wfill = 0;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        if (audio_mic_read(pcm, FRAME) != FRAME) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (s_st == ST_IDLE) {
            if (wake) wake_feed(pcm, FRAME);
            continue;
        }
        if (s_st != ST_LISTEN) continue;
        was = true;
        /* moi lan bat dau nghe (go_listen dat s_since moi): dat lai VAD */
        if (last_listen_gen != s_gen || last_since != s_since) {
            last_listen_gen = s_gen;
            last_since = s_since;
            fr = run = quiet = 0;
            hist = 0;
            started = sent_end = false;
            floor_e = 0;
        }
        /* nang luong khung (RMS) + nen on thich nghi */
        int64_t acc = 0;
        for (int i = 0; i < FRAME; i++) acc += (int32_t)pcm[i] * pcm[i];
        float e = sqrtf((float)acc / FRAME);
        /* bo 300 ms dau (tieng tit vang lai lam nen qua cao -> khong bao gio "bat dau noi" -> treo "Anh noi di"),
         * roi lay nen = min cua 300 ms tiep. Nguong bi chan tren de xe on/vang loa khong che mat giong. */
        const int ready = VAD_SKIP_FR + VAD_FLOOR_FR;
        if (fr >= VAD_SKIP_FR && fr < ready) floor_e = fr == VAD_SKIP_FR ? e : (floor_e < e ? floor_e : e);
        float thr = floor_e * 2.5f;
        if (thr < 250.0f) thr = 250.0f;                 /* micro bo nay nho: giong noi do duoc ~500-800 */
        if (thr > 2500.0f) thr = 2500.0f;
        /* tre (hysteresis): da noi thi chi can 0,7 nguong de coi la con noi, tranh cat giua cau */
        bool speech = fr >= ready && e > (started && run > 0 ? thr * 0.7f : thr);
        if (fr == ready) s_vad_peak = 0;
        if (fr >= ready && e > s_vad_peak) s_vad_peak = e;
        s_vad_thr = thr;
        if (fr >= ready) {
            if (!speech) floor_e = floor_e * 0.95f + e * 0.05f;
            else floor_e = floor_e * 0.998f + e * 0.002f;   /* on xe tang dan van theo kip, khong "noi mai" */
        }
        fr++;
        hist = (uint8_t)((hist << 1) | (speech ? 1 : 0));   /* 5 khung gan nhat */
        if (speech) {
            run++;
            quiet = 0;
            /* giong nho/ngat quang: 3 trong 5 khung gan nhat co tieng la du (truoc can 3 khung lien) */
            if (!started && (run >= VAD_START_FR || __builtin_popcount(hist & 0x1F) >= 3)) {
                started = true;
                s_heard = true;
                post(M_VAD_HEARD, s_gen, NULL);
            }
        } else {
            run = 0;
            quiet++;
        }
        if (!sent_end) {
            if (started && (quiet >= VAD_END_FR || fr >= VAD_MAX_FR)) {
                sent_end = true;
                post(M_VAD_END, s_gen, NULL);
            } else if (!started && fr >= VAD_NONE_FR) {
                sent_end = true;
                post(M_VAD_NONE, s_gen, NULL);
            }
        }
        esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)pcm, .len = (uint32_t)in_sz};
        esp_audio_enc_out_frame_t o = {.buffer = out, .len = (uint32_t)(out_sz > 0 ? out_sz : 1500)};
        if (esp_opus_enc_process(enc, &in, &o) != ESP_AUDIO_ERR_OK || o.encoded_bytes == 0) continue;
        esp_websocket_client_handle_t ws = s_ws;
        if (ws && s_st == ST_LISTEN && esp_websocket_client_is_connected(ws))
            esp_websocket_client_send_bin(ws, (const char *)out, (int)o.encoded_bytes, pdMS_TO_TICKS(200));
    }
}

static void spk_task(void *arg)
{
    (void)arg;
    esp_opus_dec_cfg_t cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
    cfg.sample_rate = RATE;                           /* may chu ma hoa 24 kHz: Opus giai ma thang ra 16 kHz cua loa */
    cfg.channel = 1;
    /* Google TTS gui goi 20 ms (Xiaozhi 60 ms): khong co dinh do dai goi, bo giai ma nhan moi co toi da 60 ms */
    cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID;
    void *dec = NULL;
    if (esp_opus_dec_open(&cfg, sizeof cfg, &dec) != ESP_AUDIO_ERR_OK || !dec) {
        ESP_LOGE(TAG, "khong mo duoc bo giai ma Opus");
        vTaskDelete(NULL);
        return;
    }
    const int cap = RATE * 120 / 1000;                /* du cho goi 120 ms */
    int16_t *pcm = malloc((size_t)cap * sizeof(int16_t));
    if (!pcm) {
        vTaskDelete(NULL);
        return;
    }
    int idle = 0, got = 0, played = 0, bad = 0, last_err = 0;
    for (;;) {
        pkt_t *p = NULL;
        if (xQueueReceive(s_play, &p, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (s_tts_stop && s_st == ST_SPEAK && ++idle >= 3) {   /* het cau cuoi + 300 ms khong co goi moi */
                s_tts_stop = false;
                idle = 0;
                ESP_LOGI(TAG, "loa: nhan %d goi, phat %d, loi giai ma %d (ma %d)", got, played, bad, last_err);
                if (got > 0 && played == 0) {                   /* co tieng tu may chu ma khong phat duoc: bao ro */
                    static char m[96];
                    snprintf(m, sizeof m, "Mi Mi: nhận %d gói tiếng, giải mã lỗi %d (mã %d)", got, bad, last_err);
                    toast(m);
                }
                got = played = bad = 0;
                post(M_DRAINED, s_gen, NULL);
            }
            continue;
        }
        idle = 0;
        got++;
        if (!s_drop) {
            esp_audio_dec_in_raw_t raw = {.buffer = p->data, .len = (uint32_t)p->len};
            esp_audio_dec_out_frame_t fr = {.buffer = (uint8_t *)pcm, .len = (uint32_t)cap * sizeof(int16_t)};
            esp_audio_dec_info_t info = {0};
            int r = esp_opus_dec_decode(dec, &raw, &fr, &info);
            if (r == ESP_AUDIO_ERR_OK && fr.decoded_size > 0) {
                int n = (int)(fr.decoded_size / sizeof(int16_t));
                /* Giong noi nghe nho hon tieng tit cung bien do: tu dong khuech dai (toi da x3) theo dinh gan day,
                 * dinh sau khuech dai ~28000 nen khong meo; dinh giam cham (~1 s) de am luong deu giua cac cau. */
                int pk = 0;
                for (int i = 0; i < n; i++) {
                    int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
                    if (a > pk) pk = a;
                }
                if (pk > s_tts_peak) s_tts_peak = (float)pk;
                else s_tts_peak = s_tts_peak * 0.98f + pk * 0.02f;
                float g = s_tts_peak > 1000.0f ? 28000.0f / s_tts_peak : 3.0f;
                if (g > 3.0f) g = 3.0f;
                if (g < 1.0f) g = 1.0f;
                for (int i = 0; i < n; i++) {
                    int32_t v = (int32_t)(pcm[i] * g);
                    pcm[i] = (int16_t)(v > 32000 ? 32000 : v < -32000 ? -32000 : v);
                }
                if (!s_drop) audio_play(pcm, n);
                played++;
            } else {
                bad++;
                last_err = r;
            }
        }
        free(p);
    }
}

static bool tasks_start(void)
{
    if (s_tasks) return true;
    /* libopus can ngan xep lon (~24-32 KB); de trong RAM trong chip cho nhanh */
    if (xTaskCreate(mic_task, "ai_mic", 32768, NULL, 5, NULL) != pdPASS) return false;
    if (xTaskCreate(spk_task, "ai_spk", 24576, NULL, 5, NULL) != pdPASS) return false;
    s_tasks = true;
    return true;
}

static void ctl_entry(void *arg)
{
    (void)arg;
    if (!tasks_start()) {
        ESP_LOGE(TAG, "thieu RAM cho luong micro / loa");
        vTaskDelete(NULL);
        return;
    }
    ctl_task(NULL);
}

void ai_wifi_changed(bool up)
{
    s_wifi = up;
    if (!up && s_ctl) post(M_WIFI_DOWN, 0, NULL);
}

void ai_start(void)
{
    if (s_ctl) return;
    if (!audio_ready()) {
        /* codec mo trong luong audio: doi toi 3 giay */
        for (int i = 0; i < 30 && !audio_ready(); i++) vTaskDelay(pdMS_TO_TICKS(100));
        if (!audio_ready()) {
            ESP_LOGW(TAG, "khong co loa / micro: tat Mi Mi");
            return;
        }
    }
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(s_mac, sizeof s_mac, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    uint8_t r[16];
    esp_fill_random(r, sizeof r);
    r[6] = (r[6] & 0x0F) | 0x40;
    r[8] = (r[8] & 0x3F) | 0x80;
    snprintf(s_uuid, sizeof s_uuid, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", r[0], r[1], r[2], r[3],
             r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
    s_ev = xEventGroupCreate();
    s_play = xQueueCreate(PLAY_Q, sizeof(pkt_t *));
    s_ctl = xQueueCreate(16, sizeof(ai_msg_t));
    if (!s_ev || !s_play || !s_ctl) return;
    board_ui_lock();
    ui_build();
    board_ui_unlock();
    if (xTaskCreate(ctl_entry, "ai_ctl", 8192, NULL, 4, NULL) != pdPASS) ESP_LOGE(TAG, "khong tao duoc luong Mi Mi");
    else ESP_LOGI(TAG, "Mi Mi san sang (cham nut micro)");
}
