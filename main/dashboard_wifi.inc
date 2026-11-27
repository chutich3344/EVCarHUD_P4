/* ======================= trang Wi-Fi (chen vao dashboard.c) =======================
 * Theo man mau: nen xanh la dam nhu man cai dat. Dau trang: nut quay lai, tieu de, dong phu, cong tac Wi-Fi.
 *  - Danh sach mang: vach song, ten, "Da ket noi", o khoa (co mat khau) / "Mo"; nut "Do lai".
 *  - Nhap mat khau: o nhap (an bang dau cham, nut mat de hien), ban phim QWERTY: 123/ABC, Shift, xoa, dau cach, "Ket noi".
 *    It nhat 8 ky tu. Ket noi duoc thi wifi_link.c luu NVS (nhieu mang), lan bat may sau tu vao mang da luu song manh nhat.
 *  - Mang da luu: cham la ket noi luon (khong hoi lai mat khau); giu lau de quen mang.
 *  - Dang ket noi / da ket noi / loi. Lop tren cung (lv_layer_top): khong dung toi cac man bang dieu khien. */
#define WF_MAX 12
static ui_wifi_hooks_t wf_h;
static lv_obj_t *wf_scr, *wf_list_pg, *wf_sub, *wf_sw, *wf_list, *wf_busy, *wf_off_l, *wf_rescan;
static lv_obj_t *wf_pass_pg, *wf_pass_sub, *wf_in_box, *wf_in, *wf_ph, *wf_caret, *wf_eye_l, *wf_keys;
static lv_obj_t *wf_conn_pg, *wf_conn_spin, *wf_conn_icon, *wf_conn_t, *wf_conn_s;
static bool wf_on, wf_scanning, wf_show, wf_shift, wf_num, wf_joining, wf_use_saved;
static char wf_pwd[64], wf_sel[33], wf_err[48];
static ui_wifi_ap_t wf_aps[WF_MAX];
static int wf_n;
static lv_timer_t *wf_close_tmr;

enum { WK_SHIFT = 1, WK_DEL, WK_NUM, WK_JOIN };

static void wf_build(void);
static void wf_render_list(void);
static void wf_render_pass(void);

static void wf_page(lv_obj_t *pg)
{
    show(wf_list_pg, pg == wf_list_pg);
    show(wf_pass_pg, pg == wf_pass_pg);
    show(wf_conn_pg, pg == wf_conn_pg);
}

static void wf_close(void)
{
    if (wf_scr) show(wf_scr, false);
    if (wf_close_tmr) lv_timer_pause(wf_close_tmr);
}

static void wf_close_cb(lv_timer_t *t) { (void)t; wf_close(); }

static void wf_do_scan(void)
{
    if (!wf_on) return;
    wf_scanning = true;
    wf_render_list();
    if (wf_h.scan) wf_h.scan();
}

static void wf_back_cb(lv_event_t *e)
{
    (void)e;
    if (shown(wf_pass_pg)) wf_page(wf_list_pg);
    else wf_close();
}

static lv_obj_t *wf_head(lv_obj_t *pg, const char *title, lv_obj_t **sub)
{
    lv_obj_t *b = mk_card(pg, 12, 12, 36, 36, 18);
    lv_obj_set_style_bg_color(b, S_CARD, 0);
    lv_obj_set_style_border_color(b, S_BORDER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_add_event_cb(b, wf_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = mk_label(b, &lv_font_montserrat_14, 0, 0, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    lv_obj_t *t = mk_label(pg, &ui_font_txt16, 58, 12, title);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    *sub = mk_label_w(pg, &ui_font_sm12, 58, 12 + 21, 330, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(*sub, S_GREEN3, 0);
    return b;
}

static void wf_spin_style(lv_obj_t *sp, int w)
{
    lv_spinner_set_anim_params(sp, 900, 90);
    lv_obj_set_style_arc_width(sp, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, S_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sp, S_GREEN2, LV_PART_INDICATOR);
}

/* ---- danh sach mang ---- */
static int wf_bars(int rssi) { return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1; }

static void wf_toggle_cb(lv_event_t *e)
{
    (void)e;
    wf_on = !wf_on;
    if (wf_h.enable) wf_h.enable(wf_on);
    wifi_st = wf_on ? UI_WIFI_IDLE : UI_WIFI_OFF;
    if (!wf_on) { wf_scanning = false; wifi_ssid[0] = 0; }
    render_standby();
    if (wf_on) wf_do_scan();
    else wf_render_list();
}

static void wf_rescan_cb(lv_event_t *e) { (void)e; wf_do_scan(); }

static void wf_join_now(void)
{
    wf_joining = true;
    wifi_st = UI_WIFI_CONNECTING;
    snprintf(wifi_ssid, sizeof wifi_ssid, "%s", wf_sel);
    char b[80];
    snprintf(b, sizeof b, "Đang kết nối %s…", wf_sel);
    lv_label_set_text(wf_conn_t, b);
    lv_label_set_text(wf_conn_s, wf_use_saved ? "Mạng đã lưu · không cần nhập mật khẩu" : "Lưu mạng này để lần sau tự kết nối");
    show(wf_conn_spin, true);
    show(wf_conn_icon, false);
    wf_page(wf_conn_pg);
    render_standby();
    if (wf_h.join) wf_h.join(wf_sel, wf_use_saved ? NULL : wf_pwd);
    wf_use_saved = false;
}

static void wf_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= wf_n) return;
    if (wifi_st == UI_WIFI_CONNECTED && !strcmp(wifi_ssid, wf_aps[i].ssid)) return;
    snprintf(wf_sel, sizeof wf_sel, "%s", wf_aps[i].ssid);
    wf_pwd[0] = 0;
    wf_err[0] = 0;
    if (wf_aps[i].saved) { wf_use_saved = true; wf_join_now(); return; }   /* da luu: vao luon */
    if (!wf_aps[i].lock) { wf_join_now(); return; }
    wf_shift = wf_num = wf_show = false;
    lv_label_set_text(wf_pass_sub, wf_sel);
    wf_render_pass();
    wf_page(wf_pass_pg);
}

static void wf_forget_cb(lv_event_t *e)                          /* giu lau mang da luu: quen mang */
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= wf_n || !wf_aps[i].saved) return;
    wf_aps[i].saved = false;
    if (wf_h.forget) wf_h.forget(wf_aps[i].ssid);
    char b[64];
    snprintf(b, sizeof b, "Đã quên mạng %s", wf_aps[i].ssid);
    wf_render_list();
    lv_label_set_text(wf_sub, b);                                   /* trang Wi-Fi nam tren lop tren cung: bao o dong phu */
}

static void wf_render_list(void)
{
    if (!wf_scr) return;
    char b[80];
    if (!wf_on) snprintf(b, sizeof b, "Bật để lấy giờ (NTP) và thời tiết");
    else if (wifi_st == UI_WIFI_CONNECTED) snprintf(b, sizeof b, "Đã kết nối %s", wifi_ssid);
    else if (wifi_st == UI_WIFI_CONNECTING) snprintf(b, sizeof b, "Đang kết nối %s…", wifi_ssid);
    else {
        bool any = false;
        for (int i = 0; i < wf_n; i++) any |= wf_aps[i].saved;
        snprintf(b, sizeof b, "%s", any && !wf_scanning ? "Chạm mạng đã lưu để vào ngay · giữ lâu để quên"
                                                         : "Dò mạng để lấy giờ (NTP) và thời tiết");
    }
    lv_label_set_text(wf_sub, b);
    style_switch(wf_sw, wf_on);
    show(wf_off_l, !wf_on);
    show(wf_busy, wf_on && wf_scanning);
    show(wf_list, wf_on && !wf_scanning);
    lv_obj_set_style_opa(wf_rescan, wf_on ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_clean(wf_list);
    if (!wf_on || wf_scanning) return;
    int w = UI_W - 24;
    if (!wf_n) {
        lv_obj_t *l = mk_label_w(wf_list, &ui_font_txt16, 0, 80, w, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(l, "Không thấy mạng nào · bấm Dò lại");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        return;
    }
    for (int i = 0; i < wf_n; i++) {
        bool on = wifi_st == UI_WIFI_CONNECTED && !strcmp(wifi_ssid, wf_aps[i].ssid);
        lv_obj_t *r = mk_card(wf_list, 0, i * 50, w, 44, 12);
        lv_obj_set_style_bg_color(r, on ? S_ON_BG : S_CARD, 0);
        lv_obj_set_style_border_color(r, on ? S_GREEN : S_BORDER, 0);
        lv_obj_set_style_bg_color(r, S_ON_BG, LV_STATE_PRESSED);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, wf_row_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r, wf_forget_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        int n = wf_bars(wf_aps[i].rssi);
        static const int H[4] = {5, 8, 11, 14};
        for (int k = 0; k < 4; k++) {
            lv_obj_t *bar = mk_fill(r, 12 + k * 6, (42 - 14) / 2 + 14 - H[k], 4, H[k], 1);
            lv_obj_set_style_bg_color(bar, k < n ? S_GREEN2 : S_TRACK, 0);
        }
        int right = w - 2 - 12;
        if (wf_aps[i].lock) {
            mk_lock(r, right - 12, (42 - 14) / 2, lv_color_white());
            right -= 12 + 10;
        } else {
            lv_obj_t *m = mk_label(r, &ui_font_sm12, 0, (42 - ui_font_sm12.line_height) / 2, "Mở");
            lv_obj_set_style_text_color(m, lv_color_white(), 0);
            right -= text_w("Mở", &ui_font_sm12);
            lv_obj_set_x(m, right);
            right -= 10;
        }
        if (on || wf_aps[i].saved) {
            const char *tag = on ? "Đã kết nối" : "Đã lưu";
            lv_obj_t *c = mk_label(r, &ui_font_sm12, 0, (42 - ui_font_sm12.line_height) / 2, tag);
            lv_obj_set_style_text_color(c, S_GREEN3, 0);
            right -= text_w(tag, &ui_font_sm12);
            lv_obj_set_x(c, right);
            right -= 10;
        }
        lv_obj_t *t = mk_label_w(r, &ui_font_txt16, 46, (42 - ui_font_txt16.line_height) / 2, right - 46, LV_TEXT_ALIGN_LEFT);
        lv_label_set_text(t, wf_aps[i].ssid);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
    }
}

/* ---- nhap mat khau ---- */
static void wf_render_pass(void)
{
    char b[sizeof wf_pwd];
    int n = (int)strlen(wf_pwd);
    if (wf_show) snprintf(b, sizeof b, "%s", wf_pwd);
    else {
        int k = 0;
        for (int i = 0; i < n && k + 3 < (int)sizeof b; i++) { b[k++] = (char)0xE2; b[k++] = (char)0x80; b[k++] = (char)0xA2; }
        b[k] = 0;
    }
    /* chi hien phan cuoi vua o nhap */
    int maxw = UI_W - 24 - 2 - 12 - 44 - 6;
    const char *v = b;
    while (*v && text_w(v, &ui_font_txt20) > maxw) {
        v++;
        while ((*v & 0xC0) == 0x80) v++;
    }
    lv_label_set_text(wf_in, v);
    show(wf_ph, !n);
    lv_label_set_text(wf_ph, wf_err[0] ? wf_err : "Mật khẩu (ít nhất 8 ký tự)");
    lv_obj_set_style_text_color(wf_ph, wf_err[0] ? lv_color_hex(0xFF8A8A) : lv_color_white(), 0);
    lv_obj_set_style_border_color(wf_in_box, wf_err[0] ? lv_color_hex(0xFF4D4F) : S_GREEN, 0);
    int cx = n ? 12 + text_w(v, &ui_font_txt20) + 2 : 12 + (wf_err[0] ? 0 : 0);
    lv_obj_set_x(wf_caret, n ? cx : 10);
    lv_label_set_text(wf_eye_l, wf_show ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void wf_build_keys(void);

static void wf_key_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    size_t n = strlen(wf_pwd);
    if (k >= 32) {
        if (n + 1 < sizeof wf_pwd) { wf_pwd[n] = (char)k; wf_pwd[n + 1] = 0; }
        wf_err[0] = 0;
        if (wf_shift && !wf_num) { wf_shift = false; wf_build_keys(); }
    } else if (k == WK_DEL) {
        if (n) wf_pwd[n - 1] = 0;
    } else if (k == WK_SHIFT) {
        wf_shift = !wf_shift;
        wf_build_keys();
    } else if (k == WK_NUM) {
        wf_num = !wf_num;
        wf_build_keys();
    } else if (k == WK_JOIN) {
        if (n < 8) {
            snprintf(wf_err, sizeof wf_err, "Mật khẩu cần ít nhất 8 ký tự");
            wf_pwd[0] = 0;
        } else {
            wf_join_now();
            return;
        }
    }
    wf_render_pass();
}

static lv_obj_t *wf_key(int x, int y, int w, const char *txt, const lv_font_t *f, int code)
{
    lv_obj_t *o = mk_card(wf_keys, x, y, w, 42, 8);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x2C3149), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0x32374C), 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x7A4535), LV_STATE_PRESSED);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, wf_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = mk_label(o, f, 0, 0, txt);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return o;
}

static void wf_build_keys(void)
{
    static const char *const LO[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char *const NU[3] = {"1234567890", "@#$_&-+()/", ".,?!:;'"};
    lv_obj_clean(wf_keys);
    int W = UI_W - 24, KW = 41, G = 5, RH = 42 + 6;
    for (int r = 0; r < 3; r++) {
        const char *s = wf_num ? NU[r] : LO[r];
        int n = (int)strlen(s), row = n * KW + (n - 1) * G + (r == 2 ? 2 * (62 + G) : 0);
        int x = (W - row) / 2, y = r * RH;
        if (r == 2) {
            lv_obj_t *o = wf_key(x, y, 62, LV_SYMBOL_UP, &lv_font_montserrat_14, WK_SHIFT);
            if (wf_shift && !wf_num) {
                lv_obj_set_style_bg_color(o, S_ON_BG, 0);
                lv_obj_set_style_border_color(o, S_GREEN, 0);
            }
            x += 62 + G;
        }
        for (int i = 0; i < n; i++) {
            char c = s[i];
            if (wf_shift && !wf_num && c >= 'a' && c <= 'z') c = (char)(c - 32);
            char t[2] = {c, 0};
            wf_key(x, y, KW, t, &ui_font_txt20, (unsigned char)c);
            x += KW + G;
        }
        if (r == 2) wf_key(x, y, 62, LV_SYMBOL_BACKSPACE, &lv_font_montserrat_14, WK_DEL);
    }
    int y = 3 * RH;
    wf_key(0, y, 72, wf_num ? "ABC" : "123", &ui_font_txt14, WK_NUM);
    wf_key(72 + G, y, W - 72 - 130 - 2 * G, "dấu cách", &ui_font_sm12, ' ');
    lv_obj_t *j = wf_key(W - 130, y, 130, "Kết nối", &ui_font_txt16, WK_JOIN);
    lv_obj_set_style_bg_color(j, lv_color_hex(0xFF5A46), 0);
    lv_obj_set_style_border_color(j, S_GREEN, 0);
    lv_obj_set_style_bg_color(j, S_GREEN, LV_STATE_PRESSED);
}

static void wf_eye_cb(lv_event_t *e) { (void)e; wf_show = !wf_show; wf_render_pass(); }

static void wf_caret_anim(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, v > 50 ? LV_OPA_COVER : LV_OPA_TRANSP, 0); }

static void wf_conn_cb(lv_event_t *e)
{
    (void)e;
    if (wf_joining) return;
    if (wifi_st == UI_WIFI_CONNECTED) wf_close();
    else { wf_render_pass(); wf_page(wf_pass_pg); }
}

static void wf_build(void)
{
    wf_scr = mk_fill(lv_layer_top(), 0, 0, UI_W, UI_H, 0);
    lv_obj_set_style_bg_color(wf_scr, S_BG, 0);
    lv_obj_add_flag(wf_scr, LV_OBJ_FLAG_CLICKABLE);                /* chan cham xuong man duoi */

    /* danh sach */
    lv_obj_t *p = wf_list_pg = mk_box(wf_scr, 0, 0, UI_W, UI_H);
    wf_head(p, "Wi-Fi", &wf_sub);
    wf_sw = mk_switch(p, UI_W - 12 - 46, 12 + 5);
    lv_obj_add_flag(wf_sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(wf_sw, 10);
    lv_obj_add_event_cb(wf_sw, wf_toggle_cb, LV_EVENT_CLICKED, NULL);
    wf_list = mk_box(p, 12, 58, UI_W - 24, 206);
    lv_obj_add_flag(wf_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wf_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(wf_list, LV_SCROLLBAR_MODE_OFF);
    wf_busy = mk_box(p, 12, 58, UI_W - 24, 206);
    lv_obj_t *sp = lv_spinner_create(wf_busy);
    lv_obj_set_size(sp, 34, 34);
    lv_obj_align(sp, LV_ALIGN_CENTER, 0, -16);
    wf_spin_style(sp, 4);
    lv_obj_t *bl = mk_label_w(wf_busy, &ui_font_txt16, 0, 206 / 2 + 12, UI_W - 24, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(bl, "Đang dò mạng Wi-Fi…");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    wf_off_l = mk_label_w(p, &ui_font_txt16, 0, 58 + 206 / 2 - 10, UI_W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(wf_off_l, "Wi-Fi đang tắt");
    lv_obj_set_style_text_color(wf_off_l, lv_color_white(), 0);
    wf_rescan = mk_card(p, 12, UI_H - 10 - 36, UI_W - 24, 36, 18);
    lv_obj_set_style_bg_color(wf_rescan, S_CARD, 0);
    lv_obj_set_style_border_color(wf_rescan, lv_color_hex(0x7A4535), 0);
    lv_obj_set_style_bg_color(wf_rescan, S_ON_BG, LV_STATE_PRESSED);
    lv_obj_add_flag(wf_rescan, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wf_rescan, wf_rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = mk_label(wf_rescan, &ui_font_txt14, 0, 0, "Dò lại");
    lv_obj_set_style_text_color(rl, S_GREEN3, 0);
    lv_obj_center(rl);

    /* mat khau */
    p = wf_pass_pg = mk_box(wf_scr, 0, 0, UI_W, UI_H);
    wf_head(p, "Nhập mật khẩu", &wf_pass_sub);
    wf_in_box = mk_card(p, 12, 56, UI_W - 24, 40, 12);
    lv_obj_set_style_bg_color(wf_in_box, S_CARD, 0);
    wf_in = mk_label(wf_in_box, &ui_font_txt20, 12, (38 - ui_font_txt20.line_height) / 2, "");
    lv_obj_set_style_text_color(wf_in, lv_color_white(), 0);
    wf_ph = mk_label(wf_in_box, &ui_font_txt14, 16, (38 - ui_font_txt14.line_height) / 2, "");
    wf_caret = mk_fill(wf_in_box, 12, (38 - 20) / 2, 2, 20, 1);
    lv_obj_set_style_bg_color(wf_caret, S_GREEN2, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, wf_caret);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_playback_duration(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, wf_caret_anim);
    lv_anim_start(&a);
    lv_obj_t *eye = mk_box(wf_in_box, UI_W - 24 - 2 - 6 - 36, 1, 36, 36);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(eye, wf_eye_cb, LV_EVENT_CLICKED, NULL);
    wf_eye_l = mk_label(eye, &lv_font_montserrat_14, 0, 0, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(wf_eye_l, S_GREEN3, 0);
    lv_obj_center(wf_eye_l);
    wf_keys = mk_box(p, 12, UI_H - 10 - 4 * 42 - 3 * 6, UI_W - 24, 4 * 42 + 3 * 6);
    wf_build_keys();

    /* dang ket noi */
    p = wf_conn_pg = mk_box(wf_scr, 0, 0, UI_W, UI_H);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p, wf_conn_cb, LV_EVENT_CLICKED, NULL);
    wf_conn_spin = lv_spinner_create(p);
    lv_obj_set_size(wf_conn_spin, 44, 44);
    lv_obj_set_pos(wf_conn_spin, (UI_W - 44) / 2, 96);
    wf_spin_style(wf_conn_spin, 5);
    wf_conn_icon = mk_wifi_icon(p, (UI_W - 44) / 2, 96, 44);
    wifi_icon_color(wf_conn_icon, S_GREEN2, false);
    wf_conn_t = mk_label_w(p, &ui_font_txt20, 12, 156, UI_W - 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(wf_conn_t, lv_color_white(), 0);
    wf_conn_s = mk_label_w(p, &ui_font_txt14, 12, 188, UI_W - 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(wf_conn_s, S_GREEN3, 0);

    wf_close_tmr = lv_timer_create(wf_close_cb, 2200, NULL);
    lv_timer_pause(wf_close_tmr);
    wf_page(wf_list_pg);
    show(wf_scr, false);
}

void ui_open_wifi(void)
{
    if (!wf_scr) wf_build();
    show(wf_scr, true);
    lv_obj_move_foreground(wf_scr);
    wf_page(wf_list_pg);
    if (wf_on && !wf_scanning) wf_do_scan();
    else wf_render_list();
}

void ui_set_wifi_hooks(const ui_wifi_hooks_t *h) { if (h) wf_h = *h; }

static char wf_hint[96] = "Sai mật khẩu hoặc sóng yếu";
void ui_wifi_fail_hint(const char *hint) { snprintf(wf_hint, sizeof wf_hint, "%s", hint && *hint ? hint : "Sai mật khẩu hoặc sóng yếu"); }

void ui_wifi_set_state(int st, const char *ssid)
{
    wifi_st = st;
    wf_on = st != UI_WIFI_OFF;
    if (ssid) snprintf(wifi_ssid, sizeof wifi_ssid, "%s", ssid);
    render_standby();
    if (!wf_scr) return;
    wf_render_list();
    if (!wf_joining || !shown(wf_scr)) {
        if (st == UI_WIFI_CONNECTED || st == UI_WIFI_FAIL) wf_joining = false;
        return;
    }
    char b[128];
    if (st == UI_WIFI_CONNECTED) {
        wf_joining = false;
        snprintf(b, sizeof b, "Đã kết nối %s", wifi_ssid);
        lv_label_set_text(wf_conn_t, b);
        lv_label_set_text(wf_conn_s, "Đang đồng bộ giờ (NTP) và tải thời tiết…");
        show(wf_conn_spin, false);
        show(wf_conn_icon, true);
        lv_timer_reset(wf_close_tmr);
        lv_timer_resume(wf_close_tmr);
    } else if (st == UI_WIFI_FAIL) {
        wf_joining = false;
        snprintf(b, sizeof b, "Không kết nối được %s", wf_sel);
        lv_label_set_text(wf_conn_t, b);
        snprintf(b, sizeof b, "%s · chạm để nhập lại", wf_hint);
        lv_label_set_text(wf_conn_s, b);
        show(wf_conn_spin, false);
        show(wf_conn_icon, true);
        wifi_icon_color(wf_conn_icon, lv_color_hex(0xFF4D4F), true);
        return;
    }
    wifi_icon_color(wf_conn_icon, S_GREEN2, false);
}

void ui_wifi_scan_done(const ui_wifi_ap_t *aps, int n)
{
    wf_n = 0;
    for (int i = 0; i < n && wf_n < WF_MAX; i++) {
        if (!aps[i].ssid[0]) continue;
        bool dup = false;
        for (int k = 0; k < wf_n; k++) if (!strcmp(wf_aps[k].ssid, aps[i].ssid)) dup = true;
        if (!dup) wf_aps[wf_n++] = aps[i];
    }
    wf_scanning = false;
    wf_render_list();
}

/* chi de chup anh xem truoc: trang nhap mat khau da go san */
void ui_wifi_test_pass(const char *ssid, const char *pwd, bool err)
{
    if (!wf_scr) wf_build();
    show(wf_scr, true);
    snprintf(wf_sel, sizeof wf_sel, "%s", ssid);
    snprintf(wf_pwd, sizeof wf_pwd, "%s", err ? "" : pwd);
    snprintf(wf_err, sizeof wf_err, "%s", err ? "Mật khẩu cần ít nhất 8 ký tự" : "");
    lv_label_set_text(wf_pass_sub, wf_sel);
    wf_render_pass();
    wf_page(wf_pass_pg);
}

void ui_wifi_test_join(void) { wf_join_now(); }
