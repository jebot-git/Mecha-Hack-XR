"""Add shared timing and contact tracks to Blender's paired, matched-camera renders."""
import argparse
import json
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

here=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument("frames",type=Path)
parser.add_argument("--font",default="DejaVuSans.ttf")
args=parser.parse_args()
work=args.frames
masks=json.loads((work/"contacts.json").read_bytes())
def font(size):
    return ImageFont.truetype(args.font, size)

regular=font(17)
small=font(14)
title=font(24)
paper=(17,25,32)
ink=(235,240,238)
muted=(176,194,198)
mint=(116,222,183)
fps=24
command=["ffmpeg","-loglevel","error","-y","-f","rawvideo","-pix_fmt","rgb24","-s","1000x600",
         "-r",str(fps),"-i","-","-an","-c:v","libx264","-crf","18","-preset","fast","-pix_fmt",
         "yuv420p","-movflags","+faststart",str(here/"stalker-comparison.mp4")]
anchors=("Front L","Front R","Rear L","Rear R")
with subprocess.Popen(command,stdin=subprocess.PIPE) as encoder:
    for i,contacts in enumerate(masks):
        t=i/fps
        state="Approach" if t<3.38 else "Load" if t<4.47 else "Bite" if t<5.05 else "Recover"
        image=Image.new("RGB",(1000,600),paper)
        render=Image.open(work/f"frame{i+1:04}.png").convert("RGB")
        image.paste(render,(0,63))
        draw=ImageDraw.Draw(image)
        draw.text((19,7),"Voidmaw Stalker",font=title,fill=ink)
        draw.text((20,38),"Live animation",font=regular,fill=muted)
        draw.text((518,38),"Contact-constrained study",font=regular,fill=mint)
        draw.text((979,13),f"{state}   {t:04.2f} s",anchor="ra",font=regular,fill=ink)
        draw.line((500,62,500,497),fill=(68,88,92),width=1)
        draw.text((20,515),"Study claw contacts",font=regular,fill=ink)
        draw.text((20,542),"Bright bars mark planted feet",font=small,fill=muted)
        draw.text((20,565),"Ground grid  3 m",font=small,fill=muted)
        left,right=347,976
        for leg,label in enumerate(anchors):
            y=509+leg*20
            draw.text((334,y-4),label,anchor="ra",font=small,fill=ink if contacts[leg] else muted)
            draw.rectangle((left,y,right,y+9),fill=(49,63,71))
            for j,mask in enumerate(masks):
                if mask[leg]:
                    draw.rectangle((left+j*(right-left)/len(masks),y,left+(j+1)*(right-left)/len(masks),y+9),
                                   fill=(74,145,123))
        x=left+i*(right-left)/(len(masks)-1)
        draw.line((x,504,x,582),fill=(255,236,172),width=2)
        if i==110:
            image.save(here/"stalker-review.jpg",quality=92)
        encoder.stdin.write(image.tobytes())
    encoder.stdin.close()
    if encoder.wait():
        raise RuntimeError("Video encode failed")
