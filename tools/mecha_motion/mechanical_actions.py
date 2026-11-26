"""Offline mechanical and infantry moves, in the published rigid-mesh coordinates."""

import json
import subprocess
from functools import lru_cache
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

from .bake import apply, rigid, sample, smooth, solve_leg, triangles_by_joint

# Durations are seconds for the whole review gesture, generally including its
# preparation and recovery. Volley spacing follows rpg/mecha_ranged.hpp.
_SECONDS = {
    "idle": 2.4, "walk": 2.28, "run": 1.46, "retreat": 2.1,
    "strafe": 1.9, "pivot-turn": 1.7, "boost-evade": 1.15,
    "stomp": 1.12, "leap-land": 1.55, "hobbled-walk": 2.8,
    "ranged-aim": 1.25, "ranged-windup": 1.15, "fire-recoil": .66,
    "skirt-volley": 2.5, "lofted-missile-salvo": 1.65,
    "melee-rush": 1.55, "melee-windup": .78, "left-cut": 1.30,
    "right-cut": 1.30, "overhead-cut": 1.48, "guard": 1.14,
    "parry": .72, "close-strike": 1.05, "stagger": .92, "collapse": 2.15,
    "drop-landing": 1.65, "vehicle-transform": 1.4, "combine-brace": 1.75,
    "grenade-throw": 1.32, "swim-ascent": 2.2, "zero-g-thrust": 1.9,
    "warp-arrival": 1.1, "drop-entry": 2.35, "canal-emergence": 2.0,
    "pilot-ejection": 1.55, "disabled-kneel": 2.05,
}


def duration(action):
    return _SECONDS[action]


@lru_cache(maxsize=4)
def catalog(pack):
    # The executable exports the same EnemySpec instances as the game. Allow a
    # separate asset pack while preferring its own build's matching catalog.
    candidates = [pack.parent.parent / "mecha-catalog",
                  Path(__file__).resolve().parents[2] / "build-mecha/mecha-catalog"]
    binary = next((p for p in candidates if p.is_file()), None)
    if binary is None:
        raise FileNotFoundError("Build the mecha-catalog target for mechanical action selection")
    data = json.loads(subprocess.check_output([binary]))
    return {e["id"]: e for e in data["enemies"]}


def actions(rig_id, rig, context):
    if rig["family"] not in ("mecha", "infantry"):
        return []
    result = ["idle", "walk", "run", "retreat", "strafe", "pivot-turn", "stagger", "collapse"]
    if rig["player"]:
        return result + ["ranged-aim", "ranged-windup", "fire-recoil", "melee-windup",
                         "left-cut", "right-cut", "overhead-cut", "guard", "parry",
                         "boost-evade", "stomp", "leap-land", "hobbled-walk", "disabled-kneel",
                         "drop-entry", "drop-landing", "warp-arrival", "canal-emergence",
                         "swim-ascent", "zero-g-thrust", "vehicle-transform", "pilot-ejection"]
    base = rig_id.removesuffix("_alt")
    # The story actors and fused hull resolve to these same runtime specs.
    aliases = {"story_requisition_enforcer": "mecha_recruit", "story_arena_collector": "mecha_ace",
               "story_archive_sentinel": "aeon_diode", "combiner_mecha_combined": "combiner_mecha"}
    spec = catalog(Path(context["pack"]))[aliases.get(base, base)]
    attacks = spec["attacks"]
    if any(a["range"] > 12 for a in attacks):
        result += ["ranged-aim", "ranged-windup", "fire-recoil"]
    if rig["family"] == "infantry":
        if any("Grenade" in a["name"] for a in attacks):
            result.append("grenade-throw")
        return result
    mecha = spec["kind"] in (1, 6)
    traits = spec["traits"]
    if mecha:
        result += ["boost-evade", "stomp", "drop-landing", "warp-arrival", "hobbled-walk"]
        if any(a["effect"] == 0 and a["range"] > 18 for a in attacks) and spec["id"] != "mecha_sniper":
            agile = traits & (1 | 2 | 4 | 32 | 4096 | 4194304)
            carrier = not agile and (spec["plating"] >= 2 or spec["kind"] == 6)
            result.append("lofted-missile-salvo" if carrier else "skirt-volley")
    # Mirror duelWeapon() rather than assigning sword animations to snipers.
    duel = mecha and any(a["minimum"] == 0 and a["recharge"] == 0 and a["dice"] > 0
                         and a["effect"] in (0, 8)
                         and (a["range"] <= 12 or a["name"].startswith("Thermal Hatchet"))
                         for a in attacks)
    if duel:
        result += ["melee-rush", "melee-windup", "left-cut", "right-cut", "overhead-cut", "guard", "parry"]
    elif any(a["range"] <= 12 for a in attacks):
        result += ["melee-windup", "close-strike"]
    if traits & 4:
        result.append("leap-land")
    if traits & 4194304:
        result.append("vehicle-transform")
    if traits & 524288:
        result.append("combine-brace")
    return result


def _shots(action):
    spacing, count = {
        "fire-recoil": (0., 1), "skirt-volley": (.74, 3), "lofted-missile-salvo": (.26, 4),
    }[action]
    return tuple(.20 + i*spacing/duration(action) for i in range(count))


def timing_anchors(action):
    """Lock contact/shot phases when fitting the time between authored events."""
    if action in {"fire-recoil", "skirt-volley", "lofted-missile-salvo"}:
        return _shots(action)
    if action in {"leap-land", "drop-entry", "drop-landing", "warp-arrival"}:
        return (.26,.73) if action=="leap-land" else (.74,)
    if action in {"left-cut", "right-cut", "overhead-cut", "close-strike", "grenade-throw"}:
        return (.49,.62)
    if action=="parry":
        return (.36,)
    if action=="stomp":
        return (.79,)
    if action in _TRAVEL:
        return (.25,.50,.75)
    return ()


def _pulse(t, start, peak, end):
    return smooth((t-start)/(peak-start)) * (1-smooth((t-peak)/(end-peak)))


def _stage(curve, t, start, peak, release, end):
    return sample(curve, smooth((t-start)/(peak-start)) * (1-smooth((t-release)/(end-release))))


def _basis(rig, context):
    key = "_mechanical_basis"
    if key not in context:
        g = triangles_by_joint(context["model"], rig)
        pivots = np.array([p["pivot"] for p in rig["parts"]])
        h = min(pivots[l["hip"],1]-pivots[l["foot"],1] for l in rig["legs"])
        ref = context["reference"]
        last = np.asarray(ref["frames"][-1])
        binding = np.tile(np.eye(4), (len(last), 1, 1))
        binding[:, :3, :3] = Rotation.from_quat(last[:, :4]).as_matrix()
        binding[:, :3, 3] = last[:, 4:]
        context[key] = g, pivots, h, binding
    return context[key]


# Offsets of the next stance anchor in the ground plane (X right, -Z forward).
_TRAVEL = {"walk": (0,-.80), "run": (0,-1.09), "retreat": (0,.70),
           "strafe": (.61,0), "pivot-turn": (0,0), "hobbled-walk": (0,-.46),
           "melee-rush": (0,-1.16), "skirt-volley": (.78,-.08)}


def _gait(rig, action, t, context):
    geometry, piv, height, neutral = _basis(rig, context)
    n = len(t)
    legs = rig["legs"]
    curve = context["curves"]
    cycle = 2*t
    travel = np.array([_TRAVEL[action][0],0.,_TRAVEL[action][1]]) * height
    turn = np.deg2rad(52.) if action == "pivot-turn" else 0.
    planted = np.array([0.,1.,0.])
    for gain in (1., .72, .48, .3, .18, .08):
        scale = context["reference"]["metrics"]["reach_scale"] * gain
        root = cycle[:,None] * travel * scale
        yaw = Rotation.from_rotvec((cycle*turn*scale)[:,None]*planted).as_matrix()
        body = np.tile(np.eye(4), (n,1,1))
        body[:,0,3] = -.036*height*scale*sample(curve["sway"],cycle%1)
        body[:,1,3] = neutral[0,1,3] - height*scale*(.032 + .015*np.cos(4*np.pi*cycle))
        if action == "hobbled-walk":
            body[:,1,3] -= height*.064*scale*(1-np.cos(2*np.pi*cycle))*.5
        frames = np.broadcast_to(body[:,None], (n,len(piv),4,4)).copy()
        for leg in legs:
            hi,ki,fi=(leg[k] for k in ("hip","knee","foot"))
            shift = leg["phase"]/(2*np.pi)
            phase = (cycle-shift)%1
            duty = .54 if leg["side"]<0 else .52
            if action=="hobbled-walk" and leg["side"]<0:
                duty=.73
            swing = np.clip((phase-duty)/(1-duty),0,1)
            step = np.floor(cycle-shift)+shift+duty/2
            foot = apply(neutral[None,fi],piv[fi])[0]
            def anchor(k):
                rot=Rotation.from_rotvec((k*turn*scale)[:,None]*planted).as_matrix()
                return rot@foot+(k[:,None]*travel*scale)
            a,b=anchor(step),anchor(step+1)
            fraction=smooth(swing)
            target=a+(b-a)*fraction[:,None]
            target[:,1]+=height*.115*scale*sample(curve["left" if leg["side"]<0 else "right"],swing)
            # Solve in body-heading space so a turn keeps its knees on the rig's bend planes.
            target=np.swapaxes(yaw,1,2)@(target-root)[:,:,None]
            target=target[:,:,0]
            answer=solve_leg(piv[hi],piv[ki],piv[fi],apply(body,piv[hi]),target)
            if answer is None:
                break
            frames[:,hi],frames[:,ki]=answer
            foot_yaw=(step+fraction-cycle)*turn*scale
            spin=Rotation.from_rotvec(foot_yaw[:,None]*planted).as_matrix()
            frames[:,fi]=rigid(spin,piv[fi],target)
        else:
            frames[:,:,:3,:]=yaw[:,None,:,:]@frames[:,:,:3,:]
            # Attached knee and foot details inherit the leg transform exactly.
            joints={leg[k] for leg in legs for k in ("hip","knee","foot")}
            for i,p in enumerate(rig["parts"]):
                if i not in joints and p["parent"]>=0:
                    frames[:,i]=frames[:,p["parent"]]
            return frames,root
    raise ValueError(f"No reachable {action} gait for {rig['mesh']}")


def _base_pose(rig, action, t, context):
    if action in _TRAVEL:
        return _gait(rig,action,t,context)
    _,_,_,neutral=_basis(rig,context)
    frames=np.broadcast_to(neutral,(len(t),*neutral.shape)).copy()
    return frames,np.zeros((len(t),3))


def _weapon_side(rig):
    # Long rifles can share the forearm mesh. Their forward projection is
    # measurable even on rigs without a dedicated barrel socket.
    sides = [p["side"] for p in rig["parts"] if p["role"] == "barrel"
             and p.get("side") and p["parent"] >= 0 and rig["parts"][p["parent"]]["role"] != "turret"]
    if sides:
        return 1 if 1 in sides else sides[0]
    forearms = [p for p in rig["parts"] if "forearm" in p["name"] and p.get("side")]
    if forearms:
        front = lambda p: p["pivot"][2]-p["low"][2]
        arm = max(forearms, key=front)
        if front(arm) > (arm["high"][1]-arm["low"][1])*.85:
            return arm["side"]
    return 1


def _plant_legs(rig, frames, piv, displacement):
    """Move the loaded chassis over fixed soles, solving both links at each sample."""
    if not np.any(displacement):
        return
    originals = frames.copy()
    frames[:, :, :3, 3] += displacement[:, None, :]
    solved = set()
    for leg in rig["legs"]:
        hip, knee, foot = (leg[k] for k in ("hip", "knee", "foot"))
        base = apply(originals[:, hip], piv[hip]) + displacement
        target = apply(originals[:, foot], piv[foot])
        joints = solve_leg(piv[hip], piv[knee], piv[foot], base, target)
        if joints is None:
            raise ValueError(f"Unreachable planted brace on {rig['mesh']}")
        frames[:, hip], frames[:, knee] = joints
        frames[:, foot] = originals[:, foot]
        solved.update((hip, knee, foot))
    for i, part in enumerate(rig["parts"]):
        parent = part["parent"]
        if i not in solved and parent in solved:
            frames[:, i] = frames[:, parent] @ np.linalg.solve(originals[:, parent], originals[:, i])
            solved.add(i)


def pose(rig_id, rig, action, t, context):
    """Return bind-to-posed rotations/offsets and separate world root travel."""
    t=np.asarray(t,dtype=float)
    if action not in _SECONDS or not len(t) or np.any((t<0)|(t>1)):
        raise ValueError(f"Invalid mechanical motion or normalized time for {rig_id}: {action}")
    geometry,piv,height,_=_basis(rig,context)
    frames,root=_base_pose(rig,action,t,context)
    curves=context["curves"]
    beat=np.sin(t*4*np.pi)
    shoulder=_stage(curves["shoulder"],t,.02,.43,.53,.98)
    depth=_stage(curves["depth"],t,0,.40,.52,.99)
    pulse=lambda a,b,c:_pulse(t,a,b,c)
    pitch,yaw,roll,arms,spread,elbow,head,kick,drop=np.zeros((9,len(t)))
    load = np.zeros((len(t), 3))  # in leg heights, +Z is a backward shift
    retract, counter, trailing, shoulder_yaw = np.zeros((4, len(t)))
    lead = _weapon_side(rig)
    ranged = action in {"ranged-aim","ranged-windup","fire-recoil","skirt-volley","lofted-missile-salvo"}
    fold=np.zeros(len(t))
    if action in _TRAVEL:
        speed=1.45 if action in {"run","melee-rush"} else 1.
        pitch+=.09*speed
        yaw+=.09*beat
        arms+=.26*speed*beat
        if action=="retreat":arms*=-1;pitch-=.22
        if action in {"strafe","skirt-volley"}:roll-=.10;head+=.21
        if action=="melee-rush":pitch+=.24*shoulder;elbow+=.52*shoulder
        if action=="hobbled-walk":roll+=.13*np.cos(t*4*np.pi)
        # Hull banks toward its supporting leg; the head counter-rotates for sight stability.
        roll += (.055 if action != "hobbled-walk" else .095) * beat
        pitch += .026 * speed * (1-np.cos(t*8*np.pi))
        elbow += .13 * speed * (1-np.cos(t*4*np.pi))
        head -= .36*yaw
    if action=="idle":
        scan = np.sin(t*2*np.pi)
        head += .24*scan
        yaw += .068*scan*(1-.35*np.cos(t*2*np.pi))
        pitch += .022*(1-np.cos(t*2*np.pi))
        elbow += .047*(1-np.cos(t*2*np.pi))
    elif ranged:
        held=shoulder
        if action in {"fire-recoil","skirt-volley","lofted-missile-salvo"}:
            held=_stage(curves["shoulder"],t,0,.14,.81,1.)
            for shot in _shots(action):
                # The mount recoils first, shoulder takes the impulse, then the hull settles.
                retract += pulse(shot-.02, shot+.021, shot+.13)
                kick += pulse(shot-.004, shot+.058, shot+.19)
                trailing += pulse(shot+.030, shot+.098, shot+.23)
        arms+=.62*held;elbow+=.32*held;pitch+=.11*held-.15*kick;head+=.18*held
        # Sight the weapon from a bladed stance. The off arm closes toward the guard.
        shoulder_yaw += lead*.24*held
        head += .16*held
        counter += .40*held
        if action != "skirt-volley":
            load[:,0] += lead*(.033*held - .03*trailing)
            load[:,1] -= .075*held + .055*trailing
            load[:,2] += -.038*held + .087*trailing
        pitch -= .10*trailing
        if action=="ranged-windup":spread+=.20*depth; pitch+=.10*depth
        if action=="lofted-missile-salvo":pitch-=.28*held;spread+=.32*held
    elif action in {"left-cut","right-cut","overhead-cut","close-strike","grenade-throw","melee-windup"}:
        lead=-1 if action=="left-cut" else 1
        wind=_stage(curves["shoulder"],t,.015,.37,.49,.63)
        hit=pulse(.48,.62,.88)
        follow=pulse(.61,.72,1.)
        if action=="melee-windup":
            wind=shoulder; hit=np.zeros_like(t); follow=np.zeros_like(t)
        # Shift onto the rear foot and turn the pelvis before the blade crosses.
        load[:,0] += lead*(-.055*wind + .065*hit + .035*follow)
        load[:,1] -= .12*wind + .075*hit + .105*follow
        load[:,2] += .085*wind - .14*hit - .09*follow
        yaw += lead*(-.36*wind + .68*hit + .30*follow)
        roll += lead*(.07*wind - .13*hit)
        pitch += .13*wind + .21*hit + .12*follow
        elbow += .81*wind - .20*hit + .29*follow
        counter += .63*wind + .86*hit + .40*follow
        if action in {"overhead-cut","grenade-throw"}:
            arms += 1.34*wind + .73*hit + .27*follow
            pitch -= .30*wind
            spread += .28*wind - .30*hit
        else:
            arms += .52*wind + 1.07*hit + .56*follow
            spread += lead*(.81*wind - 1.26*hit - .54*follow)
    elif action in {"guard","parry","combine-brace","disabled-kneel"}:
        pitch+=.15*depth;elbow+=.92*shoulder;arms+=.72*shoulder;spread-=.32*shoulder
        load[:,1] -= .09*depth
        if action=="parry":
            intercept=pulse(.14,.36,.72)
            impact=pulse(.36,.48,.79)
            spread += .85*intercept - .34*impact
            yaw += .43*intercept - .19*impact
            counter += .58*intercept + .32*impact
            load[:,0] += .047*intercept - .052*impact
            load[:,1] -= .10*impact
            load[:,2] += -.06*intercept + .10*impact
        if action=="combine-brace":
            spread += 1.13*depth
            pitch -= .21*depth
            load[:,1] -= .11*depth
            elbow -= .28*pulse(.34,.55,.91)
        if action=="disabled-kneel":
            sink = smooth((t-.11)/.74)
            pitch += .52*sink
            arms -= .65*sink
            load[:,0] -= .075*sink
            load[:,1] -= .26*sink
            roll += .16*sink
    elif action in {"boost-evade","zero-g-thrust","swim-ascent","leap-land","drop-landing","drop-entry","warp-arrival","canal-emergence","stomp","pilot-ejection"}:
        thrust=pulse(.12,.32,.79);landing=pulse(.60,.77,1.)
        pitch+=.21*thrust+.26*landing;elbow+=.44*thrust;spread+=.23*thrust
        if action=="boost-evade":
            roll += -.45*thrust + .14*landing
            counter += .36*thrust
            load[:,1] -= .10*pulse(0,.12,.26) + .17*landing
            root[:,0] += height*1.35*smooth((t-.14)/.58)
            root[:,1] += height*.15*thrust
            fold = .25*thrust
        elif action=="zero-g-thrust":
            pitch -= .65*thrust
            arms -= .42*thrust
            fold = .30*thrust
            root[:,2] -= height*1.1*smooth(t)
            root[:,1] += height*.56*smooth(t)
        elif action=="swim-ascent":
            pitch -= .48*shoulder
            swimbeat = np.sin(2*np.pi*t)
            arms += .45*swimbeat
            spread += .37*(1-np.cos(2*np.pi*t))
            fold = .19*(1-np.cos(2*np.pi*t))
            root[:,1] += height*.9*smooth(t)
        elif action=="canal-emergence":
            surface=smooth((t-.18)/.60)
            pitch-=.34*thrust;pitch+=.14*landing;spread+=.44*landing;arms+=.25*thrust
            fold = .31*thrust
            arms -= .26*landing
            root[:,1]+=height*.84*surface;root[:,2]-=height*.45*surface
        elif action=="leap-land":
            # Separate foot release, ballistic arc, touchdown, and damped compression.
            crouch = pulse(0,.16,.26)
            flight = np.clip((t-.26)/.47, 0, 1)
            airtime = 4*flight*(1-flight)
            absorb = pulse(.73,.81,.96)
            rebound = pulse(.85,.94,1.)
            root[:,1] += height*.90*airtime
            root[:,2] -= height*.90*flight
            load[:,1] -= .22*crouch + .25*absorb + .045*rebound
            load[:,2] += .07*crouch - .072*absorb
            fold = .52*airtime
            pitch += .18*crouch + .21*absorb - .12*rebound
            arms += -.41*crouch + .58*airtime - .29*absorb
        elif action in {"drop-entry","drop-landing","warp-arrival"}:
            root[:,1]+=height*({"drop-entry":2.3,"drop-landing":.85,"warp-arrival":.22}[action])*(1-smooth(t/.74))
            airfold = 1-smooth(t/.74)
            absorb = pulse(.74,.82,.96)
            fold = .28*airfold
            load[:,1] -= .23*absorb + .045*pulse(.88,.95,1.)
            load[:,2] -= .06*absorb
            if action=="drop-entry":
                brakes=pulse(.09,.33,.65)
                spread+=.70*brakes;arms+=.35*brakes
            elif action=="warp-arrival":
                yaw+=.38*thrust;spread+=.30*landing
        elif action=="pilot-ejection":
            blast = pulse(.36,.43,.66)
            slump = smooth((t-.49)/.46)
            pitch += -.28*blast + .51*slump
            arms += .20*blast - .37*slump
            load[:,1] -= .09*blast + .16*slump
            roll += .12*slump
        elif action=="stomp":
            roll += .19*thrust-.10*landing
            fold = .65*thrust
            load[:,0] -= lead*.09*thrust
            load[:,1] -= .09*thrust + .13*landing
            load[:,2] -= .045*landing
    elif action=="vehicle-transform":
        # Unlock the legs, tuck the arms, then settle the forward hull.
        fold = smooth(t/.84)
        tuck = smooth((t-.10)/.66)
        pitch -= 1.18*smooth((t-.22)/.72)
        arms += .82*tuck
        elbow += .56*tuck
        drop += .36*fold
    elif action=="stagger":
        blow=pulse(0,.13,.70);settle=pulse(.36,.60,.98)
        load[:,0] += .055*blow
        load[:,1] -= .14*blow + .06*settle
        load[:,2] += .10*blow
        pitch+=-.36*blow+.12*settle;roll+=.29*blow-.10*settle;arms-=.32*blow;spread+=.40*blow
    elif action=="collapse":
        fall=smooth((t-.17)/.68);pitch+=.27*fall;elbow+=.45*fall;arms-=.50*fall
        fold=.43*fall
        roll += .18*pulse(.04,.26,.52)
        arms += .34*pulse(.08,.31,.55)
        elbow += .23*pulse(.64,.81,1.)
    _plant_legs(rig, frames, piv, height*load)
    # Apply each authored hinge in its bind frame and propagate the correction
    # to every descendant, including barrels parented to the Ace's split elbow.
    corrections=np.empty_like(frames)
    identity=np.broadcast_to(np.eye(4),(len(t),4,4))
    for i,p in enumerate(rig["parts"]):
        parent=p["parent"]
        inherited=corrections[:,parent] if parent>=0 else identity
        role=p["role"].split("-")[-1]
        side=p.get("side") or (-1 if p.get("arm")==0 or piv[i,0]<0 else 1)
        angles=np.zeros((len(t),3)); shift=np.zeros((len(t),3))
        segment=p.get("tracked_segment")
        if role=="torso":angles[:,0]=pitch;angles[:,1]=yaw;angles[:,2]=roll;shift[:,1]=-height*drop
        elif role=="head" or role=="scanner":angles[:,1]=head-.25*yaw;angles[:,0]=-.27*pitch
        elif (p.get("hinge")=="elbow" or segment=="forearm"
              or (p["name"].endswith("_forearm") and parent>=0 and rig["parts"][parent]["role"]=="arm")):
            angles[:,0] = elbow + (.30*retract if side==lead else .28*counter)
            if ranged and side==lead and p["pivot"][2]-p["low"][2] > p["high"][1]-p["low"][1]:
                # Long forearm-integrated barrels already face downrange in bind
                # pose. Rotate the wrist back as the upper arm lifts the sight.
                angles[:,0] = -arms - pitch + .11*held + .18*retract
        elif segment=="shoulder":
            # Tracked chassis have separate shoulder girdles over the upper arms.
            angles[:,1] = shoulder_yaw*.28
            angles[:,2] = side*(.06*shoulder + .08*retract)
        elif role=="arm" or segment=="upper-arm" or p.get("arm",-1)>=0:
            locomotion=action in _TRAVEL and action!="skirt-volley"
            striking=action in {"left-cut","right-cut","overhead-cut","close-strike","grenade-throw","melee-windup"}
            weight=(1 if side==lead else -.32) if striking else 1
            angles[:,0]=arms*(side if locomotion else weight)-kick*(.24 if side==lead else .08)
            if side!=lead:
                angles[:,0] += counter*.53
            angles[:,1] = spread*(weight if striking else side)*.60 + shoulder_yaw
            if side!=lead:
                angles[:,1] -= side*.44*counter
            angles[:,2]=side*(.12*shoulder+spread*.31)
        elif role=="turret":
            angles[:,1]=head*.85
        elif role=="barrel" and parent>=0 and rig["parts"][parent]["role"]=="turret":
            angles[:,0]=-.30*arms;shift[:,2]=height*.065*kick
        elif role in {"hip","knee","foot"}:
            visible_fold=fold
            if action=="stomp" and side!=lead:visible_fold=fold*0
            angles[:,0]=visible_fold*{"hip":-1.05,"knee":1.92,"foot":-.87}[role]
        current=inherited@frames[:,i]
        if angles.any() or shift.any():
            local=rigid(Rotation.from_euler("xyz",angles).as_matrix(),piv[i],piv[i]+shift)
            modified=current@local
            # M @ L @ inverse(M) is the correction about the posed pivot.
            inv=np.broadcast_to(np.eye(4), current.shape).copy()
            inv[:,:3,:3]=np.swapaxes(frames[:,i,:3,:3],1,2)
            inv[:,:3,3]=-(inv[:,:3,:3]@frames[:,i,:3,3,None])[:,:,0]
            corrections[:,i]=modified@inv
            frames[:,i]=modified
        else:
            corrections[:,i]=inherited
            frames[:,i]=current
    groups = [list(range(len(rig["parts"])))]
    if action == "collapse" and rig["family"] == "infantry":
        # Each member has separate torso and leg roots within the single squad mesh.
        members = {}
        for i, part in enumerate(rig["parts"]):
            member = part["name"].split("_")[1]
            members.setdefault(member, []).append(i)
        groups = list(members.values())
    for member, joints in enumerate(groups):
        if action == "collapse":
            fall = smooth((t - .17 - .045 * member) / .68)
            angle = 1.38 if member % 2 == 0 else -1.38
            spin = Rotation.from_euler("xyz", np.column_stack((.43*fall, .20*fall, angle*fall))).as_matrix()
            origin = piv[joints[0]].copy()
            origin[1] = rig["floor"]
            offset = origin - spin @ origin
            frames[:,joints,:3,:] = spin[:,None] @ frames[:,joints,:3,:]
            frames[:,:,:3,3][:,joints] += offset[:,None,:]
        bottom = np.full(len(t), np.inf)
        for i in joints:
            vertices = geometry[i]
            if len(vertices):
                y = frames[:,i,1,:3] @ vertices.T + frames[:,i,1,3,None]
                bottom = np.minimum(bottom, y.min(axis=1))
        lift = rig["floor"] - bottom
        if action not in {"collapse", "vehicle-transform"}:
            lift = np.maximum(0, lift)
        frames[:,joints,1,3] += lift[:,None]
    return frames, root
