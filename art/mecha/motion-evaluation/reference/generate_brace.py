"""Generate the study's animation timing from an authored G1 pose transition."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

MODEL_SHA = "79258d3613988b74ab84a530352ea600bbe78a1048584f059e231e44641cea16"


def g1_pose(height, hip, knee, shoulder, elbow):
    pose = np.zeros(36, np.float32)
    pose[2] = height
    pose[6] = 1  # Identity root orientation, XYZW
    for leg in (7, 13):
        pose[leg] = hip
        pose[leg + 3] = knee
        pose[leg + 4] = -hip - knee
    for arm in (22, 29):
        pose[arm] = shoulder
        pose[arm + 3] = elbow
    return np.tile(pose, (1, 4, 1))


def normalized(values):
    span = values[-1] - values[0]
    if abs(span) < .03 or not np.isfinite(values).all():
        raise ValueError("The generated poses have insufficient travel for retargeting")
    return [round(float(value), 5) for value in np.clip((values - values[0]) / span, 0, 1)]


def run_openvino_arc(model_path, inputs, index):
    import openvino as ov

    core = ov.Core()
    available = core.available_devices
    device = f"GPU.{index}"
    if index == 0 and device not in available and "GPU" in available:
        device = "GPU"
    if device not in available:
        raise RuntimeError(f"Requested OpenVINO GPU.{index}; available devices: {available}")
    name = core.get_property(device, "FULL_DEVICE_NAME")
    if "arc" not in name.lower():
        raise RuntimeError(f"{device} identifies as {name!r}; select the Intel Arc device index")
    graph = core.read_model(model_path)
    # The G1 reference has one fixed batch. Static shapes avoid dynamic
    # batch specializations on the GPU and preserve the ONNX input contract.
    graph.reshape({key: list(value.shape) for key, value in inputs.items()})
    compiled = core.compile_model(graph, device, {"INFERENCE_PRECISION_HINT": "f32"})
    execution = list(compiled.get_property("EXECUTION_DEVICES"))
    aliases = {device}
    if index == 0:
        aliases.update(["GPU", "GPU.0"])
    if not execution or any(actual not in aliases for actual in execution):
        raise RuntimeError(f"Unexpected OpenVINO execution devices: {execution}")
    print(f"MotionBricks on {name} via OpenVINO {ov.__version__}, {execution}", flush=True)
    result = compiled(inputs)
    outputs = {key: result[compiled.output(key)] for key in ("pred_num_tokens", "pred_qpos")}
    return outputs, dict(runtime=f"OpenVINO {ov.__version__}", device=device,
                         hardware=name, execution_devices=execution)


def run_cpu(model_path, inputs):
    import onnxruntime as ort

    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    ort.set_seed(7)
    session = ort.InferenceSession(str(model_path), sess_options=options,
                                   providers=["CPUExecutionProvider"])
    return dict(zip((out.name for out in session.get_outputs()), session.run(None, inputs)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path, help="Official Unitree G1 FP32 ONNX file")
    parser.add_argument("--device", choices=("cpu", "arc"), default="cpu")
    parser.add_argument("--device-index", type=int, default=0, help="OpenVINO GPU index for --device arc")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.device_index < 0 or (args.device == "cpu" and args.device_index):
        parser.error("--device-index must be nonnegative and zero for CPU")
    destination = args.out or Path(__file__).with_name("brace-arc.json" if args.device == "arc" else "brace.json")
    with args.model.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != MODEL_SHA:
            raise ValueError("Expected the MotionBricks G1 FP32 graph at the documented revision")
    start = g1_pose(.77, -.12, .26, -.08, .35)
    end = g1_pose(.62, -.70, 1.33, -.78, .91)
    mask = np.ones((1, 8), dtype=np.int32)
    inputs = dict(context_qpos=start, target_qpos=end, has_global_root_values=mask,
                  has_local_root_values=mask, has_local_poses=mask,
                  allowed_pred_num_tokens=np.ones((1, 11), dtype=np.int32))
    provenance = None
    if args.device == "arc":
        outputs, provenance = run_openvino_arc(args.model, inputs, args.device_index)
    else:
        outputs = run_cpu(args.model, inputs)
    tokens = int(outputs["pred_num_tokens"].item())
    poses = outputs["pred_qpos"][0, 3:tokens * 4 - 3]
    record = {"fps": 30, "model_sha256": MODEL_SHA, "tokens": tokens,
              "depth": normalized(poses[:, [10, 16]].mean(axis=1)),
              "shoulder": normalized(poses[:, [22, 29]].mean(axis=1))}
    if provenance:
        record["inference"] = provenance
    destination.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
