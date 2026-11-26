# Doc so dem 2 chu so bang so khop ca so 0..99 (ban de port Kotlin y het)
import numpy as np, math
CW,CH=40,28
SEG={0:'abcdef',1:'bc',2:'abged',3:'abgcd',4:'fgbc',5:'afgcd',6:'afgedc',7:'abc',8:'abcdefg',9:'abcdfg'}
def segs(d,x):  # don vi: cao 1, rong a
    return {'a':(x,0,x+1,0),'b':(x+1,0,x+1,.5),'c':(x+1,.5,x+1,1),'d':(x,1,x+1,1),'e':(x,.5,x,1),'f':(x,0,x,.5),'g':(x,.5,x+1,.5)}
def render(n,asp,gap,thk,sl,S=48):
    lines=[]
    cells=[(n//10,0.0)] if n>=10 else []
    cells.append((n%10,1+gap))
    for d,xo in cells:
        p=segs(d,xo)
        for s in SEG[d]:
            x0,y0,x1,y1=p[s]; lines.append((x0*asp,y0,x1*asp,y1))
    m=thk*2.5; W=(2+gap)*asp+2*m+sl; H=1+2*m
    w=int(round(W*S)); h=int(round(H*S))
    ys=(np.arange(h)+.5)/S-m; xs=(np.arange(w)+.5)/S-m
    X,Y=np.meshgrid(xs,ys); X=X-sl*(1-Y)   # nghieng: tren lech phai
    d=np.full(X.shape,9.0)
    for x0,y0,x1,y1 in lines:
        dx,dy=x1-x0,y1-y0; L=dx*dx+dy*dy
        t=np.clip(((X-x0)*dx+(Y-y0)*dy)/L,0,1)
        d=np.minimum(d,np.hypot(X-x0-t*dx,Y-y0-t*dy))
    sg=thk*0.6
    return np.exp(-np.maximum(0,d-thk/2)**2/(2*sg*sg)).astype(np.float32)
def sample(a,fx,fy):
    h,w=a.shape; fx=min(max(fx,0),w-1.001); fy=min(max(fy,0),h-1.001)
    x0=int(fx); y0=int(fy); ax=fx-x0; ay=fy-y0
    return (a[y0,x0]*(1-ax)+a[y0,x0+1]*ax)*(1-ay)+(a[y0+1,x0]*(1-ax)+a[y0+1,x0+1]*ax)*ay
def crop_norm(a):
    """-> (vector chuan hoa CWxCH, ti le rong/cao vung net) hoac None"""
    mn,mx=float(a.min()),float(a.max())
    if mx-mn<1e-3: return None
    t=mn+0.5*(mx-mn); ink=a>t
    r=np.where(ink.any(1))[0]; c=np.where(ink.any(0))[0]
    if len(r)<3 or len(c)<2: return None
    y0,y1,x0,x1=r[0],r[-1]+1,c[0],c[-1]+1
    bw,bh=x1-x0,y1-y0
    q=np.zeros((CH,CW),np.float32)
    for j in range(CH):
        for i in range(CW):
            s=0.0
            for u in (1/6,.5,5/6):
                for v in (1/6,.5,5/6):
                    s+=sample(a,x0+(i+u)*bw/CW-.5,y0+(j+v)*bh/CH-.5)
            q[j,i]=s/9
    q-=q.mean(); s=float(np.sqrt((q*q).mean()))
    if s<1e-6: return None
    return (q/s).ravel(), bw/bh
_T=None
def templates():
    global _T
    if _T is not None: return _T
    vals=[];asps=[];mats=[]
    for n in range(100):
        for asp in (0.42,0.55):
            for gap in (0.18,0.4):
                for thk in (0.10,0.17):
                    for sl in (0.0,0.12,0.24):
                        r=crop_norm(render(n,asp,gap,thk,sl))
                        if r: vals.append(n); mats.append(r[0]); asps.append(r[1])
    _T=(np.array(vals),np.array(asps),np.stack(mats)); return _T
def read(a):
    r=crop_norm(a)
    if r is None: return None,0,0
    v,ar=r; vals,asps,M=templates(); sc=M@v/len(v)
    ok=np.abs(np.log(ar/asps))<0.35
    best={}
    for n,s,o in zip(vals,sc,ok):
        if o and s>best.get(n,-9): best[n]=s
    if len(best)<2: return None,0,0
    o=sorted(best.items(),key=lambda x:-x[1])
    return int(o[0][0]), float(o[0][1]-o[1][1]), float(o[0][1])
import os
_c=os.path.join(os.path.dirname(__file__),'t2cache.npz')
if os.path.exists(_c):
    _z=np.load(_c); _T=(_z['v'],_z['a'],_z['m'])
def has_gap(a):
    """Co khe (theo do nghieng 0/.12/.25) tach so 1 dau trong 8..50% rong vung net."""
    mn,mx=float(a.min()),float(a.max()); t=mn+0.5*(mx-mn); ink=a>t
    r=np.where(ink.any(1))[0]; c=np.where(ink.any(0))[0]
    q=ink[r[0]:r[-1]+1,c[0]:c[-1]+1]; h,w=q.shape
    for sl in (0.0,0.12,0.25):
        prof=np.zeros(w+int(sl*h)+2,np.int32)
        for j in range(h):
            o=int(round(sl*(h-1-j)))   # dua dong tren ve trai
            prof[0:w]+=0
            for i in np.where(q[j])[0]: prof[i-o+int(sl*h)]+=1
        nz=np.where(prof>0)[0]; p=prof[nz[0]:nz[-1]+1]; W=len(p)
        lo=max(1,int(0.08*W)); hi=max(lo+1,int(0.5*W))
        if p[lo:hi].min()<=0.28*p.max(): return True
    return False
def read2(a):
    r=crop_norm(a)
    if r is None: return None,0,0
    v,ar=r; vals,asps,M=templates(); sc=M@v/len(v)
    ok=np.abs(np.log(ar/asps))<0.35
    if not has_gap(a): ok&=~((vals>=10)&(vals<20))
    best={}
    for n,s,o in zip(vals,sc,ok):
        if o and s>best.get(n,-9): best[n]=s
    if len(best)<2: return None,0,0
    o=sorted(best.items(),key=lambda x:-x[1])
    return int(o[0][0]), float(o[0][1]-o[1][1]), float(o[0][1])
