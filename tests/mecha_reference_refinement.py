#!/usr/bin/env python3
"""Reference-led geometry invariants, including the restored Jump-Pod identity."""
import hashlib,json
from collections import Counter
from mecha_refinement_support import ROOT,IDS,revision
from mecha_redesign_assets import bounds,close
from mecha_enemy_redesign_assets import topology
P=ROOT/'assets/mecha'
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 data=revision();assert data is not None
 for field in ('source_blend','authoring_script'):assert sha(ROOT/data[field])==data[field+'_sha256']
 assert len(data['references'])==6
 assert sha(P/data['rig_file'])==data['rig_sha256'];rigs=read(P/data['rig_file'])['rigs'];assert set(rigs)==IDS
 hashes=set();triangles=0
 for row in data['assets']:
  key=row['id'];mesh=read(P/row['file']);old=read(P/row['previous_native']);gpu=read(P/row['model']);rig=rigs[key]
  assert sha(P/row['file'])==row['sha256'] and sha(P/row['model'])==row['model_sha256']
  assert sha(P/row['previous_native'])==row['previous_sha256'] and sha(P/row['previous_model'])==row['previous_model_sha256']
  assert row['sha256']!=row['previous_sha256']
  assert mesh['budget']==old['budget']==row['budget'] and len(mesh['faces'])==row['triangles']<=row['budget']
  assert mesh['pivot']==old['pivot'] and all(close(a,b)for a,b in zip(bounds(mesh['vertices']),bounds(old['vertices'])))
  assert topology(key,mesh)>0
  assert gpu['faces']==mesh['faces'] and all(close([n*100 for n in a],b,1e-3)for a,b in zip(mesh['vertices'],gpu['vertices']))
  fingerprint=hashlib.sha256(json.dumps([mesh['vertices'],mesh['face_semantics']]).encode()).hexdigest();assert fingerprint not in hashes;hashes.add(fingerprint)
  assert rig['mesh_vertices']==len(mesh['faces'])*3
  roles=Counter(p['role']for p in rig['parts'])
  for i,part in enumerate(rig['parts']):assert -1<=part['parent']<i
  if key.startswith('jump_pod'):
   assert roles['arm']==0 and roles['hip']==roles['knee']==roles['foot']==2
   assert roles['barrel']==2 and roles['head']==roles['torso']==1
   assert len([p for p in rig['parts']if p['ground']])==2
   # Missile boxes sit above the pod; bent legs occupy the lower half.
   hips=[p for p in rig['parts']if p['role']=='hip'];knees=[p for p in rig['parts']if p['role']=='knee']
   assert all(k['pivot'][1]<h['pivot'][1]for h,k in zip(hips,knees))
   assert min(p['low'][1]for p in rig['parts']if p['role']=='barrel')>mesh['bounds']['high'][1]*.65
  if key.startswith('mechwalker'):assert roles['hip']==roles['knee']==roles['foot']==4
  if key=='cyclone_drone':assert roles['rotor']==3 and roles['wing']==2 and rig['motion_profile']=='hover'
  if key=='heavy_hover_tank':assert roles['suspension']==2 and roles['turret']==roles['barrel']==1 and rig['motion_profile']=='tracked'
  triangles+=len(mesh['faces'])
 print(f'{len(hashes)} unique scoped enemy revisions, {triangles} triangles: identity, budgets, bounds, closed geometry, historical pins and roles passed')
if __name__=='__main__':main()
