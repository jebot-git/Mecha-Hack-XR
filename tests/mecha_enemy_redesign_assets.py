#!/usr/bin/env python3
"""Verify the remaining44 enemy redesign without releasing prior source pins."""
import argparse
import collections
import hashlib
import json
import math
from mecha_refinement_support import historical_source,current_rigs
from pathlib import Path
from mecha_redesign_assets import ROOT, ASSETS, read, check, bounds, close, panel_diagnostics

CATEGORIES={'kaiju_hybrid':2,'organic_kaiju':17,'anti_mecha_squad':4,'hover_vehicle':5,
            'installation':6,'fighter':2,'capital_craft':4,'hover_drone':1,'submarine':1,'gunship':2}
MOVING_ROLES={'head','hip','knee','foot','arm','tail','wing','rotor','turret','barrel','suspension','jaw','scanner'}
# Preserve authored functional equipment/creature appendages while allowing a
# new construction or number of rigid armor groups around the same mechanisms.
FUNCTION_ROLES={'tail','wing','rotor','turret','barrel','jaw','scanner'}


def topology(id_,mesh):
    vertices=mesh['vertices'];neighbors=collections.defaultdict(set);edges=collections.Counter();directed=collections.Counter();used=set()
    for face in mesh['faces']:
        ids=face['indices']
        check(len(ids)==3 and len(set(ids))==3 and all(isinstance(i,int)and 0<=i<len(vertices)for i in ids),f'{id_}: invalid triangle')
        used.update(ids);a,b,c=(vertices[i]for i in ids)
        ab=[b[i]-a[i]for i in range(3)];ac=[c[i]-a[i]for i in range(3)]
        cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
        check(sum(n*n for n in cross)>1e-14,f'{id_}: degenerate triangle')
        check(face.get('texture')=='psx' and len(face['uv'])==3,f'{id_}: missing atlas material')
        check(all(len(uv)==2 and all(math.isfinite(n)and 0<=n<=1 for n in uv)for uv in face['uv']),f'{id_}: invalid UV')
        for i in range(3):
            a,b=ids[i],ids[(i+1)%3];edges[tuple(sorted((a,b)))]+=1;directed[(a,b)]+=1
            neighbors[a].add(b);neighbors[b].add(a)
    check(used==set(range(len(vertices))),f'{id_}: loose vertices')
    check(set(edges.values())=={2},f'{id_}: open/nonmanifold components')
    check(all(directed[(b,a)]==n for (a,b),n in directed.items()),f'{id_}: inconsistent winding')
    unseen=set(used);components=0
    while unseen:
        group={unseen.pop()};pending=list(group)
        while pending:
            for vertex in neighbors[pending.pop()]:
                if vertex in unseen:unseen.remove(vertex);group.add(vertex);pending.append(vertex)
        volume=0
        for face in mesh['faces']:
            if face['indices'][0]not in group:continue
            a,b,c=(vertices[i]for i in face['indices'])
            volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6
        check(volume>1e-12,f'{id_}: inverted/zero-volume component');components+=1
    return components


def rig_check(id_,mesh,rig,baseline):
    check(rig['mesh_vertices']==len(mesh['faces'])*3,f'{id_}: rig vertex count mismatch')
    check(math.isfinite(rig['floor']),f'{id_}: invalid floor')
    covered=set();roles=collections.Counter()
    for index,part in enumerate(rig['parts']):
        check(-1<=part['parent']<index,f'{id_}: invalid hierarchy')
        roles[part['role']]+=1
        check(part['role']in MOVING_ROLES|{'rigid','torso'},f'{id_}: invalid joint role')
        if 'rotation_axis' in part:
            check(part['rotation_axis'] in ('x','y','z') and part['role'] in ('rotor','wing','jaw','tail'),f'{id_}: invalid authored hinge axis')
        for key in ('pivot','low','high'):
            check(len(part[key])==3 and all(math.isfinite(n)for n in part[key]),f'{id_}: invalid {key}')
        check(all(a<=b for a,b in zip(part['low'],part['high'])),f'{id_}: inverted part bounds')
        check(part['ranges'],f'{id_}: empty rigid part')
        for first,count in part['ranges']:
            check(first>=0 and count>0 and first%3==count%3==0 and first+count<=len(mesh['faces'])*3,f'{id_}: invalid range')
            values=set(range(first,first+count));check(not covered&values,f'{id_}: overlapping ranges');covered|=values
            for triangle in range(first//3,(first+count)//3):
                for vertex in mesh['faces'][triangle]['indices']:
                    check(all(lo-1e-5<=n<=hi+1e-5 for n,lo,hi in zip(mesh['vertices'][vertex],part['low'],part['high'])),f'{id_}: triangle escapes rigid culling bounds')
    check(len(covered)==len(mesh['faces'])*3,f'{id_}: unbound geometry')
    old_roles=collections.Counter(part['role']for part in baseline['parts'])
    for role in FUNCTION_ROLES:
        check(roles[role]>=old_roles[role],f'{id_}: lost functional {role} articulation')
    check(set(roles)&MOVING_ROLES,f'{id_}: static enemy lacks appropriate animation')
    if id_=='abyssal_kaiju':
        # The approved anatomy replaces placeholder land support with a radial
        # aquatic body and independently segmented swimming tentacles. Only this
        # swimmer may intentionally drop historical contact planting.
        check(rig.get('motion_profile')=='serpent',f'{id_}: aquatic exception requires serpent movement')
        check(not any(p.get('ground')or p['role']=='foot'for p in rig['parts']),f'{id_}: swimmer has artificial sole contacts')
        check(roles['foot']==roles['hip']==roles['knee']==0,f'{id_}: swimmer grew artificial land legs')
        check(roles['tail']>=8 and roles['head']>=1 and roles['jaw']>=1,f'{id_}: swimmer lacks segmented tentacles or separate head/jaw')
        tentacle_parts=[p for p in rig['parts']if p['role']=='tail']
        check(all(p['parent']>=0 for p in tentacle_parts),f'{id_}: tentacle segments are detached from the aquatic body')
    elif any(p.get('ground')or p['role']=='foot'for p in baseline['parts']):
        check(any(p.get('ground')or p['role']=='foot'for p in rig['parts']),f'{id_}: lost authored ground contacts')
    return dict(roles)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path);args=parser.parse_args()
    rows=read(ASSETS/'enemy-redesign-budgets.json')['assets']
    check(len(rows)==len({row['id']for row in rows})==44,'Expected44 unique remaining enemies')
    check(collections.Counter(row['category']for row in rows)==CATEGORIES,'Remaining enemy category scope changed')
    check(sum(row['budget']for row in rows)==32522,'Authorized aggregate enemy cap changed')
    rigs=current_rigs(read(ASSETS/'models/enemy-redesign-rigs.json')['rigs'])
    check(set(rigs)=={row['id']for row in rows},'Dedicated enemy rig scope must be exactly44')
    source_rigs={}
    for name in ('enemy-animation-rigs.json','manual-animation-rigs.json'):
        source_rigs.update(read(ASSETS/'models'/name)['rigs'])
    atlas_tiles={tile['name']for tile in read(ASSETS/'textures/manifest.json')['tiles']}
    geometries=set();reports=[]
    for row in rows:
        id_=row['id'];baseline_path=ASSETS/row['baseline_file'];baseline=read(baseline_path)
        check(hashlib.sha256(baseline_path.read_bytes()).hexdigest()==row['baseline_sha256'],f'{id_}: immutable baseline changed')
        check(len(baseline['faces'])==row['baseline_triangles'],f'{id_}: baseline count changed')
        multiplier=2 if row['category']in ('organic_kaiju','kaiju_hybrid')else 1
        check(row['budget_multiplier']==multiplier and row['budget']==row['baseline_budget']*multiplier,f'{id_}: unauthorized cap increase')
        mesh=read(ASSETS/row['file']);gpu=read(ASSETS/'models'/f'{id_}.json')
        check(mesh['id']==id_ and mesh['budget']==row['budget'] and 0<len(mesh['faces'])<=row['budget'],f'{id_}: budget/identity mismatch')
        check(mesh['pivot']==baseline['pivot']==row['pivot'],f'{id_}: original pivot changed')
        check(all(len(v)==3 and all(math.isfinite(n)for n in v)for v in mesh['vertices']),f'{id_}: invalid positions')
        for actual,old in zip(bounds(mesh['vertices']),bounds(baseline['vertices'])):check(close(actual,old),f'{id_}: collision envelope changed')
        geometry=json.dumps([mesh['vertices'],[f['indices']for f in mesh['faces']]],separators=(',',':'))
        old_geometry=json.dumps([baseline['vertices'],[f['indices']for f in baseline['faces']]],separators=(',',':'))
        check(geometry!=old_geometry and geometry not in geometries,f'{id_}: stand-in or duplicate geometry');geometries.add(geometry)
        check(len(gpu['vertices'])==len(mesh['vertices'])and gpu['faces']==mesh['faces'],f'{id_}: native/GPU topology/UV differs')
        for a,b in zip(mesh['vertices'],gpu['vertices']):check(close([n*100 for n in a],b,1e-3),f'{id_}: GPU scale/axis mismatch')
        check(all(f.get('texture_tile')in atlas_tiles for f in mesh['faces']),f'{id_}: atlas tile name missing')
        profile=rigs[id_].get('motion_profile')
        expected={'fighter':'flight','gunship':'flight','capital_craft':'naval','submarine':'naval',
                  'installation':'stationary','hover_vehicle':'hover','hover_drone':'hover','anti_mecha_squad':'infantry'}.get(row['category'])
        if id_.removesuffix('_alt') in ('anti_mecha_hovertank','heavy_hover_tank'):expected='tracked'
        check(profile==expected if expected else profile in ('organic','insect','serpent'),f'{id_}: missing/inappropriate explicit motion profile')
        components=topology(id_,mesh);roles=rig_check(id_,mesh,rigs[id_],source_rigs[id_])
        reports.append({'id':id_,'category':row['category'],'archetype':rigs[id_]['archetype'],
                        'motion_profile':profile,'triangles':len(mesh['faces']),'budget':row['budget'],'baseline_triangles':len(baseline['faces']),
                        'closed_components':components,'roles':roles,'authored_hinge_axes':{p['name']:p['rotation_axis']for p in rigs[id_]['parts']if 'rotation_axis'in p},
                        'panel_diagnostics':panel_diagnostics(mesh)if multiplier==1 else None})
    preservation=read(ROOT/'tests/fixtures/mecha_redesign_preserved.json')
    check(len(preservation['files'])==659,'Expected455 unchanged pins+74 archived source pins+130 prior mecha pins')
    for path,sha in preservation['files'].items():check(hashlib.sha256(historical_source(ROOT/path).read_bytes()).hexdigest()==sha,f'Preserved source/prior mecha changed: {path}')
    protected_metadata=preservation['protected_previous_mecha_metadata']
    check(set(protected_metadata)=={'assets/mecha/models/mecha-redesign-player-rigs.json','assets/mecha/models/mecha-redesign-enemy-rigs.json','art/mecha/mecha-redesign/mecha-redesign.blend','assets/mecha/mecha-redesign-assets.json'},'Prior65 source/rig metadata protection was removed')
    for path,sha in protected_metadata.items():
        check(hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,f'Protected prior65 rig/source metadata changed: {path}')
    for path,aliases in preservation['protected_alias_records'].items():
        models=read(ROOT/path)['models']
        for name,expected in aliases.items():check(all(models[name].get(k)==v for k,v in expected.items()),f'Original IS alias routing changed: {name}')
    result={'schema':1,'assets':reports,'triangles':sum(row['triangles']for row in reports),
            'preserved_sources':659,'prior_mecha_geometry_files':130,'prior_mecha_metadata_files':4,'anatomy_exceptions':{'abyssal_kaiju':'Radial aquatic serpent: no land contacts or legs; at least8 independently articulated tentacle segments and separate head/jaw.'},'visual_review_required':True}
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(f"44 redesigned enemies: {result['triangles']} triangles,19 doubled organic/hybrid caps,25 unchanged caps, functional rigid parts, positive closed geometry, atlas and659 source/prior-mecha pins verified")

if __name__=='__main__':main()
