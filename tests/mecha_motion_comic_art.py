#!/usr/bin/env python3
"""Verify generated motion art, source trace and approved adaptation contracts."""
import hashlib
import json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/'art/mecha/motion-comic-production'
ART=ROOT/'assets/mecha/cinematics/motion-art'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
library=json.loads((ROOT/'assets/mecha/stories/campaigns.json').read_text())['campaigns']
manifest=json.loads((ART/'manifest.json').read_text());rows=manifest['assets']
expected={f'foldout-{a["id"]}-{n}' for a in library for n in range(3)}|{f'foldout-{a["id"]}-actor-{n}' for a in library for n in range(3)}
assert manifest['schema']==1 and len(rows)==48 and {r['name'] for r in rows}==expected
assert manifest['runtime_texture_bytes']==48*512*384*4<=48*1024*1024
backgrounds=set();poses={0:set(),1:set(),2:set()};raw_files=set()
for r in rows:
    assert Path(r['rgba']).name==r['rgba'] and (r['width'],r['height'])==(512,384)
    raw=ART/r['rgba'];raw_files.add(raw)
    assert raw.stat().st_size==512*384*4 and sha(raw)==r['sha256']
    image=Image.open(ROOT/r['png']).convert('RGBA');assert image.size==(512,384)
    assert image.tobytes()==raw.read_bytes() and sha(ROOT/r['png'])==r['png_sha256']
    assert sha(ROOT/r['source'])==r['source_sha256']
    if r['role']=='actor_cel':
        h=image.getchannel('A').histogram();assert h[0]>10000 and sum(h[128:])>10000
        poses[r['cel']].add(r['sha256'])
    else:
        backgrounds.add(r['sha256']);assert len(image.convert('RGB').getcolors(512*384))>16
assert len(raw_files)==27 and len(backgrounds)==24
assert all(len(p)==1 for p in poses.values()) and len(set.union(*poses.values()))==3
for marker in ['visual-bible/STYLE_APPROVED','visual-bible/reference-cards/APPROVED']:
    assert (PROJECT/marker).is_file()
units=0
for c in sorted((PROJECT/'chapters').glob('ch*')):
    if not c.is_dir():continue
    for marker in ['PAGE_STORY_PLAN_APPROVED','PAGE_SCRIPT_APPROVED','STORYBOARD_APPROVED']:assert (c/marker).is_file()
    script=json.loads((c/'page-script.json').read_text());assert len(script['pages'])==31;units+=len(script['pages'])
    assert len(list((c/'director-briefs').glob('page-*-director-brief.md')))==31
assert units==248
print('Validated 8 generated worlds,3 shared posed cels,48 bounded GPU records,248 source-traced approved motion scripts')
