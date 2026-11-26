"""Export an animated native rigid hierarchy to glTF 2.0."""

import io
import json
import struct

import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation


def streams(model):
    faces = model["faces"]
    triangles = np.array(model["vertices"])[np.array([f["indices"] for f in faces])]
    uv = np.array([f["uv"] for f in faces])
    colors = np.array([f["color"] for f in faces]) / 255.0
    return triangles, uv, colors


def atlas(root):
    return Image.frombytes("RGBA", (256, 256), (root / "textures/psx-atlas.rgba").read_bytes())


def export_rigid_glb(path, model, parts, owners, track, travel, root, fps, takes=None):
    triangles, uv, colors = streams(model)
    data = bytearray()
    doc = {
        "asset": {"version": "2.0", "generator": "Mecha Hack rigid motion baker"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "bufferViews": [],
        "accessors": [],
        "nodes": [
            {"name": "Root travel", "children": [i + 1 for i, p in enumerate(parts) if p["parent"] < 0]}
        ],
        "meshes": [],
        "animations": [],
    }

    def view(raw):
        data.extend(b"\0" * (-len(data) % 4))
        i = len(doc["bufferViews"])
        doc["bufferViews"].append({"buffer": 0, "byteOffset": len(data), "byteLength": len(raw)})
        data.extend(raw)
        return i

    def acc(array, kind, limits=False):
        array = np.asarray(array, dtype="<f4").reshape(
            -1, {"VEC2": 2, "VEC3": 3, "VEC4": 4, "SCALAR": 1}[kind]
        )
        item = {"bufferView": view(array.tobytes()), "componentType": 5126, "type": kind, "count": len(array)}
        if limits:
            item.update(min=array.min(axis=0).tolist(), max=array.max(axis=0).tolist())
        doc["accessors"].append(item)
        return len(doc["accessors"]) - 1

    pixels = atlas(root).transpose(Image.Transpose.FLIP_TOP_BOTTOM).convert("RGB")
    png = io.BytesIO()
    pixels.save(png, format="PNG")
    doc["images"] = [{"bufferView": view(png.getvalue()), "mimeType": "image/png"}]
    doc["textures"] = [{"source": 0}]
    doc["materials"] = [
        {
            "name": "Field finish",
            "doubleSided": True,
            "pbrMetallicRoughness": {
                "metallicFactor": 0.0,
                "roughnessFactor": 0.8,
                "baseColorTexture": {"index": 0},
            },
        }
    ]
    if takes is None:
        takes = [("Motion study", 0, len(travel))]
    clocks = []
    end = 0
    for name, start, stop in takes:
        if start != end or stop <= start or stop > len(travel):
            raise ValueError(f"Invalid animation range for {name}")
        doc["animations"].append({"name": name, "channels": [], "samplers": []})
        clocks.append(acc(np.arange(stop-start) / fps, "SCALAR", True))
        end = stop
    if end != len(travel):
        raise ValueError("Animation ranges do not cover the pose stream")

    def channel(node, name, values, kind):
        for animation, clock, (_, start, stop) in zip(doc["animations"], clocks, takes, strict=True):
            sampler = len(animation["samplers"])
            animation["samplers"].append({"input": clock, "output": acc(values[start:stop], kind), "interpolation": "LINEAR"})
            animation["channels"].append({"sampler": sampler, "target": {"node": node, "path": name}})

    channel(0, "translation", travel, "VEC3")
    world = track.copy()
    for i, p in enumerate(parts):
        world[:, i, :3, 3] += track[:, i, :3, :3] @ np.array(p["pivot"])
    for i, p in enumerate(parts):
        ids = owners == i
        mesh_index = None
        if np.any(ids):
            mesh_index = len(doc["meshes"])
            xyz = triangles[ids] - p["pivot"]
            normals = np.cross(xyz[:, 1] - xyz[:, 0], xyz[:, 2] - xyz[:, 0])
            normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 0.00001)
            tex = uv[ids].copy()
            tex[:, :, 1] = 1 - tex[:, :, 1]
            attributes = {
                "POSITION": acc(xyz, "VEC3", True),
                "NORMAL": acc(np.repeat(normals, 3, axis=0), "VEC3"),
                "TEXCOORD_0": acc(tex, "VEC2"),
                "COLOR_0": acc(np.repeat(colors[ids], 3, axis=0), "VEC3"),
            }
            for accessor in attributes.values():
                doc["bufferViews"][doc["accessors"][accessor]["bufferView"]]["target"] = 34962
            doc["meshes"].append({"primitives": [{"attributes": attributes, "material": 0}]})
        local = world[:, i] if p["parent"] < 0 else np.linalg.inv(world[:, p["parent"]]) @ world[:, i]
        rotations = Rotation.from_matrix(local[:, :3, :3]).as_quat()
        flips = np.sum(rotations[1:] * rotations[:-1], axis=1) < 0
        rotations[1:] *= np.cumprod(np.where(flips, -1, 1))[:, None]
        node = {"name": p["name"], "translation": local[0, :3, 3].tolist(), "rotation": rotations[0].tolist()}
        if mesh_index is not None:
            node["mesh"] = mesh_index
        children = [j + 1 for j, child in enumerate(parts) if child["parent"] == i]
        if children:
            node["children"] = children
        doc["nodes"].append(node)
        channel(i + 1, "translation", local[:, :3, 3], "VEC3")
        channel(i + 1, "rotation", rotations, "VEC4")
    data.extend(b"\0" * (-len(data) % 4))
    doc["buffers"] = [{"byteLength": len(data)}]
    j = json.dumps(doc, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, 28 + len(j) + len(data))
        + struct.pack("<I4s", len(j), b"JSON")
        + j
        + struct.pack("<I4s", len(data), b"BIN\0")
        + data
    )


def export_clip(pack, clip, destination):
    """Bind the baked skin transforms to the exact native draw-vertex ranges."""
    model = json.loads((pack / clip["mesh"]).read_bytes())
    model["vertices"] = np.asarray(model["vertices"], dtype=float) * clip["mesh_scale"]
    parts = clip["parts"]
    owners = np.full(len(model["faces"]), -1, dtype=int)
    for index, part in enumerate(parts):
        for first, count in part["ranges"]:
            if first % 3 or count % 3 or first < 0 or count < 0 or first + count > owners.size * 3:
                raise ValueError(f"Invalid draw range in {part['name']}")
            group = owners[first // 3 : (first + count) // 3]
            if np.any(group >= 0):
                raise ValueError(f"Overlapping draw range in {part['name']}")
            group[:] = index
    if np.any(owners < 0):
        raise ValueError("Missing triangles in rigid binding")
    poses = np.asarray(clip["frames"], dtype=float)
    travel = np.asarray(clip["root"], dtype=float)
    if poses.shape != (len(travel), len(parts), 7) or not np.isfinite(poses).all():
        raise ValueError("Invalid rigid pose stream")
    if travel.shape != (len(travel), 3) or not np.isfinite(travel).all():
        raise ValueError("Invalid root track")
    rotations = poses[:, :, :4]
    if not np.allclose(np.linalg.norm(rotations, axis=-1), 1, atol=1e-4):
        raise ValueError("Invalid rigid rotation")
    track = np.broadcast_to(np.eye(4), poses.shape[:2] + (4, 4)).copy()
    track[:, :, :3, :3] = (
        Rotation.from_quat(rotations.reshape(-1, 4)).as_matrix().reshape(*poses.shape[:2], 3, 3)
    )
    track[:, :, :3, 3] = poses[:, :, 4:]
    export_rigid_glb(destination, model, parts, owners, track, travel, pack, clip["fps"])
