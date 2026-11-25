"""Check the installed model set against the game's compiled catalog and approved art."""

import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
authoring = repo / "assets/mecha"
published = Path(sys.argv[2])
catalog = json.loads(subprocess.check_output([sys.argv[1]]))
study = repo / "art/mecha/vehicle-redesign-study"


def read(path):
    return json.loads(path.read_bytes())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# Old packs can fall back to vertex colours, so a missing release texture would
# otherwise be invisible to the mesh inventory checks below.
portrait_path = Path("fx/pilot-service.rgba")
assert sha(published / portrait_path) == sha(authoring / portrait_path)
portrait = (published / portrait_path).read_bytes()
assert len(portrait) == 512 * 512 * 4
helmets = [b"".join(portrait[(y*512+x)*4:(y*512+x+256)*4] for y in range(top,top+256))
           for top in (0,256) for x in (0,256)]
assert len(set(helmets)) == 4 and all(0 in cell[3::4] and 255 in cell[3::4] for cell in helmets)

terrain_path = Path("textures/terrain-surfaces.rgba")
assert sha(published / terrain_path) == sha(authoring / terrain_path)
assert (published / terrain_path).stat().st_size == 12 * 128 * 128 * 4

# Missing optional files activate the legacy cockpit path. Require the complete
# accepted set in the shipped pack and verify every sight cell is nonempty.
chassis = ("brawler", "scout", "striker", "titan", "auxiliary", "colossus", "hybrid", "vanguard")
atlas = Path("fx/cockpit-sights.rgba")
assert sha(authoring / atlas) == sha(published / atlas)
rgba = (published / atlas).read_bytes()
assert len(rgba) == 192 * 512 * 4
for i, name in enumerate(chassis):
    mesh = Path("cockpit/interiors") / (name + ".mesh")
    layout = mesh.with_suffix(".json")
    assert sha(authoring / mesh) == sha(published / mesh)
    assert sha(authoring / layout) == sha(published / layout)
    magic, version, count = struct.unpack_from("<4sII", (published / mesh).read_bytes())
    surfaces = read(published / layout)
    assert (magic, version) == (b"ISCP", 1) and (published / mesh).stat().st_size == 12 + count * 40
    assert 0 < surfaces["hull_vertices"] < count and len(surfaces["ammo"]) == 6
    for column in range(3):
        visible = 0
        for row in range(i * 64, (i + 1) * 64):
            start = (row * 192 + column * 64) * 4
            visible += sum(alpha > 64 for alpha in rgba[start+3:start+256:4])
        assert 10 < visible < 2048, (name, column, visible)
arms = Path("models/cockpit-arms.json")
assert sha(authoring / arms) == sha(published / arms)
assert set(read(published / arms)["pieces"]) == {
    f"cockpit.{name}.{side}.{part}" for name in chassis for side in ("L", "R") for part in ("upper", "fore", "hand")
}

# Alpha carries emission in the native atlas. A legacy opaque mask would
# make the entire model glow, including at distant mip levels.
texture_dir = published / "textures"
layout = read(texture_dir / "manifest.json")
assert (layout["alpha_usage"], layout["max_mip_level"], layout["max_sample_lod"]) == ("emission", 8, 5)
atlas = texture_dir / layout["rgba"]
assert sha(atlas) == layout["rgba_sha256"] == sha(authoring / "textures" / atlas.name)
alpha = atlas.read_bytes()[3::4]
assert len(alpha) == layout["width"] * layout["height"]
emitters = {"screen_scan", "energy_cell", "reactor_coils", "acid_glands", "circuit_array"}
for tile in layout["tiles"]:
    x, y, w, h = tile["cell"]
    rows = [alpha[(255-row)*256+x:(255-row)*256+x+w] for row in range(y, y+h)]
    assert (max(map(max, rows)) > 0) == (tile["name"] in emitters), tile["name"]

report = read(published / "runtime-models.json")
original_gpu = read(authoring / "models/manifest.json")["models"]
live_gpu = read(published / "models/manifest.json")["models"]
native = {p.stem for p in (published / "meshes").glob("*.json")}
assert native == set(report["active_native"])
assert set(live_gpu) == set(report["active_gpu"])
assert (
    set(report["retired_native"])
    == {p.stem for p in (authoring / "meshes").glob("*.json")} - native
)
assert set(report["retired_gpu"]) == original_gpu.keys() - live_gpu.keys()
assert all(not (published / "models" / (k + ".json")).exists() for k in report["retired_gpu"])

accepted = {}
for section in ("frames", "ground", "flight"):
    for path in (study / "fleet" / section / "meshes").glob("*.json"):
        accepted[path.stem] = path
for path in (
    "fleet/frames/recon/mecha_sniper.json",
    "vehicle/native/meshes/heavy_hover_tank.json",
    "aircraft/salvager_gunship.json",
):
    accepted[Path(path).stem] = study / path
assert len(accepted) == 115
voidmaw = study.parent / "voidmaw-redesign-study"
kaiju = {"kaiju_" + name: voidmaw / name for name in ("drone", "stalker", "obliterator")}
for model, folder in read(voidmaw / "roster/models.json").items():
    kaiju[model] = voidmaw / "roster" / folder
kaiju_kinds = {e["kind"] for e in catalog["enemies"] if e["id"] in ("kaiju_drone", "kaiju_stalker")}
base = {e["model"] for e in catalog["enemies"] if e["kind"] in kaiju_kinds}
alternates = {k + "_alt" for k in base if (authoring / "meshes" / (k + "_alt.json")).is_file()}
story = {r["id"] for r in read(authoring / "story-assets.json")["assets"] if r.get("source_id") in base}
assert kaiju.keys() == base | alternates | story and len(kaiju) == 20
registries = [read(p)["rigs"] for p in (published / "models").glob("*rigs.json")]
# Each published tank needs a yaw ring and an elevating barrel on that ring.
fleet_rigs = read(study / "fleet/ground/rigs.json")["rigs"]
hover_rigs = read(study / "vehicle/native/models/enemy-reference-rigs.json")["rigs"]
for model in ("heavy_hover_tank", "anti_mecha_hovertank", "anti_mecha_hovertank_alt"):
    heavy = model == "heavy_hover_tank"
    rig = (hover_rigs if heavy else fleet_rigs)[model]
    assert [r[model] for r in registries if model in r] == [rig], model
    assert rig["motion_profile"] == ("hover" if heavy else "tracked"), model
    parts = rig["parts"]
    if heavy:
        assert sum(part["role"] == "suspension" for part in parts) == 4, model
    barrels = [part for part in parts if part["role"] == "barrel"]
    assert barrels and all(part["parent"] >= 0 and parts[part["parent"]]["role"] == "turret"
                           for part in barrels), model

for model, folder in kaiju.items():
    accepted[model] = folder / (model + ".json")
    assert [r[model] for r in registries if model in r] == [read(folder / "rigs.json")["rigs"][model]], model
for name in ("chassis-voidmaw-appearance.json", "cover-unit-appearance.json"):
    assert kaiju.keys().isdisjoint(read(published / "models" / name)["models"])
# The skyline deck selects these IDs using their original footprint and ground pivot.
for family in ("setback", "industrial", "arcology"):
    model = "city_tower_" + family
    filename = model + ".json"
    source = study.parent / "environment-study" / "buildings" / "meshes" / filename
    approved = read(source)
    original = read(authoring / "meshes" / filename)
    assert approved["id"] == model and approved["pivot"] == original["pivot"]
    assert sha(source) != sha(authoring / "meshes" / filename), model
    for axis in range(3):
        coordinates = [v[axis] for v in approved["vertices"]]
        for edge, value in (("low", min(coordinates)), ("high", max(coordinates))):
            assert abs(value - original["bounds"][edge][axis]) < 0.001, model
    accepted[model] = source
# The expansion covers every live environment model outside the previously
# accepted replacements. Check the scope against the original role assignments.
environment_dir = study.parent / "environment-expansion"
environment = read(environment_dir / "catalog.json")["assets"]
roles = {"building", "scenery", "location_prop", "interior", "interior_prop", "prop", "objective"}
expected = {model for model in native - accepted.keys()
            if read(authoring / "meshes" / (model + ".json")).get("role") in roles}
assert expected == environment.keys()
for model in expected:
    filename = model + ".json"
    path = environment_dir / "meshes" / filename
    old = read(authoring / "meshes" / filename)
    new = read(path)
    assert new["id"] == model and new["pivot"] == old["pivot"], model
    assert new["faces"] != old["faces"] or new["vertices"] != old["vertices"], model
    for axis in range(3):
        for edge in (min, max):
            assert abs(edge(v[axis] for v in new["vertices"]) -
                       edge(v[axis] for v in old["vertices"])) < 0.001, model
    accepted[model] = path
assert accepted.keys() == report["replacements"].keys()
for model in native:
    filename = model + ".json"
    expected = accepted.get(model, authoring / "meshes" / filename)
    actual = sha(published / "meshes" / filename)
    assert actual == sha(expected), model
    if model in accepted:
        assert actual == report["replacements"][model]
        mesh = read(expected)
        assert mesh["budget"] == read(authoring / "meshes" / filename)["budget"], model
        assert len(mesh["faces"]) <= mesh["budget"], model

# New playable frames need both a complete third-person rig and tracked hand meshes.
manual_players = {"chassis_"+name for name in ("auxiliary", "colossus", "hybrid", "vanguard")}
manual_rigs = read(published / "models/manual-player-rigs.json")["rigs"]
assert manual_rigs.keys() == manual_players <= native and manual_players <= live_gpu.keys()
art = {row["name"]: row for row in read(published / "art/manifest.json")["assets"]}
for model, rig in manual_rigs.items():
    assert rig["mesh_vertices"] == len(read(published / "meshes" / (model+".json"))["faces"])*3
    arms = {(p["arm"], p["tracked_segment"]) for p in rig["parts"] if "tracked_segment" in p}
    assert arms == {(side, part) for side in (0, 1) for part in ("shoulder", "upper-arm", "forearm")}
    assert len(rig["booster_sockets"]) == 2
    portrait = art["portrait-"+model.removeprefix("chassis_")+"-0"]
    assert sha(published / "art" / portrait["rgba"]) == portrait["sha256"]

for model, record in live_gpu.items():
    file = Path("models") / record["file"]
    if model in accepted:
        native_mesh = read(accepted[model])
        gpu_mesh = read(published / file)
        assert gpu_mesh["faces"] == native_mesh["faces"], model
        assert gpu_mesh["vertices"] == [
            [v * 100 for v in xyz] for xyz in native_mesh["vertices"]
        ], model
    else:
        assert sha(published / file) == sha(authoring / file), model

arsenal = {r["id"] for r in read(published / "arsenal-manifest.json")["assets"]}
assert "arsenal_safe_box" in arsenal

# Old packs fall back to static weapons. The shipping pack must include a
# complete, contiguous mesh library with moving parts for every armament.
for suffix in ("json", "bin"):
    file = Path(f"models/weapon-workings.{suffix}")
    assert sha(published / file) == sha(authoring / file)
workings = read(published / "models/weapon-workings.json")
assert workings["schema"] == 1 and set(workings["weapons"]) == arsenal - {"arsenal_safe_box"}
ranges = []
weapon_range = {row["model"]: row["range"] for row in catalog["equipment"] if row["model"] in arsenal}
weapon_bytes = (published / "models/weapon-workings.bin").read_bytes()
for model, assembly in workings["weapons"].items():
    parts = assembly["parts"]
    expected_ports = 4 if model.startswith("arsenal_rocket_rack") else 2 if model.startswith(("arsenal_scattergun", "arsenal_seeker_pod")) else 0
    assert len(assembly.get("ports", [])) == expected_ports, model
    assert any(len(part.get("frames", [])) == 25 for part in parts), model
    for part in parts:
        assert part["count"] > 0 and part["count"] % 3 == 0, model
        ranges.append((part["first"], part["first"] + part["count"]))
    if assembly["reach"] > 0 or model.startswith(("arsenal_rifle", "arsenal_scattergun", "arsenal_pulse_carbine")):
        vertices = [
            struct.unpack_from("<3f", weapon_bytes, i * 52)
            for part in parts for i in range(part["first"], part["first"] + part["count"])
        ]
        if assembly["reach"] > 0:
            assert assembly["reach"] == weapon_range[model], model
            tip = max(-z for _, _, z in vertices)
            assert abs(tip - assembly["socket"] - weapon_range[model]) < .001, model
        else:
            old = read(authoring / "meshes" / (model + ".json"))["bounds"]
            new_width = max(v[0] for v in vertices) - min(v[0] for v in vertices)
            assert new_width > 1.1 * (old["high"][0] - old["low"][0]), model
    if model.startswith(("arsenal_pulse_carbine", "arsenal_rail_lance", "arsenal_blade")):
        assert any(len(part.get("glow", [])) == 25 for part in parts), model
ranges.sort()
assert ranges[0][0] == 0 and all(a[1] == b[0] for a, b in zip(ranges, ranges[1:]))
assert ranges[-1][1] * 13 * 4 == (published / "models/weapon-workings.bin").stat().st_size
gear = {g["model"] for g in catalog["equipment"] if g["player_visual"]}
assert gear == set(read(repo / "tools/mecha_assets/player-visuals.json"))
enemies = {entry["model"] for entry in catalog["enemies"]}
for model in enemies | gear:
    assert model in native, model
    alternate = model + "_alt"
    if (authoring / "meshes" / (alternate + ".json")).exists():
        assert alternate in native, alternate
        if model in gear:
            assert alternate in live_gpu or alternate in arsenal, alternate
for model in gear:
    assert model in live_gpu or model in arsenal, model
assert {
    "weapon_scouting_drone",
    "weapon_scouting_drone_alt",
    "airbrake_panel",
    "iron-soldier",
} <= live_gpu.keys()
assert {
    "floor_tile",
    "weapon_fusion_grenade",
    "weapon_targeting_array",
    "weapon_sentry_turret",
}.isdisjoint(native)
assert not any((published / directory).exists() for directory in ("provenance", "cinematics"))

print(
    f"{len(accepted)} replacements, {len(native)} native meshes, {len(live_gpu)} GPU bindings, "
    f"{len(arsenal)} direct arsenal bindings and {len(enemies)} enemy catalog models verified"
)
