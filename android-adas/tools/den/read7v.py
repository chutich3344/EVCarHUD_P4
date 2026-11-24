import numpy as np, cv2
DIG={0b0111111:0,0b0000110:1,0b1011011:2,0b1001111:3,0b1100110:4,0b1101101:5,0b1111101:6,0b0000111:7,0b0100111:7,0b1111111:8,0b1101111:9,0b1100111:9}
def read7v(g):
    """g: anh muc sang (float) cua 1 o chu so (da cat theo vo). Doc 7 doan bang so sanh do sang tuong doi (chong quang LED)."""
    H,W=g.shape
    if H<7 or W<3: return None,0
    t=g.min()+0.5*(g.max()-g.min())
    ink=g>t
    cols=np.where(ink.any(0))[0]; rows=np.where(ink.any(1))[0]
    if len(cols)==0: return None,0
    x0,x1=cols[0],cols[-1]+1; y0,y1=rows[0],rows[-1]+1
    q=g[y0:y1,x0:x1]; h,w=q.shape
    if h<0.5*H: return None,0
    if w<0.38*h: return 1,1.0
    def band(cx,cy,rx,ry):
        a=q[int(max(0,(cy-ry)*h)):int(min(h,(cy+ry)*h))+1,int(max(0,(cx-rx)*w)):int(min(w,(cx+rx)*w))+1]
        return float(a.mean()) if a.size else 0.
    m=[band(.5,.08,.25,.08),band(.85,.28,.13,.13),band(.85,.72,.13,.13),band(.5,.92,.25,.08),band(.15,.72,.13,.13),band(.15,.28,.13,.13),band(.5,.5,.25,.08)]
    lo,hi=min(m),max(m)
    if hi-lo<15: return None,0
    th=(lo+hi)/2; bits=sum(1<<i for i,v in enumerate(m) if v>th)
    if bits in DIG: return DIG[bits],(hi-lo)
    # gan dung: sai 1 doan, chon chu so gan nhat duy nhat
    best=[(bin(bits^b).count('1'),d) for b,d in DIG.items()]
    best.sort(); 
    if best[0][0]==1 and best[1][0]>1: return best[0][1],(hi-lo)*0.5
    return None,0
def read_box(img,box,col='R'):
    """box = vo bang dem. Chia doi giua (2 o co dinh), doc tung o tren kenh mau den."""
    x1,y1,x2,y2=box; reg=img[y1:y2,x1:x2].astype(np.float32)
    k=max(1,int(round(64/max(8,y2-y1)))); reg=cv2.resize(reg,None,fx=k,fy=k,interpolation=cv2.INTER_CUBIC)
    b,g,r=reg[...,0],reg[...,1],reg[...,2]
    ch = r-0.5*(g+b) if col=='R' else (g-0.5*r if col=='G' else (r+g)/2-b)
    W=ch.shape[1]; out=[]
    for half in (ch[:,:W//2],ch[:,W//2:]):
        v,c=read7v(half); out.append(v)
    if out[1] is None: return None
    return int(''.join(str(x) for x in out if x is not None)) if out[0] is not None else out[1]
