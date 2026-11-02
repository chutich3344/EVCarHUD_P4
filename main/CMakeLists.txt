idf_component_register(
    SRCS "main.c" "board.c" "audio.c" "buttons.c" "settings.c" "ble_link.c" "link.c" "demo.c"
         "dashboard.c" "dashboard_hlp.c" "dashboard_signs.c" "hlp.c" "gofa.c"
         "ui_font_cap12.c" "ui_font_sm12.c" "ui_font_txt14.c" "ui_font_txt16.c" "ui_font_mono14.c"
         "ui_font_dist24.c" "ui_font_sign24.c" "ui_font_speed62.c"
         "ui_font_sign32.c" "ui_logo.c"
         "ui_font_num54.c" "ui_font_unit22.c"
         "ui_font_speed140.c" "ui_font_sign64.c" "ui_font_num40.c" "ui_font_clk18.c" "ui_font_clock120.c" "ui_font_clock88.c" "ui_font_txt20.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer nvs_flash bt esp_app_format esp_driver_i2s)
