"""Gia lap doc bien: YOLO11n bien VN (2 lan/giay nhu app) + doc so bien toc do; bang bien kieu HUD goc phai.
python simsign.py out.mp4 video:start:dur [video:start:dur ...]"""
import sys, json, subprocess, numpy as np, cv2
sys.path.insert(0, '/tmp/ts')
from safe_load import load
from num import read
from ultralytics import YOLO
from PIL import Image, ImageDraw, ImageFont
net, names = load('/tmp/ts/vn11.pt')
m = YOLO('yolo11n.yaml', task='detect'); m.model = net; m.model.names = names
VN = {0:'Hết lệnh cấm',1:'Cấm đi ngược chiều',2:'Cấm ô tô',3:'Cấm ô tô rẽ phải',4:'Cấm ô tô rẽ trái',6:'Cấm xe tải',7:'Cấm xe tải (tải trọng)',
      10:'Hạn chế trọng lượng',11:'Hạn chế chiều cao',12:'Cấm rẽ trái',13:'Cấm rẽ phải',14:'Cấm quay đầu',15:'Cấm ô tô quay đầu',16:'Cấm rẽ trái + quay đầu',
      18:'Cấm bóp còi',19:'Cấm dừng đỗ',20:'Cấm đỗ xe',21:'Cấm rẽ trái phải',23:'Đi thẳng/rẽ phải',24:'Hướng rẽ trái',25:'Hướng rẽ phải',
      26:'Đi vòng bên phải',27:'Đi vòng bên trái',28:'Vòng xuyến',29:'Đường một chiều',31:'Bắt đầu cao tốc',32:'Làn xe buýt',
      34:'Chỗ ngoặt trái',35:'Chỗ ngoặt phải',36:'Nhiều chỗ ngoặt',37:'Nhiều chỗ ngoặt',38:'Đường hẹp trái',39:'Đường hẹp phải',40:'Ngã tư',41:'Ngã ba phải',42:'Ngã ba trái',
      43:'Giao đường không ưu tiên',44:'Giao đường không ưu tiên',45:'Giao đường không ưu tiên',46:'Giao đường ưu tiên',47:'Giao nhau có đèn',48:'Giao đường sắt',
      49:'Dốc xuống nguy hiểm',50:'Người đi bộ cắt ngang',51:'Trẻ em',52:'Công trường',53:'Nguy hiểm khác',54:'Đường đôi',55:'Chướng ngại vật'}
WARN = {1,12,13,14,15,16,19,20,29,31,47,48,49,50,51,52,53,55,6,7,2,10,11}
F = '/usr/share/fonts/opentype/inter/Inter-Bold.otf'
f18, f22, f30, f60, f40 = (ImageFont.truetype(F, s) for s in (18, 22, 30, 60, 40))
W, H = 1280, 720; DW, DH = 1280, 720; sx = 1.0
out = sys.argv[1]
enc = subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r','10','-i','-','-c:v','libx264','-preset','veryfast','-crf','26','-pix_fmt','yuv420p',out], stdin=subprocess.PIPE)
stats = {'frames': 0, 'det': 0, 'reads': []}
for seg in sys.argv[2:]:
    v, st, du = seg.rsplit(':', 2)
    opj = json.load(open(v.replace('.mp4', '.json'))); opt = {round(r['t'] * 20): r for r in opj}
    dec = subprocess.Popen(['ffmpeg','-v','error','-ss',st,'-t',du,'-i',v,'-vf',f'fps=10,scale={DW}:{DH}','-pix_fmt','bgr24','-f','rawvideo','-'], stdout=subprocess.PIPE)
    last = None; i = 0; boxes = []; lim = None; limAt = -99; votes = []; recent = {}
    while True:
        b = dec.stdout.read(DW * DH * 3)
        if len(b) < DW * DH * 3: break
        big = np.frombuffer(b, np.uint8).reshape(DH, DW, 3); t = i / 10
        if i % 5 == 0:   # 2 lan/giay nhu tren dien thoai
            r = m.predict(big, imgsz=1280, conf=0.4, verbose=False)[0]; boxes = []
            for bx, c, p in zip(r.boxes.xyxy.cpu().numpy(), r.boxes.cls.cpu().numpy().astype(int), r.boxes.conf.cpu().numpy()):
                x1, y1, x2, y2 = map(int, bx); val = None
                if c == 17:
                    val, s = read(big[max(0, y1-4):y2+4, max(0, x1-4):x2+4])
                    if val is not None and s >= 0.5:
                        votes.append((t, val, s)); stats['reads'].append([seg.split('/')[-1], round(t, 1), val, round(s, 2)])
                    else: val = None
                elif c in WARN:
                    recent[c] = t
                    bw, bh = x2 - x1, y2 - y1
                    crop = big[max(0, y1-6):y2+6, max(0, x1-6):x2+6].copy()
                    # bien phu: vung ngay duoi bien chinh, nen trang it mau chiem phan lon
                    ax1, ax2 = max(0, x1 - bw // 4), min(DW, x2 + bw // 4); ay1, ay2 = y2 + 2, min(DH, y2 + int(bh * 0.9))
                    aux = None
                    if ay2 - ay1 > 8:
                        a = big[ay1:ay2, ax1:ax2]; hsv = cv2.cvtColor(a, cv2.COLOR_BGR2HSV)
                        wfrac = ((hsv[..., 1] < 60) & (hsv[..., 2] > 150)).mean()
                        if wfrac > 0.45: aux = a.copy()
                    last = (t, c, crop, aux)
                boxes.append((x1, y1, x2, y2, c, p, val)); stats['det'] += 1
            # chot bien toc do: 1 lan doc chac (>=0,6) hoac 2 lan cung so trong 3 s
            votes = [x for x in votes if t - x[0] <= 3]
            for _, val, s in votes:
                if s >= 0.6 or sum(1 for x in votes if x[1] == val) >= 2: lim, limAt = val, t
        img = cv2.resize(big, (W, H), interpolation=cv2.INTER_AREA)
        for x1, y1, x2, y2, c, p, val in boxes:
            col = (60, 220, 120) if c == 17 else (255, 255, 255)
            cv2.rectangle(img, (int(x1*sx), int(y1*sx)), (int(x2*sx), int(y2*sx)), col, 2)
        pil = Image.fromarray(cv2.cvtColor(img, cv2.COLOR_BGR2RGB)); d = ImageDraw.Draw(pil, 'RGBA')
        for x1, y1, x2, y2, c, p, val in boxes:
            lab = (f'Tốc độ tối đa {val}' if val else 'Biển tốc độ (chưa đọc được số)') if c == 17 else VN.get(c, names[c])
            tx, ty = int(x1*sx), max(0, int(y1*sx) - 24)
            w = d.textlength(lab, font=f18) + 10
            d.rounded_rectangle((tx, ty, tx + w, ty + 22), 5, fill=(0, 0, 0, 170)); d.text((tx + 5, ty + 1), lab, font=f18, fill=(255, 255, 255))
        # cot doc ben phai kieu PiP: bien toc do / bien vua qua (anh that) + bien phu / camera phat nguoi
        px, pw = W - 206, 188; py = 58
        def card(h): 
            d.rounded_rectangle((px, py, px + pw, py + h), 16, fill=(10, 14, 22, 185))
        card(150); cx, cy = px + pw // 2, py + 66
        d.ellipse((cx - 52, cy - 52, cx + 52, cy + 52), fill=(255, 255, 255)); d.ellipse((cx - 52, cy - 52, cx + 52, cy + 52), outline=(220, 30, 40), width=11)
        txt = str(lim) if lim else '--'; d.text((cx, cy + 2), txt, font=f60 if len(txt) < 3 else f40, fill=(15, 15, 15), anchor='mm')
        d.text((cx, py + 136), 'tốc độ tối đa' if lim else 'chưa thấy biển', font=f18, fill=(255, 255, 255), anchor='mm')
        py += 160
        if last is not None and t - last[0] <= 8:
            _, c, crop, aux = last
            th = 104; ch = th + 34 + (0 if aux is None else 70)
            card(ch)
            ci = Image.fromarray(cv2.cvtColor(crop, cv2.COLOR_BGR2RGB)); k_ = th / max(ci.size); ci = ci.resize((max(1, int(ci.width * k_)), max(1, int(ci.height * k_))), Image.BICUBIC); pil.paste(ci, (px + (pw - ci.width) // 2, py + 8))
            yy = py + 8 + ci.height + 4
            if aux is not None:
                ai = Image.fromarray(cv2.cvtColor(aux, cv2.COLOR_BGR2RGB)); k_ = min((pw - 24) / ai.width, 62 / ai.height); ai = ai.resize((max(1, int(ai.width * k_)), max(1, int(ai.height * k_))), Image.BICUBIC); pil.paste(ai, (px + (pw - ai.width) // 2, yy)); yy += ai.height + 4
            d.text((cx, yy + 10), VN.get(c, names[c])[:19], font=ImageFont.truetype(F, 16), fill=(255, 210, 63), anchor='mm')
            py += ch + 10
        card(64)
        d.ellipse((px + 12, py + 14, px + 48, py + 50), fill=(255, 138, 61))
        d.text((px + 30, py + 32), 'CAM', font=ImageFont.truetype(F, 12), fill=(20, 20, 20), anchor='mm')
        d.text((px + 58, py + 12), 'Camera phạt nguội', font=ImageFont.truetype(F, 14), fill=(255, 255, 255))
        d.text((px + 58, py + 32), '350 m · mẫu', font=f22, fill=(255, 138, 61))
        tt_ = float(st) + t; r_ = None
        for dk in range(0, 6):
            r_ = opt.get(round(tt_ * 20) - dk)
            if r_: break
        d.rectangle((0, 0, W, 46), fill=(0, 0, 0, 255))
        d.text((W // 2, 23), f'Mô phỏng app Làn: openpilot + YOLO biển · t={tt_:.1f}s', font=f18, fill=(200, 200, 200), anchor='mm')
        if r_:
            lead = r_['lx'] if r_.get('lp', 0) > 0.5 else None
            colL = (71, 224, 138)
            if lead is not None and r_.get('ttc') and r_['ttc'] < 2.5: colL = (255, 84, 104)
            elif lead is not None and lead < max(8, r_['v'] / 3.6): colL = (255, 210, 63)
            big_ = str(round(lead)) if lead is not None else '--'
            d.text((26, 52), big_, font=ImageFont.truetype(F, 96), fill=colL if lead is not None else (255, 255, 255), stroke_width=3, stroke_fill=(0, 0, 0))
            d.text((30, 160), 'm · xe trước' if lead is not None else 'm · chưa thấy xe trước', font=f22, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))
            off = r_.get('off'); oc = round(off * 100) if off is not None else None
            ot = 'lệch làn --' if oc is None else ('giữa làn' if abs(oc) < 5 else (f'lệch trái {oc} cm' if oc > 0 else f'lệch phải {-oc} cm'))
            d.text((30, 190), f"{round(r_['v'])} km/h · {ot}", font=f22, fill=(120, 200, 255), stroke_width=2, stroke_fill=(0, 0, 0))
            if lim and r_['v'] > lim + 3:
                d.rounded_rectangle((W // 2 - 150, H - 80, W // 2 + 150, H - 36), 20, fill=(220, 40, 50, 230))
                d.text((W // 2, H - 58), f'Vượt tốc độ {lim} km/h', font=f22, fill=(255, 255, 255), anchor='mm')
        enc.stdin.write(np.asarray(pil).tobytes()); i += 1; stats['frames'] += 1
enc.stdin.close(); enc.wait()
json.dump(stats, open(out + '.json', 'w'), ensure_ascii=False)
print(stats['frames'], stats['det'], len(stats['reads']))
