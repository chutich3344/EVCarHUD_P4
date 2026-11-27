"""Doc so tren bien toc do toi da: tach tung chu so (den, it mau) roi so khop mau 0-9; rang buoc so hop le (x0 / 1x0)."""
import cv2, numpy as np
from PIL import Image, ImageDraw, ImageFont
FONTS = ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/opentype/inter/Inter-Bold.otf',
         '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf', '/usr/share/fonts/opentype/inter/Inter-SemiBold.otf']
DW, DH = 16, 24
def fit(m):
    ys, xs = np.nonzero(m)
    m = m[ys.min():ys.max()+1, xs.min():xs.max()+1].astype(np.uint8) * 255
    h, w = m.shape; s = DH / h; nw = max(1, min(DW, int(round(w * s))))
    r = cv2.resize(m, (nw, DH), interpolation=cv2.INTER_AREA)
    out = np.zeros((DH, DW), np.float32); x0 = (DW - nw) // 2; out[:, x0:x0+nw] = r / 255.0
    return out
TPL = []
for fp in FONTS:
    f = ImageFont.truetype(fp, 60)
    for d in range(10):
        im = Image.new('L', (80, 90), 0); ImageDraw.Draw(im).text((10, 5), str(d), font=f, fill=255)
        a = np.array(im) > 128
        for k in (0, 1, 2):   # day/mong khac nhau
            b = cv2.dilate(a.astype(np.uint8), np.ones((3, 3), np.uint8), iterations=k) > 0 if k else a
            t = fit(b); TPL.append((d, (t - t.mean()) / (t.std() + 1e-6)))
def digit(m):
    q = fit(m); q = (q - q.mean()) / (q.std() + 1e-6)
    best = {}
    for d, t in TPL:
        s = float((q * t).mean())
        if s > best.get(d, -9): best[d] = s
    return best
def read(crop):
    h, w = crop.shape[:2]
    if min(h, w) < 18: return None, 0
    im = cv2.resize(crop, (96, 96), interpolation=cv2.INTER_CUBIC)
    hsv = cv2.cvtColor(im, cv2.COLOR_BGR2HSV); g = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY)
    # nen trang trong vong do: vung it mau, sang, lon nhat cham vung giua; lap lo (chu so) bang bao loi
    yy, xx = np.mgrid[:96, :96]; mid = (xx - 48) ** 2 + (yy - 48) ** 2 < 18 ** 2
    wt = ((hsv[..., 1] < 90) & (hsv[..., 2] > np.percentile(hsv[..., 2], 40))).astype(np.uint8)
    n0, l0, s0, _ = cv2.connectedComponentsWithStats(wt)
    cand = [i for i in range(1, n0) if (mid & (l0 == i)).any()]
    if not cand: return None, 0
    k = max(cand, key=lambda i: s0[i, cv2.CC_STAT_AREA])
    cs, _ = cv2.findContours((l0 == k).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    disk = np.zeros((96, 96), np.uint8); cv2.drawContours(disk, [cv2.convexHull(max(cs, key=cv2.contourArea))], -1, 1, -1)
    disk = cv2.erode(disk, np.ones((3, 3), np.uint8), iterations=2) > 0
    if disk.sum() < 900: return None, 0
    v = g[disk]
    if int(v.max()) - int(v.min()) < 50: return None, 0
    t, _ = cv2.threshold(v.reshape(-1, 1), 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    dark = (disk & (g < t)).astype(np.uint8)
    ys, xs = np.nonzero(disk); cy = ys.mean()
    n, lab, st, cen = cv2.connectedComponentsWithStats(dark)
    comps = [i for i in range(1, n) if st[i, cv2.CC_STAT_AREA] >= 25 and st[i, cv2.CC_STAT_HEIGHT] >= 16 and abs(cen[i][1] - cy) < 16]
    comps.sort(key=lambda i: st[i, cv2.CC_STAT_LEFT])
    if len(comps) not in (2, 3): return None, 0
    hs = [st[i, cv2.CC_STAT_HEIGHT] for i in comps]
    if max(hs) > 1.35 * min(hs): return None, 0
    sc = [digit(lab == i) for i in comps]
    if len(comps) == 2:
        if sc[1][0] < 0.35: return None, 0
        d = max(range(2, 10), key=lambda k: sc[0][k]); s = (sc[0][d] + sc[1][0]) / 2
        return d * 10, s
    if sc[0][1] < 0.3 or sc[2][0] < 0.35: return None, 0
    d = max(range(0, 3), key=lambda k: sc[1][k]); s = (sc[0][1] + sc[1][d] + sc[2][0]) / 3
    return 100 + d * 10, s
