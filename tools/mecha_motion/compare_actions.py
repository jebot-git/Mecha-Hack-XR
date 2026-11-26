"""Compare an accepted motion library with a revised bake on matching clocks."""

import argparse
import json
import os
from pathlib import Path
import tempfile

import numpy as np
from PIL import Image, ImageDraw
from scipy.spatial.transform import Rotation

from . import vehicle_actions
from .__main__ import DEFAULT_PACK, write_json
from .preview import Studio, mesh_binding, video
from .rigs import discover
from .roster import FPS
from .verify import file_hash

EXAMPLES = (
    ("kaiju_tunnel_wurm", "burrow-entry"),
    ("kaiju_tunnel_wurm", "burrow-eruption"),
    ("kaiju_spawnling", "tackle"),
    ("jump_pod", "leap-land"),
    ("mecha_ace", "overhead-cut"),
    ("mecha_ace", "parry"),
    ("mecha_sniper", "fire-recoil"),
    ("kaiju_drone", "cockpit-cling"),
    ("kaiju_tyrant", "flying-maw-sweep"),
    ("dreadnought", "heavy-broadside"),
)


def matrices(pose):
    frames = np.broadcast_to(np.eye(4), (*pose.shape[:2], 4, 4)).copy()
    frames[:, :, :3, :3] = (
        Rotation.from_quat(pose[:, :, :4].reshape(-1, 4))
        .as_matrix()
        .reshape(*pose.shape[:2], 3, 3)
    )
    frames[:, :, :3, 3] = pose[:, :, 4:]
    return frames


def compare(accepted, revised, pack):
    load = lambda folder: json.loads((folder / "actions.json").read_bytes())
    before, after = load(accepted), load(revised)
    if set(before) != set(after):
        raise ValueError("Roster differs")
    rigs, _ = discover(pack)
    rigs.update(vehicle_actions.discover(pack))
    pairs = {}
    changed, clips, frames = 0, 0, 0
    summaries = {}
    for actor, row in after.items():
        earlier = before[actor]
        if row["actions"] != earlier["actions"] or row["fps"] != earlier["fps"]:
            raise ValueError(f"{actor}: timelines differ")
        with (
            np.load(accepted / earlier["native"], allow_pickle=False) as original,
            np.load(revised / row["native"], allow_pickle=False) as revised_clip,
        ):
            if original["parts_json"].item() != revised_clip["parts_json"].item():
                raise ValueError(f"{actor}: rig differs")
            q, root = original["poses"], original["root"]
            q2, root2 = revised_clip["poses"], revised_clip["root"]
            # Pick the nearer of q and -q, since either encodes the same rotation.
            rotation_delta = np.minimum(
                np.linalg.norm(q[:, :, :4] - q2[:, :, :4], axis=-1),
                np.linalg.norm(q[:, :, :4] + q2[:, :, :4], axis=-1),
            )
            for take in row["actions"]:
                name = take["name"]
                a = take["start"]
                b = a + take["frames"]
                different = (
                    rotation_delta[a:b].max() > 1e-4
                    or np.max(abs(root[a:b] - root2[a:b])) > 1e-4
                    or np.max(abs(q[a:b, :, 4:] - q2[a:b, :, 4:])) > 1e-4
                )
                changed += int(different)
                family = summaries.setdefault(row["family"], {"clips": 0, "changed": 0})
                family["clips"] += 1
                family["changed"] += int(different)
                clips += 1
                frames += b - a
                if (actor, name) in EXAMPLES:
                    pairs[actor, name] = [
                        (matrices(p[a:b]), r[a:b].copy())
                        for p, r in [(q, root), (q2, root2)]
                    ]
    studio = Studio(pack)
    width, height = studio.width, studio.height
    chapters = []
    cursor = 0
    with tempfile.TemporaryDirectory(dir=revised) as tmp:
        stage = Path(tmp)
        with video(stage / "comparison.mp4", (2 * width, height + 34), FPS) as pipe:
            for actor, name in EXAMPLES:
                sides = pairs[actor, name]
                rig = rigs[actor]
                mesh = json.loads((pack / rig["mesh"]).read_bytes())
                studio.bind(mesh_binding(mesh, rig), rig)
                studio.camera(
                    *(np.concatenate(values) for values in zip(*sides, strict=True))
                )
                count = len(sides[0][0])
                chapters.append(
                    {"actor": actor, "action": name, "start": cursor, "frames": count}
                )
                cursor += count
                for i in range(count):
                    panel = Image.new("RGB", (2 * width, height + 34), (15, 24, 29))
                    draw = ImageDraw.Draw(panel)
                    for j, (track, travel) in enumerate(sides):
                        draw.text(
                            (j * width + 9, 8),
                            ("Accepted", "Refined")[j],
                            font=studio.font,
                            fill="#e3f1ee",
                        )
                        panel.paste(
                            studio.labelled(
                                track[i], travel[i], actor, name, i / (count - 1)
                            ),
                            (j * width, 34),
                        )
                    pipe.write(panel.tobytes())
        os.replace(stage / "comparison.mp4", revised / "comparison.mp4")
    report = {
        "accepted_manifest_sha256": file_hash(accepted / "actions.json"),
        "revised_manifest_sha256": file_hash(revised / "actions.json"),
        "clips": clips,
        "distinctly_changed": changed,
        "frames": frames,
        "families": summaries,
        "comparison_fps": FPS,
        "chapters": chapters,
    }
    write_json(revised / "comparison.json", report)
    print(
        f"{changed}/{clips} clips changed across {frames} matched frames; {cursor} comparison frames"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accepted", required=True, type=Path)
    parser.add_argument("--revised", required=True, type=Path)
    parser.add_argument("--pack", default=DEFAULT_PACK, type=Path)
    args = parser.parse_args()
    compare(args.accepted, args.revised, args.pack)


if __name__ == "__main__":
    main()
