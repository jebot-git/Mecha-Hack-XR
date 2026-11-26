"""Verify a published motion batch, including its glTF animation bindings."""

import hashlib
import json
import struct

import numpy as np
from scipy.spatial.transform import Rotation


def file_hash(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def read_glb(path):
    blob = path.read_bytes()
    magic, version, total, length, kind = struct.unpack_from("<4sIII4s", blob)
    if (magic, version, total, kind) != (b"glTF", 2, len(blob), b"JSON"):
        raise ValueError(f"Invalid glTF header: {path}")
    doc = json.loads(blob[20 : 20 + length])
    binary_size, kind = struct.unpack_from("<I4s", blob, 20 + length)
    data = memoryview(blob)[28 + length :]
    if kind != b"BIN\0" or len(data) != binary_size:
        raise ValueError(f"Incomplete binary animation: {path}")

    return doc, data


def check_glb(path, clip):
    check_track(*read_glb(path), clip)


def check_track(doc, data, clip, take=0):
    def array(index):
        accessor = doc["accessors"][index]
        view = doc["bufferViews"][accessor["bufferView"]]
        values = np.frombuffer(data[view.get("byteOffset", 0) :][: view["byteLength"]], dtype="<f4")
        return values.reshape(accessor["count"], -1)

    poses = np.asarray(clip["frames"])
    n, joints, _ = poses.shape
    if len(doc["nodes"]) != joints + 1:
        raise ValueError("Unexpected number of rigid nodes")
    for parent, node in enumerate(doc["nodes"]):
        expected = {i+1 for i, part in enumerate(clip["parts"]) if part["parent"]+1 == parent}
        if set(node.get("children", [])) != expected:
            raise ValueError("Exported joint hierarchy differs")
    motion = doc["animations"][take]
    # Reconstruct every baked joint in actor space from the glTF node animation.
    transforms = np.tile(np.eye(4), (n, joints + 1, 1, 1))
    paths = set()
    for channel in motion["channels"]:
        node = channel["target"]["node"]
        field = channel["target"]["path"]
        sampler = motion["samplers"][channel["sampler"]]
        if not np.allclose(array(sampler["input"]).ravel(), np.arange(n) / clip["fps"], rtol=0, atol=1e-6):
            raise ValueError("Unexpected frame times")
        values = array(sampler["output"])
        if (node, field) in paths:
            raise ValueError("Duplicate animation channel")
        paths.add((node, field))
        if field == "rotation":
            transforms[:, node, :3, :3] = Rotation.from_quat(values).as_matrix()
        elif field == "translation":
            transforms[:, node, :3, 3] = values
        else:
            raise ValueError("Unexpected animation property")
    if len(paths) != 2 * joints + 1 or not np.allclose(transforms[:, 0, :3, 3], clip["root"], atol=1e-5):
        raise ValueError("Incomplete pose or root track")
    for i, part in enumerate(clip["parts"]):
        node = doc["nodes"][i + 1]
        parent = part["parent"] + 1
        if node["name"] != part["name"] or i + 1 not in doc["nodes"][parent].get("children", []):
            raise ValueError("Incorrect glTF hierarchy")
        expected_vertices = sum(count for start, count in part["ranges"])
        if expected_vertices:
            primitive = doc["meshes"][node["mesh"]]["primitives"][0]
            if len(array(primitive["attributes"]["POSITION"])) != expected_vertices:
                raise ValueError("Inconsistent rigid geometry")
        if parent:
            transforms[:, i + 1] = transforms[:, parent] @ transforms[:, i + 1]
        rotation = Rotation.from_quat(poses[:, i, :4]).as_matrix()
        pivot = rotation @ np.asarray(part["pivot"]) + poses[:, i, 4:]
        if not (
            np.allclose(rotation, transforms[:, i + 1, :3, :3], rtol=0, atol=2e-5)
            and np.allclose(pivot, transforms[:, i + 1, :3, 3], rtol=0, atol=0.0001)
        ):
            raise ValueError(f"Exported transform differs for {part['name']}")


def verify(output, pack, require_all):
    from .rigs import discover

    batch = output / json.loads((output / "latest.json").read_bytes())["report"]
    report = json.loads(batch.read_bytes())
    rigs, excluded = discover(pack)
    if report["pack_manifest_sha256"] != file_hash(pack / "runtime-models.json"):
        raise ValueError("Asset pack differs from the batch")
    selected = set(report["rigs"])
    if not selected or selected - rigs.keys() or (require_all and selected != rigs.keys()):
        raise ValueError("Incomplete or unknown rig selection")
    if set(report["inventory"]["applicable"]) != rigs.keys() or report["inventory"]["excluded"] != excluded:
        raise ValueError("Rig inventory changed")
    for relative, expected in report["source_sha256"].items():
        if file_hash(pack / relative) != expected:
            raise ValueError(f"Asset changed since bake: {relative}")
    frames = 0
    for name, record in report["rigs"].items():
        clip_path, glb = [batch.parent / record[k] for k in ("clip", "glb")]
        if file_hash(clip_path) != record["clip_sha256"] or file_hash(glb) != record["glb_sha256"]:
            raise ValueError(f"Baked files changed: {name}")
        clip = json.loads(clip_path.read_bytes())
        if clip["rig_id"] != name or len(clip["frames"]) != record["frames"]:
            raise ValueError(f"Invalid clip identity or length: {name}")
        check_glb(glb, clip)
        frames += record["frames"]
    print(f"Verified {len(selected)} rigs, {frames} frames and their exported glTF transforms.")
