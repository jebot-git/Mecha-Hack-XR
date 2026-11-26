"""Discover the articulated actor models in a published Mecha Hack asset pack."""

import json
from pathlib import Path

# These follow enemyAnimationRig() and loadModels() in order of lookup.
NPC_LIBRARIES = (
    "voidmaw-proportion-rigs",
    "story-enemy-rigs",
    "enemy-reference-rigs",
    "enemy-redesign-rigs",
    "mecha-redesign-enemy-rigs",
    "enemy-animation-rigs",
    "manual-animation-rigs",
)
PLAYER_LIBRARIES = (
    "mecha-redesign-player-rigs",
    "manual-player-rigs",
    "expansion-animation-rigs",
    "animation-rigs",
)
NON_WALKERS = {
    "aircraft": "Aircraft or naval vehicle uses its flight or naval motion controller.",
    "hovercraft": "Vehicle uses hull, suspension or turret motion.",
    "installation": "Stationary installation uses an aiming rig.",
}
ACTOR_ROLES = {"mecha", "kaiju", "walker", "squad"}


def read(path):
    with path.open(encoding="utf-8") as file:
        return json.load(file)


def ace_elbow_hinges(parts, mesh):
    """Rebind the Ace's gauntlet, hand and equipment plates at the approved elbow."""
    vertices, faces = mesh["vertices"], mesh["faces"]
    rebuilt = []
    indices = {}
    forearms = {}
    for old_index, original in enumerate(parts):
        part = original.copy()
        parent = part["parent"]
        part["parent"] = indices[parent] if parent >= 0 else -1
        if part["name"] == "barrel_1":
            part["parent"] = forearms[1]
        indices[old_index] = len(rebuilt)
        rebuilt.append(part)
        if part["name"] not in {"arm_-1", "arm_1"}:
            continue
        side = part["side"]
        pieces = [c for c in mesh["components"] if c["semantic"] == part["name"]]
        # Shoulder shell, pauldron, upper strut, gauntlet, hand, wrist fittings,
        # plus the long left-hand weapon. Both Ace liveries use this assembly.
        if [c["count"] for c in pieces] != [20, 28, 12, 28, 12, 12] + ([8] if side == -1 else []):
            raise ValueError("Ace arm components changed; inspect the elbow binding")

        def bind(groups):
            ranges = [[c["first"] * 3, c["count"] * 3] for c in groups]
            points = [
                vertices[i]
                for c in groups
                for face in faces[c["first"] : c["first"] + c["count"]]
                for i in face["indices"]
            ]
            low = list(map(min, zip(*points)))
            high = list(map(max, zip(*points)))
            return {"ranges": ranges, "low": low, "high": high}

        forearm = {
            **part,
            **bind(pieces[3:]),
            "name": f"forearm_{side}",
            "role": "rigid",
            "hinge": "elbow",
            "parent": indices[old_index],
        }
        gauntlet = pieces[3]
        points = [
            vertices[i]
            for f in faces[gauntlet["first"] : gauntlet["first"] + gauntlet["count"]]
            for i in f["indices"]
        ]
        top = max(p[1] for p in points)
        rim = [p for p in points if p[1] > top - 0.5]
        pivot = [sum(axis) / len(rim) for axis in zip(*rim)]
        # The accepted neutral hinge sits 0.421667 m below the top of its
        # 3.383334 m gauntlet; rescale along Y for the shorter alternate.
        pivot[1] = top - (top - min(p[1] for p in points)) * (0.421667 / 3.383334)
        forearm["pivot"] = pivot
        part.update(bind(pieces[:3]))
        forearms[side] = len(rebuilt)
        rebuilt.append(forearm)
    return rebuilt


def leg_chains(parts, rig_id):
    chains = []
    for i, foot in enumerate(parts):
        if foot["role"].split("-")[-1] != "foot":
            continue
        k = foot["parent"]
        h = parts[k]["parent"] if k >= 0 else -1
        if h < 0 or parts[k]["role"].split("-")[-1] != "knee" or parts[h]["role"].split("-")[-1] != "hip":
            raise ValueError(f"{rig_id}: {foot['name']} has no hip/knee chain")
        side = foot.get("side") or (-1 if foot["pivot"][0] < 0 else 1)
        phase = parts[h].get("phase")
        if not any(p.get("phase") for p in parts if p["role"].split("-")[-1] == "hip"):
            phase = 0 if side == -1 else 3.141592653589793
        chains.append({"hip": h, "knee": k, "foot": i, "side": side, "phase": phase})
    return chains


def discover(pack):
    pack = Path(pack)
    bindings = {}
    published = read(pack / "runtime-models.json")
    native = set(published["active_native"])
    active = native | set(published["active_gpu"])
    for libraries, player in ((NPC_LIBRARIES, False), (PLAYER_LIBRARIES, True)):
        for library in libraries:
            source = "models/" + library + ".json"
            registry = read(pack / source)
            if registry["schema"] != 1:
                raise ValueError(f"Unsupported registry schema in {source}")
            for rig_id, rig in registry["rigs"].items():
                if rig_id not in active:
                    raise ValueError(f"{source} binds unpublished actor {rig_id}")
                bindings.setdefault(rig_id, (source, rig, player))

    # A new rigless actor must interrupt the batch until its anatomy is classified.
    for rig_id in sorted(native - bindings.keys()):
        metadata = read(pack / "meshes" / (rig_id + ".json"))
        if metadata.get("role") in ACTOR_ROLES:
            raise ValueError(f"{rig_id}: published actor has no rig binding")

    applicable, excluded = {}, {}
    for rig_id, (source, rig, player) in sorted(bindings.items()):
        mesh = ("meshes/" if rig_id in native else "models/") + rig_id + ".json"
        if not (pack / mesh).is_file():
            raise FileNotFoundError(pack / mesh)
        if rig_id not in native and rig_id != "iron-soldier":
            raise ValueError(f"{rig_id}: unsupported GPU-only mesh coordinates")
        anatomy = rig.get("archetype", "biped" if player else None)
        if anatomy in NON_WALKERS:
            excluded[rig_id] = NON_WALKERS[anatomy]
            continue
        parts = []
        for i, raw in enumerate(rig["parts"]):
            p = raw.copy()
            p.setdefault("name", p["role"] + "_" + str(i))
            if not -1 <= p["parent"] < i or len(p["pivot"]) != 3:
                raise ValueError(f"{rig_id}: invalid parent or pivot at part {i}")
            parts.append(p)
        if rig_id in {"mecha_ace", "mecha_ace_alt"}:
            parts = ace_elbow_hinges(parts, read(pack / mesh))
        legs = leg_chains(parts, rig_id)
        profile = rig.get("motion_profile", "legacy")
        if legs:
            kind = "crawler" if len(legs) > 2 and anatomy != "infantry" else "biped"
        elif profile == "serpent" and any(p["role"] == "tail" for p in parts):
            kind = "serpent"
        elif rig_id in {"ally", "ally_alt"} and len(parts) == 1 and parts[0]["role"] == "rigid":
            excluded[rig_id] = "Objective prop has one rigid hull and no articulated joints."
            continue
        else:
            raise ValueError(f"{rig_id}: unsupported actor anatomy {anatomy}, profile {profile}")
        family = (
            "infantry"
            if anatomy == "infantry"
            else "kaiju"
            if profile in {"organic", "insect", "serpent"} or anatomy == "kaiju"
            else "mecha"
        )
        applicable[rig_id] = {
            "source": source,
            "family": family,
            "kind": kind,
            "player": player,
            "mesh": mesh,
            "mesh_scale": [1, 1, 1] if rig_id in native else [0.01, 0.01, -0.01],
            "parts": parts,
            "legs": legs,
            "profile": profile,
            "floor": rig["floor"],
            "mesh_vertices": rig["mesh_vertices"],
        }
    return applicable, excluded
