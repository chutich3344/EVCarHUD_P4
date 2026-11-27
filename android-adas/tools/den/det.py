import numpy as np, onnxruntime as ort, cv2, sys
S=ort.InferenceSession('/home/user/repo/android-adas/app/src/main/assets/xe-coco.onnx')
def yolox(img, iw, ih):
    # img BGR crop -> resized to iw x ih exactly (stretch keeps aspect if caller sizes right)
    x=cv2.resize(img,(iw,ih),interpolation=cv2.INTER_AREA).astype(np.float32).transpose(2,0,1)[None]
    o=S.run(None,{'images':x})[0][0]
    res=[]; a=0
    for st in (8,16,32):
        ny,nx=ih//st,iw//st
        g=o[a:a+ny*nx]; a+=ny*nx
        gy,gx=np.mgrid[0:ny,0:nx]; gx=gx.ravel(); gy=gy.ravel()
        sc=g[:,4]*g[:,5+9]
        for i in np.where(sc>0.15)[0]:
            cx=(g[i,0]+gx[i])*st; cy=(g[i,1]+gy[i])*st; w=np.exp(g[i,2])*st; h=np.exp(g[i,3])*st
            res.append([cx-w/2,cy-h/2,cx+w/2,cy+h/2,sc[i]])
    res.sort(key=lambda r:-r[4]); keep=[]
    for r in res:
        if all(iou(r,k)<0.4 for k in keep): keep.append(r)
    return keep
def iou(a,b):
    ix=max(0,min(a[2],b[2])-max(a[0],b[0])); iy=max(0,min(a[3],b[3])-max(a[1],b[1])); i=ix*iy
    u=(a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-i; return i/u if u>0 else 0
