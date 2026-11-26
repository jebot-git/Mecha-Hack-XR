"""Offline, capability-specific Voidmaw clips in native bind space.

Locomotion uses the Arc swing and sway channels and world-space stance anchors.
Ground attacks keep the feet planted while the upper body articulates. Burrowing
and surface motion use the creature's local ground plane before root placement.
"""

import math

import numpy as np
from scipy.spatial.transform import Rotation

from .bake import apply, rigid, sample, smooth, solve_leg, triangles_by_joint

# Catalogue attack names (catalog.cpp and manual_enemies.inc), including the
# drone variant used as the story brood guardian.
ATTACKS = {
    "kaiju_spawnling": ("rending-claws", "dorsal-spines"),
    "kaiju_flea": ("chittering-mandibles", "miniature-maw-beam"),
    "kaiju_drone": ("rending-claws", "maw-beam", "thundering-roar"),
    "kaiju_baron": ("crushing-bite", "maw-beam", "thundering-roar"),
    "kaiju_tunnel_wurm": ("rending-maw", "consume"),
    "kaiju_tyrant": ("ripping-talons", "sweeping-maw-beam", "piercing-screech"),
    "abomination": ("bone-axe", "rocket-elbow", "consume-core"),
    "kaiju_stalker": ("barbed-tail", "caustic-spit", "grapple"),
    "kaiju_obliterator": ("rending-claws", "sweeping-maw-beam", "consume"),
    "kaiju_dismantler": ("rending-mandibles", "caustic-web"),
    "abyssal_kaiju": ("coiling-tentacles", "axial-maw-beam", "screeching-sonar"),
    "kaiju_broodqueen": ("rending-mandibles", "caustic-spit", "piercing-screech"),
}
SMALL = {"kaiju_spawnling", "kaiju_flea", "kaiju_drone", "kaiju_baron"}
BURROW = {"kaiju_spawnling", "kaiju_flea", "kaiju_tunnel_wurm"}
AERIAL = {"wingbeat", "swim", "cockpit-cling", "tackle", "dodge"}
LOCOMOTION = {
    "walk",
    "retreat",
    "pursuit",
    "lumber",
    "wall-crawl",
    "ceiling-crawl",
    "slither",
    "swim",
    "wingbeat",
}

# Windup, emission and recovery for the shipped siege attack clocks. The clip
# includes the complete tell and discharge. Siege-recovery can also play alone.
SIEGE = {
    "sweeping-maw-beam": (1.75, 1.02, 2.85),
    "axial-maw-beam": (1.65, 0.64, 2.6),
    "caustic-spit": (1.30, 0.42, 2.35),
    "caustic-web": (1.05, 0.25, 2.60),
    "rocket-elbow": (1.10, 0.30, 1.90),
    "caustic-spines": (1.55, 0.52, 2.65),
    "flying-maw-sweep": (1.14, 0.60, 1.95),
}
CLOCK = {
    "idle": 2.4,
    "walk": 1.15,
    "retreat": 1.35,
    "lumber": 1.65,
    "pursuit": 0.78,
    "dodge": 0.29,
    "coil": 0.80,
    "tackle": 0.68,
    "cockpit-cling": 1.60,
    "recover": 1.15,
    "wall-crawl": 1.20,
    "ceiling-crawl": 1.20,
    "burrow-entry": 0.82,
    "burrow-eruption": 0.82,
    "siege-brace": 1.55,
    "siege-recovery": 2.40,
    "swim": 1.65,
    "slither": 1.45,
    "wingbeat": 1.05,
    "brood-release": 2.30,
    "stagger": 0.67,
    "death": 1.85,
    "warp-arrival": 0.95,
    "rending-claws": 0.83,
    "ripping-talons": 0.94,
    "bone-axe": 1.22,
    "chittering-mandibles": 0.72,
    "rending-maw": 1.02,
    "rending-mandibles": 0.91,
    "crushing-bite": 0.96,
    "consume": 1.46,
    "consume-core": 1.46,
    "coiling-tentacles": 1.72,
    "barbed-tail": 1.04,
    "grapple": 1.02,
    "maw-beam": 1.20,
    "miniature-maw-beam": 0.78,
    "dorsal-spines": 0.86,
    "thundering-roar": 1.36,
    "piercing-screech": 1.10,
    "screeching-sonar": 1.59,
}


def duration(action):
    return sum(SIEGE[action]) if action in SIEGE else CLOCK[action]


def siege_shots(action):
    load, burst, _ = SIEGE[action]
    count = {"sweeping-maw-beam": 7, "flying-maw-sweep": 5, "axial-maw-beam": 5, "caustic-web": 1}.get(action, 2)
    return np.linspace(load, load + burst, count)


def timing_anchors(action):
    """Interior landmarks that retiming must preserve in normalized clip time."""
    if action in SIEGE:
        load, burst, _ = SIEGE[action]
        seconds = set(siege_shots(action)) | {load + burst}
        return tuple(sorted(shot / duration(action) for shot in seconds))
    if action in {"dodge", "tackle"}:
        return (0.15, 0.79)  # toes leave the surface, then contact it
    if action == "burrow-entry":
        return (0.18, 0.88)
    if action == "burrow-eruption":
        return (0.10, 0.54, 0.80)
    if action == "cockpit-cling":
        return (0.20, 0.50, 0.70)
    return ()


def actions(rig_id, rig, context):
    species = rig_id.removesuffix("_alt")
    if species == "story_brood_guardian":
        species = "kaiju_drone"
    attacks = list(ATTACKS[species])
    moves = ["idle", "warp-arrival", "stagger", "death"]
    if rig["legs"]:
        moves += ["walk", "retreat"]
    if species == "kaiju_tunnel_wurm":
        moves.append("slither")
    if species in SMALL or species == "kaiju_tunnel_wurm":
        moves += ["pursuit", "dodge", "coil", "tackle", "cockpit-cling", "recover"]
    if species in SMALL:
        moves += ["wall-crawl", "ceiling-crawl"]
    if species in BURROW:
        moves += ["burrow-entry", "burrow-eruption"]
    if species not in SMALL and species != "kaiju_tunnel_wurm":
        if species != "abyssal_kaiju":
            # The ordinary close-pursuit AI may attach after a catalogue strike
            # inside twelve metres, even without the small-actor assault AI.
            moves.append("cockpit-cling")
        if rig["legs"]:
            moves.append("lumber")
        moves += ["siege-brace", "siege-recovery"]
    if species == "kaiju_tyrant":
        moves.append("wingbeat")
        attacks[attacks.index("sweeping-maw-beam")] = "flying-maw-sweep"
    if species == "abyssal_kaiju":
        moves.append("swim")
    if species == "kaiju_broodqueen":
        moves.append("brood-release")
    return moves + attacks


def pulse(t, start, peak, end):
    return smooth((t - start) / (peak - start)) * (1 - smooth((t - peak) / (end - peak)))


def flight_phase(t, start, land):
    """Ballistic interval with planted compression and landing frames on either side."""
    progress = np.clip((t - start) / (land - start), 0, 1)
    return 4 * progress * (1 - progress)


def _weights(action, t, curves):
    """Body load, release and recoil. Positive release can persist for a beam."""
    if action in SIEGE:
        load, burst, rest = SIEGE[action]
        a, b = load / (load + burst + rest), (load + burst) / (load + burst + rest)
        wind = smooth(t / a) * (1 - smooth((t - a) / 0.07))
        fire = smooth((t - a) / 0.045) * (1 - smooth((t - b) / 0.08))
        recoil = pulse(t, b, min(0.99, b + 0.13), 1)
    else:
        wind = pulse(t, 0.00, 0.32, 0.53)
        fire = pulse(t, 0.31, 0.52, 0.79)
        recoil = pulse(t, 0.58, 0.74, 1)
    return sample(curves["depth"], wind), sample(curves["shoulder"], fire), recoil


def _channels(action, t, curves, side, phase, stage):
    """Return local XYZ Euler channels for each functional group. Facing is -Z."""
    zero = np.zeros_like(t)
    torso = np.zeros((len(t), 3))
    head = torso.copy()
    jaw = zero.copy()
    arm = torso.copy()
    tail = torso.copy()
    wing = torso.copy()
    hip = torso.copy()
    wind, fire, recoil = stage
    beat = 2 * np.pi * t
    wave = np.sin(beat - phase)
    if action in LOCOMOTION or action == "idle":
        strength = {"idle": 0.42, "pursuit": 1.55, "lumber": 0.82}.get(action, 1)
        torso[:, 0] = strength * 0.055 * np.cos(beat * 2)
        torso[:, 1] = strength * 0.10 * sample(curves["sway"], t % 1)
        # Stabilise the eyes while the thorax shifts between the load-bearing legs.
        torso[:, 2] = 0.065 * strength * np.sin(beat)
        head[:, 1] = -0.76 * torso[:, 1]
        head[:, 2] = -0.62 * torso[:, 2]
        head[:, 0] = -0.58 * torso[:, 0] + 0.023 * strength * np.sin(2 * beat - 0.30)
        jaw = 0.04 * strength * (1 + np.sin(beat))
        arm[:, 0] = strength * 0.18 * side * np.sin(beat)
        tail[:, 1] = strength * (0.29 if action in {"swim", "slither"} else 0.16) * wave
        tail[:, 0] = 0.045 * strength * np.cos(beat - phase)
        if action in {"swim", "wingbeat"}:
            # Long recovery, short power stroke. The membrane feathers on the upstroke.
            stroke = np.sin(beat - phase + 0.32 * np.sin(beat - phase))
            wing[:, 2] = side * 0.64 * stroke
            wing[:, 0] = 0.22 * np.cos(beat - phase)
        else:
            wing[:, 2] = side * 0.06 * wave
        hip[:, 0] = 0.13 * side * wave
    elif action in {"stagger", "death", "recover", "siege-recovery", "warp-arrival"}:
        if action == "death":
            collapse = smooth((t - 0.16) / 0.61)
            torso[:, 0] = 0.18 * collapse
            torso[:, 2] = 0.18 * collapse
            # Momentum reaches the skull and then the distal tail after the hull hits.
            head[:, 0] = -0.27 * pulse(t, 0.02, 0.20, 0.48) + 0.30 * smooth((t - 0.49) / 0.36)
            head[:, 2] = -0.16 * pulse(t, 0.56, 0.74, 0.90)
            jaw = 0.50 * collapse + 0.16 * pulse(t, 0.06, 0.17, 0.38)
            arm[:, 0] = -0.63 * collapse
            hip[:, 2] = -side * 0.43 * collapse
            tail[:, 1] = 0.17 * collapse + 0.22 * np.sin(2 * beat - phase) * np.sin(np.pi * t) ** 2
            wing[:, 2] = -side * 0.44 * collapse
        else:
            hit = (
                pulse(t, 0, 0.13, 0.8)
                if action == "stagger"
                else (1 - smooth(t))
                if action in {"recover", "siege-recovery"}
                else pulse(t, 0.1, 0.38, 0.89)
            )
            torso[:, 0] = -0.22 * hit
            torso[:, 2] = 0.13 * hit
            head[:, 1] = 0.17 * hit
            head[:, 0] = 0.26 * hit - 0.20 * pulse(t, 0.15, 0.31, 0.60) if action == "stagger" else 0.16 * hit
            jaw = 0.30 * hit
            tail[:, 1] = 0.25 * pulse(t - 0.065 * phase, 0, 0.34, 1)
            arm[:, 2] = side * 0.20 * hit
            wing[:, 2] = side * 0.39 * hit
    elif action in {
        "dodge",
        "coil",
        "tackle",
        "cockpit-cling",
        "siege-brace",
        "burrow-entry",
        "burrow-eruption",
    }:
        load = smooth(t) if action in {"coil", "siege-brace", "burrow-entry"} else wind
        if action == "cockpit-cling":
            # Grasp stays engaged while mandibles cycle separately against the pane.
            bite = pulse((t * 2) % 1, 0, 0.4, 0.70)
            torso[:, 0] = 0.26 + 0.13 * bite
            head[:, 0] = -0.15 + 0.32 * bite
            jaw = 0.13 + 0.69 * pulse((t * 2) % 1, 0, 0.25, 0.47)
            arm[:, 0] = -0.82 + 0.11 * np.sin(beat)
            arm[:, 1] = -0.46 * side
            hip[:, 2] = -0.48 * side + 0.07 * wave
            tail[:, 1] = 0.30 * wave
            tail[:, 0] = -0.12 + 0.14 * np.sin(beat - phase)
        elif action in {"burrow-entry", "burrow-eruption"}:
            entering = action == "burrow-entry"
            drive = pulse(t, 0.06, 0.35, 0.86) if entering else pulse(t, 0.05, 0.46, 0.81)
            pitch = -1 if entering else 1
            head[:, 0] = pitch * 0.22 * drive
            torso[:, 0] = pitch * 0.16 * drive
            jaw = (0.25 if entering else 0.72) * drive + 0.10 * fire
            tail[:, 0] = -pitch * 0.22 * pulse(t - 0.055 * phase, 0.08, 0.45, 0.88)
            tail[:, 1] = 0.15 * np.sin(beat - phase) * drive
            hip[:, 0] = (0.52 * np.sin(2 * beat + phase) - 0.20) * drive
            hip[:, 2] = side * 0.33 * drive
        elif action in {"tackle", "dodge"}:
            flight = flight_phase(t, 0.15, 0.79)
            land = pulse(t, 0.72, 0.83, 1)
            load = pulse(t, 0, 0.13, 0.30)
            torso[:, 0] = -0.30 * load + 0.23 * flight - 0.27 * land
            torso[:, 2] = (0.27 if action == "dodge" else 0.04) * flight
            head[:, 0] = 0.12 * load - 0.18 * flight + 0.19 * land
            jaw = 0.15 * load + 0.62 * flight
            arm[:, 0] = 0.37 * load - 0.83 * flight + 0.23 * land
            hip[:, 2] = side * (-0.28 * load + 0.21 * flight - 0.37 * land)
            tail[:, 0] = 0.28 * pulse(t - 0.035 * phase, 0.13, 0.34, 0.72) - 0.22 * land
        else:
            torso[:, 0] = -0.19 * load + 0.24 * fire
            head[:, 0] = -0.18 * load
            if action == "dodge":
                torso[:, 2] = 0.30 * fire
            jaw = 0.20 * load + 0.46 * fire
            arm[:, 0] = 0.28 * load - 0.59 * fire
            hip[:, 0] = side * 0.24 * fire
            tail[:, 1] = 0.19 * load - 0.30 * pulse(t - phase * 0.045, 0.24, 0.53, 0.91)
            wing[:, 2] = side * 0.22 * load
    elif action in {
        "rending-claws",
        "ripping-talons",
        "bone-axe",
        "grapple",
        "barbed-tail",
        "coiling-tentacles",
    }:
        whip = action in {"barbed-tail", "coiling-tentacles"}
        torso[:, 1] = -0.23 * wind + 0.38 * fire - 0.06 * recoil
        torso[:, 0] = -0.14 * wind + 0.15 * fire
        head[:, 0] = -0.12 * wind + 0.16 * fire
        jaw = 0.18 * wind + 0.30 * fire
        # A unilateral axe slash crosses the chest; a grapple closes both limbs.
        leading = 1 if side > 0 or action == "grapple" else 0.32
        arm[:, 0] = leading * (-0.75 * wind - 0.92 * fire + 0.11 * recoil)
        arm[:, 1] = side * (0.42 * wind - 0.64 * fire) * leading
        arm[:, 2] = side * (-0.31 * wind + 0.51 * fire) * leading
        spread = pulse(t - 0.047 * phase, 0.25, 0.53, 0.88)
        tail[:, 1] = (0.32 * wind - 0.62 * spread) if whip else 0.15 * wind - 0.20 * spread
        if action == "coiling-tentacles":
            # Opposite tentacle banks wrap inward, with distal segments trailing.
            tail[:, 0] = -0.43 * spread
            tail[:, 1] = side * (0.32 * wind - 0.62 * spread)
            tail[:, 2] = side * 0.21 * spread
        wing[:, 2] = side * 0.28 * fire
    elif action in {
        "crushing-bite",
        "rending-maw",
        "chittering-mandibles",
        "rending-mandibles",
        "consume",
        "consume-core",
    }:
        chewing = action in {"consume", "consume-core", "chittering-mandibles"}
        torso[:, 0] = -0.19 * wind + 0.18 * fire
        head[:, 0] = -0.24 * wind + 0.41 * fire
        jaw = 0.77 * wind + 0.10 * recoil
        if chewing:
            jaw += 0.33 * np.sin(2 * beat) ** 2 * smooth((t - 0.38) / 0.12) * (1 - smooth((t - 0.8) / 0.2))
        arm[:, 1] = side * (0.20 * wind - 0.33 * fire)
        tail[:, 1] = 0.23 * wind - 0.30 * pulse(t - phase * 0.035, 0.28, 0.58, 0.93)
    elif action == "brood-release":
        torso[:, 0] = -0.17 * wind + 0.16 * fire
        wing[:, 2] = side * (0.76 * wind + 0.58 * fire)
        tail[:, 0] = 0.32 * wind - 0.57 * fire
        jaw = 0.24 * fire
    elif action in {"piercing-screech", "thundering-roar", "screeching-sonar"}:
        torso[:, 0] = -0.19 * wind - 0.08 * fire
        head[:, 0] = -0.37 * fire
        jaw = 0.72 * fire
        jaw += 0.09 * np.sin(5 * beat) * fire
        tail[:, 1] = 0.12 * fire * wave
        wing[:, 2] = side * 0.42 * fire
    else:
        torso[:, 0] = -0.16 * wind + 0.13 * fire - 0.14 * recoil
        head[:, 0] = -0.19 * wind + 0.16 * fire
        jaw = 0.29 * wind + 0.66 * fire
        if action in {"sweeping-maw-beam", "flying-maw-sweep"}:
            a, b, c = SIEGE[action]
            traverse = np.clip((t * (a + b + c) - a) / b, 0, 1)
            # The live rays traverse +/-0.11 rad. Aim at the first lane before
            # discharge and let the torso carry part of the same sweep.
            aim = -0.11 * smooth(t / (a / (a + b + c)))
            aim += 0.22 * smooth(traverse)
            aim *= 1 - smooth((t - (a+b)/(a+b+c)) / 0.16)
            head[:, 1] = 0.73 * aim
            torso[:, 1] = 0.27 * aim
        elif action == "rocket-elbow":
            jaw = 0.08 * wind
            # The separate rocket barrel is parented to the right arm. Its
            # opposite arm counterbalances the shoulder without miming a shot.
            arm[:, 0] = (-1.12 * wind - 0.94 * fire) if side > 0 else (0.26 * wind - 0.16 * recoil)
            arm[:, 1] = (0.35 if side > 0 else -0.12) * (wind + fire)
        elif action in {"caustic-spit", "miniature-maw-beam", "dorsal-spines"}:
            head[:, 0] += 0.20 * np.sin(2 * beat) * fire
            tail[:, 0] = -0.28 * fire if action == "dorsal-spines" else 0.08 * recoil
        if action in SIEGE and action != "caustic-web":
            # Match the actual discrete rounds/rays. An emitter kick starts
            # at the shot while the heavier thorax responds a frame behind.
            load, burst, rest = SIEGE[action]
            seconds = t * (load + burst + rest)
            kick = sum(pulse(seconds, shot, shot + 0.045, shot + 0.14)
                       for shot in siege_shots(action))
            if action == "rocket-elbow":
                if side > 0:
                    arm[:, 0] += 0.25 * kick
            else:
                head[:, 0] += 0.065 * kick
                jaw += 0.09 * kick
        wing[:, 2] = side * (0.23 * wind + 0.12 * fire)
    return torso, head, jaw, arm, tail, wing, hip


def _sample_pose(rig, action, t, curves, geometry, amount):
    parts = rig["parts"]
    legs = rig["legs"]
    piv = np.asarray([p["pivot"] for p in parts])
    height = np.ptp(np.concatenate([v for v in geometry if len(v)])[:, 1])
    reach = min((piv[l["hip"], 1] - piv[l["foot"], 1] for l in legs), default=height * 0.65)
    stages = _weights(action, t, curves)
    wind, fire, recoil = stages
    cycle = 2 * np.pi * t
    root = np.zeros((len(t), 3))
    base = np.tile(np.eye(4), (len(t), 1, 1))
    step = action in LOCOMOTION
    air = action in AERIAL or (
        not legs and rig["profile"] == "serpent" and any(p["role"] == "wing" for p in parts)
    )
    freelegs = air or action in {"death", "burrow-entry", "burrow-eruption"}
    crouch = np.full(len(t), 0.060)
    if step:
        crouch += 0.022 * np.cos(2 * cycle)
        base[:, 0, 3] = reach * 0.037 * amount * sample(curves["sway"], t % 1)
    elif action in {"coil", "siege-brace"}:
        crouch += 0.17 * sample(curves["depth"], t)
    elif action == "idle":
        crouch += 0.018 * (1 - np.cos(cycle))
    elif action in {"recover", "siege-recovery"}:
        crouch += 0.14 * (1 - smooth(t))
    else:
        crouch += 0.055 * wind + 0.035 * fire
    if action == "cockpit-cling":
        # Keep the supporting claws hooked while the creature hauls its jaws
        # toward the pane. Local support coordinates stay fixed during a bite.
        crouch += 0.12
        base[:, 2, 3] = -reach * amount * 0.08 * pulse((t * 2) % 1, 0, 0.42, 0.72)
    elif not step and action not in {"idle", "death", "warp-arrival"}:
        # The centre of mass loads onto the rear supports and follows the
        # head into the attack. World-space foot targets retain traction.
        base[:, 2, 3] = reach * amount * (0.085 * wind - 0.11 * fire + 0.045 * recoil)
    base[:, 1, 3] = -reach * amount * crouch
    direction = np.array([0, 0, 1 if action == "retreat" else -1])
    stride = reach * amount * ({"pursuit": 1.13, "retreat": 0.63, "lumber": 0.85}.get(action, 0.95))
    if step:
        root = t[:, None] * stride * direction
    attached = {}
    # Crawlers carry their claws on the forelegs. Rake with one lifted claw,
    # or draw both forelegs together to grapple, leaving the other feet planted.
    raking = set()
    if legs and not any(p["role"] == "arm" for p in parts) and action in {"rending-claws", "grapple"}:
        for flank in (1, -1) if action == "grapple" else (1,):
            candidates = [leg["foot"] for leg in legs if leg["side"] == flank]
            if candidates:
                raking.add(min(candidates, key=lambda f: piv[f, 2]))
    if not freelegs or action == "cockpit-cling":
        for leg in legs:
            h, k, f = (leg[key] for key in ("hip", "knee", "foot"))
            target = np.tile(piv[f], (len(t), 1))
            target[:, 1] += rig["floor"] - geometry[f][:, 1].min()
            if action == "cockpit-cling":
                target[:, 0] += reach * amount * 0.06 * leg["side"]
            elif step:
                shift = (leg["phase"] / (2 * math.pi)) % 1
                local = t - shift
                phase = local % 1
                side = "left" if leg["side"] < 0 else "right"
                duty = 0.54 if side == "left" else 0.52
                swing = np.clip((phase - duty) / (1 - duty), 0, 1)
                target += (
                    stride * direction * (np.floor(local) + shift + 0.5 * duty + smooth(swing) - t)[:, None]
                )
                target[:, 1] += reach * 0.17 * amount * sample(curves[side], swing)
            elif f in raking:
                lift = pulse(t, 0.02, 0.32, 0.85)
                target[:, 1] += reach * 0.43 * amount * lift
                target[:, 0] += leg["side"] * reach * 0.30 * amount * (0.45 * wind - fire)
                target[:, 2] -= reach * 0.32 * amount * fire
            solved = solve_leg(piv[h], piv[k], piv[f], apply(base, piv[h]), target)
            if solved is None:
                return None
            foot = np.tile(np.eye(4), (len(t), 1, 1))
            foot[:, :3, 3] = target - piv[f]
            attached[h], attached[k], attached[f] = *solved, foot
    frames = np.empty((len(t), len(parts), 4, 4))
    tail_depth = {}
    for j, p in enumerate(parts):
        par = p["parent"]
        role = p["role"]
        if j in attached:
            frames[:, j] = attached[j]
            continue
        parent = base if par < 0 else frames[:, par]
        side = p.get("side") or (-1 if piv[j, 0] < 0 else 1)
        phase = p.get("phase", 0)
        if role == "tail":
            tail_depth[j] = tail_depth.get(par, -1) + 1
            phase += tail_depth[j] * 0.6
        torso, head, jaw, arm, tail, wing, hip = _channels(action, t, curves, side, phase, stages)
        angles = np.zeros((len(t), 3))
        if role == "torso" or (par < 0 and not legs):
            angles = torso
        elif role == "head":
            angles = head
        elif role == "jaw":
            axis = p.get("rotation_axis", "y" if p["name"].startswith("mandible") else "x")
            angles[:, {"x": 0, "y": 1, "z": 2}[axis]] = jaw * (-1 if axis == "x" else side)
        elif role in {"arm", "barrel"}:
            # A barrel inherits its arm's aim once. Only articulate at a barrel pivot for recoil.
            if role == "arm":
                angles = arm
            elif action == "rocket-elbow":
                angles[:, 0] = 0.18 * fire
        elif role == "tail":
            angles = tail
        elif role == "wing":
            angles = wing
        elif role == "scanner":
            angles[:, 2] = side * (0.05 * np.sin(2 * cycle + phase) + 0.17 * wind)
        elif j in {l["hip"] for l in legs}:
            angles = hip.copy()
            if action == "death":
                angles[:, 0] -= 0.42 * smooth(t)
            elif action in {"burrow-entry", "burrow-eruption"}:
                angles[:, 1] += 0.25 * side * np.sin(2 * cycle + phase) * np.sin(np.pi * t) ** 2
            elif action in {"dodge", "tackle"}:
                # The forward legs reach for the landing while the rear pair
                # extend off the ground and then fold underneath the abdomen.
                rear = piv[j, 2] >= piv[0, 2]
                angles[:, 0] += (0.83 if rear else -0.47) * pulse(t, 0.08, 0.21, 0.46)
                angles[:, 0] += (-0.55 if rear else 0.64) * pulse(t, 0.35, 0.70, 0.91)
        elif role.endswith("knee") and freelegs:
            if action == "death":
                angles[:, 0] = 0.65 * smooth(t)
            elif action in {"dodge", "tackle"}:
                angles[:, 0] = 0.83 * flight_phase(t, 0.18, 0.71) - 0.24 * pulse(t, 0.72, 0.83, 1)
            elif action in {"burrow-entry", "burrow-eruption"}:
                angles[:, 0] = 0.54 * np.sin(2 * cycle + phase - 0.48) * np.sin(np.pi * t) ** 2
            else:
                angles[:, 0] = -0.28 * np.sin(cycle + phase)
        spin = Rotation.from_euler("xyz", angles * amount).as_matrix()
        frames[:, j] = parent @ rigid(spin, piv[j], piv[j])
    if action in {"wall-crawl", "ceiling-crawl"}:
        # Show a complete stride after attachment. Rotate around the average
        # foot contact plane, preserving every support's schedule on the wall.
        angle = math.pi / 2 if action == "wall-crawl" else math.pi
        rot = np.broadcast_to(Rotation.from_euler("z", angle).as_matrix(), (len(t), 3, 3))
        anchor = np.array([0.0, rig["floor"], 0.0])
        frames = rigid(rot, anchor, np.broadcast_to(anchor, (len(t), 3)))[:, None] @ frames
        # Vertical placement displays an isolated wall/ceiling in positive Y.
        root[:, 1] += height * 1.4
        return frames, root
    if freelegs or not legs:
        tilt = np.zeros((len(t), 3))
        lift = np.zeros_like(t)
        if action == "death":
            fall = smooth((t - 0.14) / 0.67)
            tilt[:, 2] = 1.42 * fall
            tilt[:, 0] = 0.18 * fall
            tilt[:, 2] -= 0.105 * pulse(t, 0.77, 0.86, 1)
        elif action == "cockpit-cling":
            tilt[:, 0] = 0.93
            tilt[:, 2] = 0.17
            lift += height * 0.45
        elif action in {"dodge", "tackle"}:
            leap = flight_phase(t, 0.15, 0.79)
            land = pulse(t, 0.73, 0.83, 1)
            tilt[:, 0] = (0.14 if action == "dodge" else 0.27) * leap - 0.19 * land
            tilt[:, 2] = (0.33 if action == "dodge" else 0.04) * leap
            travel = smooth((t - 0.11) / 0.75)
            root[:, 0] = height * 1.15 * travel if action == "dodge" else 0
            root[:, 2] = -height * (0.35 if action == "dodge" else 1.4) * travel
            lift += height * (0.57 if action == "tackle" else 0.32) * leap
        elif action in {"burrow-entry", "burrow-eruption"}:
            if action == "burrow-entry":
                # Local forward is -Z, so negative X pitch tips the head down.
                tilt[:, 0] = -0.82 * pulse(t, 0.05, 0.38, 0.92)
                lift -= height * 1.32 * smooth((t - 0.18) / 0.70)
                root[:, 2] = -height * 0.40 * smooth((t - 0.12) / 0.72)
            else:
                tilt[:, 0] = 0.79 * pulse(t, 0.07, 0.40, 0.79) - 0.16 * pulse(t, 0.70, 0.82, 1)
                lift -= height * 1.32 * (1 - smooth((t - 0.10) / 0.44))
                lift += height * 0.31 * flight_phase(t, 0.43, 0.80)
                root[:, 2] = -height * 0.33 * smooth((t - 0.13) / 0.68)
        elif action == "swim" or air:
            lift += height * (0.20 + 0.035 * np.cos(cycle))
        elif action == "slither" or (not legs and step):
            tilt[:, 1] = 0.17 * np.sin(cycle)
        origin = piv[0]
        spin = Rotation.from_euler("xyz", tilt).as_matrix()
        frames = rigid(spin, origin, np.broadcast_to(origin, (len(t), 3)))[:, None] @ frames
    else:
        lift = np.zeros_like(t)
    bottom = np.full(len(t), np.inf)
    for j, verts in enumerate(geometry):
        if len(verts):
            ys = frames[:, j, 1, :3] @ verts.T + frames[:, j, 1, 3, None]
            bottom = np.minimum(bottom, ys.min(axis=1))
    if action == "cockpit-cling" and legs:
        # Position the whole contact plane once, independent of the bite.
        bind_bottom = min(((v - origin) @ spin[0, 1] + origin[1]).min() for v in geometry if len(v))
        root[:, 1] += rig["floor"] - bind_bottom + lift
    elif freelegs or not legs:
        root[:, 1] += rig["floor"] - bottom + lift
    elif np.min(bottom) < rig["floor"] - 0.0001:
        return None
    return frames, root


def pose(rig_id, rig, action, t, context):
    if action not in actions(rig_id, rig, context):
        raise ValueError(f"{rig_id} cannot perform {action}")
    t = np.asarray(t, dtype=float)
    if t.ndim != 1 or not len(t) or not np.all(np.isfinite(t)) or np.any((t < 0) | (t > 1)):
        raise ValueError("Expected a nonempty array of normalized sample times")
    cache = context.setdefault("_creature_pose", {})
    if rig_id not in cache:
        cache[rig_id] = (triangles_by_joint(context["model"], rig), {})
    geometry, scales = cache[rig_id]
    # Calibrate once across the entire clip. Sampling one frame during
    # scrubbing must give the same pose as sampling it during playback.
    if action not in scales:
        calibration = np.linspace(0.0, 1.0, 257)
        initial = context["reference"]["metrics"]["reach_scale"]
        for factor in (1.0, 0.80, 0.60, 0.40, 0.25, 0.125, 0.0625):
            scale = initial * factor
            if _sample_pose(rig, action, calibration, context["curves"], geometry, scale) is not None:
                scales[action] = scale
                break
        else:
            raise ValueError(f"{rig_id}: no reachable contact trajectory for {action}")
    result = _sample_pose(rig, action, t, context["curves"], geometry, scales[action])
    if result is None:
        raise ValueError(f"{rig_id}: contact solve failed for {action}")
    return result
