#!/usr/bin/env python3
"""Chay lai video quay duong (dashcam / dien thoai) qua DUNG ma canh bao vat can cua firmware, xuat video kiem tra.

    bash tools/video_replay/build.sh                     # dung ./replay (LVGL + adas_feed.c + obstacle_ui.c + dashboard.c)
    python3 tools/video_replay/run.py Test03.mp4 -o out/Test03_hud.mp4

Cac buoc:
  1. Cat giua khung hinh cho giong goc nhin camera USB ong 6 mm (~42 do ngang), thu nho 320x240 nhu tren board.
     Mac dinh cat cho dashcam 70mai 2880x1620 (mat ca, ~120 do ngang): 1008x756 tai (936, 650). Doi bang --crop.
  2. Nhan dien bang YOLO11 (ultralytics) thay cho mo hinh ESPDet-Pico tren board (PC khong chay duoc file .espdl).
     Lop COCO doi sang 4 lop cua board: o to / xe lon (buyt, tai) / xe 2 banh / nguoi.
  3. Toc do xe minh: video khong co GPS -> uoc luong tu dong chay cua mat duong (Lucas-Kanade, bo vung co xe), xem ego_speed().
     Co the ep toc do co dinh bang --speed.
  4. ./replay chay adas_feed() + obstacle_ui (phuong an E) + giao dien HUD, tra ve muc canh bao + anh HUD tung khung.
  5. Ghep: trai = anh camera + chan troi + vung lan + khung vat; phai = HUD 480x320 that; duoi = thong so.
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
import cv2
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
COCO = {2: 0, 5: 1, 7: 1, 3: 2, 1: 2, 0: 3}         # car, bus/truck -> xe lon, motorcycle/bicycle -> xe 2 banh, person
NAME = ["Ô tô", "Xe lớn", "Xe 2 bánh", "Người"]
LEVEL = ["Không báo", "Chú ý", "Nguy hiểm", "Khẩn cấp"]
LCOL = [(160, 165, 185), (255, 212, 0), (255, 77, 79), (255, 255, 255)]
W, H = 320, 240                                      # anh nhan dien tren board
VW, VH = 640, 480                                    # anh camera trong video kiem tra


def font(sz, bold=True):
    for p in ("/usr/share/fonts/opentype/inter/Inter-Bold.otf" if bold else "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def frames(path, crop, step):
    """khung 640x480 (BGR) da cat, moi khung goc 30 fps; tra ve (chi so, anh)"""
    cw, ch, cx, cy = crop
    cmd = ["ffmpeg", "-loglevel", "error", "-i", path, "-vf", f"crop={cw}:{ch}:{cx}:{cy},scale={VW}:{VH}",
           "-f", "rawvideo", "-pix_fmt", "bgr24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    i = 0
    while True:
        b = p.stdout.read(VW * VH * 3)
        if len(b) < VW * VH * 3:
            break
        yield i, np.frombuffer(b, np.uint8).reshape(VH, VW, 3)
        i += 1
    p.wait()


class EgoSpeed:
    """Toc do xe minh tu dong chay mat duong: diem tren mat dat o hang y cach chan troi (y - yh) nam cach Z = f*H/(y - yh);
    theo diem qua 2 khung lien tiep -> dZ/dt. Lay trung vi, bo diem trong khung xe (vat dang chay), loc EMA."""

    def __init__(self, f640, cam_h, fps):
        self.f, self.h, self.dt = f640, cam_h, 1.0 / fps
        self.prev = None
        self.v = 0.0

    def update(self, bgr, yh, boxes640):
        g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        if self.prev is None:
            self.prev = g
            return self.v
        mask = np.zeros_like(g)
        y0 = int(yh + 30)
        mask[y0:VH - 6, 60:VW - 60] = 255
        for (x1, y1, x2, y2) in boxes640:
            mask[max(0, y1 - 4):y2 + 4, max(0, x1 - 4):x2 + 4] = 0
        pts = cv2.goodFeaturesToTrack(self.prev, 150, 0.01, 8, mask=mask)
        est = None
        if pts is not None and len(pts) >= 8:
            nxt, st, _ = cv2.calcOpticalFlowPyrLK(self.prev, g, pts, None, winSize=(21, 21), maxLevel=3)
            vs = []
            for a, b, s in zip(pts.reshape(-1, 2), nxt.reshape(-1, 2), st.reshape(-1)):
                if not s or a[1] <= yh + 5 or b[1] <= yh + 5:
                    continue
                za = self.f * self.h / (a[1] - yh)
                zb = self.f * self.h / (b[1] - yh)
                if za > 40:
                    continue
                vs.append((za - zb) / self.dt)
            if len(vs) >= 6:
                est = float(np.median(vs)) * 3.6
        self.prev = g
        if est is not None:
            est = min(max(est, 0.0), 130.0)
            self.v += (est - self.v) * 0.12
        return self.v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--crop", default="1008,756,936,650", help="rong,cao,x,y vung cat tren khung goc")
    ap.add_argument("--hfov", type=float, default=40.0, help="goc ngang sau khi cat (do)")
    ap.add_argument("--cam-h", type=float, default=1.4, help="do cao camera (m), chi dung uoc luong toc do")
    ap.add_argument("--speed", type=float, default=None, help="ep toc do co dinh (km/h)")
    ap.add_argument("--model", default="yolo11n.pt")
    ap.add_argument("--det-hz", type=int, default=10, help="so lan nhan dien / giay (board: ~8-12)")
    ap.add_argument("--start", type=float, default=0)
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--day", action="store_true", help="HUD che do Ngay")
    a = ap.parse_args()

    from ultralytics import YOLO
    model = YOLO(a.model)
    crop = [int(v) for v in a.crop.split(",")]
    fps = 30.0
    step = max(1, int(round(fps / a.det_hz)))
    f640 = (VW / 2) / np.tan(np.radians(a.hfov / 2))
    ego = EgoSpeed(f640, a.cam_h, fps)

    tmp = tempfile.mkdtemp()
    rp = subprocess.Popen([os.path.join(HERE, "replay"), tmp] + (["day"] if a.day else []), stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True, bufsize=1)
    hud_f = None
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    enc = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "1280x540",
                            "-r", str(fps / step), "-i", "-", "-c:v", "libx264", "-preset", "veryfast", "-crf", "26",
                            "-pix_fmt", "yuv420p", a.out], stdin=subprocess.PIPE)
    f_big, f_mid, f_sm = font(22), font(18), font(15, False)
    yh = VH * 0.45
    stats = {"frames": 0, "lv": [0, 0, 0, 0], "beeps": 0, "lead": 0}
    t_beg, t_end = a.start * fps, (a.start + a.seconds) * fps if a.seconds else 1e12
    hud_bytes = 480 * 320 * 3
    boxes640 = []
    csv = open(os.path.splitext(a.out)[0] + ".csv", "w")
    csv.write("giay,km_h,muc,vat,met,tit\n")
    for i, img in frames(a.video, crop, step):
        if i < t_beg:
            continue
        if i > t_end:
            break
        v = a.speed if a.speed is not None else ego.update(img, yh, boxes640)
        if (i - int(t_beg)) % step:
            continue
        small = cv2.resize(img, (W, H), interpolation=cv2.INTER_AREA)
        r = model.predict(small, imgsz=320, conf=0.30, verbose=False)[0]
        boxes, boxes640 = [], []
        for (x1, y1, x2, y2), c, s in zip(r.boxes.xyxy.tolist(), r.boxes.cls.tolist(), r.boxes.conf.tolist()):
            if int(c) not in COCO:
                continue
            boxes.append((COCO[int(c)], int(x1), int(y1), int(x2), int(y2), int(s * 100)))
        boxes = sorted(boxes, key=lambda b: -b[5])[:16]
        boxes640 = [(b[1] * 2, b[2] * 2, b[3] * 2, b[4] * 2) for b in boxes]
        ms = int(i * 1000 / fps)
        kmh = int(round(v))
        rp.stdin.write(f"F {ms} {kmh} {len(boxes)}\n" + "".join("%d %d %d %d %d %d\n" % b for b in boxes))
        rp.stdin.flush()
        line = rp.stdout.readline().split()
        hor, lead, dm, lv, beeps = float(line[1]), int(line[2]), int(line[3]), int(line[4]), int(line[5])
        yh = hor * 2
        if hud_f is None:
            hud_f = open(os.path.join(tmp, "hud.rgb"), "rb")
        hb = b""
        for _ in range(1000):
            if len(hb) >= hud_bytes:
                break
            c = hud_f.read(hud_bytes - len(hb))
            if not c:
                import time
                time.sleep(0.002)
            hb += c
        if len(hb) < hud_bytes:
            sys.exit("replay khong ghi anh HUD")
        hud = Image.frombytes("RGB", (480, 320), hb)

        # trai: camera + chan troi + vung lan (cung cong thuc adas_feed_lane_half) + khung
        cam = Image.fromarray(cv2.cvtColor(img, cv2.COLOR_BGR2RGB))
        ov = Image.new("RGBA", cam.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(ov)
        half = lambda y: 0 if y <= hor else (y - hor) * 1.6 / 1.30 + W * 0.02      # anh 320x240
        poly = [(W / 2 - half(hor + 1), hor + 1), (W / 2 + half(hor + 1), hor + 1), (W / 2 + half(H), H), (W / 2 - half(H), H)]
        d.polygon([(x * 2, y * 2) for x, y in poly], fill=(34, 197, 94, 40), outline=(34, 197, 94, 220))
        d.line([(0, yh), (VW, yh)], fill=(255, 160, 32, 230), width=2)
        for k, b in enumerate(boxes):
            col = (255, 224, 0, 255) if k == lead else (200, 200, 210, 170)
            d.rectangle([b[1] * 2, b[2] * 2, b[3] * 2, b[4] * 2], outline=col, width=3 if k == lead else 1)
            if k == lead:
                d.text((b[1] * 2 + 2, b[2] * 2 - 24), f"{dm} m", font=f_big, fill=col)
        cam = Image.alpha_composite(cam.convert("RGBA"), ov).convert("RGB")

        out = Image.new("RGB", (1280, 540), (18, 20, 30))
        out.paste(cam, (0, 0))
        out.paste(hud.resize((640, 427), Image.LANCZOS), (640, 0))
        dd = ImageDraw.Draw(out)
        tsec = i / fps
        dd.text((654, 438), f"{os.path.basename(a.video)}  {int(tsec // 60):02d}:{tsec % 60:04.1f}", font=f_mid, fill=(230, 232, 240))
        dd.text((654, 466), f"Tốc độ {'(cố định)' if a.speed is not None else 'ước lượng'}: {kmh} km/h", font=f_mid, fill=(230, 232, 240))
        dd.text((654, 494), f"Mức: {LEVEL[lv]}", font=f_big, fill=LCOL[lv])
        dd.text((900, 494), f"Tiếng tít: {beeps}", font=f_mid, fill=(230, 232, 240))
        dd.text((10, 488), f"Vật thấy: {len(boxes)}   Chân trời: {hor * 100 / H:.0f}%   "
                f"{('Phía trước: ' + NAME[boxes[lead][0]] + ' ' + str(dm) + ' m') if lead >= 0 else 'Không có vật trong làn'}",
                font=f_mid, fill=(230, 232, 240))
        dd.text((10, 514), "Cam vàng: vật đang xét · cam: chân trời tự chỉnh · xanh: vùng làn", font=f_sm, fill=(160, 165, 185))
        enc.stdin.write(out.tobytes())
        csv.write(f"{tsec:.1f},{kmh},{lv},{NAME[boxes[lead][0]] if lead >= 0 else ''},{dm},{beeps}\n")
        stats["frames"] += 1
        stats["lv"][lv] += 1
        stats["beeps"] = beeps
        stats["lead"] += lead >= 0
        if stats["frames"] % 100 == 0:
            print(f"  {tsec:6.1f} s  v={kmh:3d}  muc={lv}  tit={beeps}  chan troi={hor * 100 / H:.0f}%", flush=True)
    rp.stdin.close()
    rp.wait()
    enc.stdin.close()
    enc.wait()
    n = max(1, stats["frames"])
    secs = n / (fps / step)
    print(f"KET QUA {os.path.basename(a.video)}: {secs:.0f} s, co vat trong lan {stats['lead'] * 100 // n}% thoi gian; "
          + ", ".join(f"{LEVEL[k]} {stats['lv'][k] * 100 / n:.1f}%" for k in range(4))
          + f"; tit {stats['beeps']} tieng ({stats['beeps'] * 60 / secs:.1f}/phut)")


if __name__ == "__main__":
    main()
