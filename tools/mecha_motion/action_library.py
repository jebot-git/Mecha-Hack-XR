"""Bake, render and round-trip the gameplay action studies for each published rig."""

from concurrent.futures import ProcessPoolExecutor
from functools import lru_cache
import hashlib
import json
import multiprocessing
import os
from pathlib import Path
import tempfile

import numpy as np
from scipy.spatial.transform import Rotation

from . import action_timing, creature_actions, mechanical_actions, vehicle_actions
from .__main__ import write_json
from .glb import export_rigid_glb
from .preview import Studio, mesh_binding, video
from .rigs import discover
from .roster import FPS, load_reference, reference_clip
from .verify import check_track, file_hash, read_glb


@lru_cache(maxsize=1)
def studio_for(pack):
    return Studio(pack)


def check_pose(actor, action, track, roots, samples, joints):
    if (
        track.shape != (samples, joints, 4, 4)
        or roots.shape != (samples, 3)
        or not np.isfinite(track).all()
        or not np.isfinite(roots).all()
    ):
        raise ValueError(
            f"{actor}/{action}: invalid pose dimensions or non-finite values"
        )
    r = track[:, :, :3, :3]
    if (
        not np.allclose(np.swapaxes(r, -2, -1) @ r, np.eye(3), atol=1e-5)
        or not np.allclose(np.linalg.det(r), 1, atol=1e-5)
        or not np.allclose(track[:, :, 3, :], [0, 0, 0, 1], atol=1e-6)
    ):
        raise ValueError(f"{actor}/{action}: non-rigid transform")


def author(rig):
    if rig["family"] == "kaiju":
        return creature_actions
    if rig["family"] in ("mecha", "infantry"):
        return mechanical_actions
    return vehicle_actions


def bake_actor(job):
    actor, rig, folder, baseline, curves, pack, out, inputs, author_hash, timing = job
    module = author(rig)
    directory = out / actor
    directory.mkdir(exist_ok=True, parents=True)
    paths = [pack / rig[key] for key in ("mesh", "source")]
    if file_hash(Path(module.__file__)) != author_hash:
        raise RuntimeError(
            f"{module.__name__} changed during the batch. Restart to use the new poses."
        )
    fingerprint = hashlib.sha256(
        (inputs + author_hash + "".join(file_hash(p) for p in paths)).encode()
    ).hexdigest()
    marker = directory / "actions.json"
    if marker.exists():
        record = json.loads(marker.read_bytes())
        if record.get("inputs") == fingerprint and all(
            (directory / name).is_file() and file_hash(directory / name) == digest
            for name, digest in record["files"].items()
        ):
            return actor, record

    model = json.loads(paths[0].read_bytes())
    reference = reference_clip(folder, baseline) if baseline else None
    context = {"model": model, "pack": pack, "reference": reference, "curves": curves}
    actions = module.actions(actor, rig, context)
    if not actions or len(set(actions)) != len(actions):
        raise ValueError(f"Invalid actions for {actor}")
    geom = mesh_binding(model, rig)
    parts = rig["parts"]
    studio = studio_for(pack)
    studio.bind(geom, rig)
    takes = []
    tracks = []
    travel = []
    end = 0
    with tempfile.TemporaryDirectory(dir=directory, prefix=".working-") as tmp:
        stage = Path(tmp)
        with video(stage / "actions.mp4", (studio.width, studio.height), FPS) as pipe:
            for idx, action in enumerate(actions):
                count = max(2, round(module.duration(action) * FPS) + 1)
                t = np.linspace(0.0, 1.0, count)
                if timing is not None:
                    key = f"{rig['family']}/{action}"
                    t = action_timing.retime(
                        t, timing[key], module.timing_anchors(action)
                    )
                track, roots = module.pose(actor, rig, action, t, context)
                check_pose(actor, action, track, roots, count, len(parts))
                studio.camera(track, roots)
                for i, (matrix, root) in enumerate(zip(track, roots, strict=True)):
                    frame = studio.labelled(
                        matrix, root, actor, action, i / (count - 1)
                    )
                    if idx == 0 and i == count // 2:
                        frame.save(stage / "actions.jpg", quality=89)
                    pipe.write(frame.tobytes())
                takes.append((action, end, end + count))
                end += count
                tracks.append(track)
                travel.append(roots)
        track = np.concatenate(tracks)
        roots = np.concatenate(travel)
        del tracks, travel
        mesh = dict(model, vertices=np.asarray(model["vertices"]) * rig["mesh_scale"])
        export_rigid_glb(
            stage / "actions.glb", mesh, parts, geom[-1], track, roots, pack, FPS, takes
        )
        native = np.empty((*track.shape[:2], 7), dtype=np.float32)
        native[:, :, :4] = (
            Rotation.from_matrix(track[:, :, :3, :3].reshape(-1, 3, 3))
            .as_quat()
            .reshape(*track.shape[:2], 4)
        )
        native[:, :, 4:] = track[:, :, :3, 3]
        np.savez_compressed(
            stage / "actions.npz",
            poses=native,
            root=roots.astype(np.float32),
            fps=FPS,
            parts_json=json.dumps(parts),
            names=[t[0] for t in takes],
            offsets=[t[1] for t in takes] + [end],
        )
        doc, data = read_glb(stage / "actions.glb")
        if [m["name"] for m in doc["animations"]] != actions:
            raise ValueError(f"{actor}: GLB action names differ")
        with np.load(stage / "actions.npz", allow_pickle=False) as saved:
            poses = saved["poses"]
            root = saved["root"]
            saved_parts = json.loads(saved["parts_json"].item())
            for i, (_, start, stop) in enumerate(takes):
                check_track(
                    doc,
                    data,
                    {
                        "frames": poses[start:stop],
                        "fps": FPS,
                        "parts": saved_parts,
                        "root": root[start:stop],
                    },
                    i,
                )
        files = {path.name: file_hash(path) for path in stage.iterdir()}
        record = {
            "family": rig["family"],
            "kind": rig["kind"],
            "video": actor + "/actions.mp4",
            "poster": actor + "/actions.jpg",
            "glb": actor + "/actions.glb",
            "native": actor + "/actions.npz",
            "actions": [
                {"name": name, "start": start, "frames": stop - start}
                for name, start, stop in takes
            ],
            "frames": end,
            "fps": FPS,
            "inputs": fingerprint,
            "files": files,
        }
        for name in files:
            os.replace(stage / name, directory / name)
        write_json(marker, record)
    return actor, record


def build(motion, pack, out, jobs=2, timing_path=None):
    from .training import predict

    pack = pack.resolve()
    folder, report = load_reference(motion, pack)
    checkpoint = motion / "curve-adapter.pt"
    if file_hash(checkpoint) != report["checkpoint_sha256"]:
        raise ValueError("Arc checkpoint differs from reference batch")
    curves = predict(checkpoint, report["device"])
    rigs, excluded = discover(pack)
    other = vehicle_actions.discover(pack)
    if set(other) != set(excluded) or rigs.keys() & other.keys():
        raise ValueError("Incomplete non-gait inventory")
    rigs.update(other)
    inputs = (
        report["checkpoint_sha256"]
        + file_hash(folder / "report.json")
        + file_hash(pack / "textures/psx-atlas.rgba")
    )
    timing = None
    if timing_path is not None:
        trained = json.loads(timing_path.read_bytes())
        if trained["format"] != action_timing.FORMAT:
            raise ValueError("Unknown action timing model")
        timing = trained["curves"]
        inputs += file_hash(timing_path)
    for source in (
        "action_timing.py",
        "action_library.py",
        "rigs.py",
        "roster.py",
        "preview.py",
        "glb.py",
        "verify.py",
        "bake.py",
    ):
        inputs += file_hash(Path(__file__).with_name(source))
    # Hash the effective capability table, including when using an external pack.
    inputs += hashlib.sha256(
        json.dumps(mechanical_actions.catalog(pack), sort_keys=True).encode()
    ).hexdigest()
    author_hashes = {
        module: file_hash(Path(module.__file__))
        for module in (mechanical_actions, creature_actions, vehicle_actions)
    }
    tasks = [
        (
            actor,
            rig,
            folder,
            report["rigs"].get(actor),
            curves,
            pack,
            out.resolve(),
            inputs,
            author_hashes[author(rig)],
            {k: v for k, v in timing.items() if k.startswith(rig["family"] + "/")}
            if timing is not None
            else None,
        )
        for actor, rig in sorted(rigs.items())
    ]
    records = {}
    with ProcessPoolExecutor(
        max_workers=jobs, mp_context=multiprocessing.get_context("spawn")
    ) as pool:
        for actor, record in pool.map(bake_actor, tasks, chunksize=1):
            records[actor] = record
            print(
                f"{len(records)}/{len(rigs)} {actor} {len(record['actions'])} actions",
                flush=True,
            )
    if any(
        file_hash(Path(module.__file__)) != digest
        for module, digest in author_hashes.items()
    ):
        raise RuntimeError(
            "An action definition changed during the batch. Rerun before publishing the gallery."
        )
    write_json(out / "actions.json", records)
    from .gallery import finish

    finish(out, records, json.loads((out / "reference.json").read_bytes()))
