import sys,cv2,numpy as np
sys.path.insert(0,__file__.rsplit('/',1)[0]); from det import yolox
RX0,RX1,RY0,RY1=0.22,0.78,0.0,0.5
def color_of(hsv,vmin=150,smin=90):
    h,s,v=hsv[...,0].astype(int),hsv[...,1].astype(int),hsv[...,2].astype(int)
    lit=(v>vmin)&(s>smin)
    r=lit&((h<12)|(h>165)); y=lit&(h>=12)&(h<35); g=lit&(h>=45)&(h<97)
    c={'R':int(r.sum()),'Y':int(y.sum()),'G':int(g.sum())}
    k=max(c,key=c.get); return (k if c[k]>=3 else None), {'R':r,'Y':y,'G':g}
SEG=[(0.5,0.08),(0.85,0.28),(0.85,0.72),(0.5,0.92),(0.15,0.72),(0.15,0.28),(0.5,0.5)]  # a b c d e f g
DIG={0b0111111:0,0b0000110:1,0b1011011:2,0b1001111:3,0b1100110:4,0b1101101:5,0b1111101:6,0b0000111:7,0b0100111:7,0b1111111:8,0b1101111:9,0b1100111:9}
def read7(m):
    """m: bool mask cua 1 chu so (da cat sat). Doc 7 doan bang dai quet: ngang a,g,d; doc f,b,e,c."""
    H,W=m.shape
    if H<7: return None
    if W<0.38*H: return 1
    def hband(fy):
        r0,r1=int(max(0,(fy-0.13)*H)),int(min(H,(fy+0.13)*H+1)); c0,c1=int(0.25*W),int(0.75*W)+1
        p=m[r0:r1,c0:c1]; return p.size>0 and p.any(0).mean()>0.5
    def vband(fx,top):
        c0,c1=int(max(0,(fx-0.2)*W)),int(min(W,(fx+0.2)*W+1)); r0,r1=(int(0.15*H),int(0.42*H)+1) if top else (int(0.58*H),int(0.85*H)+1)
        p=m[r0:r1,c0:c1]; return p.size>0 and p.any(1).mean()>0.5
    seg=[hband(0.08),vband(0.88,1),vband(0.88,0),hband(0.92),vband(0.12,0),vband(0.12,1),hband(0.5)]
    bits=sum(1<<i for i,v in enumerate(seg) if v)
    return DIG.get(bits)
def countdown(img,box,col,sides=('R','L','B')):
    x1,y1,x2,y2=box; bw,bh=x2-x1,y2-y1; s=min(bw,bh)
    for side in sides:
        if side=='R': rx0,rx1,ry0,ry1=x2,x2+3*s,y1-0.5*s,y2+0.5*s
        elif side=='L': rx0,rx1,ry0,ry1=x1-3*s,x1,y1-0.5*s,y2+0.5*s
        elif side=='B': rx0,rx1,ry0,ry1=x1-0.5*s,x2+0.5*s,y2,y2+2.5*s
        else: rx0,rx1,ry0,ry1=x1-0.15*bw,x2+0.15*bw,y1-0.15*bh,y2+0.15*bh; s=bh
        rx0,ry0=int(max(0,rx0)),int(max(0,ry0)); rx1,ry1=int(min(img.shape[1],rx1)),int(min(img.shape[0],ry1))
        if rx1-rx0<4 or ry1-ry0<4: continue
        reg0=img[ry0:ry1,rx0:rx1]
        for k in (max(2,int(round(72/max(8,s)))),max(1,int(round(48/max(8,s))))):
            r=_cd(reg0,k,col,s)
            if r: v,(X0,Y0,X1,Y1)=r; return (v,side,(rx0+X0/k,ry0+Y0/k,rx0+X1/k,ry0+Y1/k))
    return None
from read7v import read7v
import tmpl2

def _cd(reg0,k,col,s):
    reg=cv2.resize(reg0,None,fx=k,fy=k,interpolation=cv2.INTER_CUBIC)
    rf=reg.astype(np.float32); B,G_,R=rf[...,0],rf[...,1],rf[...,2]
    chan = R-0.5*(G_+B) if col=='R' else (G_-0.5*R if col=='G' else (R+G_)/2-B)
    hsv=cv2.cvtColor(reg,cv2.COLOR_BGR2HSV)
    _,ms=color_of(hsv,95,60); m=ms[col]
    if m.sum()>=8:   # quang LED (nhat la so do) -> chi giu net sang that: >= 60% do sang cua net sang nhat
        vv=hsv[...,2]; vref=np.percentile(vv[m],95); m=m&(vv>=0.6*vref)
    m=cv2.morphologyEx(m.astype(np.uint8),cv2.MORPH_CLOSE,np.ones((3,3),np.uint8))
    n,lab,st,_=cv2.connectedComponentsWithStats(m)
    comps=[st[i] for i in range(1,n) if st[i,4]>=6]
    if not comps: return None
    anc=max(comps,key=lambda c:c[3]); hmax=anc[3]
    if hmax<0.35*s*k or hmax>2.2*s*k: return None
    ay0,ay1=anc[1],anc[1]+anc[3]
    tall=[c for c in comps if min(ay1,c[1]+c[3])-max(ay0,c[1])>0.3*min(hmax,c[3]) and abs((c[0]+c[2]/2)-(anc[0]+anc[2]/2))<1.3*hmax and c[3]<=1.15*hmax]
    X0=min(c[0] for c in tall); Y0=min(c[1] for c in tall); X1=max(c[0]+c[2] for c in tall); Y1=max(c[1]+c[3] for c in tall)
    mm=m[Y0:Y1,X0:X1]>0; H=Y1-Y0; W=X1-X0
    if W<2 or H<7: return None
    dark=((hsv[...,2]<90)|(m>0)).astype(np.uint8)
    kk=max(3,int(0.35*H)); dark=cv2.morphologyEx(dark,cv2.MORPH_OPEN,np.ones((kk,kk),np.uint8))
    n2,lab2,st2,_=cv2.connectedComponentsWithStats(dark)
    L=lab2[(Y0+Y1)//2,(X0+X1)//2]; hx=None
    if L>0:
        bx,by,bw2,bh2,_=st2[L]
        if 0.9*bh2<=bw2<=2.4*bh2 and 0.9*H<=bh2<=2.0*H and bx<=X0+2 and bx+bw2>=X1-2: hx=(bx+bw2/2)-X0
    if hx is None:
        # khong tach duoc vo hop: van chap nhan neu nen quanh chu so toi (vo hop den nho)
        g=max(1,H//6); ring=hsv[max(0,Y0-g):Y1+g,max(0,X0-g):X1+g,2].astype(float); inner=np.zeros(ring.shape,bool)
        inner[min(g,Y0):min(g,Y0)+H,min(g,X0):min(g,X0)+W]=True
        if ring[~inner & ~(cv2.dilate(m,np.ones((3,3),np.uint8))[max(0,Y0-g):Y1+g,max(0,X0-g):X1+g]>0)].mean()>110: return None
    # 24/11 ("o den 70 bat co 7"): so khop ca so 0..99 tren kenh mau (7 va 0 dinh nhau vi nhoe van doc dung)
    px=max(1,int(0.15*H)); py=max(1,int(0.04*H)); sub=np.clip(chan[max(0,Y0-py):Y1+py,max(0,X0-px):X1+px],0,None)
    tv,tm,ts=tmpl2.read2(sub)
    if tv is not None and ts>=0.55 and tm>=0.01: return tv,(X0,Y0,X1,Y1)
    if hx is not None and 0.2*W<hx<0.95*W: sp=int(round(hx)); parts=[mm[:,:sp],mm[:,sp:]]
    elif W>0.95*H:
        cs=mm.sum(0).astype(float); lo,hi=int(0.3*W),int(0.7*W)+1
        sp=lo+int(np.argmin(cs[lo:hi])); parts=[mm[:,:sp],mm[:,sp:]]
    else: parts=[mm]
    parts=[p for p in parts if p.any()]
    if not parts: return None
    # doc theo do sang tuong doi tren kenh mau (chong quang LED); khong duoc thi doc mat na 7 doan
    if len(parts)==2 or hx is not None:
        cuts=[(X0,X0+sp),(X0+sp,X1)] if (hx is not None and 0.2*W<hx<0.95*W) or W>0.95*H else [(X0,X1)]
        vs=[read7v(chan[Y0:Y1,a:b])[0] for a,b in cuts]
        if vs and vs[-1] is not None and all(v is not None for v in vs):
            return int(''.join(map(str,vs))),(X0,Y0,X1,Y1)
    vals=[]
    for p in parts:
        cols=np.where(p.any(0))[0]; q=p[:,cols[0]:cols[-1]+1]
        if q.shape[1]>0.38*q.shape[0] and q.mean()>0.72: return None   # khoi dac (quang sang) khong phai 7 doan
        vals.append(read7(q))
    if None in vals: return None
    return int(''.join(map(str,vals))),(X0,Y0,X1,Y1)
def inside(b,o,th=0.6):
    ix=max(0,min(b[2],o[2])-max(b[0],o[0])); iy=max(0,min(b[3],o[3])-max(b[1],o[1]))
    return ix*iy>th*max(1,(b[2]-b[0])*(b[3]-b[1]))
def analyse(im):
    H,W=im.shape[:2]
    x0,x1,y0,y1=int(RX0*W),int(RX1*W),int(RY0*H),int(RY1*H)
    iw=(x1-x0)//32*32; ih=(y1-y0)//32*32
    crop=im[y0:y1,x0:x1]; k=yolox(crop,iw,ih); sx=(x1-x0)/iw; sy=(y1-y0)/ih
    lamps=[]; cds=[]
    for r in k:
        if r[4]<0.25: continue
        b=[x0+r[0]*sx,y0+r[1]*sy,x0+r[2]*sx,y0+r[3]*sy]
        bi=[int(max(0,b[0])),int(max(0,b[1])),int(min(W,b[2])),int(min(H,b[3]))]
        if bi[2]-bi[0]<4 or bi[3]-bi[1]<4: continue
        if any(inside(bi,l['box']) for l in lamps+cds): continue
        hb=cv2.cvtColor(im[bi[1]:bi[3],bi[0]:bi[2]],cv2.COLOR_BGR2HSV)
        col,ms0=color_of(hb)
        if col:
            # chong den duong / den pha: den giao thong = cham sang nho trong vo hop toi
            litf=ms0[col].mean(); bright=(hb[...,2]>150).mean(); darkf=(hb[...,2]<90).mean()
            if bright>0.45 or darkf<0.25 or litf>0.4: col=None
        if not col: lamps.append(dict(box=bi,conf=float(r[4]),col=None,cd=None)); continue
        bw,bh=bi[2]-bi[0],bi[3]-bi[1]
        own=countdown(im,bi,col,('I',)) if 0.9*bh<=bw<=1.7*bh else None
        if own and own[0]>=10 and own[0]<=99:   # hop nay chinh la bang dem giay
            cds.append(dict(box=bi,conf=float(r[4]),col=col,cd=own)); continue
        lamps.append(dict(box=bi,conf=float(r[4]),col=col,cd=countdown(im,bi,col)))
    lamps=[l for l in lamps if not any(c['cd'] and inside(l['box'],c['cd'][2],0.4) for c in lamps+cds if c is not l)]
    # trang thai: den sang lon nhat (gan nhat) + so dem
    lit=[l for l in lamps if l['col']]
    st=None
    if lit:
        m=max(lit,key=lambda l:(l['box'][3]-l['box'][1])*(l['box'][2]-l['box'][0])*l['conf'])
        sec=m['cd'][0] if m['cd'] else next((c['cd'][0] for c in cds if c['col']==m['col']),None)
        if sec is None: sec=next((l['cd'][0] for l in lit if l['cd'] and l['col']==m['col']),None)
        st=(m['col'],sec)
    return dict(lamps=lamps,cds=cds,state=st),(x0,y0,x1,y1)
