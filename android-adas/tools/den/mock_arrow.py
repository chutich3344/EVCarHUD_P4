import cv2,numpy as np,subprocess,sys
from PIL import Image,ImageDraw,ImageFont
sys.path.insert(0,__file__.rsplit('/',1)[0]); from arrow import arrows
F='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-Bold.ttf'; FS='/home/user/repo/android-adas/app/src/main/assets/fonts/BeVietnamPro-SemiBold.ttf'
im=cv2.imread('/root/.claude/uploads/a94732ae-171b-5d05-8797-59dc73f71e8b/e5366d22-15057.jpg')
H,W=im.shape[:2]; s=1.188
# xoa the cu (che bang mau anh lan can)
x0,y0,x1,y1=int(785*s),int(132*s),int(1215*s),int(280*s)
base=im.copy(); base[y0:y1,x0:x1]=cv2.GaussianBlur(im[y0-200 if y0>200 else 0:y1,x0:x1],(1,1),0)[-(y1-y0):] if False else cv2.resize(im[int(300*s):int(300*s)+(y1-y0),x0:x1],(x1-x0,y1-y0))
ar=arrows(im,(int(862*s),int(40*s),int(978*s),int(92*s)))
print(ar)
def card(state):
    pil=Image.fromarray(cv2.cvtColor(base,cv2.COLOR_BGR2RGB)).convert('RGBA'); ov=Image.new('RGBA',pil.size,(0,0,0,0)); d=ImageDraw.Draw(ov)
    u=W/100; f1=ImageFont.truetype(F,int(1.7*u)); f2=ImageFont.truetype(FS,int(1.3*u)); f3=ImageFont.truetype(F,int(5.2*u)); f4=ImageFont.truetype(F,int(1.4*u))
    R=(255,59,48); G=(48,209,88)
    go=state=='go'
    w=(27 if go else 21)*u; h=7*u; cx0=W/2-w/2; cy0=6.8*u
    d.rounded_rectangle([cx0,cy0,cx0+w,cy0+h],int(1.5*u),fill=(10,12,16,220))
    for i,k in enumerate('RYG'):
        cy=cy0+1.45*u+i*2.05*u; cx=cx0+2.4*u; r0=0.9*u
        if k=='G' and go:
            d.polygon([(cx,cy-r0),(cx+r0,cy),(cx+.35*r0,cy),(cx+.35*r0,cy+r0),(cx-.35*r0,cy+r0),(cx-.35*r0,cy),(cx-r0,cy)],fill=G)
        else: d.ellipse([cx-.85*u,cy-.85*u,cx+.85*u,cy+.85*u],fill=R if k=='R' else (55,58,64))
    d.text((cx0+4.6*u,cy0+1.0*u),'ĐƯỢC ĐI THẲNG' if go else 'ĐÈN ĐỎ',font=f1,fill=G if go else R)
    d.text((cx0+4.6*u,cy0+3.9*u),'đèn tròn đỏ · còn' if go else 'còn',font=f2,fill=(200,200,205))
    tw=d.textlength('7',font=f3); d.text((cx0+w-1.4*u-tw,cy0+0.2*u),'7',font=f3,fill=R)
    if go and ar:
        b=ar[0][2]; d.rectangle([b[0]-6,b[1]-6,b[2]+6,b[3]+6],outline=G,width=4); d.text((b[0]-10,b[3]+10),'mũi tên xanh đi thẳng',font=f4,fill=G)
    lab='TRƯỚC (0.1.130): chỉ thấy đèn tròn đỏ' if not go else 'SAU: nhận mũi tên xanh đi thẳng, báo ĐƯỢC ĐI THẲNG, HUD báo xanh'
    d.rounded_rectangle([20,H-70,W-20,H-16],12,fill=(0,0,0,190)); d.text((36,H-62),lab+'  ·  minh hoạ trên ảnh anh gửi 24/11',font=f4,fill=(255,255,255))
    return Image.alpha_composite(pil,ov).convert('RGB')
a=card('red'); b=card('go'); a.save('mui-ten-truoc.jpg',quality=90); b.save('mui-ten-sau.jpg',quality=90)
Wv,Hv=1920,int(1920*H/W)//2*2
p=subprocess.Popen(['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{Wv}x{Hv}','-r','10','-i','-','-c:v','libx264','-pix_fmt','yuv420p','-crf','22','mui-ten.mp4'],stdin=subprocess.PIPE)
for img,n in [(a,30),(b,50)]:
    fr=np.asarray(img.resize((Wv,Hv))).tobytes()
    for _ in range(n): p.stdin.write(fr)
p.stdin.close(); p.wait()
