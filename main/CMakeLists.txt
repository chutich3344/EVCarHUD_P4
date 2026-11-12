idf_component_register(
    SRCS "main.c" "board.c" "audio.c" "buttons.c" "settings.c" "ble_link.c" "link.c" "demo.c"
         "dashboard.c" "dashboard_hlp.c" "dashboard_signs.c" "hlp.c" "gofa.c" "rtc_ds3231.c" "wifi_link.c" "c6_update.c" "ota.c" "bt_diag.c" "ai.c" "usb_cam.c"
         "detect.cpp" "adas.c" "adas_feed.c" "obstacle_ui.c" "obstacle_icons.c" "ui_font_warn64.c" "ui_font_warn40.c"
         "ui_font_cap12.c" "ui_font_sm12.c" "ui_font_txt14.c" "ui_font_txt16.c" "ui_font_mono14.c"
         "ui_font_dist24.c"
         "ui_font_unit22.c"
         "ui_font_logo34.c"
         "ui_logo_icon.c"
         "ui_font_sign64.c" "ui_font_num40.c" "ui_font_clk18.c" "ui_font_clock120.c" "ui_font_txt20.c"
        
         "ui_font_d190.c" "ui_font_d150.c" "ui_font_d120.c" "ui_font_d104.c" "ui_font_d96.c" "ui_font_r76.c" "ui_font_r50.c" "ui_font_r44.c" "ui_font_r36.c" "ui_font_dist26.c" "ui_font_dist30.c" "ui_font_r92.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer nvs_flash bt esp_app_format esp_driver_i2s
             esp_wifi esp_netif esp_event esp_http_client json mbedtls lwip usb esp_driver_jpeg esp_partition
             app_update esp_http_server)

# esp-sr (tu danh thuc Mi Mi) bat esp-dl chi bien dich phep tinh WakeNet can; khai bao them TOAN BO phep tinh cho mo hinh
# nhan dien vat can evdet, neu khong dl::Model khong tao duoc (main/evdet.compile_req.yml).
idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp-dl" IN_LIST build_components)
    idf_component_get_property(_espdl_dir espressif__esp-dl COMPONENT_DIR)
    include(${_espdl_dir}/cmake/compile_register.cmake)
    esp_dl_compile_add_yml(${CMAKE_CURRENT_LIST_DIR}/evdet.compile_req.yml)
endif()

# Mo hinh nhan dien vat can (lay tu repo EVCarHUD/esp, huan luyen bang ml/train_evhud.py): ghi vao vung flash "evdet".
# idf.py flash va file gop (idf.py merge-bin) deu co mo hinh nay. Doi mo hinh = chep file .espdl moi de len models/evdet.espdl.
if(CONFIG_EVCARHUD_DETECT)
    get_filename_component(evdet_model "${CMAKE_CURRENT_LIST_DIR}/../models/evdet.espdl" ABSOLUTE)
    if(NOT EXISTS "${evdet_model}")
        message(FATAL_ERROR "Thieu mo hinh nhan dien: ${evdet_model}")
    endif()
    esptool_py_flash_to_partition(flash "evdet" "${evdet_model}")
endif()

# Chan doan Bluetooth C6 (main/bt_diag.c): boc ham gui/nhan HCI cua esp_hosted de dem goi
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=eh_host_transport_tx" "-Wl,--wrap=eh_host_mcu_hci_rx_deliver"
    "-Wl,-u,__wrap_eh_host_transport_tx" "-Wl,-u,__wrap_eh_host_mcu_hci_rx_deliver")
