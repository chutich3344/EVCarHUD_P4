#!/usr/bin/env python3
"""Tao main/ui_logo.c (logo ngang EVCarHUD, RGB565 tron san tren nen toi 0x05080F) tu evcarhud-logo-ngang-nen-toi.png.
Chay: python3 tools/logo/gen_logo.py  (can Pillow)"""
import os
from PIL import Image

W, H = 288, 64
BG = (0x05, 0x08, 0x0F)
here = os.path.dirname(os.path.abspath(__file__))
src = Image.open(os.path.join(here, "evcarhud-logo-ngang-nen-toi.png")).convert("RGBA").resize((W, H), Image.LANCZOS)
img = Image.new("RGBA", (W, H), BG + (255,))
img.alpha_composite(src)
data = bytearray()
for r, g, b, _ in img.getdata():
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    data += bytes((v & 0xFF, v >> 8))                    # LVGL: RGB565 little-endian
lines = [", ".join(f"0x{x:02x}" for x in data[i:i + 16]) for i in range(0, len(data), 16)]
out = os.path.join(here, "..", "..", "main", "ui_logo.c")
with open(out, "w") as f:
    f.write("/* Tu sinh boi tools/logo/gen_logo.py: logo ngang EVCarHUD %dx%d, RGB565 tren nen 0x05080F. Khong sua tay. */\n" % (W, H))
    f.write('#include "lvgl.h"\n\n')
    f.write("static const uint8_t px[] = {\n    " + ",\n    ".join(lines) + "\n};\n\n")
    f.write("const lv_image_dsc_t ui_logo = {\n")
    f.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = %d, .h = %d, .stride = %d},\n" % (W, H, W * 2))
    f.write("    .data_size = sizeof px,\n    .data = px,\n};\n")
print("wrote", out, len(data), "bytes")
