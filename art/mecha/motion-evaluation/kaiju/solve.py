"""Bake a constrained four-legged contact schedule on the published Stalker's native joints.

Run in Blender's background Python. Native coordinates are metres with Y up and -Z forward.
"""
import json
import math
import subprocess
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
SOURCE = REPO / "art/mecha/voidmaw-redesign-study/stalker"
WORK = Path(sys.argv[sys.argv.index("--") + 1])
WORK.mkdir(exist_ok=True, parents=True)
FPS = 24
COUNT = 157
rig = json.loads((SOURCE / "rigs.json").read_bytes())["rigs"]["kaiju_stalker"]["parts"]
model = json.loads((SOURCE / "kaiju_stalker.json").read_bytes())
piv = [Vector(part["pivot"]) for part in rig]
identity = Quaternion()
legs = [i for i, part in enumerate(rig) if part["role"] == "hip"]

def smooth(x):
    x = min(1., max(0., x))
    return x*x*(3-2*x)

def envelope(t, start, peak, end):
    return smooth((t-start)/(peak-start))*(1-smooth((t-peak)/(end-peak)))

def distance(t):
    return 8.3*smooth((t-.25)/3.0)

def offsets(t):
    u = max(0., min(1., (t-.25)/3.))
    return dict(t=t, distance=distance(t), speed=8.3/3.*6*u*(1-u))

inputs = [offsets(i/FPS) for i in range(COUNT)]
(WORK/"inputs.json").write_text(json.dumps(inputs))
subprocess.run([str(WORK/"sample-live"), str(REPO/"build-mecha/game-assets/mecha"),
                str(WORK/"inputs.json"), str(WORK/"live.json")], check=True)
live = json.loads((WORK/"live.json").read_bytes())

# Each foot has stationary world-space holds separated by smooth swing arcs.
# Footfalls progress around the body, preserving two or more supports.
contacts = []
for stagger in (0, .36, .54, .18):
    starts = [.25+stagger+i*.78 for i in range(4)]
    ends = [start+.34 for start in starts]
    marks = [0.] + [distance((end+start)*.5) for end,start in zip(ends,starts[1:])] + [distance(4.)]
    contacts.append(list(zip(starts,ends,marks,marks[1:])))

def foot_target(n,t):
    z = 0.
    lift = 0.
    planted = True
    for start,end,a,b in contacts[n]:
        if t < start:
            break
        z = b
        if t < end:
            u = (t-start)/(end-start)
            z = a+(b-a)*smooth(u)
            lift = .85*math.sin(math.pi*u)**2 * min(1.,abs(b-a)/.5)
            planted = False
            break
    return piv[legs[n]+2] + Vector((0,lift,-z)), planted

def around(pivot, rotation, shift=Vector((0,0,0))):
    return Matrix.Translation(shift+pivot) @ rotation.to_matrix().to_4x4() @ Matrix.Translation(-pivot)

def xyz(x=0,y=0,z=0):
    return Quaternion((0,1,0),y) @ Quaternion((1,0,0),x) @ Quaternion((0,0,1),z)

def solve_legs(out, t):
    contacts_now = []
    for n,h in enumerate(legs):
        k,f = h+1,h+2
        anchor, planted = foot_target(n,t)
        contacts_now.append(planted)
        hip = out[0] @ piv[h]
        upper, lower = piv[k]-piv[h], piv[f]-piv[k]
        axis = (anchor-hip).normalized()
        reach = (anchor-hip).length
        if not abs(upper.length-lower.length) < reach < upper.length+lower.length:
            raise RuntimeError(f"Unreachable {rig[f]['name']} at {t:.3f}")
        pole = piv[k]-piv[h]
        bend = (pole-axis*pole.dot(axis)).normalized()
        run = (upper.length_squared-lower.length_squared+reach**2)/(2*reach)
        knee = hip + axis*run + bend*math.sqrt(max(0,upper.length_squared-run**2))
        # Preserve segment lengths, using the outward rest knee as the guide.
        out[h] = around(piv[h],upper.rotation_difference(knee-hip),hip-piv[h])
        out[k] = around(piv[k],lower.rotation_difference(anchor-knee),knee-piv[k])
        out[f] = Matrix.Translation(anchor-piv[f])
    return contacts_now

brace = json.loads((HERE.parent/"reference/brace.json").read_text())["depth"]

def loading(t):
    phase = min(1.,max(0.,(t-3.38)/(4.47-3.38)))*(len(brace)-1)
    i = min(int(phase),len(brace)-2)
    return (brace[i]+(brace[i+1]-brace[i])*(phase-i))*(1-smooth((t-4.47)/.29))

def sample(t):
    d, speed = distance(t), offsets(t)["speed"]
    coil = loading(t)
    snap = envelope(t,4.46,4.83,5.48)
    settle = envelope(t,5.02,5.43,6.19)
    gait = speed/4.15
    beat = math.tau*(t-.25)/.78
    shift = Vector((.27*gait*math.sin(beat), -.15*gait*(1-math.cos(beat))-.89*coil-.46*snap,
                    -d+.68*coil-1.12*snap+.17*settle))
    root = around(piv[0],xyz(x=-.045*coil+.07*snap,z=.028*gait*math.sin(beat)),shift)
    out = [root] + [Matrix.Identity(4) for _ in rig[1:]]
    out[1] = root @ around(piv[1],xyz(x=.13*coil-.19*snap+.07*settle,y=-.09*coil+.13*snap))
    out[2] = out[1] @ around(piv[2],xyz(x=-.13*coil+.20*snap+.035*math.sin(beat*.5)*gait))
    out[3] = out[2] @ around(piv[3],xyz(x=-.36*coil-.66*envelope(t,4.47,4.70,4.99)))
    planted = solve_legs(out,t)
    for n in range(3):
        delay=n*.10
        q=xyz(x=.08*envelope(t-delay,3.33,4.42,5.11)-.15*envelope(t-delay,4.5,4.87,5.66),
              y=.13*gait*math.sin(beat*.5-n*.65)+.25*envelope(t-delay,3.34,4.42,5.10)
              -.38*envelope(t-delay,4.49,4.83,5.73)+.07*envelope(t-delay,5.14,5.75,6.25))
        j = 16+n
        out[j] = out[rig[j]["parent"]] @ around(piv[j],q)
    return out, planted

def transform(row):
    m=Quaternion(row[:4]).to_matrix().to_4x4()
    m.translation=Vector(row[4:])
    return m

def flatten(m):
    return [*m.to_quaternion(),*m.translation]

def validate(frames, planted):
    soles = {}
    for h in legs:
        f=h+2
        idx={v for first,count in rig[f]["ranges"] for face in model["faces"][first//3:(first+count)//3] for v in face["indices"]}
        sole=min((Vector(model["vertices"][i]) for i in idx),key=lambda v:v.y)
        soles[f]=sole
    peak=0.
    distances=[]
    interval=[]
    for n,h in enumerate(legs):
        f=h+2
        last=None
        for frame,mask in zip(frames,planted):
            point = frame[f] @ soles[f]
            peak=max(peak,-point.y)
            if mask[n]:
                if last is not None:
                    interval.append(math.hypot(point.x-last.x,point.z-last.z))
                last=point
            else:
                if interval: distances.append(sum(interval))
                interval=[]
                last=None
        if interval: distances.append(sum(interval))
        interval=[]
    return {"max_world_travel_per_authored_hold_m":round(max(distances),5),
            "mean_world_travel_per_authored_hold_m":round(sum(distances)/len(distances),5),
            "max_sampled_sole_penetration_m":round(peak,5)}

def dump(name,obj):
    (WORK/name).write_text(json.dumps(obj,separators=(",",":")))

proto, masks = zip(*(sample(i/FPS) for i in range(COUNT)))
dump("prototype.json",[[flatten(m) for m in pose] for pose in proto])
dump("contacts.json",masks)
metrics={"fps":FPS,"samples":COUNT,"triangles":len(model["faces"]),"rig_parts":len(rig),
         "min_planted_feet":min(sum(m) for m in masks),"root_travel_m":8.3,
         "live":validate([[transform(row) for row in frame] for frame in live],masks),
         "prototype":validate(proto,masks)}
assert metrics["min_planted_feet"]>=2
assert metrics["prototype"]["max_world_travel_per_authored_hold_m"]<.0001
assert metrics["prototype"]["max_sampled_sole_penetration_m"]<.0001

# Blender review scene. Native transforms are baked back to parent-relative
# location/quaternion channels on the exact published rigid membership.
basis = Matrix.Rotation(math.pi/2,4,'X')
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene
scene.render.engine='BLENDER_WORKBENCH'
shading=scene.display.shading
shading.light='STUDIO'
shading.color_type='VERTEX'
shading.show_shadows=True
shading.show_cavity=True
shading.cavity_type='WORLD'
shading.show_specular_highlight=True
shading.show_object_outline=False
scene.display.render_aa='FXAA'
scene.view_settings.view_transform='Standard'
scene.view_settings.exposure=1.10
shading.shadow_intensity=.22
scene.world=bpy.data.worlds.new('Slate')
shading.background_type='WORLD'
scene.world.color=(.042,.055,.076)
direction=Vector((-.92, .55,.33)).normalized()
right=Vector((0,0,1)).cross(direction).normalized()
cam_data=bpy.data.cameras.new("Orthographic comparison")
cam=bpy.data.objects.new("Paired camera",cam_data)
scene.collection.objects.link(cam)
cam_data.type='ORTHO'
cam_data.ortho_scale=52
scene.camera=cam
look=Vector((0,1.0,7.35))
cam.rotation_euler=(-direction).to_track_quat('-Z','Y').to_euler()
for i,row in enumerate(inputs,1):
    cam.location=look+direction*65+Vector((0,.85*row["distance"],0))
    cam.keyframe_insert(data_path="location",frame=i)
scene.frame_start=1
scene.frame_end=COUNT
scene.render.fps=FPS
scene.render.resolution_x=1000
scene.render.resolution_y=435
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.image_settings.color_mode='RGB'
scene.render.film_transparent=False

# Per-face pigment plus a small sampled atlas variation in Workbench's vertex-colour pass.
image=bpy.data.images.load(str(REPO/"build-mecha/game-assets/mecha/textures/psx-atlas.png"))
image.pack()
import numpy as np
pixels=np.array(image.pixels[:]).reshape((image.size[1],image.size[0],4))
palettes=[]
for face in model["faces"]:
    uv=np.mean(face['uv'],axis=0)
    tex=pixels[int(uv[1]*255),int(uv[0]*255),:3]
    palettes.append([*(np.array(face["color"])/255*(.70+.30*np.sqrt(tex))),1.])

def make_rig(name,frames,side):
    mesh=bpy.data.meshes.new(name)
    pts=[];tris=[];mapping={};owner={}
    for part_index,part in enumerate(rig):
        for first,count in part["ranges"]:
            for n in range(first//3,(first+count)//3):
                owner[n]=part_index
    for i,face in enumerate(model["faces"]):
        tri=[]
        for j in face["indices"]:
            pair=(owner[i],j)
            if pair not in mapping:
                mapping[pair]=len(pts)
                pts.append(basis @ Vector(model['vertices'][j]))
            tri.append(mapping[pair])
        tris.append(tri)
    mesh.from_pydata(pts,[],tris)
    attr=mesh.color_attributes.new(name="Pigment",type='BYTE_COLOR',domain='CORNER')
    mesh.color_attributes.active_color=attr
    uv=mesh.uv_layers.new(name='Native atlas')
    for p,c,f in zip(mesh.polygons,palettes,model["faces"]):
        for loop,coord in zip(p.loop_indices,f["uv"]):
            attr.data[loop].color_srgb=c
            uv.data[loop].uv=coord
    obj=bpy.data.objects.new(name+" mesh",mesh)
    scene.collection.objects.link(obj)
    bones=bpy.data.armatures.new(name)
    arm=bpy.data.objects.new(name,bones)
    scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.object.mode_set(mode='EDIT')
    for idx,part in enumerate(rig):
        bone=bones.edit_bones.new(part['name'])
        point=basis @ piv[idx]
        bone.head=point;bone.tail=point+Vector((0,.6,0))
        if part['parent']>=0:
            bone.parent=bones.edit_bones[rig[part['parent']]['name']]
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.location=right*(side*13.0)
    obj.parent=arm
    mod=obj.modifiers.new("Native articulated parts",'ARMATURE');mod.object=arm
    for j,p in enumerate(rig):
        group=obj.vertex_groups.new(name=p['name'])
        group.add([v for (i,_),v in mapping.items() if i==j],1.,'REPLACE')
    prev=[identity.copy() for _ in rig]
    for f,frame in enumerate(frames,1):
        skin=[basis@m@basis.inverted() for m in frame]
        for j,part in enumerate(rig):
            pose=arm.pose.bones[part['name']]
            rest=pose.bone.matrix_local
            parent=skin[part['parent']] if part['parent']>=0 else Matrix.Identity(4)
            local=rest.inverted()@parent.inverted()@skin[j]@rest
            loc,q,_=local.decompose()
            q.make_compatible(prev[j]);prev[j]=q.copy()
            pose.location=loc;pose.rotation_quaternion=q
            pose.keyframe_insert(data_path='location',frame=f,group=part['name'])
            pose.keyframe_insert(data_path='rotation_quaternion',frame=f,group=part['name'])
    arm.animation_data.action.name=name
    return arm

live_frames=[[transform(row) for row in frame] for frame in live]
live_actor=make_rig('AN_Stalker_Live',live_frames,-1)
actor=make_rig('AN_Stalker_ContactStudy',proto,1)
# Four control empties display world-space end-effector trajectory and contact status.
for leg,n in enumerate(legs):
    control=bpy.data.objects.new("Contact • "+rig[n+2]["name"],None)
    scene.collection.objects.link(control)
    control.empty_display_type='CIRCLE';control.empty_display_size=.55
    for i,frame in enumerate(proto,1):
        control.location=basis @ (frame[n+2] @ piv[n+2])+actor.location
        control["planted"]=bool(masks[i-1][leg])
        control.keyframe_insert(data_path="location",frame=i)
        control.keyframe_insert(data_path='["planted"]',frame=i)
# Metered checkerboard gives contact and root translation a fixed reference.
pts=[];faces=[];colors=[]
for x in range(-18,19):
    for y in range(-18,19):
        i=len(pts);step=3
        pts.extend([((x+a)*step,(y+b)*step,-.035) for a,b in ((0,0),(1,0),(1,1),(0,1))])
        faces.append((i,i+1,i+2,i+3))
        colors.append((.30,.34,.38,1.) if (x+y)%2 else (.34,.38,.41,1.))
m=bpy.data.meshes.new("3 m ground grid");m.from_pydata(pts,[],faces)
col=m.color_attributes.new(name='Ground',type='BYTE_COLOR',domain='CORNER');m.color_attributes.active_color=col
for poly,c in zip(m.polygons,colors):
    for l in poly.loop_indices: col.data[l].color_srgb=c
scene.collection.objects.link(bpy.data.objects.new('3 m ground grid',m))
for name,frame in [('Stalk',1),('Coil',83),('Bite',110),('Recover',129)]:
    scene.timeline_markers.new(name,frame=frame)
# Evaluate the editable Blender rig against both source pose streams after baking.
largest=0.
for arm,source in [(actor,proto),(live_actor,live_frames)]:
    for i,frame in enumerate(source,1):
        scene.frame_set(i)
        for part,transform_native in zip(rig,frame):
            b=arm.pose.bones[part['name']]
            baked=b.matrix@b.bone.matrix_local.inverted()
            expected=basis@transform_native@basis.inverted()
            for corner in (part['low'],part['high']):
                v=basis@Vector(corner)
                largest=max(largest,((baked@v)-(expected@v)).length)
assert largest<.0002, largest
metrics['max_blender_bake_corner_error_m']=round(largest,7)
(HERE/"metrics.json").write_text(json.dumps(metrics,indent=2)+"\n")
scene.frame_set(1)
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_perspective='CAMERA'
        area.spaces.active.overlay.show_overlays=False
        area.spaces.active.shading.color_type='VERTEX'
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(HERE/"stalker-rig.blend"),compress=True)
