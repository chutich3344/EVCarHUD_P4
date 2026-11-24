import sys; sys.path.insert(0,'.')
import cv2,numpy as np,tl,tmpl2,syn7,subprocess
from PIL import Image,ImageDraw,ImageFont
F='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-Bold.ttf'
f18=ImageFont.truetype(F,18); f24=ImageFont.truetype(F,24); f60=ImageFont.truetype(F,60); f14=ImageFont.truetype(F,14)
real=cv2.imread('/mnt/project-files/uploads/hearth/e5366d22-e39b-472e-9aca-443c20e0d5ee')
def scene(n,blur,h,sl):
    img=np.full((360,640,3),(205,185,165),np.uint8)
    s=h+12; x=230; y=60
    cv2.rectangle(img,(x,y),(x+3*s,y+s),(30,28,28),-1)
    cv2.circle(img,(x+s//2,y+s//2),int(s*0.38),(60,60,255),-1); g=cv2.GaussianBlur(img,(0,0),3); img=cv2.addWeighted(img,0.6,g,0.5,0)
    im,b=syn7.make(n,blur=blur,out_h=h,slant=sl); d=im[b[1]:b[3],b[0]:b[2]]
    cx=x+3*s+6; cv2.rectangle(img,(cx-4,y-4),(cx+d.shape[1]+4,y+d.shape[0]+4),(30,28,28),-1)
    img[y:y+d.shape[0],cx:cx+d.shape[1]]=d
    return img,(x,y,x+s,y+s)
orig=tmpl2.read2
def read(img,lb,new):
    tmpl2.read2=orig if new else (lambda a:(None,0,0))
    r=tl.countdown(img,lb,'R',sides=('R',)); return r and r[0]
frames=[]
def card(dr,x,y,title,v,ok):
    dr.rounded_rectangle((x,y,x+250,y+120),16,fill=(20,20,24))
    dr.text((x+16,y+10),title,font=f18,fill=(200,200,200))
    dr.text((x+16,y+38),'ĐÈN ĐỎ · còn',font=f18,fill=(255,59,48))
    dr.text((x+150,y+30),str(v) if v is not None else '–',font=f60,fill=(255,59,48))
    dr.text((x+16,y+90),'✓ đúng' if ok else '✗ sai',font=f18,fill=(48,209,88) if ok else (255,90,80))
# phan 1: anh that cua anh
B,G,R=[real[...,i].astype(np.float32) for i in range(3)]; ch=np.clip(R-(G+B)/2,0,None)
rv=tmpl2.read2(ch[41:96,935:1011])[0]
crop=cv2.resize(real[25:110,920:1030],None,fx=3,fy=3,interpolation=cv2.INTER_CUBIC)
base=np.full((720,1280,3),(16,16,18),np.uint8); base[150:150+crop.shape[0],80:80+crop.shape[1]]=crop
im=Image.fromarray(cv2.cvtColor(base,cv2.COLOR_BGR2RGB)); dr=ImageDraw.Draw(im)
dr.text((80,60),'Ảnh thật anh gửi (24/11) — bảng đếm ghi 70',font=f24,fill=(255,255,255))
dr.text((80,420),'(khung đỏ là khung app vẽ, lúc chạy thật app đọc ảnh gốc không có khung)',font=f14,fill=(150,150,150))
card(dr,520,150,'Bản 0.1.130 (cũ)',7,False); card(dr,820,150,'Bản sửa (mới)',rv,rv==70)
fr=cv2.cvtColor(np.array(im),cv2.COLOR_RGB2BGR); frames+= [fr]*30
# phan 2: dem lui gia lap, nhoe/nghieng nhu 70mai
okN=[0,0];N=0
for n in range(72,57,-1):
    for blur,h,sl in ((10,30,0.2),):
        img,lb=scene(n,blur,h,sl); o=read(img,lb,False); w=read(img,lb,True); N+=1; okN[0]+=o==n; okN[1]+=w==n
        base=np.full((720,1280,3),(16,16,18),np.uint8)
        z=cv2.resize(img[30:170,200:520],None,fx=2.4,fy=2.4,interpolation=cv2.INTER_CUBIC); base[150:150+z.shape[0],60:60+z.shape[1]]=z
        im=Image.fromarray(cv2.cvtColor(base,cv2.COLOR_BGR2RGB)); dr=ImageDraw.Draw(im)
        dr.text((60,60),f'Giả lập đèn đỏ đếm lùi, LED nhoè + nghiêng như 70mai — đang hiện {n}',font=f24,fill=(255,255,255))
        card(dr,860,150,'Cách đọc cũ',o,o==n); card(dr,860,320,'Bản sửa (mới)',w,w==n)
        fr=cv2.cvtColor(np.array(im),cv2.COLOR_RGB2BGR); frames+=[fr]*10
        if n==70: cv2.imwrite('../sim70.jpg',fr)
print('real',rv,'cu/moi dung',okN,'/',N)
cv2.imwrite('../sim-real.jpg',frames[0])
vw=cv2.VideoWriter('../sim70.avi',cv2.VideoWriter_fourcc(*'MJPG'),10,(1280,720))
for f in frames: vw.write(f)
vw.release()
subprocess.run(['ffmpeg','-y','-loglevel','error','-i','../sim70.avi','-c:v','libx264','-pix_fmt','yuv420p','../sim70.mp4'])
