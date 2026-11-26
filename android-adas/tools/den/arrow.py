import cv2,numpy as np
def arrows(img,box):
    """Tim mui ten sang trong hop den. Tra ve list (dir 'U'/'L'/'R', mau 'R'/'Y'/'G'/None, bbox)."""
    x1,y1,x2,y2=box; bw,bh=x2-x1,y2-y1
    X0,Y0=max(0,int(x1-0.1*bw)),max(0,int(y1-0.1*bh)); X1,Y1=min(img.shape[1],int(x2+0.1*bw)),min(img.shape[0],int(y2+0.1*bh))
    reg=img[Y0:Y1,X0:X1]; k=max(1,int(round(40/max(8,min(bw,bh)))))
    reg=cv2.resize(reg,None,fx=k,fy=k,interpolation=cv2.INTER_CUBIC)
    hsv=cv2.cvtColor(reg,cv2.COLOR_BGR2HSV); h,s,v=[hsv[...,i].astype(int) for i in range(3)]
    vmax=v.max()
    if vmax<150: return []
    core=(v>=max(170,0.8*vmax)).astype(np.uint8)
    kc=max(3,int(0.12*min(bw,bh)*k)|1); core=cv2.morphologyEx(core,cv2.MORPH_CLOSE,np.ones((kc,kc),np.uint8))
    n,lab,st,_=cv2.connectedComponentsWithStats(core)
    out=[]
    for i in range(1,n):
        cx,cy,cw,ch,a=st[i]
        if a<10 or max(cw,ch)<6: continue
        m=(lab[cy:cy+ch,cx:cx+cw]==i)
        fill=a/(cw*ch)
        if fill>0.62: continue   # cham tron (bong den tron) hoac khoi dac
        t=max(1,int(0.35*ch)); l=max(1,int(0.35*cw))
        wTop=np.count_nonzero(m[:t].any(0)); wBot=np.count_nonzero(m[-t:].any(0))
        hL=np.count_nonzero(m[:,:l].any(1)); hR=np.count_nonzero(m[:,-l:].any(1))
        d=None
        if ch>=0.8*cw and wTop>=1.5*max(1,wBot): d='U'
        elif cw>=0.8*ch and hL>=1.5*max(1,hR): d='L'
        elif cw>=0.8*ch and hR>=1.5*max(1,hL): d='R'
        if not d: continue
        # mau: vien quanh loi sang (loi hay chay trang)
        mm=(lab==i).astype(np.uint8)
        ring=(cv2.dilate(mm,np.ones((2*k+3,2*k+3),np.uint8))>0)&(mm==0)
        sel=ring|(mm>0)
        far=(cv2.dilate(mm,np.ones((6*k+5,6*k+5),np.uint8))>0)&~(cv2.dilate(mm,np.ones((3*k+3,3*k+3),np.uint8))>0)
        if far.sum() and v[far].mean()>110: continue   # khong nam trong vo den toi (troi sang lot qua khe...)
        ok=sel&(s>70)&(v>80)
        hh=h[ok]
        cnt={'R':int(((hh<12)|(hh>165)).sum()),'Y':int(((hh>=12)&(hh<35)).sum()),'G':int(((hh>=45)&(hh<97)).sum())}
        c=max(cnt,key=cnt.get); c=c if cnt[c]>=max(4,0.25*ok.sum()) else None
        # Den VN tich hop: mui ten XANH tren nen bong DO dang sang -> camera chay loi mui ten thanh trang, nen do lam vien do.
        # Nhan ra: loi mui ten trang (it bao hoa) + vung dia quanh (cach 1..4 net) phan lon la do sang -> mui ten khac mau do = xanh.
        disc=(cv2.dilate(mm,np.ones((5*k+5,5*k+5),np.uint8))>0)&~(cv2.dilate(mm,np.ones((k+2,k+2),np.uint8))>0)
        redl=disc&(s>70)&(v>70)&((h<12)|(h>165))
        coreS=s[mm>0].mean()
        if c=='R' and disc.sum() and redl.sum()>0.45*disc.sum() and coreS<70: c='G*'
        out.append((d,c,(X0+cx/k,Y0+cy/k,X0+(cx+cw)/k,Y0+(cy+ch)/k),round(fill,2)))
    return out
def synth(kind='U',col=(80,255,80)):
    im=np.full((120,240,3),(200,170,140),np.uint8)
    cv2.rectangle(im,(40,40),(200,90),(25,25,25),-1)
    cv2.circle(im,(70,65),16,(30,30,255),-1); cv2.circle(im,(70,65),9,(210,210,255),-1)   # den do tron (loi chay)
    c=(120,65)
    pts={'U':[(c[0],c[1]-14),(c[0]-11,c[1]-2),(c[0],c[1]-14),(c[0]+11,c[1]-2),(c[0],c[1]-14),(c[0],c[1]+14)],
         'L':[(c[0]-14,c[1]),(c[0]-2,c[1]-11),(c[0]-14,c[1]),(c[0]-2,c[1]+11),(c[0]-14,c[1]),(c[0]+14,c[1])],
         'R':[(c[0]+14,c[1]),(c[0]+2,c[1]-11),(c[0]+14,c[1]),(c[0]+2,c[1]+11),(c[0]+14,c[1]),(c[0]-14,c[1])]}[kind]
    for a,b in zip(pts[::2],pts[1::2]): cv2.line(im,a,b,col,7)
    for a,b in zip(pts[::2],pts[1::2]): cv2.line(im,a,b,(235,255,235),3)
    return cv2.GaussianBlur(im,(3,3),0)
if __name__=='__main__':
    import sys
    for kd in 'ULR':
        im=synth(kd); print(kd,arrows(im,(40,40,200,90)))
    im=synth('U',(40,40,255)); print('U red arrow on dark',arrows(im,(40,40,200,90)))
    im=synth('U'); cv2.circle(im,(120,65),20,(40,40,230),-1); 
    for a,b in [((120,51),(109,63)),((120,51),(131,63)),((120,51),(120,79))]: cv2.line(im,a,b,(240,250,240),4)
    im=cv2.GaussianBlur(im,(3,3),0); cv2.imwrite('syn-combo.png',cv2.resize(im,None,fx=3,fy=3)); print('green arrow on red disc',arrows(im,(40,40,200,90)))
    im=cv2.imread('/root/.claude/uploads/a94732ae-171b-5d05-8797-59dc73f71e8b/e5366d22-15057.jpg')
    # hop den trong anh (toa do anh hien thi 2000 -> goc x1.188)
    s=1.188; print('anh that', arrows(im,(int(862*s),int(40*s),int(978*s),int(92*s)))); cv2.imwrite('box.png',cv2.resize(im[int(40*s):int(92*s),int(862*s):int(978*s)],None,fx=4,fy=4))
