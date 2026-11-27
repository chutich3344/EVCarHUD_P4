/* ======================= man Cap nhat firmware (chen vao dashboard.c) =======================
 * Mo tu Cai dat > He thong > "Cap nhat firmware". Cung nen voi man cai dat. Dau trang: nut quay lai + tieu de.
 * The giua: bieu tuong tron theo trang thai, dong chinh, dong rat to (dia chi trang tai len), dong phu, thanh tien trinh.
 * Duoi: 2 nut (phu ben trai, chinh ben phai mau nhan). Noi dung do ota.c dat qua ui_ota_view; nut goi hook action.
 * Nut BOOT khi man nay mo: 1 lan = nut chinh (khong co thi nut phu), 2 lan = quay lai. */
static void (*ota_cb)(int);
static lv_obj_t *ota_scr, *ota_back, *ota_icon, *ota_icon_l, *ota_spin, *ota_title, *ota_big, *ota_detail;
static lv_obj_t *ota_bar, *ota_fill, *ota_pct, *ota_btn[2], *ota_btn_l[2];
static lv_obj_t *upd_row, *upd_sub, *upd_dot;
static int ota_act[2];
static bool ota_lock;
static char ota_new[24];

static void ota_send(int act)
{
    if (ota_cb && act) ota_cb(act);
}

static void ota_close_now(void)
{
    if (ota_scr) show(ota_scr, false);
    if (set_tmr && shown(set_scr)) { lv_timer_reset(set_tmr); lv_timer_resume(set_tmr); }
}

static void ota_back_cb(lv_event_t *e)
{
    (void)e;
    if (ota_lock) return;
    ota_send(UI_OTA_ACT_CLOSE);
    ota_close_now();
}

static void ota_btn_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (ota_lock) return;
    if (ota_act[i] == UI_OTA_ACT_CLOSE) { ota_back_cb(e); return; }
    ota_send(ota_act[i]);
}

static void ota_build(void)
{
    ota_scr = mk_fill(set_scr, 0, 0, UI_W, UI_H, 0);
    lv_obj_set_style_bg_color(ota_scr, S_BG, 0);
    lv_obj_add_flag(ota_scr, LV_OBJ_FLAG_CLICKABLE);                /* chan cham xuong trang cai dat */

    ota_back = mk_card(ota_scr, 12, 12, 36, 36, 18);
    lv_obj_set_style_bg_color(ota_back, S_CARD, 0);
    lv_obj_set_style_border_color(ota_back, S_BORDER, 0);
    lv_obj_add_flag(ota_back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ota_back, 8);
    lv_obj_add_event_cb(ota_back, ota_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = mk_label(ota_back, &lv_font_montserrat_14, 0, 0, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, S_FG, 0);
    lv_obj_center(l);
    l = mk_label(ota_scr, &ui_font_txt16, 58, 12, "Cập nhật firmware");
    lv_obj_set_style_text_color(l, S_FG, 0);
    l = mk_label(ota_scr, &ui_font_sm12, 58, 33, "EVCarHUD 3.5 · ESP32-P4 · OTA qua Wi-Fi");
    lv_obj_set_style_text_color(l, S_GREEN3, 0);

    lv_obj_t *c = mk_card(ota_scr, 12, 60, UI_W - 24, 190, 16);
    lv_obj_set_style_bg_color(c, S_CARD, 0);
    lv_obj_set_style_border_color(c, S_BORDER, 0);
    ota_icon = mk_fill(c, 16, 16, 40, 40, 20);
    ota_icon_l = mk_label(ota_icon, &lv_font_montserrat_14, 0, 0, "");
    lv_obj_set_style_text_color(ota_icon_l, S_BG, 0);
    lv_obj_center(ota_icon_l);
    ota_spin = lv_spinner_create(c);
    lv_obj_set_pos(ota_spin, 16, 16);
    lv_obj_set_size(ota_spin, 40, 40);
    lv_spinner_set_anim_params(ota_spin, 900, 90);
    lv_obj_set_style_arc_width(ota_spin, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ota_spin, S_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ota_spin, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ota_spin, S_GREEN2, LV_PART_INDICATOR);
    ota_title = mk_label_w(c, &ui_font_txt20, 70, 16, UI_W - 24 - 2 - 70 - 14, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(ota_title, S_FG, 0);
    ota_big = mk_label_w(c, &ui_font_dist24, 70, 46, UI_W - 24 - 2 - 70 - 14, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(ota_big, S_GREEN2, 0);
    ota_detail = mk_label_w(c, &ui_font_sm12, 70, 46, UI_W - 24 - 2 - 70 - 14, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_color(ota_detail, S_MUT, 0);
    lv_obj_set_style_text_line_space(ota_detail, 3, 0);
    lv_label_set_long_mode(ota_detail, LV_LABEL_LONG_WRAP);
    ota_bar = mk_fill(c, 70, 150, UI_W - 24 - 2 - 70 - 14 - 56, 10, 5);
    lv_obj_set_style_bg_color(ota_bar, S_TRACK, 0);
    ota_fill = mk_fill(ota_bar, 0, 0, 0, 10, 5);
    lv_obj_set_style_bg_color(ota_fill, S_GREEN, 0);
    ota_pct = mk_label_w(c, &ui_font_txt16, UI_W - 24 - 2 - 14 - 50, 145, 50, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_style_text_color(ota_pct, S_FG, 0);

    for (int i = 0; i < 2; i++) {
        ota_btn[i] = mk_card(ota_scr, 0, 262, 100, 46, 23);
        lv_obj_add_flag(ota_btn[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ota_btn[i], ota_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ota_btn_l[i] = mk_label(ota_btn[i], &ui_font_txt16, 0, 0, "");
    }
    lv_obj_set_style_bg_color(ota_btn[0], S_GREEN, 0);
    lv_obj_set_style_border_color(ota_btn[0], S_GREEN, 0);
    lv_obj_set_style_shadow_width(ota_btn[0], 14, 0);
    lv_obj_set_style_shadow_color(ota_btn[0], S_GREEN, 0);
    lv_obj_set_style_shadow_opa(ota_btn[0], LV_OPA_30, 0);
    lv_obj_set_style_text_color(ota_btn_l[0], lv_color_white(), 0);
    lv_obj_set_style_bg_color(ota_btn[1], S_CARD, 0);
    lv_obj_set_style_border_color(ota_btn[1], S_BORDER, 0);
    lv_obj_set_style_text_color(ota_btn_l[1], S_FG, 0);
    show(ota_scr, false);
}

void ui_set_ota_hook(void (*action)(int act)) { ota_cb = action; }

void ui_ota_view(const ui_ota_view_t *v)
{
    if (!ota_scr) return;
    static const uint32_t tone_c[] = {0x60A5FA, 0x22C55E, 0xFF6A4D, 0xFFB067, 0xFF4560};
    static const char *const tone_s[] = {LV_SYMBOL_DOWNLOAD, LV_SYMBOL_OK, LV_SYMBOL_DOWNLOAD, LV_SYMBOL_REFRESH, LV_SYMBOL_WARNING};
    int t = v->tone >= 0 && v->tone <= UI_OTA_FAIL ? v->tone : 0;
    ota_lock = v->lock;
    show(ota_back, !v->lock);
    show(ota_spin, v->spin);
    show(ota_icon, !v->spin);
    lv_obj_set_style_bg_color(ota_icon, lv_color_hex(tone_c[t]), 0);
    lv_label_set_text(ota_icon_l, tone_s[t]);
    lv_label_set_text(ota_title, v->title ? v->title : "");
    lv_obj_update_layout(ota_title);
    int y = 16 + lv_obj_get_height(ota_title) + 8;
    show(ota_big, v->big != NULL);
    if (v->big) {
        lv_label_set_text(ota_big, v->big);
        lv_obj_set_y(ota_big, y);
        y += 34;
    }
    lv_label_set_text(ota_detail, v->detail ? v->detail : "");
    lv_obj_set_y(ota_detail, y);
    bool bar = v->pct >= 0;
    show(ota_bar, bar);
    show(ota_pct, bar);
    if (bar) {
        int p = v->pct > 100 ? 100 : v->pct;
        set_bar(ota_bar, ota_fill, p * 10);
        lv_label_set_text_fmt(ota_pct, "%d%%", p);
    }
    /* nut: 2 nut chia doi; 1 nut thi rong ca hang */
    const char *lab[2] = {v->btn_a, v->btn_b};
    ota_act[0] = v->btn_a ? v->act_a : 0;
    ota_act[1] = v->btn_b ? v->act_b : 0;
    int n = (lab[0] != NULL) + (lab[1] != NULL), w = n == 2 ? (UI_W - 24 - 10) / 2 : UI_W - 24, x = 12;
    for (int k = 1; k >= 0; k--) {                                  /* nut phu ben trai, nut chinh ben phai */
        show(ota_btn[k], lab[k] != NULL);
        if (!lab[k]) continue;
        lv_obj_set_pos(ota_btn[k], x, 262);
        lv_obj_set_width(ota_btn[k], w);
        lv_label_set_text(ota_btn_l[k], lab[k]);
        lv_obj_center(ota_btn_l[k]);
        x += w + 10;
    }
    if (v->lock && !shown(set_scr)) ui_open_settings_page(PG_SYSTEM);   /* dang ghi flash (vd tai len tu trinh duyet): luon hien */
    if (!shown(ota_scr) && shown(set_scr)) show(ota_scr, true);
    if (shown(ota_scr)) {
        lv_obj_move_foreground(ota_scr);
        lv_timer_pause(set_tmr);                                    /* dang o man cap nhat: khong tu dong dong cai dat */
    }
}

void ui_ota_close(void) { ota_close_now(); }

void ui_ota_badge(const char *ver)
{
    snprintf(ota_new, sizeof ota_new, "%s", ver ? ver : "");
    if (!upd_sub) return;
    if (ota_new[0]) lv_label_set_text_fmt(upd_sub, "Có bản mới %s · chạm để cập nhật", ota_new);
    else lv_label_set_text(upd_sub, "Tải bản mới qua Wi-Fi hoặc từ trình duyệt");
    lv_obj_set_style_text_color(upd_sub, ota_new[0] ? S_GREEN2 : S_MUT, 0);
    show(upd_dot, ota_new[0] != 0);
}

static void ota_open(void)
{
    if (!ota_scr) return;
    show(ota_scr, true);
    lv_obj_move_foreground(ota_scr);
    lv_timer_pause(set_tmr);
    ota_send(UI_OTA_ACT_OPEN);                                      /* ota.c ve noi dung */
}

void ui_open_ota(void)
{
    ui_open_settings_page(PG_SYSTEM);
    ota_open();
}

/* nut BOOT khi man cap nhat mo. true = da xu ly */
static bool ota_key(int key)
{
    if (!ota_scr || !shown(ota_scr)) return false;
    if (ota_lock) return true;
    if (key == UI_KEY_OK) { ota_back_cb(NULL); return true; }
    int i = ota_act[0] ? 0 : 1;
    if (ota_act[i]) lv_obj_send_event(ota_btn[i], LV_EVENT_CLICKED, NULL);
    return true;
}
