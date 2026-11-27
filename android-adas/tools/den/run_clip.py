import sys,cv2
sys.path.insert(0,__file__.rsplit('/',1)[0]); from tl import analyse
cap=cv2.VideoCapture(sys.argv[1]); a,b,st=map(float,sys.argv[2:5])
t=a
while t<b:
    cap.set(0,t*1000); ok,im=cap.read()
    if not ok: break
    im=cv2.resize(im,(1280,720),interpolation=cv2.INTER_AREA)
    o,_=analyse(im)
    if o['lamps'] or o['cds']: print(f"{t:6.2f}",o['state'],[(d['col'],d['cd'] and d['cd'][0],round(d['conf'],2)) for d in o['lamps']],'cd:',[c['cd'][0] for c in o['cds']],flush=True)
    t+=st
