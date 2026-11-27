import re,json,io,os,cairosvg
from PIL import Image
# Dung: python3 build_icons.py <package/dist/cjs/signs.generated.js> (tai: npm pack @road-signs/vn@0.1.2)
import sys
t=open(sys.argv[1]).read(); r={}
for blk in re.split(r'\n    \{\n',t)[1:]:
    c=re.search(r'code: "([^"]*)"',blk); i=re.search(r'id: "([^"]*)"',blk); sv=re.search(r'\bsvg: "((?:[^"\\]|\\.)*<svg(?:[^"\\]|\\.)*)"',blk)
    if not sv: continue
    sv=json.loads('"'+sv.group(1)+'"')
    if c: r.setdefault(c.group(1),sv)
    if i: r.setdefault(i.group(1),sv)
B='#0046aa'
STR_RIGHT=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 702 702"><circle cx="351" cy="351" r="350" fill="{B}"/>
<g fill="none" stroke="#fff" stroke-width="58" stroke-linejoin="miter"><path d="M300 600V200"/><path d="M300 430Q300 340 420 340H470"/></g>
<path d="M300 90L200 220H400Z" fill="#fff"/><path d="M590 340L460 250V430Z" fill="#fff"/></svg>'''
def arc_arrow(a0):
    import math
    cx=cy=351; R=165
    a1=a0+80
    p=lambda a,rr=R:(cx+rr*math.cos(math.radians(a)),cy+rr*math.sin(math.radians(a)))
    x0,y0=p(a0);x1,y1=p(a1)
    t=p(a1+28); l=p(a1-4,R+72); rr_=p(a1-4,R-72)
    return f'<path d="M{x0:.0f} {y0:.0f}A{R} {R} 0 0 1 {x1:.0f} {y1:.0f}" fill="none" stroke="#fff" stroke-width="52"/><path d="M{t[0]:.0f} {t[1]:.0f}L{l[0]:.0f} {l[1]:.0f}L{rr_[0]:.0f} {rr_[1]:.0f}Z" fill="#fff"/>'
# Viet Nam di ben phai -> vong xuyen nguoc chieu kim dong ho: ve chieu duong (cung kim) roi lat ngang
ROUND=f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 702 702"><circle cx="351" cy="351" r="350" fill="{B}"/><g transform="translate(702 0) scale(-1 1)">'+''.join(arc_arrow(a) for a in (-60,60,180))+'</g></svg>'
TURN_R=f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 702 702"><circle cx="351" cy="351" r="350" fill="{B}"/>
<path d="M290 600V400Q290 330 360 330H450" fill="none" stroke="#fff" stroke-width="64"/><path d="M590 330L450 220V440Z" fill="#fff"/></svg>'''
TURN_L=TURN_R.replace('<path','<g transform="translate(702 0) scale(-1 1)"><path',1).replace('</svg>','</g></svg>')
SPEED=re.sub(r'<g aria-label="\d+".*?</g>','',r['P.127'],flags=re.S)
SPEED=re.sub(r'<g aria-label="\d+".*?</g>','',SPEED,flags=re.S)
M={0:'DP.135',1:'P.102',2:'P.103',3:'P.103B',4:'P.103C',5:'P.104',6:'P.106A',7:'P.106B',8:'P.107A',9:'P.112',10:'P.115',11:'P.117',
12:'P.123A',13:'P.123B',14:'p-124a1-no-left-u-turns',15:'p-124b1-no-left-u-turns-for-motor-vehicles',16:'P.124C',17:SPEED,18:'P.128',
19:'P.130',20:'P.131A',21:'P.137',22:'P.104',23:STR_RIGHT,24:TURN_L,25:TURN_R,26:'R.302A',27:'R.302B',28:ROUND,29:'I.407A',
30:'I.409',31:'I.437',32:'R.403C',33:'S.509B',34:'W.201A',35:'W.201B',36:'W.202A',37:'W.202B',38:'W.203B',39:'W.203C',40:'W.205A',
41:'W.205B',42:'W.205D',43:'W.207A',44:'W.207B',45:'W.207C',46:'W.208',47:'W.209',48:'W.210',49:'W.219',50:'W.224',51:'W.225',
52:'W.227',53:'W.233',54:'W.235',55:'W.245A'}
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','app','src','main','assets','bien')
os.makedirs(out,exist_ok=True)
SZ=128
sheet=Image.new('RGBA',(14*132,4*132),'#555')
for k in range(56):
    v=M[k]; svg=v if v.startswith('<svg') else r[v]
    im=Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode(),output_width=SZ,output_height=SZ))).convert('RGBA')
    if k==22:  # cam xe dien: bien cam mo to + tia set vang
        from PIL import ImageDraw
        d=ImageDraw.Draw(im); s=SZ/128
        pts=[(96,70),(80,96),(91,96),(84,118),(106,88),(94,88),(102,70)]
        d.polygon([(x*s,y*s) for x,y in pts],fill='#ffd400',outline='#000')
    im.save(f'{out}/{k}.png',optimize=True)
    sheet.alpha_composite(im,((k%14)*132+2,(k//14)*132+2))
sheet.save('icons_sheet.png')
