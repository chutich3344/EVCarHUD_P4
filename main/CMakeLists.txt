idf_component_register(
    SRCS "main.c" "board.c" "audio.c" "buttons.c" "settings.c" "ble_link.c" "link.c" "demo.c"
         "dashboard.c" "dashboard_hlp.c" "dashboard_signs.c" "hlp.c" "gofa.c" "rtc_ds3231.c" "wifi_link.c" "ota.c" "usb_cam.c"
         "detect.cpp" "adas.c" "adas_feed.c" "lidar.c" "obstacle_ui.c" "obstacle_icons.c" "ui_font_warn64.c" "ui_font_warn40.c"
         "ui_font_cap12.c" "ui_font_sm12.c" "ui_font_txt14.c" "ui_font_txt16.c" "ui_font_mono14.c"
         "ui_font_dist24.c"
         "ui_font_unit22.c"
         "ui_font_logo34.c"
         "ui_logo_icon.c"
         "ui_font_sign64.c" "ui_font_num40.c" "ui_font_clk18.c" "ui_font_clock120.c" "ui_font_txt20.c"
        
         "ui_font_d190.c" "ui_font_d150.c" "ui_font_d120.c" "ui_font_d104.c" "ui_font_d96.c" "ui_font_r76.c" "ui_font_r50.c" "ui_font_r44.c" "ui_font_r36.c" "ui_font_dist26.c" "ui_font_dist30.c" "ui_font_r92.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer nvs_flash bt esp_app_format esp_driver_i2s
             esp_wifi esp_netif esp_event esp_http_client json mbedtls lwip usb esp_driver_jpeg esp_driver_uart esp_partition
             app_update esp_http_server)

# Mo hinh nhan dien vat can (lay tu repo EVCarHUD/esp, huan luyen bang ml/train_evhud.py): ghi vao vung flash "evdet".
# idf.py flash va file gop (idf.py merge-bin) deu co mo hinh nay. Doi mo hinh = chep file .espdl moi de len models/evdet.espdl.
if(CONFIG_EVCARHUD_DETECT)
    get_filename_component(evdet_model "${CMAKE_CURRENT_LIST_DIR}/../models/evdet.espdl" ABSOLUTE)
    if(NOT EXISTS "${evdet_model}")
        message(FATAL_ERROR "Thieu mo hinh nhan dien: ${evdet_model}")
    endif()
    esptool_py_flash_to_partition(flash "evdet" "${evdet_model}")
endif()
