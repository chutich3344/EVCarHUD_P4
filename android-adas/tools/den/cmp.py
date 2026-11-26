import sys; sys.path.insert(0,'.')
import cv2,tl
cap=cv2.VideoCapture(sys.argv[1]); a,b,st=map(float,sys.argv[2:5])
cfgs={'cu':(0.5,None),'moi':(0.62,(.30,.70,.25,.62,640,384))}
t=a
while t<b:
    cap.set(0,t*1000); ok,im=cap.read()
    if not ok: break
    im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA); row=[]
    for nm,(ry,z) in cfgs.items():
        tl.RY1=ry; tl.ZOOM=z; o,_=tl.analyse(im)
        row.append(f"{nm} {o['state']} {[(l['col'],l['box'][2]-l['box'][0]) for l in o['lamps'] if l['col']]}")
    if any('None []' not in r for r in row): print(f"{t:6.1f}"," | ".join(row),flush=True)
    t+=st
