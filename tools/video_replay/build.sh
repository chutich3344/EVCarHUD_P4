#!/usr/bin/env bash
# Dung cong cu chay lai video (Linux / WSL). Dung chung LVGL da tai cua tools/pc_preview.
set -euo pipefail
cd "$(dirname "$0")"
P=../pc_preview
[ -d $P/lvgl ] || git clone --depth 1 -b v9.2.2 https://github.com/lvgl/lvgl $P/lvgl
M=../../main
mkdir -p $P/build
for f in $(cd $P && find lvgl/src -name '*.c'); do
  o=$P/build/$(echo "$f" | tr / _).o
  [ "$o" -nt "$P/$f" ] || cc -c -O1 -DLV_CONF_INCLUDE_SIMPLE -I$P -I$P/lvgl "$P/$f" -o "$o"
done
SRC="$M/dashboard.c $M/dashboard_hlp.c $M/hlp.c $M/gofa.c $M/demo.c $M/dashboard_signs.c $M/ui_font_*.c $M/ui_logo_icon.c
     $M/obstacle_ui.c $M/obstacle_icons.c $M/adas.c $M/adas_feed.c replay.c"
cc -O1 -Wall -DLV_CONF_INCLUDE_SIMPLE ${EXTRA:-} -Istub -I$P -I$P/lvgl -I$M $SRC $P/build/*.o -lm -o replay
echo "da dung tools/video_replay/replay"
