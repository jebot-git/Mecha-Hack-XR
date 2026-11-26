"""Render the verified Arc reference roster and the expanded action library."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from scipy.spatial.transform import Rotation

from .__main__ import DEFAULT_PACK, write_json
from .preview import Studio, mesh_binding, video
from .verify import file_hash

FPS = 24


def load_reference(motion, pack):
    batch = motion / json.loads((motion / "latest.json").read_bytes())["report"]
    report = json.loads(batch.read_bytes())
    if file_hash(pack / "runtime-models.json") != report["pack_manifest_sha256"]:
        raise ValueError("Pack and reference batch do not match")
    for source, digest in report["source_sha256"].items():
        if file_hash(pack / source) != digest:
            raise ValueError(f"Changed reference input: {source}")
    if set(report["rigs"]) != set(report["inventory"]["applicable"]):
        raise ValueError("Full roster reference required")
    return batch.parent, report


def reference_track(clip):
    raw = np.asarray(clip["frames"])
    track = np.broadcast_to(np.eye(4), (*raw.shape[:2], 4, 4)).copy()
    track[:, :, :3, :3] = (
        Rotation.from_quat(raw[:, :, :4].reshape(-1, 4))
        .as_matrix()
        .reshape(*raw.shape[:2], 3, 3)
    )
    track[:, :, :3, 3] = raw[:, :, 4:]
    return track, np.asarray(clip["root"])


def reference_clip(folder, record):
    path = folder / record["clip"]
    if file_hash(path) != record["clip_sha256"]:
        raise ValueError(f"Reference clip changed: {path}")
    return json.loads(path.read_bytes())


def render_reference(motion, pack, out):
    folder, report = load_reference(motion, pack)
    from .rigs import discover

    rigs, _ = discover(pack)
    studio = Studio(pack)
    print("Preview GPU:", studio.gl.info["GL_RENDERER"], flush=True)
    rows = {}
    for actor, record in report["rigs"].items():
        directory = out / actor
        directory.mkdir(parents=True, exist_ok=True)
        clip = reference_clip(folder, record)
        rig = rigs[actor]
        if rig["parts"] != clip["parts"]:
            raise ValueError(f"Binding changed for {actor}")
        model = json.loads((pack / rig["mesh"]).read_bytes())
        studio.bind(mesh_binding(model, rig), rig)
        track, root = reference_track(clip)
        studio.camera(track, root)
        with video(
            directory / "reference.mp4", (studio.width, studio.height), clip["fps"]
        ) as pipe:
            for i, (mx, tr) in enumerate(zip(track, root, strict=True)):
                image = studio.labelled(
                    mx, tr, actor, clip["action"], i / (len(root) - 1)
                )
                if i == len(root) // 3:
                    image.save(directory / "reference.jpg", quality=88)
                pipe.write(image.tobytes())
        rows[actor] = {
            "family": rig["family"],
            "file": actor + "/reference.mp4",
            "poster": actor + "/reference.jpg",
            "action": clip["action"],
            "frames": len(root),
            "fps": clip["fps"],
        }
    write_json(out / "reference.json", rows)
    return rows


def montage(rows, out):
    # Keep names burned into each tile and the original sequence at full speed.
    names = sorted(rows, key=lambda n: (rows[n]["family"], n))
    pages = []
    with tempfile.TemporaryDirectory(dir=out) as scratch:
        for index in range(0, len(names), 9):
            block = names[index : index + 9]
            args = ["ffmpeg", "-v", "error", "-y", "-filter_complex_threads", "2"]
            for n in block:
                args += ["-threads", "1", "-i", str(out / rows[n]["file"])]
            filters = [f"[{i}:v]scale=448:274[v{i}]" for i in range(len(block))]
            inputs = "".join(f"[v{i}]" for i in range(len(block)))
            if len(block) > 1:
                layout = "|".join(
                    f"{i % 3 * 448}_{i // 3 * 274}" for i in range(len(block))
                )
                filters.append(
                    f"{inputs}xstack=inputs={len(block)}:layout={layout}:fill=black[tiles]"
                )
            else:
                filters.append(inputs + "null[tiles]")
            filters.append("[tiles]pad=1344:822:0:0,setsar=1[v]")
            page = Path(scratch) / f"page-{index // 9:02d}.mp4"
            subprocess.run(
                args
                + [
                    "-filter_complex",
                    ";".join(filters),
                    "-map",
                    "[v]",
                    "-an",
                    "-c:v",
                    "libx264",
                    "-preset",
                    "veryfast",
                    "-threads",
                    "3",
                    "-crf",
                    "20",
                    "-pix_fmt",
                    "yuv420p",
                    str(page),
                ],
                check=True,
            )
            pages.append(page)
        playlist = Path(scratch) / "clips.txt"
        playlist.write_text("".join(f"file '{p.name}'\n" for p in pages))
        subprocess.run(
            [
                "ffmpeg",
                "-v",
                "error",
                "-y",
                "-f",
                "concat",
                "-safe",
                "1",
                "-i",
                str(playlist),
                "-c",
                "copy",
                "-movflags",
                "+faststart",
                str(out / "reference-roster.mp4"),
            ],
            check=True,
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=["reference", "actions"])
    parser.add_argument("--pack", type=Path, default=DEFAULT_PACK)
    parser.add_argument("--motion", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--jobs", type=int, choices=range(1, 17), default=2, metavar="1..16"
    )
    parser.add_argument(
        "--timing", type=Path, help="Learned per-action timing JSON for the action bake"
    )
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.stage == "reference":
        montage(render_reference(args.motion, args.pack, args.out), args.out)
    else:
        from .action_library import build

        build(args.motion, args.pack, args.out, args.jobs, args.timing)


if __name__ == "__main__":
    main()
