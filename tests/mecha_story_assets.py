#!/usr/bin/env python3
"""Independent delivery/preservation checks for the story kit; no Blender or game launch."""
import hashlib,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];ASSETS=ROOT/'assets/mecha'
load=lambda path:json.loads(path.read_text())
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
def inherited(rig):
 return dict(rig,parts=[{k:v for k,v in part.items() if k not in ('low','high')} for part in rig['parts']])

manifest=load(ASSETS/'story-assets.json');assert manifest['schema']==1 and manifest['all_pass']
assert len(manifest['assets'])==12 and manifest['environment_count']==8 and manifest['enemy_variant_count']==4
rigs=load(ASSETS/'models/story-enemy-rigs.json')['rigs'];models=load(ASSETS/'models/manifest.json')['models']
props=enemies=0
for row in manifest['assets']:
 native=load(ASSETS/row['file']);gpu=load(ASSETS/row['model_file']);asset=row['id']
 assert native['id']==gpu['id']==asset
 assert sha(ASSETS/row['file'])==row['sha256'] and sha(ASSETS/row['model_file'])==row['model_sha256']
 assert len(native['faces'])==len(gpu['faces'])==row['triangles']<=row['budget']
 assert models[asset]['file']==Path(row['model_file']).name and models[asset]['budget']==row['budget']
 assert all(math.isfinite(x) and abs(x)<=500 for p in native['vertices'] for x in p)
 assert len(native['vertices'])==len(gpu['vertices'])
 assert native['units']=='metres' and gpu['units']=='centimetres'
 assert all(abs(b-100*a)<2e-3 for p,q in zip(native['vertices'],gpu['vertices']) for a,b in zip(p,q))
 for f,g in zip(native['faces'],gpu['faces']):
  assert f['indices']==g['indices'] and len(f['indices'])==3 and len(set(f['indices']))==3
  assert f.get('uv') and all(0<=u<=1 and 0<=v<=1 for u,v in f['uv'])
  assert f.get('texture')=='psx' and f.get('texture_tile')
  assert all(0<=i<len(native['vertices']) for i in f['indices'])
 if row['kind']=='environment':assert row['budget']<=256;props+=1
 else:
  enemies+=1;source=load(ASSETS/'meshes'/(row['source_id']+'.json'))
  assert sha(ASSETS/'meshes'/(row['source_id']+'.json'))==row['source_mesh_sha256']==manifest['baseline_hashes'][row['source_id']]
  assert row['budget']<=row['source_budget']==source['budget']
  assert len(native['vertices'])==len(source['vertices']) and len(native['faces'])==len(source['faces'])
  assert [f['indices'] for f in native['faces']]==[f['indices'] for f in source['faces']]
  assert native['vertices']!=source['vertices'],'Enemy role is only a palette change'
  for axis in range(3):
   for fn in (min,max):assert abs(fn(p[axis] for p in native['vertices'])-fn(p[axis] for p in source['vertices']))<2e-5
  rig=rigs[asset];assert rig['mesh_vertices']==len(native['faces'])*3
  assert rig['parts'] and any(p['role'] in ('hip','rotor','suspension','tail') for p in rig['parts'])
  # Story variants inherit this rig entry, while their reshaped parts have new bounds.
  source_rig=load(ASSETS/row['source_rig_file'])['rigs'][row['source_id']]
  source_rig.setdefault('motion_profile','legacy')
  assert inherited(rig)==inherited(source_rig),asset
  for part in rig['parts']:
   points=[native['vertices'][i] for start,count in part['ranges']
           for face in native['faces'][start//3:(start+count)//3] for i in face['indices']]
   for axis in range(3):
    for key,fn in [('low',min),('high',max)]:
     assert abs(part[key][axis]-fn(p[axis] for p in points))<2e-5,(asset,part['name'],key)
assert props==8 and enemies==len(rigs)==4
blend=ROOT/manifest['source_blend'];assert blend.is_file() and sha(blend)==manifest['source_blend_sha256']
assert manifest['rigs']==4 and manifest['animation_actions']==12
print('Story kit: 8 textured landmarks,4 genuinely reshaped source-preserving enemies,rigs/budgets/hashes verified')
