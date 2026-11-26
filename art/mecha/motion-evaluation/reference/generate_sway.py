"""Generate a G1 stride from three pose keys and extract its weight transfer."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

WEIGHTS = "e18c1de73e2ce17a107b06d85155fbbc5debe68eb35455aa5b033e6ddbe056a5"
SAMPLE = "eda98d489984ac45289336207dafe3215757e7bc75b770a6a96239e093b4a0a9"


def lateral_channel(joints):
    # Measure pelvis over the midpoint between the two ankles, toward the
    # character's left. Remove endpoint drift and DC before scaling to unit peak.
    left = joints[:, 1] - joints[:, 8]
    left[:, 1] = 0
    left /= np.linalg.norm(left, axis=1, keepdims=True)
    midfoot = .5 * (joints[:, 6] + joints[:, 13])
    lateral = np.sum((joints[:, 0] - midfoot) * left, axis=1)
    lateral -= np.linspace(0, lateral[-1] - lateral[0], len(lateral))
    lateral -= lateral[:-1].mean()
    peak = np.max(np.abs(lateral))
    if not np.isfinite(peak) or peak < .005:
        raise ValueError("Expected measurable weight transfer from this stride")
    return np.round(lateral.astype(float) / peak, 5).tolist()


def apply_tuned_weights(module, path):
    """Replace either both complete output heads or the denoiser's full parameter set."""
    import torch
    from safetensors.torch import load_file

    params = dict(module.named_parameters())
    heads = {f"{branch}.output_linear.{name}" for branch in ("root_model", "body_model")
             for name in ("bias", "weight")}
    values = load_file(path, device="cpu")
    if not heads <= params.keys() or values.keys() not in (heads, params.keys()):
        raise ValueError("Expected all four root/body output head parameters or the full denoiser parameter set")
    for name, value in values.items():
        target = params[name]
        if value.shape != target.shape or value.dtype != target.dtype or not torch.isfinite(value).all():
            raise ValueError(f"Incompatible tuned parameter: {name}")
    with torch.no_grad():
        for name, value in values.items():
            params[name].copy_(value)
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    import os
    import torch
    from kimodo import load_model
    from kimodo.constraints import FullBodyConstraintSet
    from kimodo.motion_rep.feature_utils import compute_heading_angle

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint_dir", type=Path, help="Local Kimodo-G1-RP-v1 directory")
    parser.add_argument("example", type=Path, help="Upstream root-waypoints motion.npz")
    parser.add_argument("--device", choices=("cpu", "arc"), default="cpu")
    parser.add_argument("--device-index", type=int, default=0, help="PyTorch XPU index for --device arc")
    parser.add_argument("--tuned-weights", type=Path, help="Safetensors replacement parameters for the heads or full denoiser")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.device_index < 0 or (args.device == "cpu" and args.device_index):
        parser.error("--device-index must be nonnegative and zero for CPU")
    suffix = ("-arc" if args.device == "arc" else "") + ("-tuned" if args.tuned_weights else "")
    destination = args.out or Path(__file__).with_name(f"kimodo-sway{suffix}.json")
    device = torch.device("cpu")
    provenance = None
    if args.device == "arc":
        if not torch.xpu.is_available() or args.device_index >= torch.xpu.device_count():
            raise RuntimeError(f"Requested XPU {args.device_index}; PyTorch sees {torch.xpu.device_count()} Intel GPUs")
        name = torch.xpu.get_device_name(args.device_index)
        if "arc" not in name.lower():
            raise RuntimeError(f"XPU {args.device_index} identifies as {name!r}; select the Intel Arc device index")
        device = torch.device("xpu", args.device_index)
        torch.xpu.set_device(device)
        provenance = dict(runtime=f"PyTorch {torch.__version__}", device=str(device), hardware=name)
        print(f"Kimodo on {name} via {provenance['runtime']}, {device}", flush=True)
    with (args.checkpoint_dir / "model.safetensors").open("rb") as model_file:
        if hashlib.file_digest(model_file, "sha256").hexdigest() != WEIGHTS:
            raise ValueError("Unexpected Kimodo checkpoint")
    if hashlib.sha256(args.example.read_bytes()).hexdigest() != SAMPLE:
        raise ValueError("Unexpected Kimodo waypoint example")

    class EmptyPrompt(torch.nn.Module):
        def forward(self, prompts):
            if any(prompts):
                raise ValueError("This run conditions on joint poses only")
            return torch.zeros(len(prompts), 1, 4096, device=device), [0] * len(prompts)

    os.environ["CHECKPOINT_DIR"] = str(args.checkpoint_dir.resolve().parent)
    torch.set_num_threads(2)
    torch.manual_seed(37)
    model = load_model("Kimodo-G1-RP-v1", device=str(device), text_encoder=EmptyPrompt())
    if any(parameter.device != device for parameter in model.denoiser.parameters()):
        raise RuntimeError(f"Kimodo denoiser parameters did not load on {device}")
    tuned_sha = apply_tuned_weights(model.denoiser.model, args.tuned_weights) if args.tuned_weights else None
    with np.load(args.example, allow_pickle=False) as example:
        keys = [99, 117, 134]
        xyz = torch.as_tensor(example["posed_joints"][keys], device=device)
        rot = torch.as_tensor(example["global_rot_mats"][keys], device=device)
    xyz -= xyz[0, 0] * torch.tensor([1, 0, 1], device=device)
    target_frames = torch.tensor([0, 23, 44], device=device)
    targets = FullBodyConstraintSet(model.skeleton, target_frames, xyz, rot)
    with torch.inference_mode():
        result = model("", 45, num_denoising_steps=18, constraint_lst=[targets],
                       cfg_type="regular", cfg_weight=2.0,
                       first_heading_angle=compute_heading_angle(xyz[None, :1], model.skeleton).flatten(),
                       return_numpy=False, post_processing=False)
    tensor = result["posed_joints"]
    if tensor.device != device:
        raise RuntimeError(f"Expected generated joints on {device}; got {tensor.device}")
    joints = tensor.detach().cpu().numpy()
    if joints.shape != (45, 34, 3) or not np.isfinite(joints).all():
        raise ValueError("Invalid G1 output")
    record = dict(fps=30, model_sha256=WEIGHTS, seed=37, lateral=lateral_channel(joints))
    if tuned_sha:
        record["tuned_weights_sha256"] = tuned_sha
    if provenance:
        record["inference"] = provenance
    destination.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
