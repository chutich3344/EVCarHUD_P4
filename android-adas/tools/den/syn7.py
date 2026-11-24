import numpy as np,cv2,sys
sys.path.insert(0,__file__.rsplit('/',1)[0]); import read7v as R
SEGS={0:'abcdef',1:'bc',2:'abged',3:'abgcd',4:'fgbc',5:'afgcd',6:'afgedc',7:'abc',8:'abcdefg',9:'abcdfg'}
def digit(img,x,y,w,h,d,col,th):
    p={'a':((x,y),(x+w,y)),'b':((x+w,y),(x+w,y+h//2)),'c':((x+w,y+h//2),(x+w,y+h)),'d':((x,y+h),(x+w,y+h)),'e':((x,y+h//2),(x,y+h)),'f':((x,y),(x,y+h//2)),'g':((x,y+h//2),(x+w,y+h//2))}
    for s_ in SEGS[d]: cv2.line(img,p[s_][0],p[s_][1],col,th)
def make(n,col=(40,40,255),blur=9,out_h=26,slant=0.12,gap=0.25):
    H=200; W=int(H*1.35); img=np.full((H,W,3),(35,30,30),np.uint8)
    dw=int(0.42*H*0.85); dh=int(0.7*H); y=int(0.15*H); xs=[int(0.12*W), int(0.12*W)+dw+int(gap*dw)+12]
    digit(img,xs[0],y,dw,dh,n//10,col,14); digit(img,xs[1],y,dw,dh,n%10,col,14)
    M=np.float32([[1,-slant,slant*H/2],[0,1,0]]); img=cv2.warpAffine(img,M,(W,H),borderValue=(35,30,30))
    glow=cv2.GaussianBlur(img,(0,0),blur); img=cv2.addWeighted(img,0.7,glow,0.9,0)
    img=cv2.resize(img,(int(W*out_h/H),out_h),interpolation=cv2.INTER_AREA)
    bg=np.full((out_h+20,img.shape[1]+40,3),(180,170,160),np.uint8); bg[10:10+out_h,20:20+img.shape[1]]=img
    return bg,(20,10,20+img.shape[1],10+out_h)
if __name__=='__main__':
    ok={'box':0,'cells':0}; N=0
    for blur in (4,8,12):
      for h in (22,30,40):
        for n in range(10,100,3):
            im,b=make(n,blur=blur,out_h=h); N+=1
            if R.read_box(im,b)==n: ok['box']+=1
            x0,y0,x1,y1=b; reg=im[y0:y1,x0:x1].astype(np.float32); k=max(1,int(round(64/(y1-y0)))); reg=cv2.resize(reg,None,fx=k,fy=k,interpolation=cv2.INTER_CUBIC)
            ch=reg[...,2]-0.5*(reg[...,1]+reg[...,0]); r=R.cells_read(ch)
            if isinstance(r,list) and all(x and x[0] is not None for x in r) and r[0][0]*10+r[1][0]==n: ok['cells']+=1
    print(N,ok)
    im,b=make(70,blur=10,out_h=26); cv2.imwrite('syn70.png',cv2.resize(im,None,fx=6,fy=6,interpolation=cv2.INTER_NEAREST))
