"""Fit and replay phase-dependent motion channels from the motion evaluation."""

import hashlib
import itertools
import json
import math
import os
import tempfile
from pathlib import Path

import torch
from torch import nn

LENGTHS = {"left": 9, "right": 9, "sway": 45, "depth": 34, "shoulder": 34}
FORMAT = "mecha-phase-adapter-v1"
DEFAULT_SEED = 37
INTERVALS = 12


def training_device(request):
    device = torch.device(request)
    if device.type == "cpu" and device.index is None:
        return device
    if device.type != "xpu":
        raise ValueError("Choose 'cpu', 'xpu' or 'xpu:N'.")
    if not hasattr(torch, "xpu") or not torch.xpu.is_available():
        raise RuntimeError("Intel XPU was selected, but this PyTorch runtime has no available XPU.")
    index = torch.xpu.current_device() if device.index is None else device.index
    if index >= torch.xpu.device_count():
        raise ValueError(f"Intel XPU {index} is unavailable.")
    return torch.device("xpu", index)


class _CurveAdapter(nn.Module):
    """Small independent smooth regressors with channel-specific boundary conditions."""

    def __init__(self, seed):
        super().__init__()
        gen = torch.Generator().manual_seed(seed)
        widths = {"left": 5, "right": 5, "sway": 9, "depth": INTERVALS, "shoulder": INTERVALS}
        self.coeffs = nn.ParameterDict(
            {name: nn.Parameter(torch.randn(width, generator=gen) * 0.015) for name, width in widths.items()}
        )

    def forward(self, name, phase):
        w = self.coeffs[name]
        if name == "sway":
            # Constructing both boundary phases as zero gives an exact loop.
            angle = (2 * math.pi) * torch.remainder(phase, 1.0)
            freqs = torch.arange(1, 5, device=w.device, dtype=w.dtype)
            bands = angle[:, None] * freqs
            return (w[0] + torch.cos(bands) @ w[1:5] + torch.sin(bands) @ w[5:9]).clamp(-1, 1)
        if name in ("left", "right"):
            centres = torch.linspace(0, 1, w.numel(), device=w.device)
            phi = torch.exp(-0.5 * ((phase[:, None] - centres) / 0.19).square())
            profile = 0.55 + (phi @ w) / phi.sum(dim=1)
            return (4 * phase * (1 - phase) * profile).clamp(0, 1)
        # The integral of positive interval weights is monotone and exactly
        # reaches its two authored endpoints regardless of the optimizer state.
        widths = torch.softmax(w, dim=0)
        progress = (phase[:, None] * INTERVALS - torch.arange(INTERVALS, device=w.device)).clamp(0, 1)
        values = progress @ widths
        return torch.where(phase == 1, torch.ones_like(values), values)


def _dataset(reference_dir):
    ref = Path(reference_dir)
    if (ref / "kimodo-gait.json").is_file():
        ref = ref / "reference"
    hashes = {}
    samples = {name: [] for name in LENGTHS}

    def read(path, label):
        payload = path.read_bytes()
        hashes[label] = hashlib.sha256(payload).hexdigest()
        return json.loads(payload)

    def add(name, values, split, label):
        if name not in LENGTHS:
            raise ValueError(f"Unknown channel {name!r} in {label}")
        if not isinstance(values, list) or len(values) < 4:
            raise ValueError(f"{label}/{name} needs at least four equally spaced samples")
        lo = -1 if name == "sway" else 0
        for i, value in enumerate(values):
            if type(value) not in (int, float) or not math.isfinite(value) or not lo <= value <= 1:
                raise ValueError(f"Invalid {name} sample {i} in {label}")
        if name == "sway" and abs(values[0] - values[-1]) > 0.001:
            raise ValueError(f"{label}/sway must close the loop")
        if name in ("left", "right") and (values[0] != 0 or values[-1] != 0):
            raise ValueError(f"{label}/{name} must start and finish on the floor")
        if name in ("depth", "shoulder") and (
            values[0] != 0 or values[-1] != 1 or any(b < a for a, b in itertools.pairwise(values))
        ):
            raise ValueError(f"{label}/{name} must rise monotonically from 0 to 1")
        p = torch.linspace(0, 1, len(values))
        # Reserve the same phase bands across clips, even at different sampling rates.
        band = (p * (LENGTHS[name] - 1)).round().int()
        hold = (band % 4 == 2) if split == "train" else torch.ones_like(p, dtype=torch.bool)
        samples[name].append((p, torch.tensor(values, dtype=torch.float32), hold))

    gait = read(ref.parent / "kimodo-gait.json", "kimodo-gait.json")
    sway = read(ref / "kimodo-sway.json", "reference/kimodo-sway.json")
    brace = read(ref / "brace.json", "reference/brace.json")
    for side in ("left", "right"):
        add(side, gait["sides"][side]["swing_clearance"], "train", "kimodo-gait.json")
    add("sway", sway["lateral"], "train", "kimodo-sway.json")
    for name in ("depth", "shoulder"):
        add(name, brace[name], "train", "brace.json")
    for path in sorted((ref / "training-clips").glob("*.json")):
        label = f"reference/training-clips/{path.name}"
        clip = read(path, label)
        if clip.get("split", "train") not in ("train", "validation") or not clip.get("curves"):
            raise ValueError(f"{label} needs curves and a train or validation split")
        for name, values in clip["curves"].items():
            add(name, values, clip.get("split", "train"), label)

    return samples, hashes


def _tensors(samples, device):
    # Each participating clip has equal weight within its channel; longer
    # recordings only give more finely sampled points along the phase axis.
    sets = {}
    for name, clips in samples.items():
        phases, values, train_weights, valid_weights = [], [], [], []
        for p, y, valid in clips:
            phases.append(p)
            values.append(y)
            train_weights.append((~valid).float() / max(1, int((~valid).sum())))
            valid_weights.append(valid.float() / max(1, int(valid.sum())))
        sets[name] = tuple(
            torch.cat(parts).to(device) for parts in (phases, values, train_weights, valid_weights)
        )
    return sets


def _read_state(path):
    state = torch.load(path, map_location="cpu", weights_only=True)
    if not isinstance(state, dict) or state.get("format") != FORMAT:
        raise ValueError(f"Unsupported motion adapter checkpoint: {path}")
    return state


def _save(state, path):
    with tempfile.NamedTemporaryFile(
        dir=path.parent, prefix="curve-", suffix=".pt.tmp", delete=False
    ) as stream:
        temp = Path(stream.name)
    try:
        torch.save(state, temp)
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def _metrics(model, data):
    result = {}
    with torch.no_grad():
        for name, (p, y, train_weight, val_weight) in data.items():
            error = (model(name, p) - y).square()
            result[name] = {}
            for label, w in (("train", train_weight), ("validation", val_weight)):
                result[name][label + "_samples"] = int((w > 0).sum().item())
                result[name][label + "_rmse"] = float(((error @ w) / w.sum()).sqrt().item())
    return result


def train(reference_dir, output_dir, device="cpu", epochs=400, resume=False, *, seed=None):
    """Fit through an absolute epoch number and save an atomically replaced checkpoint."""
    if type(epochs) is not int or epochs < 1:
        raise ValueError("epochs must be a positive integer")
    target = training_device(device)
    samples, hashes = _dataset(reference_dir)
    data = _tensors(samples, target)
    path = Path(output_dir) / "curve-adapter.pt"
    path.parent.mkdir(parents=True, exist_ok=True)
    state = _read_state(path) if resume else None
    if state is not None:
        if state["source_sha256"] != hashes:
            raise ValueError("Training inputs changed since the checkpoint. Start a new fit.")
        if seed is not None and seed != state["seed"]:
            raise ValueError("Resume requires the original seed")
        seed = state["seed"]
        if epochs < state["epoch"]:
            raise ValueError("epochs must be at least the saved epoch")
    seed = DEFAULT_SEED if seed is None else seed
    model = _CurveAdapter(seed).to(target)
    optimizer = torch.optim.Adam(model.parameters(), lr=0.035, foreach=False)
    first_epoch = 0
    if state is not None:
        first_epoch = state["epoch"]
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
    for epoch in range(first_epoch + 1, epochs + 1):
        optimizer.zero_grad(set_to_none=True)
        losses = []
        for name, (phase, values, weight, _) in data.items():
            error = model(name, phase) - values
            losses.append((error.square() @ weight) / weight.sum())
        torch.stack(losses).mean().backward()
        optimizer.step()
        if epoch == epochs or epoch % 50 == 0:
            state = {
                "format": FORMAT,
                "epoch": epoch,
                "seed": seed,
                "model": model.state_dict(),
                "optimizer": optimizer.state_dict(),
                "source_sha256": hashes,
                "torch_version": str(torch.__version__),
                "training_device": str(target),
                "device_name": torch.xpu.get_device_name(target) if target.type == "xpu" else "CPU",
                "metrics": _metrics(model, data),
            }
            _save(state, path)
    return {
        "checkpoint": str(path),
        "epoch": state["epoch"],
        "metrics": state["metrics"],
        "source_sha256": hashes,
        "training_device": state["training_device"],
    }


def predict(checkpoint, device="cpu"):
    """Sample the fitted curves at the native preview phases, including endpoints."""
    target = training_device(device)
    state = _read_state(checkpoint)
    model = _CurveAdapter(state["seed"]).to(target)
    model.load_state_dict(state["model"])
    model.eval()
    with torch.inference_mode():
        return {
            name: model(name, torch.linspace(0, 1, n, device=target)).cpu().tolist()
            for name, n in LENGTHS.items()
        }
