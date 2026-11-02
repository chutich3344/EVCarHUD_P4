#!/usr/bin/env bash
# Sinh font LVGL cho giao dien the (Inter + JetBrains Mono, giay phep OFL) vao main/ui_font_*.c
# Can Node.js: bash tools/fonts/gen_fonts.sh
set -euo pipefail
cd "$(dirname "$0")"
[ -d node_modules/lv_font_conv ] || npm i --no-save lv_font_conv @fontsource/inter @fontsource/jetbrains-mono
F=node_modules/@fontsource
OUT=../../main
LATIN="0x20-0x7E,0xA0-0xFF"
VIET="0x102-0x103,0x110-0x111,0x128-0x129,0x168-0x169,0x1A0-0x1A1,0x1AF-0x1B0,0x1EA0-0x1EF9"
gen() {  # ten co ho do_dam bo_ky_tu(full|digits|ascii)
  local name=$1 size=$2 fam=$3 w=$4 set=$5 args=()
  case $set in
    full)   args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r $LATIN --font $F/$fam/files/$fam-vietnamese-$w-normal.woff -r $VIET) ;;
    ascii)  args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x20-0x7E) ;;
    digits) args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20) ;;
    onoff)  args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x46,0x4E,0x4F) ;;
    num)    args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20,0x2C) ;;
    clock)  args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20,0x3A) ;;
  esac
  npx lv_font_conv --bpp 4 --size $size --format lvgl --no-compress --lv-include lvgl.h \
      --lv-font-name $name "${args[@]}" -o $OUT/$name.c
  echo "$name.c"
}
gen ui_font_cap12   12 inter 700 full
gen ui_font_sm12    12 inter 500 full
gen ui_font_txt14   14 inter 600 full
gen ui_font_txt16   16 inter 700 full
gen ui_font_mono14  14 jetbrains-mono 700 full
gen ui_font_dist24  24 jetbrains-mono 700 ascii
gen ui_font_sign24  24 inter 700 digits
gen ui_font_speed62 62 inter 700 digits
gen ui_font_speed88 88 inter 700 digits
gen ui_font_sign32  32 inter 700 digits
gen ui_font_clock60 60 jetbrains-mono 700 digits
gen ui_font_clock80 80 jetbrains-mono 700 clock
# giao dien A (dan duong) + C (chay thuong): so to, it chu
gen ui_font_num54   54 inter 700 num
gen ui_font_num30   30 inter 700 num
gen ui_font_speed72 72 inter 700 digits
gen ui_font_speed110 110 inter 700 digits
gen ui_font_sign40  40 inter 700 digits
gen ui_font_unit22  22 inter 700 ascii
gen ui_font_onoff44 44 inter 700 onoff
