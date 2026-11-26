# Fine-tune the Kimodo G1 denoiser

This optional stage adapts the released G1 checkpoint to new motion recordings. Its output feeds the Kimodo reference generator and then the shared rig baker. It uses full-body pose conditioning and an empty text embedding. The default trains the root and body output layers. `--scope full` enables gradients for every denoiser parameter, with substantially higher memory requirements for gradients and Adam optimizer state. Use the same scope and corpus for resume.

Use the Kimodo checkout and Python dependencies from the reference setup, including safetensors. Provide separate `train/` and `validation/` directories containing independent 30 Hz G1 recordings in metres, Y up, in the upstream G1 34-joint order. Each NPZ contains `posed_joints[T,34,3]` and `global_rot_mats[T,34,3,3]`. An optional scalar `fps` is checked against 30. Use a character-, take- or sequence-level split before windowing. Exact duplicate files across splits are rejected. [Reference setup and base revision](arc-references.md) · [G1 example format](../../art/mecha/motion-evaluation/methods.md)

```sh
export PYTHONPATH="$work/kimodo"
"$py" -m tools.mecha_motion.finetune --base "$work/Kimodo-G1-RP-v1" \
    --corpus "$work/g1-clips" --scope heads --epochs 4 --frames 45 \
    --device xpu:0 --output "$work/g1-fit"
"$py" -m tools.mecha_motion.finetune --base "$work/Kimodo-G1-RP-v1" \
    --corpus "$work/g1-clips" --scope heads --epochs 8 --frames 45 \
    --resume --device xpu:0 --output "$work/g1-fit"
```

Every epoch visits windows across all training clips. The upstream feature encoder canonicalizes and normalizes each window. Three observed poses condition the denoiser as it learns to reconstruct clean features on the intervening frames from cosine-schedule diffusion noise. The validation windows use fixed noise and timesteps for comparable per-epoch errors. The checkpoint includes the trainable parameters, optimizer, epoch, corpus digests and base digest. `finetune.json` records the endpoint validation errors and the exported weights digest.

The exported `tuned.safetensors` holds replacement parameter values for the selected scope. To generate motion from the updated model, run the Arc Kimodo command with `--tuned-weights "$work/g1-fit/tuned.safetensors"` and the destination `--out "$out/reference/kimodo-sway.json"`. Use that reference directory for a fresh adapter fit. [Reference generation](arc-references.md)
