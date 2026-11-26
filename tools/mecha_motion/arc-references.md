# Regenerate motion references on Intel Arc

Kimodo uses PyTorch XPU for the G1 diffusion denoiser. MotionBricks uses OpenVINO’s Intel GPU plugin for its FP32 G1 ONNX graph. Both generators accept `--device arc --device-index N`. XPU and OpenVINO enumerate devices independently. Use the Arc index reported by each runtime. The scripts check the hardware name, print the selected device and record it in the output JSON.

### Environment and source models

Use a Linux host with an Intel Arc GPU, the Intel GPU compute driver and access to its render device. Install the runtime versions below into an isolated Python 3.12 environment. The OpenVINO GPU plugin also needs the Intel OpenCL runtime. [PyTorch Intel GPU setup](https://docs.pytorch.org/docs/stable/notes/get_start_xpu.html) · [OpenVINO Intel GPU driver setup](https://docs.openvino.ai/2026/get-started/install-openvino/configurations/configurations-intel-gpu.html)

From the repository root, prepare the upstream source, checkpoint and waypoint example using the pinned revisions in the study. Point `work` at that directory. The commands below use the example from the Kimodo checkout and the MotionBricks ONNX file downloaded to `/tmp/motionbricks-g1.onnx`. [Pinned sources and model download commands](../../art/mecha/motion-evaluation/methods.md)

```sh
work=/tmp/mecha-motion-tools
uv venv --python 3.12 "$work/arc-venv"
py="$work/arc-venv/bin/python"
uv pip install --python "$py" torch==2.15.0 --index-url https://download.pytorch.org/whl/xpu
uv pip install --python "$py" numpy scipy hydra-core huggingface-hub safetensors tqdm \
  transformers==5.1.0 peft==0.22.0 einops pydantic trimesh Pillow openvino==2026.4.1
"$py" -c 'import torch; print([(i, torch.xpu.get_device_name(i)) for i in range(torch.xpu.device_count())])'
"$py" -c 'import openvino as ov; c=ov.Core(); print([(d, c.get_property(d, "FULL_DEVICE_NAME")) for d in c.available_devices])'
```

### Generate a reference set

The reference set for the adapter also includes the foot-contact measurements taken from the original waypoint sequence. Copy those measurements and generate the other two files into a separate directory.

```sh
study=art/mecha/motion-evaluation
out="$work/arc-reference-set"
mkdir -p "$out/reference"
cp "$study/kimodo-gait.json" "$out/"
example="$work/kimodo/kimodo/assets/demo/examples/kimodo-g1-rp/06_root_waypoints/motion.npz"
PYTHONPATH="$work/kimodo" "$py" "$study/reference/generate_sway.py" \
  "$work/Kimodo-G1-RP-v1" "$example" --device arc --device-index 0 \
  --out "$out/reference/kimodo-sway.json"
"$py" "$study/reference/generate_brace.py" /tmp/motionbricks-g1.onnx \
  --device arc --device-index 0 --out "$out/reference/brace.json"
```

Kimodo checks where its denoiser parameters and output joints reside. MotionBricks verifies the compiled model’s `EXECUTION_DEVICES` before inference. An unavailable device, a different GPU family or failed model compilation stops the run before it writes a curve. A successful Arc file records `inference.runtime`, `inference.device` and `inference.hardware`. The OpenVINO record also lists `inference.execution_devices`. An XPU seed can produce a different valid diffusion sample from the CPU seed. [OpenVINO device properties](https://docs.openvino.ai/nightly/openvino-workflow/running-inference/inference-devices-and-modes/query-device-properties.html)

To sample a fine-tuned Kimodo model, add `--tuned-weights /path/to/tuned.safetensors` to the sway command. The file replaces all four parameters of the root/body output layers or the complete denoiser parameter set. The generator checks names, shapes, dtypes and finiteness and records the replacement file’s SHA-256 alongside the base model hash. Use the adapter command’s `--references "$out"` to train against this reference set. [Adapter training](training.md)

### CPU replay and verification

The default backend remains CPU. The original sway and brace commands reproduce their checked-in JSON on the evaluation host. Install `onnxruntime` to use the CPU brace recipe. An explicit Arc request on this host raised a device-selection error in each generator. Its Intel N95 machine exposes no Arc device, so Arc numerical results and throughput still require measurement on target hardware. [CPU replay recipes](../../art/mecha/motion-evaluation/methods.md)
