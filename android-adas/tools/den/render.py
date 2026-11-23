import cv2,sys,numpy as np,subprocess
from PIL import Image,ImageDraw,ImageFont
sys.path.insert(0,__file__.rsplit('/',1)[0]); from tl import analyse
F='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-Bold.ttf'
f20=ImageFont.truetype(F,20); f16=ImageFont.truetype(F,16); f54=ImageFont.truetype(F,54); f26=ImageFont.truetype(F,26)
COL={'R':(255,59,48),'Y':(255,204,0),'G':(48,209,88)}; NAME={'R':'ĐÈN ĐỎ','Y':'ĐÈN VÀNG','G':'ĐÈN XANH'}
class Track:
    """Giu trang thai den: mau can 2 lan khop moi doi; so giay dem lui theo dong ho, so doc moi chi nhan khi khop du doan +-2 hoac 2 lan lien."""
    def __init__(s): s.col=None; s.colN=0; s.cand=None; s.sec=None; s.secAt=0; s.pend=None; s.seen=-99
    def upd(s,t,st):
        if st is None:
            if t-s.seen>3: s.col=None; s.sec=None
            return
        c,v=st; s.seen=t
        if c!=s.col:
            if s.cand==c: s.colN+=1
            else: s.cand=c; s.colN=1
            if s.colN>=2 or s.col is None: s.col=c; s.sec=None; s.pend=None
            else: return
        if v is not None:
            pred=s.cur(t)
            if pred is None or abs(pred-v)<=2: s.sec=v; s.secAt=t; s.pend=None
            elif s.pend and abs((s.pend[1]-(t-s.pend[0]))-v)<=1: s.sec=v; s.secAt=t; s.pend=None
            else: s.pend=(t,v)
    def cur(s,t):
        if s.sec is None: return None
        v=s.sec-int(t-s.secAt); return v if v>=0 else None
def chip(d,W,tr,t,top=14):
    if not tr.col: return
    c=COL[tr.col]; sec=tr.cur(t)
    w=260; x0=(W-w)//2; d.rounded_rectangle([x0,top,x0+w,top+86],18,fill=(10,12,16,215))
    # 3 bong den
    for i,k in enumerate('RYG'):
        cx=x0+30; cy=top+18+i*25; on=(k==tr.col)
        d.ellipse([cx-10,cy-10,cx+10,cy+10],fill=COL[k] if on else (55,58,64))
    d.text((x0+56,top+12),NAME[tr.col],font=f20,fill=c)
    d.text((x0+56,top+44),'còn' if sec is not None else 'không thấy số',font=f16,fill=(200,200,205))
    if sec is not None:
        txt=f'{sec}'; tw=d.textlength(txt,font=f54); d.text((x0+w-18-tw,top+8),txt,font=f54,fill=c)
def run(src,a,b,out,title,recolor=False,fps=10,every=0.5):
    cap=cv2.VideoCapture(src); tr=Track(); frames=[]; last=None; lastT=-9
    p=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s','1280x720','-r',str(fps),'-i','-','-c:v','libx264','-pix_fmt','yuv420p','-crf','23',out],stdin=subprocess.PIPE)
    t=a
    while t<b:
        cap.set(0,t*1000); ok,im=cap.read()
        if not ok: break
        im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA)
        if recolor:
            h=cv2.cvtColor(im,cv2.COLOR_BGR2HSV); m=(h[...,0]>=45)&(h[...,0]<97)&(h[...,1]>60); h[...,0][m]=177; im=cv2.cvtColor(h,cv2.COLOR_HSV2BGR)
        if t-lastT>=every-1e-6:
            last,reg=analyse(im); lastT=t; tr.upd(t,last['state'])
        pil=Image.fromarray(cv2.cvtColor(im,cv2.COLOR_BGR2RGB)).convert('RGBA'); ov=Image.new('RGBA',pil.size,(0,0,0,0)); d=ImageDraw.Draw(ov)
        x0,y0,x1,y1=reg; d.rectangle([x0,y0,x1,y1],outline=(255,255,255,70),width=2); d.text((x0+8,y1-24),'vùng quét đèn',font=f16,fill=(255,255,255,140))
        if last:
            for l in last['lamps']:
                if not l['col']: continue
                c=COL[l['col']]; b_=l['box']; d.rectangle(b_,outline=c+(255,),width=3)
                if l['cd']: cb=[int(v) for v in l['cd'][2]]; d.rectangle([cb[0]-3,cb[1]-3,cb[2]+3,cb[3]+3],outline=(255,255,255,255),width=2); d.text((cb[0],cb[3]+6),f"{l['cd'][0]}",font=f20,fill=(255,255,255,255))
        chip(d,1280,tr,t)
        d.rounded_rectangle([12,650,1268,708],10,fill=(0,0,0,170))
        d.text((24,656),title,font=f20,fill=(255,255,255))
        st=last['state'] if last else None
        d.text((24,682),f"t={t:5.1f}s · lần quét gần nhất: {('màu '+st[0]+(' · số '+str(st[1]) if st[1] is not None else ' · chưa đọc được số')) if st else 'không thấy đèn'} · quét 2 lần/giây",font=f16,fill=(210,210,215))
        img=Image.alpha_composite(pil,ov).convert('RGB'); p.stdin.write(np.asarray(img).tobytes())
        t+=1/fps
    p.stdin.close(); p.wait()
if __name__=='__main__':
    run(sys.argv[1],float(sys.argv[2]),float(sys.argv[3]),sys.argv[4],sys.argv[5],len(sys.argv)>6)
