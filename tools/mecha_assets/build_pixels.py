#!/usr/bin/env python3
"""Draw original bitmap UI, glyphs, skies and 10-frame effects; no source image inputs."""
from pathlib import Path
import json,math,random,hashlib
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'assets/mecha';ART=ROOT/'art/mecha'
# Authored five-column glyphs; raster only, no system scalable font dependency.
GLYPHS={
'A':['01110','10001','10001','11111','10001','10001','10001'],'B':['11110','10001','10001','11110','10001','10001','11110'],'C':['01111','10000','10000','10000','10000','10000','01111'],'D':['11110','10001','10001','10001','10001','10001','11110'],'E':['11111','10000','10000','11110','10000','10000','11111'],'F':['11111','10000','10000','11110','10000','10000','10000'],'G':['01111','10000','10000','10111','10001','10001','01111'],'H':['10001','10001','10001','11111','10001','10001','10001'],'I':['11111','00100','00100','00100','00100','00100','11111'],'J':['00111','00010','00010','00010','10010','10010','01100'],'K':['10001','10010','10100','11000','10100','10010','10001'],'L':['10000','10000','10000','10000','10000','10000','11111'],'M':['10001','11011','10101','10101','10001','10001','10001'],'N':['10001','11001','11001','10101','10011','10011','10001'],'O':['01110','10001','10001','10001','10001','10001','01110'],'P':['11110','10001','10001','11110','10000','10000','10000'],'Q':['01110','10001','10001','10001','10101','10010','01101'],'R':['11110','10001','10001','11110','10100','10010','10001'],'S':['01111','10000','10000','01110','00001','00001','11110'],'T':['11111','00100','00100','00100','00100','00100','00100'],'U':['10001','10001','10001','10001','10001','10001','01110'],'V':['10001','10001','10001','10001','01010','01010','00100'],'W':['10001','10001','10001','10101','10101','11011','10001'],'X':['10001','10001','01010','00100','01010','10001','10001'],'Y':['10001','10001','01010','00100','00100','00100','00100'],'Z':['11111','00001','00010','00100','01000','10000','11111'],
'0':['01110','10001','10011','10101','11001','10001','01110'],'1':['00100','01100','00100','00100','00100','00100','01110'],'2':['01110','10001','00001','00010','00100','01000','11111'],'3':['11110','00001','00001','01110','00001','00001','11110'],'4':['00010','00110','01010','10010','11111','00010','00010'],'5':['11111','10000','10000','11110','00001','00001','11110'],'6':['01110','10000','10000','11110','10001','10001','01110'],'7':['11111','00001','00010','00100','01000','01000','01000'],'8':['01110','10001','10001','01110','10001','10001','01110'],'9':['01110','10001','10001','01111','00001','00001','01110'],
'-':['00000','00000','00000','11111','00000','00000','00000'],':':['00000','00100','00100','00000','00100','00100','00000'],'.':['00000','00000','00000','00000','00000','00100','00100'],'/':['00001','00010','00010','00100','01000','01000','10000'],'+':['00000','00100','00100','11111','00100','00100','00000'],'>':['10000','01000','00100','00010','00100','01000','10000'],'<':['00001','00010','00100','01000','00100','00010','00001'],'?':['01110','10001','00001','00010','00100','00000','00100'],'!':['00100','00100','00100','00100','00100','00000','00100'],'=':['00000','00000','11111','00000','11111','00000','00000'],'%':['11001','11010','00010','00100','01000','01011','10011'],'(':['00010','00100','01000','01000','01000','00100','00010'],')':['01000','00100','00010','00010','00010','00100','01000'],'_':['00000','00000','00000','00000','00000','00000','11111'],'^':['00100','01010','10001','00000','00000','00000','00000'],'[':['01110','01000','01000','01000','01000','01000','01110'],']':['01110','00010','00010','00010','00010','00010','01110'],'*':['00000','10101','01110','11111','01110','10101','00000']}
def text(im,xy,string,color=(179,198,199,255),scale=1):
 d=ImageDraw.Draw(im);x,y=xy
 for c in string.upper():
  for j,row in enumerate(GLYPHS.get(c,[])):
   for i,p in enumerate(row):
    if p=='1':d.rectangle((x+i*scale,y+j*scale,x+(i+1)*scale-1,y+(j+1)*scale-1),fill=color)
  x+=6*scale
entries=[]
def save(name,im,**metadata):
 p=OUT/'art'/(name+'.rgba');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(im.tobytes());entries.append(dict(name=name,rgba=p.name,width=im.width,height=im.height,sha256=hashlib.sha256(im.tobytes()).hexdigest(),**metadata));return im

def font():
 im=Image.new('RGBA',(128,48))
 for index in range(96):
  c=chr(index+32);rows=GLYPHS.get(c.upper(),GLYPHS['?'] if c!=' ' else [])
  for y,row in enumerate(rows):
   for x,v in enumerate(row):
    if v=='1':im.putpixel(((index%16)*8+x+1,(index//16)*8+y),(255,255,255,255))
 save('hud-font',im,glyph_width=8,glyph_height=8,columns=16,first_codepoint=32,provenance='Original 5x7 bitmap alphabet, white pixels in 8x8cells.')

def effects():
 random.seed(916)
 for name,size in [('explosion-atlas',80),('smoke-atlas',32)]:
  im=Image.new('RGBA',(size,size*10));p=im.load()
  for frame in range(10):
   t=(frame+.5)/10
   for y in range(size):
    for x in range(size):
     dx=(x+.5-size/2)/(size/2);dy=(y+.5-size/2)/(size/2);a=math.atan2(dy,dx);r=math.hypot(dx,dy)
     edge=(.32+.61*math.sin(min(1,t*1.5)*math.pi/2))*(1+.085*math.sin(a*7+frame)+.07*math.cos(a*11-frame))
     if name=='explosion-atlas':
      field=r/edge
      if field>1:continue
      alpha=int(255*min(1,(1-field)*6)*(1-t*.65));noise=((x*13+y*7+frame*17)%19)/19
      if t<.35 and field<.32:rgb=(255,246,179)
      elif field<.58 and t<.6:rgb=(255,196-int(noise*40),69)
      elif field<.88:rgb=(210-int(t*100),77+int(noise*20),39)
      else:rgb=(90,56,47)
     else:
      field=r/(edge*.92)
      if field>1:continue
      alpha=int(170*(1-t*.8)*min(1,(1-field)*4));n=int(17*math.sin(dx*16+frame)*math.cos(dy*12-frame));rgb=(96+n+int(t*25),106+n+int(t*25),113+n+int(t*25))
     p[x,y+frame*size]=(*rgb,alpha)
  save(name,im,frames=10,fps=10);im.save(ART/(name+'.png'))
 for name,color,stretch in [('shot-rifle',(255,215,107),1.9),('shot-gatling',(255,144,65),1.3),('shot-rail',(119,237,220),2.4),('shot-yellow',(255,215,107),1.5),('shot-pink',(204,145,230),1.5)]:
  im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im);d.polygon([(16,1),(21,15),(16,30),(11,15)],fill=(*color,255));d.polygon([(16,7),(18,16),(16,24),(14,16)],fill=(255,250,220,255));save(name,im)

def panels():
 for name in ['mission-selection-panel','mission-briefing-panel','weapon-loadout-panel']:
  im=Image.new('RGBA',(320,274),(16,23,31,255));d=ImageDraw.Draw(im)
  for y in range(0,274,4):d.line((0,y,319,y),fill=(19,28,37,255))
  d.polygon([(5,6),(308,6),(315,13),(315,260),(307,268),(5,268)],outline=(67,101,111,255),width=1)
  d.line((8,29,311,29),fill=(51,177,171,255));d.line((8,236,311,236),fill=(67,101,111,255))
  for x in [9,306]:d.rectangle((x,11,x+4,16),fill=(227,116,55,255))
  save(name,im)
 for i in range(1,10):
  im=Image.new('RGBA',(34,29),(24,33,42,255));d=ImageDraw.Draw(im);random.seed(i*113)
  col=[(103,99,68),(116,116,138),(53,118,119),(105,116,124),(142,76,57),(78,105,79),(148,182,192),(117,118,134),(104,85,125)][i-1]
  for n in range(7):
   x=random.randrange(34);y=random.randrange(29);d.polygon([(x,y),(x+random.randrange(4,12),y-3),(x+10,y+7),(x-3,y+10)],fill=(*col,255))
  d.rectangle((0,0,33,28),outline=(51,177,171,255));d.line((3,23,30,5),fill=(227,116,55,255));d.ellipse((13,10,20,17),outline=(220,225,196,255));save(f'mission-thumbnail-{i:02}',im)
 # Some legacy engine screens still request thumbnail numbers up to16. Distinct generated bitmapmaps remain original.
 for i in range(10,17):
  im=Image.new('RGBA',(34,29),(24,33,42,255));d=ImageDraw.Draw(im);d.rectangle((1,1,32,27),outline=(51,177,171,255));text(im,(10,10),str(i));save(f'mission-thumbnail-{i:02}',im)
 logo=Image.new('RGBA',(320,90),(16,23,31,255));text(logo,(24,17),'MECHA HACK',(215,207,174,255),4);text(logo,(26,57),'REACTOR / PILOT / FRONTIER',(51,177,171,255),1);save('title-logo',logo)
 for name,w,h in [('title-background',320,240),('intermission-background',320,256)]:
  im=Image.new('RGBA',(w,h),(16,23,31,255));d=ImageDraw.Draw(im)
  for x in range(0,w,20):d.line((x,0,x,h),fill=(23,38,46,255))
  for y in range(0,h,20):d.line((0,y,w,y),fill=(23,38,46,255))
  save(name,im)
 frontend=dict(schema=1,original=True,missions=[dict(id=i+1,title='Generated operation '+str(i+1),lines=[dict(text='DYNAMIC RPG OPERATION')]) for i in range(9)])
 (OUT/'art/frontend.json').write_text(json.dumps(frontend))

def environment():
 for i in range(1,7):
  sky=[(59,88,112),(48,75,86),(35,43,64),(92,83,78),(82,109,123),(27,34,49)][i-1];rim=[(147,151,140),(98,137,128),(88,93,118),(180,132,94),(175,187,181),(74,89,113)][i-1]
  im=Image.new('RGBA',(402,34));d=ImageDraw.Draw(im)
  for y in range(34):
   t=y/33;c=tuple(round(a*(1-t)+b*t) for a,b in zip(sky,rim));d.line((0,y,401,y),fill=(*c,255))
  random.seed(221+i);points=[(0,34)]+[(x,random.randrange(22,33)) for x in range(0,403,18)]+[(401,34)];d.polygon(points,fill=(42,58,64,255));save(f'horizon-{i:02}',im)
 for name in ['tree-conifer','tree-round','tree-leafless']:
  im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im);d.rectangle((14,14,17,31),fill=(106,74,55,255))
  if name=='tree-conifer':d.polygon([(16,1),(29,26),(3,26)],fill=(83,113,79,255))
  elif name=='tree-round':d.ellipse((2,1,30,26),fill=(83,113,79,255))
  else:d.line((16,22,5,8),fill=(106,74,55,255),width=2);d.line((16,17,25,4),fill=(106,74,55,255),width=2)
  save(name,im)
 # Engine compatibility ground atlas: 256x158, muted original color swatches, no copied texture pixels.
 for night in [False,True]:
  im=Image.new('RGBA',(256,158));p=im.load()
  colors=[(78,89,95),(112,120,118),(144,133,102),(84,112,91),(65,99,118),(139,81,58),(175,190,195),(106,100,120)]
  for y in range(im.height):
   for x in range(im.width):
    c=colors[(x//32+y//32)%8];n=((x*13+y*7)%11)-5;p[x,y]=(*[max(0,min(255,int(v*(.5 if night else 1)+n))) for v in c],255)
  folder=OUT/'gameplay';folder.mkdir(parents=True,exist_ok=True);(folder/('atlas-night.rgba' if night else 'atlas-day.rgba')).write_bytes(im.tobytes())
 (OUT/'gameplay/missions.json').write_text(json.dumps(dict(schema=1,atlas_size=[256,158],missions=[],original=True)))

def main():
 ART.mkdir(parents=True,exist_ok=True);font();effects();panels();environment()
 (OUT/'art/manifest.json').write_text(json.dumps(dict(schema=1,original=True,provenance='Original deterministic raster art and authored bitmap glyphs; no external image or font source.',assets=entries),indent=2)+'\n')
 sheet=Image.new('RGBA',(500,250),(16,23,31,255));x=5
 for name in ['explosion-atlas','smoke-atlas']:
  size=80 if name.startswith('explosion') else 32;im=Image.open(ART/(name+'.png'))
  for f in range(10):sheet.alpha_composite(im.crop((0,f*size,size,(f+1)*size)).resize((40,40),Image.Resampling.NEAREST),(5+f*48,25 if size==80 else 93))
 text(sheet,(5,8),'ORIGINAL / 10 FRAME EXPLOSION');text(sheet,(5,77),'ORIGINAL / 10 FRAME SMOKE');fontimg=Image.frombytes('RGBA',(128,48),(OUT/'art/hud-font.rgba').read_bytes());sheet.alpha_composite(fontimg.resize((256,96),Image.Resampling.NEAREST),(5,144));sheet.save(ART/'pixel-qa.png')
 print('Generated',len(entries),'original bitmap resources, ground atlas and9-mission frontend metadata')
if __name__=='__main__':main()
