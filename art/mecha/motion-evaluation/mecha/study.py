"""Retarget source timing to the Ace's published pivots; run from any directory."""
import argparse
import json
import math
from pathlib import Path
import shlex
import subprocess
import tempfile

import numpy as np
from scipy.interpolate import PchipInterpolator
from scipy.spatial.transform import Rotation
from render import render, export_glb

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
FPS = 30
PERIOD = 1.6
STRIDE = 5.4
WALK_END = 3 * PERIOD
RECOVER = 6.35
FRAMES = 237


def clamp(x):
    return np.clip(x, 0, 1)


def ease(x):
    x = clamp(x)
    return x*x*(3-2*x)


def distance(cycle):
    # Slow for the last full stance pair. A half cosine preserves C1 root travel.
    c = min(cycle, 3)
    if c < 2:
        return STRIDE*c
    u = c-2
    return STRIDE*(2 + u/2 + math.sin(math.pi*u)/(2*math.pi))


def move(p=(0,0,0), rotation=None):
    m = np.eye(4)
    if rotation is not None:
        m[:3,:3] = rotation.as_matrix()
    m[:3,3] = p
    return m


def around(pivot, rotation):
    return move(pivot, rotation) @ move(-pivot)


def point(matrix, pos):
    return matrix[:3,:3] @ pos + matrix[:3,3]


def unit(x):
    return x / np.linalg.norm(x)


def align(a, b):
    a, b = unit(a), unit(b)
    axis = np.cross(a, b)
    return Rotation.from_quat([*axis, 1+np.dot(a,b)])


def leg_chain(hip, knee, ankle, hip_target, ankle_target):
    upper, lower = knee-hip, ankle-knee
    a, b = np.linalg.norm(upper), np.linalg.norm(lower)
    axis = unit(ankle_target-hip_target)
    reach = np.linalg.norm(ankle_target-hip_target)
    if not abs(a-b)<reach<a+b:
        raise ValueError(f'Unreachable ankle, distance {reach} exceeds {a+b}')
    along = (reach*reach + a*a - b*b)/(2*reach)
    pole = upper-unit(ankle-hip)*np.dot(upper,unit(ankle-hip))
    bend = unit(pole-axis*np.dot(pole,axis))
    joint = hip_target + along*axis + np.sqrt(max(0, a*a-along*along))*bend
    return (move(hip_target,align(upper,joint-hip_target))@move(-hip),
            move(joint,align(lower,ankle_target-joint))@move(-knee))


def load_mesh(root):
    model = json.loads((root/'meshes/mecha_ace.json').read_text())
    rig = json.loads((root/'models/enemy-reference-rigs.json').read_text())['rigs']['mecha_ace']
    parts = rig['parts']
    owner = np.full(len(model['faces']),-1)
    for i, part in enumerate(parts):
        for start, count in part['ranges']:
            owner[start//3:(start+count)//3] = i
    assert np.all(owner>=0)
    owners = owner.copy()
    # Keep connected armour components together when separating the forearms.
    vertices = np.array(model['vertices'])
    faces = np.array([f['indices'] for f in model['faces']])
    index = {part['name']: i for i,part in enumerate(parts)}
    for side in (-1,1):
        arm = index[f'arm_{side}']
        forearm = len(parts)
        elbow_panel = []
        for section in model['components']:
            if section['semantic']!=f'arm_{side}':
                continue
            selection = slice(section['first'],section['first']+section['count'])
            xyz = vertices[faces[selection]]
            if xyz[:,:,1].max() < 8:
                owners[selection] = forearm
                if not elbow_panel:
                    elbow_panel = xyz.reshape(-1,3).tolist()
        elbow_panel = np.array(elbow_panel)
        pivot = np.mean(elbow_panel[elbow_panel[:,1] > elbow_panel[:,1].max()-.5],axis=0)
        pivot[1] = 7.22
        parts.append(dict(name=f'forearm_{side}',parent=arm,pivot=pivot.tolist()))
        if side==1:
            parts[index['barrel_1']]['parent'] = forearm
    return model, parts, owner, owners


def baseline(build, root, model, inputs):
    cmds = subprocess.check_output(['ninja','-t','commands','mecha-render-probe'],cwd=build,text=True).splitlines()
    cc = shlex.split(next(line for line in cmds if ' -c ' in line and '/tools/mecha_preview/main.cpp' in line))
    link = shlex.split(cmds[-1])
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        exe = path/'sample'
        subprocess.run(cc[:cc.index('-MD')]+[HERE/'baseline.cpp','-o',exe]+link[link.index('libmecha-renderer.a'):link.index('&&',2)],cwd=build,check=True)
        request = dict(vertices=len(model['faces'])*3,frames=inputs)
        (path/'input.json').write_text(json.dumps(request))
        raw = np.array(json.loads(subprocess.check_output([exe,root/'models',path/'input.json'])))
    matrices = np.tile(np.eye(4),(len(inputs),raw.shape[1],1,1))
    for i, frame in enumerate(raw):
        matrices[i,:,:3,:3] = Rotation.from_quat(frame[:,:4]).as_matrix()
        matrices[i,:,:3,3] = frame[:,4:7]
    return matrices


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=REPO/'build-mecha')
    p.add_argument('--output',type=Path,default=HERE)
    args = p.parse_args()
    root = args.build.resolve()/'game-assets/mecha'
    out = args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    model,parts,old_owner,owner = load_mesh(root)
    pivots = {p['name']:np.array(p['pivot']) for p in parts}
    index = {p['name']:i for i,p in enumerate(parts)}
    gait = json.loads((HERE.parent/'kimodo-gait.json').read_text())['sides']
    clearances = {s:PchipInterpolator(np.linspace(0,1,len(v['swing_clearance'])),v['swing_clearance'])
                  for s,v in gait.items()}
    bracing = json.loads((HERE.parent/'reference/brace.json').read_text())
    sway = np.array(json.loads((HERE.parent/'reference/kimodo-sway.json').read_text())['lateral'])
    sway_phase = np.linspace(0,1,len(sway))
    def learned(channel,time):
        progress = (time-WALK_END)/.95 if time<RECOVER else 1-(time-RECOVER)/1.15
        return float(np.interp(clamp(progress),np.linspace(0,1,len(bracing[channel])),bracing[channel]))
    track = []
    inputs = []
    contacts = []
    for n in range(FRAMES):
        t = n/FPS
        c = t/PERIOD
        d = distance(c)
        speed = (distance(c+.0001)-d)/(.0001*PERIOD)
        depth = learned('depth',t) if t>=WALK_END else 0.
        shoulder = learned('shoulder',t) if t>=WALK_END else 0.
        inputs.append([t,d,float(speed),depth])
        breathing = math.sin(2*math.pi*c)
        moving = 1-ease(c-2)
        weight = -.23*float(np.interp(c%1,sway_phase,sway))*moving
        lean = .045*moving+.16*depth
        world = np.tile(np.eye(4),(len(parts),1,1))
        world[index['pelvis']] = move([weight,-.30-.095*math.cos(4*math.pi*c)*moving-.86*depth,0])
        pelvis = world[index['pelvis']]
        torso = pelvis @ around(pivots['torso'],Rotation.from_euler('xyz',[lean,.11*breathing*moving,-weight*.17]))
        world[index['torso']] = torso
        world[index['head']] = torso @ around(pivots['head'], Rotation.from_euler('xyz',[-lean*.85,-.07*breathing*moving,weight*.11]))
        contact = []
        for side,name,offset in ((-1,'left',0),(1,'right',18/35)):
            cycle = math.floor(c-offset)
            phase = c-offset-cycle
            duty = gait[name]['contact_fraction']
            start = cycle+offset
            anchor = distance(start+duty*.5)
            end = distance(start+1+duty*.5)
            swing = float(clamp((phase-duty)/(1-duty)))
            # At the final step shorten the swing height as the root brakes.
            span = end-anchor
            lift = float(clearances[name](swing))*.94*min(1,span/2.2)
            # The G1 sample rolls onto its toe as its ankle descends early.
            # This chassis has a rigid shoe; finish the forward pass before
            # that low-clearance interval to avoid dragging the whole sole.
            foot_z = -(anchor + ease(swing/.79)*span-d)
            hip,knee,ankle = [pivots[f'{role}_{side}'] for role in ('thigh','shin','foot')]
            target = ankle + [0,lift,foot_z]
            a,b = leg_chain(hip,knee,ankle,point(pelvis,hip),target)
            world[index[f'thigh_{side}']] = a
            world[index[f'shin_{side}']] = b
            world[index[f'foot_{side}']] = move(target-ankle)
            contact.append(phase<=duty or span<1e-6)
            arm_name = f'arm_{side}'
            arm = torso @ around(pivots[arm_name], Rotation.from_euler('xyz',
                [-side*.235*breathing*moving+.27*shoulder,-side*.11*shoulder,side*.13*shoulder]))
            elbow_name = f'forearm_{side}'
            forearm = arm @ around(pivots[elbow_name],Rotation.from_euler('x',.13+.14*moving*(1+side*breathing)+.66*shoulder))
            world[index[arm_name]] = arm
            world[index[elbow_name]] = forearm
            if side==1:
                world[index['barrel_1']] = forearm
        contacts.append(contact)
        track.append(world)
    track = np.array(track)
    live = baseline(args.build.resolve(),root,model,inputs)
    feet = [index['foot_-1'],index['foot_1']]
    def metrics(frames, owners):
        verts = np.array(model['vertices'])
        faces = np.array([f['indices'] for f in model['faces']])
        slips,grounded_speeds,penetration,gaps = [],[],[],[]
        for side,i in enumerate(feet):
            sole = verts[np.unique(faces[owners==i])]
            sole = sole[sole[:,1]<=sole[:,1].min()+1e-5]
            world = np.einsum('nij,kj->nki',frames[:,i,:3,:3],sole)+frames[:,i,None,:3,3]
            world[:,:,2] -= np.array(inputs)[:,None,1]
            active = np.array(contacts)[:,side]
            consecutive = active[1:] & active[:-1]
            sole_speed = np.linalg.norm(np.diff(world.mean(axis=1)[:,[0,2]],axis=0),axis=1)*FPS
            slips.extend(sole_speed[consecutive])
            # A second measure detects each controller's own full-sole contacts.
            down = world[:,:,1].max(axis=1)<.025
            walking = np.array(inputs)[1:,2]>.2
            grounded_speeds.extend(sole_speed[down[1:] & down[:-1] & walking])
            penetration.extend(np.maximum(0,-world[:,:,1].min(axis=1)))
            gaps.extend(world[active,:,1].min(axis=1))
        return dict(observed_ground_contact_intervals=len(grounded_speeds),
                    observed_ground_contact_rms_speed_mps=round(float(np.sqrt(np.mean(np.square(grounded_speeds)))),4),
                    stance_rms_horizontal_speed_mps=round(float(np.sqrt(np.mean(np.square(slips)))),4),
                    max_sole_penetration_m=round(float(max(0.,max(penetration))),5),
                    max_scheduled_contact_gap_m=round(float(max(gaps)),5))
    report = dict(model='mecha_ace',fps=FPS,frames=FRAMES,triangles=len(model['faces']),
                  live_rigid_parts=live.shape[1],study_rigid_parts=len(parts),
                  baseline=metrics(live,old_owner),prototype=metrics(track,owner))
    assert np.isfinite(track).all()
    assert report['prototype']['stance_rms_horizontal_speed_mps']<.001
    assert report['prototype']['max_sole_penetration_m']<.001
    (out/'metrics.json').write_text(json.dumps(report,indent=2)+'\n')
    export_glb(out/'mecha-ace.glb',model,parts,owner,track,inputs,root,FPS)
    render(out,root,model,old_owner,owner,live,track,inputs,FPS)

if __name__=='__main__':
    main()
