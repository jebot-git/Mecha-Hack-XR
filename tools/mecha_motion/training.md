# Train the motion curve adapter

Run from the repository root in a Python environment with PyTorch. The training command defaults to Intel XPU. The Intel build of PyTorch, a supported GPU and a working Intel driver are required for that backend. Select CPU explicitly to use a CPU build.

```sh
python -m tools.mecha_motion train --output /tmp/mecha-fit --device cpu --epochs 400
python -m tools.mecha_motion train --output /tmp/mecha-fit --device cpu --epochs 600 --resume
```

For Intel hardware, use `--device xpu` or a specific device such as `--device xpu:0`. Parameters, forward evaluation, gradients and Adam updates run on the selected device. An unavailable XPU raises an error before the tool creates a checkpoint.

## What the fit learns

The five regressors learn normalized motion values as functions of phase from 0 through 1. Left and right swing clearance each use five radial basis coefficients and a zero-height envelope at both foot contacts. Lateral sway uses a four-harmonic Fourier series with an exactly closed loop. Brace depth and shoulder travel accumulate twelve learned positive interval weights, giving monotone progress from 0 to 1. The 43 trainable coefficients form a shared timing reference for the rig baker. This stage trains the adapter. The upstream Kimodo and MotionBricks weights supplied its reference motion. [Reference generation and sources](../../art/mecha/motion-evaluation/methods.md)

The default fit reads `kimodo-gait.json` from the evaluation root and `reference/kimodo-sway.json` and `reference/brace.json` beneath it. The left and right channels sample a foot's swing interval. Sway covers one complete walking stride from left touchdown to the next left touchdown. Depth and shoulder each cover one standing-to-braced transition. The baker supplies rig proportions, stride length, contact intervals and clip duration.

To use additional recordings, copy the reference files to a working directory with the same structure, point `--references` at its root, and add JSON files in `reference/training-clips/`. An example additional clip follows.

```json
{
  "split": "train",
  "curves": {
    "left": [0, 0.3, 0.8, 1.0, 0.7, 0.3, 0.1, 0.02, 0]
  }
}
```

Each file can supply any subset of `left`, `right`, `sway`, `depth` and `shoulder`. Supply at least four equally spaced, finite samples per curve. Align the phase and units of each recording to the definitions above. Swing values lie in [0, 1] with zero endpoints. Sway lies in [−1, 1], positive toward the character's left, with matching endpoints. Brace values increase monotonically from 0 to 1. More clips train the same five phase responses, so recordings with different styles are averaged. Train separate output directories for distinct styles.

## Validation and recovery

The optimizer holds out phase bands across all training clips. At the canonical sample count for each channel, indices 2, 6, 10 and so on belong to validation. An additional clip sampled at another rate uses the nearest canonical index for its split. A clip marked `"split": "validation"` contributes only to the score. The objective weights each channel equally and averages each contributing clip within its channel.

The default reference set supplies 100 fit samples and 31 held-out samples. On CPU at epoch 400, normalized held-out RMSE measured 0.0059 left swing, 0.0492 right swing, 0.0397 sway, 0.0012 depth and 0.0032 shoulder. These measure timing interpolation for the supplied recordings. Foot contact, joint limits and motion on individual rigs need separate bake measurements.

`curve-adapter.pt` records the model, optimizer, epoch, seed, PyTorch build and training device, SHA-256 of every input JSON, and per-channel train/validation sample counts and RMSE. The process saves every 50 epochs and at completion using atomic replacement. The full-batch optimizer and seeded initialization make a same-configuration run reproducible. Resume restores optimizer moments and interprets `--epochs` as the total target epoch. A changed reference hash requires a fresh fit.

Inspect a checkpoint or sample its five curves with Python.

```python
import torch
from tools.mecha_motion.training import predict

checkpoint = "/tmp/mecha-fit/curve-adapter.pt"
print(torch.load(checkpoint, map_location="cpu", weights_only=True)["metrics"])
curves = predict(checkpoint, device="cpu")
```

Run `python tests/mecha_motion_training.py` to check held-out error, exact CPU resume, curve endpoints, monotonic brace progress, extra clips, and device selection.
