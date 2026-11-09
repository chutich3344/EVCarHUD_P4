# EVCarHUD cho board Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 (man 3.5 inch 480x320). Build: xem README.md
cmake_minimum_required(VERSION 3.16)
# Phien ban firmware = noi dung version.txt (vd 1.0.0). HUD so voi phien ban moi nhat tren evcarhud.com/admin:
# moi lan phat hanh ban moi qua OTA thi tang so trong version.txt va dung dung so do khi tai len trang admin.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(evcarhud_p4)
