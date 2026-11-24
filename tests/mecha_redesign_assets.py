#!/usr/bin/env python3
"""Independent structural gate for the 65-model redesign; visual approval stays human."""
import argparse
import collections
import hashlib
import json
import math
from mecha_refinement_support import historical_source,current_rigs
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/mecha'

def read(path):
    return json.loads(path.read_text())

def check(condition, message):
    if not condition:
        raise AssertionError(message)

def bounds(vertices):
    return [[fn(p[i] for p in vertices) for i in range(3)] for fn in (min, max)]

def close(a, b, tolerance=1e-5):
    return len(a) == len(b) and all(abs(x-y) <= tolerance for x, y in zip(a,b))

def panel_diagnostics(mesh):
    # Edge-connected coplanar faces form manufactured panels; report their area
    # rather than pretending a triangle count proves the desired visual language.
    normals=[];areas=[];edges=collections.defaultdict(list)
    for index,face in enumerate(mesh['faces']):
        ids=face['indices'];a,b,c=(mesh['vertices'][i] for i in ids)
        ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
        normal=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
        length=math.sqrt(sum(v*v for v in normal));normals.append([v/max(length,1e-30) for v in normal]);areas.append(length*.5)
        for n in range(3):edges[tuple(sorted((ids[n],ids[(n+1)%3])))].append(index)
    planar=set()
    for faces in edges.values():
        if len(faces)==2 and sum(a*b for a,b in zip(normals[faces[0]],normals[faces[1]]))>math.cos(math.radians(.25)):
            planar.update(faces)
    total=sum(areas)
    return {'coplanar_neighbor_surface_fraction':sum(areas[i] for i in planar)/total,
            'axis_aligned_surface_fraction':sum(area for area,normal in zip(areas,normals) if max(abs(v) for v in normal)>.985)/total,
            'method':'Area weighted native triangles sharing an edge with normals within0.25degrees; axis fraction within9.94degrees. Diagnostic only: human review checks broad coherent panels, wedges, box limbs and faceted hinges.'}

def rig_check(id_, rig, mesh, player):
    parts = rig['parts']
    covered = set()
    roles = collections.Counter()
    tracked = collections.Counter()
    for index, part in enumerate(parts):
        check(-1 <= part['parent'] < index, f'{id_}: invalid parent ordering')
        roles[part['role']] += 1
        for key in ('pivot','low','high'):
            check(len(part[key]) == 3 and all(math.isfinite(v) for v in part[key]), f'{id_}: invalid {key}')
        for first, count in part['ranges']:
            check(first >= 0 and count > 0 and first%3 == 0 and count%3 == 0 and first+count <= len(mesh['faces'])*3,
                  f'{id_}: invalid triangle range')
            values = set(range(first, first+count))
            check(not (covered & values), f'{id_}: overlapping rigid membership')
            covered.update(values)
        if player and 'tracked_segment' in part:
            check(part.get('arm') in (0,1), f'{id_}: tracked segment without hand')
            tracked[(part['arm'],part['tracked_segment'])] += len(part['ranges'])
    check(len(covered) == len(mesh['faces'])*3, f'{id_}: incomplete rigid partition')
    check(roles['head'] and roles['torso'], f'{id_}: separate head and torso required')
    if player:
        for role in ('left-hip','left-knee','left-foot','right-hip','right-knee','right-foot'):
            check(roles[role], f'{id_}: missing {role}')
        for hand in (0,1):
            for segment in ('shoulder','upper-arm','forearm'):
                check(tracked[(hand,segment)], f'{id_}: missing tracked hand {hand} {segment}')
        for p in parts:
            if p['role'] == 'head':
                check(p['parent'] >= 0 and parts[p['parent']]['role'] == 'torso', f'{id_}: head must inherit torso')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,help='Write geometric readability diagnostics; human image/reference review remains mandatory')
    args=parser.parse_args()
    diagnostics=[]
    manifest = read(ASSETS/'mecha-redesign-budgets.json')
    rows = manifest['assets']
    check(manifest['schema'] == 1 and manifest['units'] == 'triangles', 'Invalid audited budget contract')
    check(len(rows) == len({r['id'] for r in rows}) == 65, 'Expected 65 unique redesign IDs')
    check(collections.Counter(r['category'] for r in rows) == {'player':8,'core':40,'manual':16,'form':1}, 'Redesign scope changed')
    player_rigs = read(ASSETS/'models/mecha-redesign-player-rigs.json')['rigs']
    enemy_rigs = current_rigs(read(ASSETS/'models/mecha-redesign-enemy-rigs.json')['rigs'])
    check(set(player_rigs) == {r['id'] for r in rows if r['category']=='player'}, 'Player rig scope differs')
    check(set(enemy_rigs) == {r['id'] for r in rows if r['category']!='player'}, 'Enemy rig scope differs')
    triangles = 0
    geometries = set()
    for row in rows:
        id_ = row['id']
        baseline_path = ASSETS/row['baseline_file']
        check(hashlib.sha256(baseline_path.read_bytes()).hexdigest() == row['baseline_sha256'], f'{id_}: immutable baseline changed')
        old = read(baseline_path)
        mesh = read(ASSETS/row['file'])
        gpu = read(ASSETS/'models'/f'{id_}.json')
        check(row['budget'] == row['baseline_budget']*2, f'{id_}: cap must exactly double audited old cap')
        check(len(old['faces']) == row['baseline_triangles'], f'{id_}: baseline triangle count changed')
        check(0 < len(mesh['faces']) <= row['budget'], f'{id_}: triangle cap exceeded')
        check(mesh['budget'] == row['budget'], f'{id_}: embedded cap differs from authorization')
        check(mesh['pivot'] == old['pivot'], f'{id_}: collision pivot changed')
        vertices = mesh['vertices']
        check(all(len(v)==3 and all(math.isfinite(n) for n in v) for v in vertices), f'{id_}: nonfinite positions')
        for a,b in zip(bounds(vertices),bounds(old['vertices'])):
            check(close(a,b), f'{id_}: approved collision envelope changed')
        check(vertices != old['vertices'] or mesh['faces'] != old['faces'], f'{id_}: unchanged stand-in')
        geometry = json.dumps([vertices,[f['indices'] for f in mesh['faces']]],separators=(',',':'))
        check(geometry not in geometries, f'{id_}: duplicated geometry')
        geometries.add(geometry)
        check(len(gpu['vertices'])==len(vertices) and gpu['faces']==mesh['faces'], f'{id_}: native/GPU topology or UV mismatch')
        for a,b in zip(vertices,gpu['vertices']):
            check(close([v*100 for v in a],b,1e-3), f'{id_}: native/GPU scale mismatch')
        edges = collections.Counter()
        directed_edges=collections.Counter()
        neighbors=collections.defaultdict(set)
        used=set()
        for face in mesh['faces']:
            indices=face['indices']
            check(len(indices)==3 and len(set(indices))==3 and all(0<=i<len(vertices) for i in indices), f'{id_}: invalid triangle')
            used.update(indices)
            a,b,c=(vertices[i] for i in indices)
            ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
            cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
            check(sum(v*v for v in cross)>1e-14, f'{id_}: degenerate triangle')
            check(len(face['uv'])==3 and all(len(uv)==2 and all(math.isfinite(v) and 0<=v<=1 for v in uv) for uv in face['uv']), f'{id_}: invalid atlas UV')
            for n in range(3):
                start,end=indices[n],indices[(n+1)%3]
                edges[tuple(sorted((start,end)))]+=1
                directed_edges[(start,end)]+=1
                neighbors[start].add(end);neighbors[end].add(start)
        check(used==set(range(len(vertices))), f'{id_}: loose vertices')
        check(set(edges.values())=={2}, f'{id_}: open/nonmanifold primitive topology')
        check(all(directed_edges[(b,a)]==count for (a,b),count in directed_edges.items()), f'{id_}: inconsistent surface winding')
        unseen=set(used)
        while unseen:
            connected={unseen.pop()};pending=list(connected)
            while pending:
                for vertex in neighbors[pending.pop()]:
                    if vertex in unseen:unseen.remove(vertex);connected.add(vertex);pending.append(vertex)
            signed_volume=0
            for face in mesh['faces']:
                if face['indices'][0] not in connected:continue
                a,b,c=(vertices[i] for i in face['indices'])
                signed_volume+=(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6
            check(signed_volume>1e-12, f'{id_}: inverted or zero-volume closed component')
        authored_rig=(player_rigs if row['category']=='player' else enemy_rigs)[id_]
        rig_check(id_,authored_rig,mesh,row['category']=='player')
        lo,hi=bounds(vertices)
        height=hi[1]-lo[1]
        heads=[p for p in authored_rig['parts'] if p['role']=='head']
        torsos=[p for p in authored_rig['parts'] if p['role']=='torso']
        head_height=sum(p['high'][1]-p['low'][1] for p in heads)
        torso_width=max((p['high'][0]-p['low'][0] for p in torsos),default=0)
        diagnostics.append({'id':id_,'category':row['category'],'triangles':len(mesh['faces']),
            'baseline_triangles':len(old['faces']),'budget':row['budget'],'height_m':height,
            'front_width_m':hi[0]-lo[0],'side_depth_m':hi[2]-lo[2],
            'head_height_at_160px':160*head_height/height,'torso_width_at_160px':160*torso_width/height,
            'rigid_parts':len(authored_rig['parts']),'manufactured_panel_diagnostics':panel_diagnostics(mesh),
            'visual_review':'Compare native gallery bind, walk-turn and aim-attack at160px against archived baseline and approved mecha references; diagnostics do not establish silhouette quality.'})
        triangles+=len(mesh['faces'])
    preservation=read(ROOT/'tests/fixtures/mecha_redesign_preserved.json')
    preserved=preservation['files']
    for path,sha in preservation.get('protected_previous_mecha_metadata',{}).items():
        check(hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,f'Protected prior65 rig/source metadata changed: {path}')
    for path,aliases in preservation['protected_alias_records'].items():
        current=read(ROOT/path)['models']
        for alias,expected in aliases.items():
            check(alias in current, f'Protected original IS model alias removed: {alias}')
            for field,value in expected.items():
                check(current[alias].get(field)==value, f'Protected original IS alias {alias} changed {field}; historical body routing/count/cap must remain unchanged')
    for path,expected in preserved.items():
        check(hashlib.sha256(historical_source(ROOT/path).read_bytes()).hexdigest()==expected, f'Out-of-scope original/source mesh changed: {path}')
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps({'schema':1,'assets':diagnostics,'preserved_source_geometry':len(preserved)},indent=2)+'\n')
    print(f'65 redesigned meshes: {triangles} triangles, authored rigid partitions, independent head/limbs, native/GPU parity and {len(preserved)} preserved source assets verified')

if __name__ == '__main__':
    main()
