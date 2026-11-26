# Motion references

## Kimodo walking reference

The study reads NVIDIA’s published G1 waypoint walk. Its generation metadata records seed 61, 100 diffusion steps and four horizontal root targets at frames 0, 87, 169 and 240. The recording supplies joint positions and predicted ankle/toe contacts for 241 frames at 30 Hz. These are samples from NVIDIA’s generation run. The local experiment measures them on CPU. [Kimodo G1 example at the sampled revision](https://github.com/nv-tlabs/kimodo/tree/e1c219c99b4b94ba48be1fb6a1cbc64ecee4c954/kimodo/assets/demo/examples/kimodo-g1-rp/06_root_waypoints)

For each side, a foot is in support when its ankle or toe flag is active. The median interval between support onsets is 34 frames on both sides. The left foot spends 53.1% of the clip in support, the right 49.8%, and both share support for 3.7%. Frames 99–134 span one left-to-left cycle. The nine samples per swing measure ankle height above the line between lift-off and touchdown, divided by that swing’s peak. Each curve peaks around three-eighths of the way through swing. On the game rig, this supplies timing and shape independently of absolute G1 leg length. [Joint and contact order](https://github.com/nv-tlabs/kimodo/blob/e1c219c99b4b94ba48be1fb6a1cbc64ecee4c954/kimodo/skeleton/definitions.py#L286-L315)

Rebuild the measurements from this directory. The script verifies the SHA-256 of the source archive before extracting either curve.

```sh
curl -fLsS https://raw.githubusercontent.com/nv-tlabs/kimodo/e1c219c99b4b94ba48be1fb6a1cbc64ecee4c954/kimodo/assets/demo/examples/kimodo-g1-rp/06_root_waypoints/motion.npz -o /tmp/kimodo-g1-walk.npz
uv run --no-project --with numpy python measure_kimodo.py /tmp/kimodo-g1-walk.npz > /tmp/kimodo-gait.json
diff kimodo-gait.json /tmp/kimodo-gait.json
```

## New Kimodo stride

We also ran the released G1 diffusion model locally on CPU. We took three full-body keys from frames 99, 117 and 134 of the waypoint walk and placed them at frames 0, 23 and 44. Kimodo generated the intervening motion with 18 DDIM steps, seed 37 and guidance weight 2. The empty prompt uses the model’s zero text-conditioning path. Each of the 45 samples contains positions for 34 joints. We projected the pelvis relative to the midpoint of the ankles onto the horizontal hip axis, removed endpoint drift and the mean offset, and scaled to unit peak. The resulting periodic curve supplies left/right weight transfer independently of game-unit scale. [Kimodo G1 weights](https://huggingface.co/nvidia/Kimodo-G1-RP-v1/tree/3020ad8c419c244e0429d360163730c63c4ed011) · [Kimodo generation code](https://github.com/nv-tlabs/kimodo/blob/e1c219c99b4b94ba48be1fb6a1cbc64ecee4c954/kimodo/model/kimodo_model.py)

The following commands reproduce the CPU run using Python 3.12 and the example downloaded above. The weight file is 1.13 GB. Set the destination outside the game repository.

```sh
work=/tmp/mecha-motion-tools
mkdir -p "$work"
git clone https://github.com/nv-tlabs/kimodo.git "$work/kimodo"
git -C "$work/kimodo" checkout --detach e1c219c99b4b94ba48be1fb6a1cbc64ecee4c954
uv venv --python 3.12 "$work/venv"
uv pip install --python "$work/venv/bin/python" torch==2.15.0 --index-url https://download.pytorch.org/whl/cpu
uv pip install --python "$work/venv/bin/python" numpy scipy hydra-core huggingface-hub safetensors tqdm transformers==5.1.0 peft==0.22.0 einops pydantic trimesh
"$work/venv/bin/hf" download nvidia/Kimodo-G1-RP-v1 --revision 3020ad8c419c244e0429d360163730c63c4ed011 --local-dir "$work/Kimodo-G1-RP-v1"
PYTHONPATH="$work/kimodo" "$work/venv/bin/python" reference/generate_sway.py "$work/Kimodo-G1-RP-v1" /tmp/kimodo-g1-walk.npz --out /tmp/kimodo-sway.json
```

## MotionBricks reference

The published model conditions a short transition on four recent poses and one to four target poses. Playback feeds its most recent samples back into the next transition. A Smart Primitive places target poses relative to an object, allowing the same action to play at different locations. The official rigs are SOMA and Unitree G1. The Stalker has an authored quadruped gait and uses the generated G1 flexion curve to time its pre-bite coil. [MotionBricks implementation](https://github.com/NVlabs/motionbricks/tree/54fc563aeaaf819a753d72cc9fdc6713989cdd43) · [Project and paper](https://nvlabs.github.io/motionbricks/)

For the brace transition, we ran the released G1 FP32 graph through ONNX Runtime’s CPU provider. Four repeated standing poses form the context and four repeated braced poses form the target. The model selected ten tokens. Trimming its context and boundary padding left 34 poses at 30 Hz. The mean knee and shoulder rotations give the two normalized response curves in `reference/brace.json`. [Pinned G1 ONNX release](https://github.com/NVlabs/motionbricks/blob/54fc563aeaaf819a753d72cc9fdc6713989cdd43/motionbricks/assets/onnx/unitree_g1/mmm_v6_g1_fp32.onnx) · [Runtime input and output contract](https://github.com/NVlabs/motionbricks/blob/54fc563aeaaf819a753d72cc9fdc6713989cdd43/docs/runtime_api.md)

```sh
curl -fLsS https://media.githubusercontent.com/media/NVlabs/motionbricks/54fc563aeaaf819a753d72cc9fdc6713989cdd43/motionbricks/assets/onnx/unitree_g1/mmm_v6_g1_fp32.onnx -o /tmp/motionbricks-g1.onnx
uv run --no-project --with numpy --with onnxruntime python reference/generate_brace.py /tmp/motionbricks-g1.onnx --out /tmp/motionbricks-brace.json
```
