#!/usr/bin/env bash
# Chup anh giao dien tren PC (Linux / WSL): bash tools/pc_preview/build.sh  -> anh .bmp trong tools/pc_preview/out
set -euo pipefail
cd "$(dirname "$0")"
[ -d lvgl ] || git clone --depth 1 -b v9.2.2 https://github.com/lvgl/lvgl lvgl
M=../../main
SRC="$M/dashboard.c $M/dashboard_hlp.c $M/hlp.c $M/gofa.c $M/demo.c $M/dashboard_signs.c $M/ui_font_*.c $M/ui_logo.c preview.c"
mkdir -p build out
for f in $(find lvgl/src -name '*.c'); do
  o=build/$(echo "$f" | tr / _).o
  [ "$o" -nt "$f" ] || cc -c -O1 -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl "$f" -o "$o"
done
cc -O1 -Wall -Wextra -DLV_CONF_INCLUDE_SIMPLE -I. -Ilvgl -I$M $SRC build/*.o -lm -o build/preview
./build/preview out
