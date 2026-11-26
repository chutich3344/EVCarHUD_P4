import sys; sys.path.insert(0,'.')
import cv2,numpy as np
from det import yolox
cap=cv2.VideoCapture(sys.argv[1]); a,b,st=map(float,sys.argv[2:5])
t=a
while t<b:
    cap.set(0,t*1000); ok,im=cap.read()
    if not ok: break
    im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA); H,W=im.shape[:2]
    def run(x0,x1,y0,y1,iw,ih):
        r=yolox(im[int(y0*H):int(y1*H),int(x0*W):int(x1*W)],iw,ih); sx=(x1-x0)*W/iw; sy=(y1-y0)*H/ih
        return [(round(q[4],2),int((q[2]-q[0])*sx),int((q[3]-q[1])*sy)) for q in r if q[4]>=0.15][:3]
    base=run(.22,.78,0,.62,704,448)
    zoom=run(.30,.70,0.25,.62,640,384)
    print(f"{t:6.1f} base {base}  zoom {zoom}",flush=True)
    t+=st
