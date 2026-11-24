import sys; sys.path.insert(0,'.')
import cv2,numpy as np,subprocess,tl
from PIL import Image,ImageDraw,ImageFont
from render import Track as OldTrack, COL, NAME
F='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-Bold.ttf'
f20=ImageFont.truetype(F,20); f16=ImageFont.truetype(F,16); f44=ImageFont.truetype(F,44); f26=ImageFont.truetype(F,26)
OLD=dict(RY1=0.5,ZOOM=None,VMAX=0,NIGHT_V=-1); NEW=dict(RY1=0.62,ZOOM=(.30,.70,.25,.62,640,384),VMAX=242,NIGHT_V=80)
def iou(a,b):
    ix=max(0,min(a[2],b[2])-max(a[0],b[0])); iy=max(0,min(a[3],b[3])-max(a[1],b[1])); i=ix*iy
    u=(a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-i; return i/u if u>0 else 0
class NewTrack:
    def __init__(s): s.col=None; s.cand=None; s.n=0; s.sec=None; s.secAt=0; s.pend=None; s.seen=-99; s.box=None
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
        pred=s.cur(t); pm=s.pend and abs((s.pend[1]-int(t-s.pend[0]))-v)<=1
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
def seg(src,a,b,P,title,recolor_until=None,fps=10,p_old=1.7,p_new=0.6):
    cap=cv2.VideoCapture(src); tr={'o':OldTrack(),'n':NewTrack()}; nxt={'o':a,'n':a}; per={'o':p_old,'n':p_new}; cfg={'o':OLD,'n':NEW}
    seen={'o':None,'n':None}; chg={'o':None,'n':None}; prev={'o':None,'n':None}; t=a
    while t<b:
        cap.set(0,t*1000); ok,im=cap.read()
        if not ok: break
        im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA)
        if recolor_until and t<recolor_until:
            h=cv2.cvtColor(im,cv2.COLOR_BGR2HSV); m=(h[...,0]>=45)&(h[...,0]<97)&(h[...,1]>60); h[...,0][m]=177; im=cv2.cvtColor(h,cv2.COLOR_HSV2BGR)
        for k in 'on':
            if t>=nxt[k]-1e-6:
                for kk,vv in cfg[k].items(): setattr(tl,kk,vv)
                o,_=tl.analyse(im); nxt[k]=t+per[k]
                if k=='o': tr[k].upd(t,o['state'])
                else: tr[k].upd(t,o)
            c=tr[k].col
            if c and seen[k] is None: seen[k]=t
            if recolor_until and prev[k]=='R' and c=='G' and chg[k] is None: chg[k]=t
            prev[k]=c
        pil=Image.fromarray(cv2.cvtColor(im,cv2.COLOR_BGR2RGB)).convert('RGBA'); ov=Image.new('RGBA',pil.size,(0,0,0,0)); d=ImageDraw.Draw(ov)
        def note(k):
            s=f'thấy đèn sau {seen[k]-a:.1f} s' if seen[k] is not None else ''
            if recolor_until and t>=recolor_until: s=(f'đổi xanh trễ {chg[k]-recolor_until:.1f} s' if chg[k] else 'chưa đổi xanh')
            return s
        chip(d,300,14,tr['o'],t,'Bản cũ (0.1.135)',note('o')); chip(d,680,14,tr['n'],t,'Bản mới',note('n'))
        d.rounded_rectangle([12,650,1268,708],10,fill=(0,0,0,170)); d.text((24,656),title,font=f20,fill=(255,255,255))
        d.text((24,682),f"t={t-a:4.1f} s"+(f" · đèn thật đổi ĐỎ→XANH lúc {recolor_until-a:.1f} s (đỏ trước đó là tô màu nhân tạo)" if recolor_until else ''),font=f16,fill=(210,210,215))
        P.stdin.write(np.asarray(Image.alpha_composite(pil,ov).convert('RGB')).tobytes())
        t+=1/fps
    return seen,chg
out=sys.argv[1]
P=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1280x720','-r','10','-i','-','-c:v','libx264','-pix_fmt','yuv420p','-crf','24',out],stdin=subprocess.PIPE)
r1=seg('/mnt/project-files/vat-can-video/Test06.mp4',104,115,P,'Clip 70mai Test06: đến gần đèn xanh từ xa')
r2=seg('/mnt/project-files/vat-can-video/Test01.mp4',150,175,P,'Clip 70mai Test01: đèn đỏ ở xa')
r3=seg('/mnt/project-files/vat-can-video/Test06.mp4',111,116,P,'Đổi màu đỏ → xanh',recolor_until=113.0)
P.stdin.close(); P.wait(); print(r1,r2,r3)
