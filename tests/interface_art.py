#!/usr/bin/env python3
"""Check optional interface art exports independently of the graphics context."""
from pathlib import Path
import collections,hashlib,json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];ART=ROOT/'assets/mecha/art'
report=json.loads((ART/'interface-expansion.json').read_text());manifest=json.loads((ART/'manifest.json').read_text())
registered={row['name']:row for row in manifest['assets']}
counts=collections.Counter()
for row in report['assets']:
 name=row['name'];assert name in registered
 raw=(ART/row['rgba']).read_bytes();assert len(raw)==row['width']*row['height']*4
 assert hashlib.sha256(raw).hexdigest()==row['sha256']==registered[name]['sha256']
 image=Image.open(ART/(name+'.png')).convert('RGBA');assert image.size==(row['width'],row['height']) and image.tobytes()==raw
 assert len(image.getcolors(image.width*image.height))>=4
 counts[row['role']]+=1
 if name.endswith('-0'):
  other=Image.open(ART/(name[:-1]+'1.png')).convert('RGBA')
  assert image.size==other.size and image.tobytes()!=other.tobytes(),name
assert counts['mission_illustration']==20 and counts['interface_panel']==14
assert counts['equipment_illustration']>=60 and counts['mecha_portrait']==8 and counts['interface_scene']==12
assert report['runtime_texture_bytes']<=16*1024*1024
for kind in ['contracts','briefing','equipment','trader','debrief','profile','defeat']:
 for variant in range(2):
  image=Image.open(ART/f'ui-panel-{kind}-{variant}.png').convert('RGB')
  # Menu print fields stay dark enough for native green and amber text.
  for x in [16,64,128,192]:
   for y in range(42,170,12):assert max(image.getpixel((x,y)))<100
assert not report['reference_pixels_copied']
print(f'Validated {sum(counts.values())} original alternate interface assets, paired variants, readable fields and runtime texture budget')
