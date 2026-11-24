#!/usr/bin/env python3
"""Check source-preserving visual-comic exports without launching graphics."""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/'art/mecha/visual-comic-production'
ART=ROOT/'assets/mecha/cinematics/visual-art'
ACTIONS=('rifle','guard','scan','carry','melee','launch','command')
COUNTS={a:18 if a=='launch' else 3 for a in ACTIONS}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
library=json.loads((ROOT/'assets/mecha/stories/campaigns.json').read_text())['campaigns']
m=json.loads((ART/'manifest.json').read_text());rows=m['assets']
expected={f'foldout-{a["id"]}-{n}'for a in library for n in range(3)}|{f'foldout-action-{a}-{n}'for a in ACTIONS for n in range(COUNTS[a])}
assert m['schema']==1 and m['presentation']=='visual_only' and len(rows)==60
assert {r['name']for r in rows}==expected and m['runtime_texture_bytes']==47185920<=48*1024*1024
assert len({r['rgba']for r in rows})==m['physical_rgba_files']==60
backgrounds=set();poses={a:set()for a in ACTIONS};cameras=set();actual_bytes=0
for r in rows:
 assert Path(r['rgba']).name==r['rgba'] and (r['width'],r['height'])==(512,384)
 raw=ART/r['rgba'];actual_bytes+=raw.stat().st_size
 assert raw.stat().st_size==512*384*4 and sha(raw)==r['sha256']
 image=Image.open(ROOT/r['png']).convert('RGBA')
 assert image.size==(512,384) and image.tobytes()==raw.read_bytes() and sha(ROOT/r['png'])==r['png_sha256']
 content=image.getchannel('A').point(lambda value:255 if value>=26 else 0).getbbox()
 assert content and r['content_uv']==[content[0]/512,content[1]/384,content[2]/512,content[3]/384]
 assert 0<=r['content_uv'][0]<r['content_uv'][2]<=1 and 0<=r['content_uv'][1]<r['content_uv'][3]<=1
 source=Image.open(ROOT/r['source']).convert('RGBA')
 assert source.size==tuple(r['source_size']) and sha(ROOT/r['source'])==r['source_sha256']
 if r['role']=='action_cel':
  h=image.getchannel('A').histogram();assert h[0]>10000 and sum(h[128:])>5000
  assert r['action'] in ACTIONS and r['cel']in range(COUNTS[r['action']]) and r['frame_count']==COUNTS[r['action']]
  assert r['visual_review']=='current_source_reviewed' and isinstance(r['actual_view'],str) and r['actual_view']
  poses[r['action']].add(r['sha256']);cameras.add(r['camera_hint'])
  x0,y0,x1,y1=r['sheet_rectangle'];bx0,by0,bx1,by1=r['packing_box']
  assert 0<=x0<=bx0<bx1<=x1<=source.width and 0<=y0<=by0<by1<=y1<=source.height
  assert r['extraction']in ('alpha_gutter_rows','explicit_reviewed_rectangles','alpha_connected_components_3x6')
  assert r['intended_framing']in ('full_body','upper_body_detail')
  fit=r['fit_uv'];content=r['content_uv']
  assert 0<=fit[0]<=content[0]<content[2]<=fit[2]<=1 and 0<=fit[1]<=content[1]<content[3]<=fit[3]<=1
  if r['action']=='launch':
   assert r['camera_phase']==r['cel']//6 and r['phase_frame']==r['cel']%6
   mask=ROOT/r['component_mask'];assert sha(mask)==r['component_mask_sha256'] and r['component_opaque_pixels']>5000
   assert r['component_connectivity']==8 and r['component_alpha_threshold']==26
  else:assert fit==content
 else:
  backgrounds.add(r['sha256']);assert len(image.convert('RGB').getcolors(512*384))>16
assert actual_bytes==47185920 and len(backgrounds)==24
assert all(len(v)==COUNTS[a] for a,v in poses.items()) and len(set.union(*poses.values()))==36
launch=sorted([r for r in rows if r.get('action')=='launch'],key=lambda r:r['cel'])
for phase in range(3):assert len({tuple(r['fit_uv'])for r in launch[phase*6:(phase+1)*6]})==1
assert launch[0]['content_uv'][3]-launch[0]['content_uv'][1] < .8*(launch[5]['content_uv'][3]-launch[5]['content_uv'][1])
assert {'Wide','LowAngle','OverShoulder','Profile','Detail','HighAngle'}<=cameras
plans=json.loads((PROJECT/'plans/index.json').read_text());assert plans['presentation']=='visual_only' and plans['campaigns']==8
assert {p['campaign']for p in plans['plans']}=={a['id']for a in library}
for p in plans['plans']:
 plan=json.loads((PROJECT/p['file']).read_text());assert len(plan['shots'])==3 and len(plan['source_span'])>=3
 assert all(s['action']in ACTIONS for s in plan['shots']) and plan['adaptation_added']
print('Validated 7 action banks, 36 unique posed camera cels, 24 panorama views, 8 visual plans and 45 MiB budget')
