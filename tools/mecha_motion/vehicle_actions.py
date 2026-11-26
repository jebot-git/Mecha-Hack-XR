"""Rigid motion studies for the vehicle, installation and objective bindings.

Motion names follow the weapon catalogue and vehicle passes. The output animates
the launch platform. Projectiles, weather, explosions and spawned air wings are
separate gameplay actors/effects. The two ally mission assets have just one
bound hull; impact and collapse can move it but cannot articulate its limbs.
"""
import numpy as np
from scipy.spatial.transform import Rotation

from .bake import rigid, sample, smooth, triangles_by_joint
from .rigs import NPC_LIBRARIES, PLAYER_LIBRARIES, NON_WALKERS, read


# Catalogue attacks that give the launch platform different windups or recoveries.
# Shared names cover attacks with the same mechanical delivery across models.
WEAPONS = {
    "anti_mecha_hovertank": ("cannon-fire", "chain-gun-burst", "rocket-volley"),
    "fast_attack_hovercraft": ("chain-gun-burst", "tow-cable-cast"),
    "heavy_hover_tank": ("point-defence-fire", "cannon-fire", "mine-release", "rocket-volley"),
    "cyclone_drone": ("ray-burst", "vortex-charge"),
    "deepdiver_submarine": ("torpedo-spread", "sonar-pulse"),
    "salvager_gunship": ("chain-gun-burst", "harpoon-cast"),
    "bomber": ("chain-gun-burst", "bombing-run"),
    "interceptor": ("chain-gun-burst", "seeker-launch"),
    "gunboat": ("point-defence-fire", "missile-salvo"),
    "carrier": ("point-defence-fire", "launch-air-wing"),
    "cruiser": ("point-defence-fire", "missile-salvo", "heavy-broadside"),
    "dreadnought": ("point-defence-fire", "missile-salvo", "heavy-broadside", "launch-air-wing"),
    "artillery_battery": ("point-defence-fire", "artillery-bombardment"),
    "sentry_gun": ("point-defence-fire", "rocket-volley"),
    "ion_disruptor_installation": ("point-defence-fire", "ion-barrage"),
    "ion_emplacement": ("point-defence-fire", "ion-barrage"),
}
MOBILITY = {
    "tracked": ("track-drive", "pivot-turn", "withdraw"),
    "hover": ("hover-cruise", "strafe-pass", "break-away"),
    "submersible": ("submerged-cruise", "dive-turn", "break-away"),
    "fixed-wing": ("flight-cruise", "banked-pass", "climb-out"),
    "vtol": ("hover-cruise", "strafe-pass", "climb-out"),
    "starship": ("fleet-cruise", "banked-pass", "withdraw"),
    "stationary": (),
}


def discover(pack):
    """Resolve the same first-match published bindings as the gait baker."""
    published = read(pack / "runtime-models.json")
    native = set(published["active_native"])
    result, seen = {}, set()
    for name in NPC_LIBRARIES + PLAYER_LIBRARIES:
        source = f"models/{name}.json"
        library = read(pack / source)
        if library["schema"] != 1:
            raise ValueError(f"Unsupported registry: {source}")
        for key, rig in library["rigs"].items():
            if key in seen:
                continue
            seen.add(key)
            base = key.removesuffix("_alt")
            if rig.get("archetype") not in NON_WALKERS and base != "ally":
                continue
            if key not in native:
                raise ValueError(f"{key}: missing native vehicle mesh")
            profile = rig.get("motion_profile", "legacy")
            kind = profile
            if base == "ally":
                kind = "objective"
                if len(rig["parts"]) != 1 or rig["parts"][0]["role"] != "rigid":
                    raise ValueError(f"{key}: inspect new objective articulation")
            elif base in {"deepdiver_submarine", "cyclone_drone"}:
                kind = "submersible"
            elif profile == "naval":
                kind = "starship"
            elif profile == "flight":
                kind = "vtol" if base == "salvager_gunship" else "fixed-wing"
            if kind != "objective" and (kind not in MOBILITY or base not in WEAPONS):
                raise ValueError(f"{key}: review movement/attack repertoire")
            family = {"objective": "objective", "stationary": "installation"}.get(kind, "vehicle")
            result[key] = {
                "source": source, "mesh": f"meshes/{key}.json", "mesh_scale": [1, 1, 1],
                "family": family, "kind": kind, "player": False, "parts": rig["parts"],
                "legs": [], "profile": profile, "floor": rig["floor"], "mesh_vertices": rig["mesh_vertices"],
            }
    return result


def actions(rig_id, rig, context):
    kind = rig["kind"]
    reactions = ["impact-stagger", "destruction"]
    if kind == "objective":
        return reactions
    base = rig_id.removesuffix("_alt")
    return ["idle-scan", *MOBILITY[kind], "aim-track", "ranged-windup", *WEAPONS[base], *reactions]


def duration(action):
    if action in {"idle-scan", "fleet-cruise", "flight-cruise", "hover-cruise", "submerged-cruise", "track-drive"}:
        return 2.5
    if action in {"bombing-run", "launch-air-wing", "artillery-bombardment", "destruction"}:
        return 2.2
    if action == "impact-stagger":
        return 0.9
    return 1.6


# Discharge instants on the normalized action timeline. Successive stations in a
# broadside fire in sequence, while the fighter fires its aligned guns together.
DISCHARGES = {
    "point-defence-fire": (.34, .50, .66),
    "chain-gun-burst": (.28, .38, .48, .58, .68, .78),
    "rocket-volley": (.40, .57, .74), "missile-salvo": (.32, .45, .58, .71),
    "seeker-launch": (.43, .67), "torpedo-spread": (.34, .68),
    "bombing-run": (.35, .51, .67), "mine-release": (.43, .64),
    "cannon-fire": (.55,), "artillery-bombardment": (.43, .72),
    "heavy-broadside": (.50,), "ion-barrage": (.48, .62, .76),
    "harpoon-cast": (.46,), "tow-cable-cast": (.38,),
    "ray-burst": (.39, .55, .71), "sonar-pulse": (.56,),
    "vortex-charge": (.70,), "launch-air-wing": (.36, .66),
}


def pulse(t, at, width):
    """Fast tripped release and a smooth, slower recovery."""
    u = (t - at) / width
    return smooth(u / .19) * (1 - smooth((u - .19) / .81))


# These actions launch from a bay, rack, or sensor. The exposed gun stays
# clear while the hull, hydroplanes, or scanner carries the motion.
AUXILIARY_RELEASES = {
    "sonar-pulse", "mine-release", "bombing-run", "launch-air-wing",
    "rocket-volley", "missile-salvo", "seeker-launch",
}


def station_kick(action, index, count, t):
    """A local breech impulse for each mount, in its barrel's own coordinates."""
    if action in AUXILIARY_RELEASES or action not in DISCHARGES:
        return np.zeros_like(t)
    if action == "heavy-broadside":
        times = (.38 + .30 * index / count,)
    elif action == "torpedo-spread":
        times = (DISCHARGES[action][index % len(DISCHARGES[action])],)
    elif action == "point-defence-fire" and count > 1:
        # Stagger neighbouring stations instead of synchronizing a whole fleet.
        times = tuple(at + .10 * index / count for at in DISCHARGES[action])
    else:
        times = DISCHARGES[action]
    width = .22 if action in {"heavy-broadside", "artillery-bombardment", "cannon-fire"} else .15
    return np.maximum.reduce([pulse(t, at, width) for at in times])


def timing_anchors(action):
    """Lock releases (including staggered hull stations) against learned retiming."""
    if action == "heavy-broadside":
        return sorted({.38 + .30 * i / n for n in (5, 6) for i in range(n)})
    if action == "point-defence-fire":
        return sorted({at + .10 * i / n for at in DISCHARGES[action]
                       for n in (1, 3, 5, 6) for i in range(n)})
    if action == "destruction":
        return (.29, .70, .94)
    if action == "impact-stagger":
        return (.12, .36, .64)
    return DISCHARGES.get(action, ())


def pose(rig_id, rig, action, t, context):
    if action not in actions(rig_id, rig, context):
        raise ValueError(f"{rig_id}: inapplicable vehicle action {action}")
    t = np.asarray(t, dtype=float)
    if t.ndim != 1 or len(t) == 0 or not np.isfinite(t).all() or np.any((t < 0) | (t > 1)):
        raise ValueError("Expected normalized sample times")
    parts, kind = rig["parts"], rig["kind"]
    curves = context["curves"]
    geometry = triangles_by_joint(context["model"], rig)
    vertices = np.concatenate([v for v in geometry if len(v)])
    height, length = np.maximum(np.ptp(vertices, axis=0), .1)[1:]
    zeros = np.zeros_like(t)
    root = np.zeros((len(t), 3))
    tau = 2 * np.pi
    arc = np.sin(np.pi * t) ** 2
    oscillate = np.sin(tau * t)
    # Borrow the Arc-trained anticipation/release curves for platform settling,
    # barrel cocking and the counter-rotation of the sight and flight surfaces.
    shoulder = sample(curves["shoulder"], np.minimum(t / .43, (1 - t) / .42).clip(0, 1))
    load = sample(curves["depth"], np.minimum(t / .47, (1 - t) / .43).clip(0, 1))
    phases = DISCHARGES.get(action, ())
    kicks = [pulse(t, at, .16) for at in phases]
    kick = np.maximum.reduce(kicks) if kicks else zeros
    heavy = action in {"cannon-fire", "artillery-bombardment", "heavy-broadside"}
    impact = zeros
    if action == "impact-stagger":
        impact = pulse(t, .12, .31) - .43 * pulse(t, .36, .32) + .13 * pulse(t, .64, .25)
    wreck = smooth((t - .12) / .70) if action == "destruction" else zeros
    breaking = action in {"break-away", "climb-out", "banked-pass", "dive-turn", "strafe-pass"}
    retreat = action == "withdraw"
    drive = action in MOBILITY.get(kind, ()) or action == "bombing-run"
    combat = bool(phases) or action == "ranged-windup"
    aerial = kind in {"fixed-wing", "vtol", "submersible", "starship"}
    hovering = kind == "hover"
    rigid_base = kind == "stationary"
    yaw, pitch, roll = zeros.copy(), zeros.copy(), zeros.copy()
    turret_yaw, barrel_pitch = zeros.copy(), zeros.copy()

    # A positive-X side slip needs negative yaw when flying along local -Z.
    # Bank toward the acceleration, then counter-bank to level out at the exit.
    turn_load = np.sin(tau * t) * np.sin(np.pi * t)
    if drive:
        travel = length * (1.0 if breaking else .9) * t
        if action == "pivot-turn":
            yaw = .98 * smooth(t)
        elif retreat:
            root[:, 2] = .76 * travel
        else:
            root[:, 2] = -travel
        if breaking:
            # Integrated sin² velocity has a smooth, zero-slope entry and exit.
            side_distance = .54 if action in {"banked-pass", "break-away", "dive-turn"} else .38
            root[:, 0] = side_distance * length * (t - oscillate / tau)
            course = -np.arctan(2 * side_distance * arc)
            if action == "strafe-pass":
                yaw = .18 * arc  # Stay pointed toward the target on the inside.
            else:
                yaw = course
            bank = {"fixed-wing": .69, "vtol": .38, "starship": .23,
                    "submersible": .32, "hover": .29}.get(kind, .2)
            roll = -bank * turn_load
        if action == "dive-turn":
            root[:, 1] = .75 * height * (1 - smooth(t))
            pitch = -np.arctan(4.5 * (height / length) * t * (1 - t))
        elif action == "climb-out":
            root[:, 1] = height * smooth(t)
            pitch = np.arctan(6 * (height / length) * t * (1 - t))
        elif action == "bombing-run":
            # Descend through the run-in, release at the trough, then pull out.
            root[:, 1] = .48 * height * (1 - arc)
            pitch = -np.arctan(.48 * height * np.pi * oscillate / (.9 * length))
    if kind == "tracked":
        # Both rigid track beds stay level in contact with the floor.
        # Breech travel and the turret bearing absorb fire without rocking the treads.
        if action in {"track-drive", "withdraw"}:
            direction = .76 if retreat else -1
            heading_range = .13 if retreat else .035
            yaw += heading_range * oscillate
            root[:, 0] += direction * .9 * length * heading_range * (1 - np.cos(tau * t)) / tau
    elif aerial or hovering:
        clearance = .32 if aerial else .09
        root[:, 1] += height * (clearance + .022 * oscillate) * (1 - wreck)
        pitch += .017 * oscillate * (1 - wreck)
        roll += .016 * np.sin(tau * t + .7) * (1 - wreck)
    if kind != "objective":
        if action == "idle-scan":
            turret_yaw += .42 * np.tanh(1.6 * oscillate) / np.tanh(1.6)
        if action == "aim-track":
            turret_yaw += .78 * oscillate
            barrel_pitch += .26 * (1 - np.cos(tau * t))
        if combat:
            turret_yaw += (.86 if action == "heavy-broadside" else .28) * shoulder
            barrel_pitch += (.48 if action == "artillery-bombardment" else .17) * load
        if action == "ranged-windup":
            # Traverse beyond the mark, settle on target, then hold the elevation.
            acquire = smooth(t / .33) * (1 - smooth((t - .77) / .23))
            turret_yaw = .31 * acquire + .055 * pulse(t, .22, .22)
            barrel_pitch = .22 * acquire * (.8 + .2 * load)
        if action == "launch-air-wing":
            # Trim the carrier level and react to each launch leaving the deck.
            pitch += .085 * kick - .045 * load
        if action in {"rocket-volley", "missile-salvo", "seeker-launch"} and not rigid_base:
            if kind != "tracked":
                pitch += .035 * kick
            root[:, 2] += .008 * length * kick
        if action == "torpedo-spread":
            yaw += .045 * (kicks[0] - kicks[1])
        if action == "bombing-run":
            pitch += .035 * kick
        if action == "sonar-pulse":
            pitch -= .024 * load
        if action in {"tow-cable-cast", "harpoon-cast"}:
            roll += .09 * kick
        if action == "mine-release":
            pitch -= .09 * load
        if rig_id.removesuffix("_alt") == "cyclone_drone" and (combat or action == "aim-track"):
            yaw += turret_yaw
            pitch += .18 * load
    if not rigid_base:
        # First rupture, main fall and a final impact before the wreck settles.
        fall = smooth((t - .29) / .47) if action == "destruction" else zeros
        landing = pulse(t, .70, .24) if action == "destruction" else zeros
        roll += .19 * impact + (.89 if aerial else .40) * fall + .12 * landing
        yaw -= .15 * impact + .21 * wreck
        pitch += .13 * impact - (.62 if aerial else .19) * wreck + .11 * landing
        root[:, 0] += length * (.025 * impact + .035 * fall)
        if action == "destruction" and aerial:
            root[:, 2] -= .65 * length * smooth(t)
        if kind == "objective":
            roll += .91 * fall  # Single-piece, top-heavy rescue objective.
        if aerial:
            if action == "heavy-broadside":
                roll += .10 * pulse(t, .39, .50)
            elif action not in AUXILIARY_RELEASES:
                pitch += (.09 if heavy else .04) * kick

    transforms = np.tile(np.eye(4), (len(t), len(parts), 1, 1))
    mount_groups = [i for i, p in enumerate(parts) if p["role"] == "barrel"]
    # One hull-local aim point gives divided batteries convergent sight lines.
    target = np.column_stack((-np.sin(turret_yaw), np.tan(barrel_pitch), -np.cos(turret_yaw)))
    target = np.asarray(parts[0]["pivot"]) + target * (length * 2.5)
    for i, part in enumerate(parts):
        joint = part["role"]
        piv = np.asarray(part["pivot"])
        rx, ry, rz = zeros.copy(), zeros.copy(), zeros.copy()
        shift = np.zeros_like(root)
        if part["parent"] < 0:
            rx, ry, rz = pitch, yaw, roll
        elif joint == "turret":
            ry = turret_yaw + .12 * impact + .32 * wreck
            if action == "aim-track":
                direction = target - piv
                ry = np.arctan2(-direction[:, 0], -direction[:, 2])
            elif action in {"pivot-turn", "strafe-pass", "track-drive", "withdraw"}:
                ry = -.84 * yaw  # Stabilized turret counters chassis steering.
            if action in {"rocket-volley", "missile-salvo", "seeker-launch"}:
                ry += .045 * kick
            if rigid_base:
                # Both bolted foundation and casing stay fixed. The failed
                # traverse bearing carries the upper carriage over to one side.
                collapse = smooth((t - .29) / .47) if action == "destruction" else zeros
                jolt = pulse(t, .70, .24) if action == "destruction" else zeros
                rz = .12 * impact + .33 * collapse + .075 * jolt
        elif joint == "barrel":
            release = station_kick(action, mount_groups.index(i), len(mount_groups), t)
            parent = part["parent"]
            direct_mount = parts[parent]["role"] != "turret"
            ry = turret_yaw if direct_mount else zeros
            rx = barrel_pitch + .08 * release - .26 * wreck - .07 * impact
            if action == "aim-track":
                # The turret has already traversed. Express the same target in
                # this parent's local space before elevating the barrel.
                parent_in_hull = np.linalg.inv(transforms[:, 0]) @ transforms[:, parent]
                relative = target - parent_in_hull[:, :3, 3]
                direction = np.einsum("nji,nj->ni", parent_in_hull[:, :3, :3], relative) - piv
                rx = np.arctan2(direction[:, 1], np.hypot(direction[:, 0], direction[:, 2]))
                if direct_mount:
                    ry = np.arctan2(-direction[:, 0], -direction[:, 2])
            stroke = np.clip((part["high"][2] - part["low"][2]) * (.21 if heavy else .12), .05, .95)
            shift[:, 2] = stroke * release
        elif joint == "wing":
            # Trim surfaces command the bank before the fuselage follows.
            command = .28 * np.sin(tau * (t + .06)) * np.sin(np.pi * t) if breaking else zeros
            rx = float(part.get("side", 1)) * (command + .45 * roll) + .62 * pitch
        elif joint == "rotor":
            turns = 3 if kind == "vtol" else 2
            if action == "vortex-charge":
                turns += 2
            # Integrate a decelerating angular velocity so wrecked rotors stop.
            if action == "destruction":
                coast = np.minimum(t / .84, 1)
                u = .84 * (coast - .5 * coast**2)
            else:
                u = t
            angle = (part.get("side", 0) or 1) * tau * turns * u
            if part.get("rotation_axis", "y") == "z":
                rz = angle
            else:
                ry = angle
        elif joint == "scanner":
            ry = (.76 * oscillate + .2 * turret_yaw) * (1 - wreck) + .15 * wreck
            if action == "sonar-pulse":
                ry += tau * smooth(t)
        elif joint == "suspension":
            side = np.sign(piv[0])
            fore = np.sign(piv[2])
            shift[:, 1] = height * (.015 * side * oscillate + .09 * side * roll
                                     + .10 * fore * pitch - (.055 if heavy else .023) * kick) * (1 - wreck)
        q = Rotation.from_euler("xyz", np.column_stack((rx, ry, rz))).as_matrix()
        if joint == "barrel":
            # Recoil slides down the elevated/traversed bore, not the hull Z axis.
            shift = np.einsum("nij,nj->ni", q, shift)
        local = rigid(q, piv, piv + shift)
        parent = part["parent"]
        transforms[:, i] = local if parent < 0 else transforms[:, parent] @ local

    bottom = np.full(len(t), np.inf)
    for i, v in enumerate(geometry):
        if len(v):
            bottom = np.minimum(bottom, (transforms[:, i, 1, :3] @ v.T).min(axis=1) + transforms[:, i, 1, 3])
    if hovering or aerial:
        # Flight altitude follows the commanded path; banking a long wing
        # mustn't lift the entire craft whenever the wingtip moves downward.
        rest_floor = vertices[:, 1].min()
        root[:, 1] += rig["floor"] - rest_floor
        root[:, 1] = np.maximum(root[:, 1], rig["floor"] - bottom)
        if action == "destruction":
            root[:, 1] += wreck * (rig["floor"] - bottom - root[:, 1])
    elif not rigid_base:
        root[:, 1] += rig["floor"] - bottom
    return transforms, root
