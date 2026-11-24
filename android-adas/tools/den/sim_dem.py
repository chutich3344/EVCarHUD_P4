import sys; sys.path.insert(0,'.')
import cv2,numpy as np,subprocess,tl
from PIL import Image,ImageDraw,ImageFont
from render import Track as OldTrack, COL, NAME
F='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-Bold.ttf'
f20=ImageFont.truetype(F,20); f16=ImageFont.truetype(F,16); f44=ImageFont.truetype(F,44); f26=ImageFont.truetype(F,26)
NEW=dict(RY1=0.62,ZOOM=(.30,.70,.25,.62,640,384),VMAX=242,NIGHT_V=80,MIN_DH=9.5); OLD=dict(NEW,MIN_DH=0)
def iou(a,b):
    ix=max(0,min(a[2],b[2])-max(a[0],b[0])); iy=max(0,min(a[3],b[3])-max(a[1],b[1])); i=ix*iy
    u=(a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-i; return i/u if u>0 else 0
class NewTrack:
    def __init__(s,strict=True): s.strict=strict; s.col=None; s.cand=None; s.n=0; s.sec=None; s.secAt=0; s.pend=None; s.seen=-99; s.box=None
    def pok(s,v,t):
        if not s.pend: return False
        el=t-s.pend[0]; d=s.pend[1]-v
        if not s.strict: return abs(s.pend[1]-int(el)-v)<=1
        import math
        return el<=2.5 and max(0,math.floor(el-.25))<=d<=math.ceil(el+.25) and not(el>=.9 and d<1)
    def cur(s,t):
        if s.sec is None: return None
        v=s.sec-int(t-s.secAt); return v if v>=0 else None
    def upd(s,t,o):
        st=o['state']
        if st is None:
            if t-s.seen>3: s.col=None; s.sec=None; s.box=None
            return
        c,v=st; L=[l for l in o['lamps'] if l['col']==c]; wl=max(L,key=lambda l:(l['box'][2]-l['box'][0])*(l['box'][3]-l['box'][1])*l['conf']) if L else None
        if c!=s.col:
            s.n=s.n+1 if s.cand==c else 1; s.cand=c
            same=wl and wl['conf']>=0.35 and s.box and iou(s.box,wl['box'])>=0.3 and t-s.seen<2.5
            first=s.col is None and wl and wl['conf']>=0.5
            if s.n>=2 or first or same: s.col=c; s.sec=None; s.pend=None
            else: s.seen=t; return
        s.seen=t
        if wl: s.box=wl['box']
        if v is None: return
        pred=s.cur(t); pm=s.pok(v,t)
        if v<10 and (pred is None or pred>=12) and not pm: s.pend=(t,v)
        elif pred is None and not pm: s.pend=(t,v)
        elif pred is None or abs(pred-v)<=2 or pm: s.sec=v; s.secAt=t; s.pend=None
        else: s.pend=(t,v)
def chip(d,x0,top,tr,t,title,first):
    w=300; d.rounded_rectangle([x0,top,x0+w,top+118],18,fill=(10,12,16,225))
    d.text((x0+16,top+8),title,font=f16,fill=(190,190,198))
    if tr.col:
        c=COL[tr.col]; sec=tr.cur(t)
        for i,k in enumerate('RYG'):
            cx=x0+30; cy=top+48+i*24; d.ellipse([cx-9,cy-9,cx+9,cy+9],fill=COL[k] if k==tr.col else (55,58,64))
        d.text((x0+56,top+38),NAME[tr.col],font=f20,fill=c)
        if sec is not None: tw=d.textlength(str(sec),font=f44); d.text((x0+w-18-tw,top+34),str(sec),font=f44,fill=c)
    else: d.text((x0+56,top+50),'chưa thấy đèn',font=f20,fill=(120,120,128))
    d.text((x0+16,top+92),first,font=f16,fill=(230,230,120))
def seg(src,a,b,P,title,recolor_until=None,fps=10,p_old=1.7,p_new=0.6,hold=None):
    cap=cv2.VideoCapture(src); tr={'o':NewTrack(False),'n':NewTrack(True)}; nxt={'o':a,'n':a}; per={'o':p_old,'n':p_new}; cfg={'o':OLD,'n':NEW}
    seen={'o':None,'n':None}; chg={'o':None,'n':None}; prev={'o':None,'n':None}; t=a
    while t<b:
        cap.set(0,(hold if hold else t)*1000); ok,im=cap.read()
        if not ok: break
        im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA)
        if recolor_until and t<recolor_until:
            h=cv2.cvtColor(im,cv2.COLOR_BGR2HSV); m=(h[...,0]>=45)&(h[...,0]<97)&(h[...,1]>60); h[...,0][m]=177; im=cv2.cvtColor(h,cv2.COLOR_HSV2BGR)
        for k in 'on':
            if t>=nxt[k]-1e-6:
                for kk,vv in cfg[k].items(): setattr(tl,kk,vv)
                o,_=tl.analyse(im); nxt[k]=t+per[k]
                tr[k].upd(t,o)
            c=tr[k].col
            if c and seen[k] is None: seen[k]=t
            if recolor_until and prev[k]=='R' and c=='G' and chg[k] is None: chg[k]=t
            prev[k]=c
        pil=Image.fromarray(cv2.cvtColor(im,cv2.COLOR_BGR2RGB)).convert('RGBA'); ov=Image.new('RGBA',pil.size,(0,0,0,0)); d=ImageDraw.Draw(ov)
        def note(k):
            s=f'thấy đèn sau {seen[k]-a:.1f} s' if seen[k] is not None else ''
            if recolor_until and t>=recolor_until: s=(f'đổi xanh trễ {chg[k]-recolor_until:.1f} s' if chg[k] else 'chưa đổi xanh')
            return s
        chip(d,300,14,tr['o'],t,'Bản 0.1.138',note('o')); chip(d,680,14,tr['n'],t,'Bản mới',note('n'))
        d.rounded_rectangle([12,650,1268,708],10,fill=(0,0,0,170)); d.text((24,656),title,font=f20,fill=(255,255,255))
        d.text((24,682),f"t={t-a:4.1f} s"+(f" · đèn thật đổi ĐỎ sang XANH lúc {recolor_until-a:.1f} s (đỏ trước đó là tô màu nhân tạo)" if recolor_until else ''),font=f16,fill=(210,210,215))
        P.stdin.write(np.asarray(Image.alpha_composite(pil,ov).convert('RGB')).tobytes())
        t+=1/fps
    return seen,chg
out=sys.argv[1]
P=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1280x720','-r','10','-i','-','-c:v','libx264','-pix_fmt','yuv420p','-crf','24',out],stdin=subprocess.PIPE)
# phan 1: anh that cua anh (hinh 3)
real=cv2.imread('/root/.claude/uploads/a94732ae-171b-5d05-8797-59dc73f71e8b/dff14178-15077.jpg')
cr=cv2.resize(real[430:510,1560:1770],None,fx=4,fy=4,interpolation=cv2.INTER_CUBIC)
base=np.full((720,1280,3),(16,16,18),np.uint8); base[120:120+cr.shape[0],60:60+cr.shape[1]]=cr
im=Image.fromarray(cv2.cvtColor(base,cv2.COLOR_BGR2RGB)); d=ImageDraw.Draw(im)
d.text((60,50),'Hình 3 của anh (phóng to x4): bảng đếm đỏ bên trái đèn chỉ vài điểm ảnh',font=f26,fill=(255,255,255))
d.text((60,460),'Chữ số cao chưa tới 9,5 px trong khung camera thì không thể đọc đúng.',font=f20,fill=(230,230,230))
d.text((60,495),'Log 11:52–11:53: app đọc lung tung 6, 1, 9, 74… rồi lọt qua bộ lọc thành "còn 4".',font=f20,fill=(230,230,230))
d.text((60,530),'Bản mới: số quá nhỏ thì KHÔNG hiện số, chỉ hiện ĐÈN ĐỎ; 2 lần đọc phải đếm lùi đúng mới nhận.',font=f20,fill=(120,230,140))
fr=np.asarray(im)
for _ in range(60): P.stdin.write(fr.tobytes())
# phan 2: phat lai log 11:52-11:53
import math
seq=[("11:52:26",1),("11:52:35",6),("11:52:38",1),("11:52:42",6),("11:52:43",6),("11:52:46",6),("11:52:48",6),("11:52:50",6),("11:52:52",6),("11:52:53",6),("11:52:55",6),("11:52:57",6),("11:53:01",9),("11:53:05",6),("11:53:08",74),("11:53:13",6),("11:53:18",6),("11:53:22",1),("11:53:24",1)]
def ts(x): h,m,z=map(int,x.split(':')); return h*3600+m*60+z
T0=ts(seq[0][0]); trs={'o':NewTrack(False),'n':NewTrack(True)}
for k in trs: trs[k].col='R'; trs[k].seen=0
i=0; t=0.0; END=ts(seq[-1][0])-T0+3; last=None
while t<END:
    while i<len(seq) and ts(seq[i][0])-T0<=t+1e-6:
        last=seq[i]
        for k in trs:
            tr=trs[k]; v=seq[i][1]; tt=ts(seq[i][0])-T0; pred=tr.cur(tt); pm=tr.pok(v,tt)
            if not tr.strict:
                if v<10 and (pred is None or pred>=12) and not pm: tr.pend=(tt,v)
                elif pred is None or abs(pred-v)<=2 or pm: tr.sec=v; tr.secAt=tt; tr.pend=None
                else: tr.pend=(tt,v)
            else:
                if v<10 and (pred is None or pred>=12) and not pm: tr.pend=(tt,v)
                elif pred is None and not pm: tr.pend=(tt,v)
                elif pred is None or abs(pred-v)<=2 or pm: tr.sec=v; tr.secAt=tt; tr.pend=None
                else: tr.pend=(tt,v)
        i+=1
    im=Image.new('RGB',(1280,720),(16,16,18)); d=ImageDraw.Draw(im)
    d.text((60,50),'Phát lại log thật 24/11 11:52–11:53 (đèn đỏ, bảng đếm quá nhỏ)',font=f26,fill=(255,255,255))
    d.text((60,100),f'{last[0]} · lần quét đọc được số: {last[1]}' if last else '',font=f20,fill=(230,230,120))
    for k,x,tt_ in (('o',300,'Bản 0.1.138'),('n',680,'Bản mới')):
        chip(d,x,180,trs[k],t,tt_,'')
    P.stdin.write(np.asarray(im).tobytes()); t+=0.1
# phan 3: clip Test06 van doc dung khi so du lon
r=seg('/mnt/project-files/vat-can-video/Test06.mp4',108,115,P,'Clip 70mai Test06: bảng đếm đủ lớn vẫn đọc đúng 31, 30',p_old=0.6)
P.stdin.close(); P.wait(); print(r)
