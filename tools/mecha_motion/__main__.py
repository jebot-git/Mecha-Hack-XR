"""Train timing curves and bake reviewable clips for the published rig roster."""

import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PACK = ROOT / "build-mecha/game-assets/mecha"
REFERENCES = ROOT / "art/mecha/motion-evaluation"


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as stream:
        json.dump(data, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    os.replace(stream.name, path)


def inventory(pack):
    from .rigs import discover

    if not (pack / "runtime-models.json").is_file():
        raise ValueError(
            f"Expected a published asset pack at {pack}. Build the mecha-game-assets target first."
        )
    return discover(pack)


def overview(rigs, excluded):
    return {
        "applicable": {
            k: {field: v[field] for field in ("family", "kind", "source", "mesh")} for k, v in rigs.items()
        },
        "excluded": excluded,
    }


def batch(args, checkpoint):
    from .bake import bake
    from .glb import export_clip
    from .training import predict

    rigs, excluded = inventory(args.pack)
    selected = sorted(set(args.only or rigs))
    unknown = sorted(set(selected) - rigs.keys())
    if unknown:
        raise ValueError(
            "Requested rigs are unavailable: "
            + ", ".join(f"{k} ({excluded.get(k, 'unknown')})" for k in unknown)
        )
    curves = predict(checkpoint, args.device)
    versions = args.output / "bakes"
    versions.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".working-", dir=versions) as temp:
        stage = Path(temp) / "clips"
        stage.mkdir()
        hashes = {"textures/psx-atlas.rgba": digest(args.pack / "textures/psx-atlas.rgba")}
        report = {
            "schema": 1,
            "checkpoint_sha256": digest(checkpoint),
            "pack_manifest_sha256": digest(args.pack / "runtime-models.json"),
            "device": args.device,
            "rigs": {},
            "inventory": overview(rigs, excluded),
        }
        for rig_id in selected:
            clip_path = stage / (rig_id + ".json")
            glb_path = stage / (rig_id + ".glb")
            bake(args.pack, rig_id, rigs[rig_id], curves, clip_path)
            clip = json.loads(clip_path.read_bytes())
            export_clip(args.pack, clip, glb_path)
            for key in ("mesh", "source"):
                source = clip[key]
                if source not in hashes:
                    hashes[source] = digest(args.pack / source)
            report["rigs"][rig_id] = {
                "clip": clip_path.name,
                "glb": glb_path.name,
                "clip_sha256": digest(clip_path),
                "glb_sha256": digest(glb_path),
                "frames": len(clip["frames"]),
                "metrics": clip["metrics"],
            }
        report["source_sha256"] = hashes
        write_json(stage / "report.json", report)
        dest = versions / ("batch-" + Path(temp).name.removeprefix(".working-"))
        stage.rename(dest)
    write_json(args.output / "latest.json", {"report": str((dest / "report.json").relative_to(args.output))})
    print(f"Baked {len(selected)} rigs. Report: {dest / 'report.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True, dest="command")
    doctor = sub.add_parser("doctor", help="List Intel devices and exercise a training update")
    doctor.add_argument("--device", default="xpu")
    verify_parser = sub.add_parser("verify", help="Verify baked files and recompute their glTF joint poses")
    verify_parser.add_argument("--output", type=Path, required=True)
    verify_parser.add_argument("--pack", type=Path, default=DEFAULT_PACK)
    verify_parser.add_argument("--all", action="store_true", help="Require every applicable rig")
    for name in ("inventory", "train", "bake", "run"):
        p = sub.add_parser(name)
        if name != "train":
            p.add_argument("--pack", type=Path, default=DEFAULT_PACK)
        if name == "inventory":
            p.add_argument("--json", type=Path)
            continue
        p.add_argument("--output", type=Path, required=True)
        p.add_argument("--device", default="xpu", help="xpu, xpu:N or cpu")
        if name in ("run", "train"):
            p.add_argument("--references", type=Path, default=REFERENCES)
            p.add_argument("--epochs", type=int, default=400)
            p.add_argument("--resume", action="store_true")
        if name in ("run", "bake"):
            p.add_argument("--only", nargs="+", metavar="RIG")
        if name == "bake":
            p.add_argument("--checkpoint", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "verify":
        from .verify import verify

        verify(args.output, args.pack, args.all)
        return
    if args.command == "doctor":
        import torch

        from .training import training_device

        devices = {f"xpu:{i}": torch.xpu.get_device_name(i) for i in range(torch.xpu.device_count())}
        print(json.dumps({"torch": torch.__version__, "intel_devices": devices}), flush=True)
        device = training_device(args.device)
        weights = torch.nn.Parameter(torch.ones(8, device=device))
        optimizer = torch.optim.Adam([weights], lr=0.01)
        weights.square().mean().backward()
        optimizer.step()
        if not (0 < weights.detach().cpu().mean().item() < 1):
            raise RuntimeError("Training step failed")
        print(f"Optimizer step verified on {device}.")
        return
    if args.command == "inventory":
        data = overview(*inventory(args.pack))
        if args.json:
            write_json(args.json, data)
        else:
            print(json.dumps(data, indent=2, sort_keys=True))
        return
    if args.command in ("train", "run"):
        from .training import train

        result = train(args.references, args.output, args.device, args.epochs, args.resume)
        checkpoint = Path(result["checkpoint"])
        print(f"Trained through epoch {result['epoch']}. Checkpoint: {checkpoint}")
    else:
        checkpoint = args.checkpoint
    if args.command in ("run", "bake"):
        batch(args, checkpoint)


if __name__ == "__main__":
    main()
