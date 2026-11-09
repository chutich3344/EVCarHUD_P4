#!/usr/bin/env python3
"""Tao main/ui_logo_icon.c (bieu tuong logo 96x96, ARGB8888 nen trong suot: dung cho ca nen Gradient dem va ngay)
tu logo-icon.png (chup bang render_icon.js). Chay: python3 tools/logo/gen_icon.py  (can Pillow)"""
import os
from PIL import Image
N = 96
here = os.path.dirname(os.path.abspath(__file__))
img = Image.open(os.path.join(here, "logo-icon.png")).convert("RGBA").resize((N, N), Image.LANCZOS)
data = bytearray()
for r, g, b, a in img.getdata():
    data += bytes((b, g, r, a))                          # LVGL ARGB8888: B G R A
lines = [", ".join(f"0x{x:02x}" for x in data[i:i + 16]) for i in range(0, len(data), 16)]
out = os.path.join(here, "..", "..", "main", "ui_logo_icon.c")
with open(out, "w") as f:
    f.write("/* Tu sinh boi tools/logo/gen_icon.py: bieu tuong logo EVCarHUD %dx%d, ARGB8888. Khong sua tay. */\n" % (N, N))
    f.write('#include "lvgl.h"\n\n')
    f.write("static const uint8_t px[] = {\n    " + ",\n    ".join(lines) + "\n};\n\n")
    f.write("const lv_image_dsc_t ui_logo_icon = {\n")
    f.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = %d, .h = %d, .stride = %d},\n" % (N, N, N * 4))
    f.write("    .data_size = sizeof px,\n    .data = px,\n};\n")
print("wrote", out, len(data), "bytes")
