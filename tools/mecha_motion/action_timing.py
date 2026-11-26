"""Learn action-specific playback timing from accepted native animation tracks."""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter1d
from scipy.spatial.transform import Rotation

from .__main__ import write_json
from .verify import file_hash

GRID = np.linspace(0, 1, 65)
FORMAT = "mecha-action-phase-v1"


def timing_target(pose, root, pivots, seconds):
    """Give anticipation and holds more screen time and carry fast gestures through.

    Joint pivots are measured in world space, so rigid rotations about an offset
    pivot do not produce spurious velocities. Quaternion signs are interchangeable.
    """
    q = pose[:, :, :4].astype(float)
    rotation = Rotation.from_quat(q.reshape(-1, 4))
    position = rotation.apply(np.broadcast_to(pivots, (*q.shape[:2], 3)).reshape(-1, 3))
    position = position.reshape(*q.shape[:2], 3) + pose[:, :, 4:] + root[:, None, :]
    extent = max(float(np.linalg.norm(np.ptp(pivots, axis=0))), 1.0)
    linear = np.linalg.norm(np.diff(position, axis=0), axis=-1) / extent
    q /= np.linalg.norm(q, axis=-1, keepdims=True)
    angle = 2 * np.arccos(np.clip(np.abs((q[:-1] * q[1:]).sum(axis=-1)), 0.0, 1.0))
    energy = np.sqrt(np.mean((linear + 0.35 * angle) ** 2, axis=1))
    midpoint = (np.arange(len(energy)) + 0.5) / len(energy)
    energy = gaussian_filter1d(np.interp(GRID, midpoint, energy), 1.6)
    average = np.mean(energy)
    if average < 1e-7:
        return GRID.copy()
    # Bound the local rate and absolute displacement. The difference is measured
    # in seconds so a long siege tell cannot accumulate a large event offset.
    rate = 1 - 0.30 * np.tanh((energy - average) / average)
    arrival = np.r_[0.0, np.cumsum((rate[1:] + rate[:-1]) / 2)]
    target = np.interp(GRID, arrival / arrival[-1], GRID)
    delta = target - GRID
    return GRID + delta * min(1.0, 0.075 / max(seconds * np.max(np.abs(delta)), 1e-12))


def corpus(review):
    manifest = review / "actions.json"
    records = json.loads(manifest.read_bytes())
    data = defaultdict(list)
    for actor, record in sorted(records.items()):
        native = review / record["native"]
        if file_hash(native) != record["files"][native.name]:
            raise ValueError(f"Accepted native track changed: {actor}")
        with np.load(native, allow_pickle=False) as clip:
            poses, roots = clip["poses"], clip["root"]
            pivots = np.array(
                [part["pivot"] for part in json.loads(clip["parts_json"].item())]
            )
            names, offsets = clip["names"].tolist(), clip["offsets"].tolist()
            if names != [a["name"] for a in record["actions"]] or offsets != [
                a["start"] for a in record["actions"]
            ] + [len(roots)]:
                raise ValueError(f"Chapter mismatch: {actor}")
            for name, a, b in zip(names, offsets[:-1], offsets[1:], strict=True):
                key = f"{record['family']}/{name}"
                phase = timing_target(
                    poses[a:b], roots[a:b], pivots, (b - a - 1) / record["fps"]
                )
                data[key].append((actor.removesuffix("_alt"), phase))
    return data, file_hash(manifest)


def fit(data, device, epochs):
    import torch
    from .training import training_device

    backend = training_device(device)
    keys = sorted(data)
    train, validation, holdout = [], [], []
    fit_clips = held_clips = 0
    for key in keys:
        # Keep alternate skins of a rig together. One-off actions have a phase
        # holdout only and are excluded from the unseen-rig score.
        groups = sorted(
            {name for name, _ in data[key]},
            key=lambda n: hashlib.sha256(n.encode()).digest(),
        )
        held = set(groups[: max(1, len(groups) // 5)]) if len(groups) > 1 else set()
        training = [v for name, v in data[key] if name not in held]
        testing = [v for name, v in data[key] if name in held]
        train.append(np.mean(training, axis=0))
        validation.append(np.array(training))
        holdout.append(np.array(testing))
        fit_clips += len(training)
        held_clips += len(testing)
    tensor = lambda v: torch.as_tensor(v, dtype=torch.float32, device=backend)
    target = tensor(np.array(train))
    # Smooth, positive playback rates integrated into an invertible phase map.
    mid = tensor((GRID[1:] + GRID[:-1]) / 2)
    centres = tensor(np.linspace(0, 1, 25))
    basis = torch.softmax(-0.5 * ((mid[:, None] - centres) / 0.045) ** 2, dim=1)
    weights = torch.nn.Parameter(torch.zeros((len(keys), len(centres)), device=backend))
    optimizer = torch.optim.Adam([weights], lr=0.035)
    phase_hold = np.arange(len(GRID)) % 5 == 2
    use = torch.as_tensor(~phase_hold, device=backend)

    def predict():
        increments = torch.softmax(weights @ basis.T, dim=1)
        return torch.nn.functional.pad(torch.cumsum(increments, dim=-1), (1, 0))

    for _ in range(epochs):
        optimizer.zero_grad()
        objective = ((predict()[:, use] - target[:, use]) * 100).square().mean()
        objective.backward()
        optimizer.step()
    values = predict().detach().cpu().numpy().astype(float)
    values[:, [0, -1]] = [0, 1]
    metrics = {}
    for label, sets, mask in [
        ("held_phases", validation, phase_hold),
        ("unseen_rigs", holdout, np.ones_like(phase_hold)),
    ]:
        errors, identity = [], []
        for i, refs in enumerate(sets):
            if refs.size:
                errors.extend(np.mean((refs[:, mask] - values[i, mask]) ** 2, axis=1))
                identity.extend(np.mean((refs[:, mask] - GRID[mask]) ** 2, axis=1))
        if errors:
            metrics[label] = {
                "clips": len(errors),
                "phase_rmse": float(np.sqrt(np.mean(errors))),
                "uniform_phase_rmse": float(np.sqrt(np.mean(identity))),
            }
    return {
        "format": FORMAT,
        "backend": str(backend),
        "device": torch.xpu.get_device_name(backend)
        if backend.type == "xpu"
        else "CPU",
        "epochs": epochs,
        "fit_clips": fit_clips,
        "held_rig_clips": held_clips,
        "metrics": metrics,
        "curves": dict(zip(keys, values.tolist(), strict=True)),
    }


def retime(t, curve, anchors):
    """Monotone phase remap with exact authored event clocks and smooth joins."""
    from scipy.interpolate import PchipInterpolator

    knots = np.unique([0.0, *anchors, 1.0])
    curve = np.asarray(curve)
    if (
        not np.isfinite(knots).all()
        or knots[0] != 0
        or knots[-1] != 1
        or curve.shape != GRID.shape
        or not np.isfinite(curve).all()
        or curve[0] != 0
        or curve[-1] != 1
        or np.any(np.diff(curve) <= 0)
    ):
        raise ValueError("Invalid action phase or event anchors")
    x = np.unique(np.r_[knots, GRID])
    y = np.interp(x, GRID, curve)
    boundaries = np.interp(knots, GRID, curve)
    for i, (a, b) in enumerate(zip(knots[:-1], knots[1:], strict=True)):
        segment = (x > a) & (x < b)
        y[segment] = a + (b - a) * (y[segment] - boundaries[i]) / (
            boundaries[i + 1] - boundaries[i]
        )
    y[np.searchsorted(x, knots)] = knots
    return np.clip(PchipInterpolator(x, y)(t), 0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accepted", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--device", default="xpu:0")
    parser.add_argument("--epochs", type=int, default=400)
    args = parser.parse_args()
    if args.epochs < 1:
        parser.error("epochs must be positive")
    data, manifest = corpus(args.accepted)
    result = fit(data, args.device, args.epochs)
    result["accepted_manifest_sha256"] = manifest
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.out, result)
    print(json.dumps({k: v for k, v in result.items() if k != "curves"}, indent=2))


if __name__ == "__main__":
    main()
