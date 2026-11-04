#!/usr/bin/env bash
# Sinh font LVGL cho giao dien the (Inter + JetBrains Mono, giay phep OFL) vao main/ui_font_*.c
# Can Node.js: bash tools/fonts/gen_fonts.sh
set -euo pipefail
cd "$(dirname "$0")"
[ -d node_modules/lv_font_conv ] || npm i --no-save lv_font_conv @fontsource/inter @fontsource/jetbrains-mono
F=node_modules/@fontsource
OUT=../../main
LATIN="0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2022,0x2026,0x203A"
VIET="0x102-0x103,0x110-0x111,0x128-0x129,0x168-0x169,0x1A0-0x1A1,0x1AF-0x1B0,0x1EA0-0x1EF9"
gen() {  # ten co ho do_dam bo_ky_tu(full|digits|ascii)
  local name=$1 size=$2 fam=$3 w=$4 set=$5 args=()
  case $set in
    full)   args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r $LATIN --font $F/$fam/files/$fam-vietnamese-$w-normal.woff -r $VIET) ;;
    ascii)  args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x20-0x7E) ;;
    digits) args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20) ;;
    onoff)  args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x46,0x4E,0x4F) ;;
    num)    args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20,0x2C) ;;
    numpct) args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20,0x2C,0x25) ;;
    dist)   args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x30-0x39,0x2D,0x20,0x2C,0x6B,0x6D,0x2F,0x68) ;;
    logo)   args=(--font $F/$fam/files/$fam-latin-$w-normal.woff -r 0x45,0x56,0x43,0x61,0x72,0x48,0x55,0x44,0x20) ;;
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
# giao dien A (dan duong) + C (chay thuong): so to, it chu
gen ui_font_unit22  22 inter 700 ascii
gen ui_font_logo34  34 inter 800 logo
# ban P4 3.5" (480x320): so to hon, van la Inter
gen ui_font_sign64  64 inter 700 digits
gen ui_font_num40   40 inter 700 numpct
gen ui_font_clk18   18 inter 600 clock
gen ui_font_clock120 120 inter 700 clock
gen ui_font_txt20   20 inter 700 full
# so to hon cho man chi dan / canh bao (gop y sau khi chay tren board that)
# bo cuc chot 02/11: toc do + bien gioi han chia doi phia tren, Inter ExtraBold (800)
gen ui_font_d150    150 inter 800 digits
gen ui_font_d120    120 inter 800 digits
gen ui_font_d104    104 inter 800 digits
gen ui_font_d96     96 inter 800 digits
gen ui_font_r76     76 inter 800 digits
gen ui_font_r50     50 inter 800 digits
gen ui_font_r44     44 inter 800 digits
gen ui_font_r36     36 inter 800 digits
gen ui_font_dist26  26 inter 800 dist
gen ui_font_dist30  30 inter 800 dist
gen ui_font_r92    92 inter 800 digits
