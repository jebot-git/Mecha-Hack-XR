"""Fine-tune the released Kimodo G1 denoiser on 30 Hz G1 clips."""

import argparse
import hashlib
import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np
import torch
from safetensors.torch import save_file

from .training import training_device

BASE_SHA = "e18c1de73e2ce17a107b06d85155fbbc5debe68eb35455aa5b033e6ddbe056a5"


def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def feature_windows(paths, model, frames, device):
    samples = []
    for path in paths:
        with np.load(path, allow_pickle=False) as data:
            xyz = torch.tensor(data["posed_joints"], dtype=torch.float32, device=device)
            rotations = torch.tensor(data["global_rot_mats"], dtype=torch.float32, device=device)
            if "fps" in data and float(data["fps"].item()) != 30:
                raise ValueError(f"{path}: expected 30 Hz")
        if (
            xyz.shape != (len(xyz), 34, 3)
            or rotations.shape != (len(xyz), 34, 3, 3)
            or len(xyz) < frames
            or not torch.isfinite(xyz).all()
            or not torch.isfinite(rotations).all()
        ):
            raise ValueError(f"Invalid 34-joint G1 motion or clip shorter than {frames} frames: {path}")
        with torch.no_grad():
            local = model.skeleton.global_rots_to_local_rots(rotations)
            starts = sorted(set(range(0, len(xyz) - frames + 1, frames)) | {len(xyz) - frames})
            for start in starts:
                cut = slice(start, start + frames)
                sample = model.motion_rep(local[cut], xyz[cut, 0], True, to_canonicalize=True)
                if not torch.isfinite(sample).all():
                    raise ValueError(f"Non-finite motion features: {path}")
                samples.append(sample.reshape(1, frames, -1).cpu())
    return samples


def denoising_loss(network, model, sample, seed, device):
    # Generate noise on CPU to make the evaluation sample independent of the backend.
    gen = torch.Generator().manual_seed(seed)
    clean = sample.to(device)
    noise = torch.randn(sample.shape, generator=gen).to(device)
    time = torch.randint(model.diffusion.num_base_steps, (1,), generator=gen).to(device)
    alpha = model.diffusion.alphas_cumprod_base[time].view(1, 1, 1)
    noisy = alpha.sqrt() * clean + (1 - alpha).sqrt() * noise
    keys = torch.zeros_like(clean)
    keys[:, [0, clean.shape[1] // 2, -1]] = 1
    prediction = network(
        noisy,
        torch.ones_like(clean[:, :, 0], dtype=torch.bool),
        torch.zeros(1, 1, 4096, device=device),
        torch.zeros(1, 1, dtype=torch.bool, device=device),
        time,
        torch.zeros(1, device=device),
        keys,
        clean * keys,
    )
    return ((prediction - clean).square() * (1 - keys)).sum() / (1 - keys).sum()


def main():
    from kimodo import load_model

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base", type=Path, required=True, help="Released Kimodo-G1-RP-v1 directory")
    p.add_argument(
        "--corpus", type=Path, required=True, help="Directory with train/ and validation/ G1 NPZ clips"
    )
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", default="xpu")
    p.add_argument("--scope", choices=["heads", "full"], default="heads")
    p.add_argument("--epochs", type=int, default=4)
    p.add_argument("--frames", type=int, default=45)
    p.add_argument("--lr", type=float, default=1e-5)
    p.add_argument("--seed", type=int, default=37)
    p.add_argument("--resume", action="store_true")
    args = p.parse_args()
    device = training_device(args.device)
    if args.epochs < 1 or not 12 <= args.frames <= 240 or not 0 < args.lr < 1:
        p.error("Expected positive epochs, 12–240 frames per window and 0 < lr < 1")
    if args.base.name != "Kimodo-G1-RP-v1" or sha(args.base / "model.safetensors") != BASE_SHA:
        p.error("Expected the published Kimodo-G1-RP-v1 weights")
    splits = {key: sorted((args.corpus / key).glob("*.npz")) for key in ("train", "validation")}
    if not all(splits.values()):
        p.error("Supply at least one independent motion clip in each split")
    hashes = {split: [sha(path) for path in paths] for split, paths in splits.items()}
    if set(hashes["train"]) & set(hashes["validation"]):
        p.error("A training clip also appears in validation")
    config = {
        "base": BASE_SHA,
        "split_sha256": hashes,
        "scope": args.scope,
        "frames": args.frames,
        "lr": args.lr,
        "seed": args.seed,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    state_path = args.output / "finetune.pt"
    state = None
    if args.resume:
        state = torch.load(state_path, weights_only=True, map_location="cpu")
        if state["config"] != config or state["epoch"] > args.epochs:
            p.error("Checkpoint configuration, corpus, or target epoch mismatch")
    elif state_path.exists():
        p.error("Use --resume or a new output directory")
    torch.set_num_threads(2)
    torch.manual_seed(args.seed)
    os.environ["CHECKPOINT_DIR"] = str(args.base.resolve().parent)
    # No text prompts are used; the trainable graph receives zero text conditioning.
    model = load_model("Kimodo-G1-RP-v1", device=str(device), text_encoder=torch.nn.Identity())
    network = model.denoiser.model
    params = {k: v for k, v in network.named_parameters() if args.scope == "full" or ".output_linear." in k}
    for k, v in network.named_parameters():
        v.requires_grad_(k in params)
    optimizer = torch.optim.AdamW(params.values(), lr=args.lr, foreach=False)
    epoch = 0
    if state:
        network.load_state_dict(state["params"], strict=False)
        optimizer.load_state_dict(state["optimizer"])
        epoch = state["epoch"]
        del state
    samples = {k: feature_windows(paths, model, args.frames, device) for k, paths in splits.items()}

    def validate():
        network.eval()
        with torch.no_grad():
            return float(
                np.mean(
                    [
                        denoising_loss(network, model, s, args.seed + i, device).item()
                        for i, s in enumerate(samples["validation"])
                    ]
                )
            )

    baseline = validate()
    while epoch < args.epochs:
        network.train()
        order = np.random.default_rng(args.seed + epoch).permutation(len(samples["train"]))
        total = 0.0
        for step, i in enumerate(order):
            optimizer.zero_grad(set_to_none=True)
            loss = denoising_loss(
                network, model, samples["train"][i], args.seed + 10000 + epoch * len(order) + step, device
            )
            if not torch.isfinite(loss):
                raise ValueError("Non-finite training loss")
            loss.backward()
            torch.nn.utils.clip_grad_norm_(params.values(), 1.0, error_if_nonfinite=True)
            optimizer.step()
            total += loss.item()
        epoch += 1
        validation = validate()
        if not np.isfinite(validation):
            raise ValueError("Non-finite validation loss")
        metrics = {
            "epoch": epoch,
            "training_mse": total / len(order),
            "validation_mse": validation,
            "train_windows": len(order),
            "validation_windows": len(samples["validation"]),
        }
        with TemporaryDirectory(dir=args.output) as temp:
            candidate = Path(temp) / "state.pt"
            torch.save(
                {
                    "epoch": epoch,
                    "params": {k: v.detach().cpu() for k, v in params.items()},
                    "optimizer": optimizer.state_dict(),
                    "metrics": metrics,
                    "config": config,
                },
                candidate,
            )
            os.replace(candidate, state_path)
        print(json.dumps(metrics), flush=True)
    with TemporaryDirectory(dir=args.output) as temp:
        folder = Path(temp)
        save_file({k: v.detach().cpu().contiguous() for k, v in params.items()}, folder / "tuned.safetensors")
        (folder / "finetune.json").write_text(
            json.dumps(
                {
                    "config": config,
                    "epoch": epoch,
                    "device": str(device),
                    "validation_at_start": baseline,
                    "validation_at_end": validate(),
                    "weights_sha256": sha(folder / "tuned.safetensors"),
                },
                indent=2,
            )
            + "\n"
        )
        for name in ("tuned.safetensors", "finetune.json"):
            os.replace(folder / name, args.output / name)


if __name__ == "__main__":
    main()
