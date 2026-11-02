#!/usr/bin/env python3
"""Tao main/ui_logo.c (logo EVCarHUD: vong dong ho + chu EVCarHUD, RGB565 tren nen toi 0x020617) tu logo-khoi-dong.png.
logo-khoi-dong.png chup tu logo-khoi-dong.html bang render_logo.js (logo that tu repo evcarhud-web).
Chay: python3 tools/logo/gen_logo.py  (can Pillow)"""
import os
from PIL import Image

W, H = 240, 136
BG = (0x02, 0x06, 0x17)
here = os.path.dirname(os.path.abspath(__file__))
src = Image.open(os.path.join(here, "logo-khoi-dong.png")).convert("RGBA").resize((W, H), Image.LANCZOS)
img = Image.new("RGBA", (W, H), BG + (255,))
img.alpha_composite(src)
data = bytearray()
for r, g, b, _ in img.getdata():
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    data += bytes((v & 0xFF, v >> 8))                    # LVGL: RGB565 little-endian
lines = [", ".join(f"0x{x:02x}" for x in data[i:i + 16]) for i in range(0, len(data), 16)]
out = os.path.join(here, "..", "..", "main", "ui_logo.c")
with open(out, "w") as f:
    f.write("/* Tu sinh boi tools/logo/gen_logo.py: logo EVCarHUD %dx%d, RGB565 tren nen 0x020617. Khong sua tay. */\n" % (W, H))
    f.write('#include "lvgl.h"\n\n')
    f.write("static const uint8_t px[] = {\n    " + ",\n    ".join(lines) + "\n};\n\n")
    f.write("const lv_image_dsc_t ui_logo = {\n")
    f.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = %d, .h = %d, .stride = %d},\n" % (W, H, W * 2))
    f.write("    .data_size = sizeof px,\n    .data = px,\n};\n")
print("wrote", out, len(data), "bytes")
