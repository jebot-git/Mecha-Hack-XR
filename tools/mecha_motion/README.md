# Retrain and bake the mecha motion roster

This offline pipeline trains a shared motion-timing adapter and retargets its predictions onto each applicable published skeleton. Each batch produces native-coordinate pose tracks, textured animated GLBs for review in Blender, and measurements for every actor. The game asset publisher is independent of these review outputs.

## Run on Intel Arc

From a Linux checkout with Python 3.12, build the published asset pack and install the Intel wheel in its own environment. Install the graphics driver appropriate for the target Arc and OS first. The tested CPU environment used PyTorch 2.15.0. The XPU wheel and graphics driver are specific to the target machine. [PyTorch Intel GPU setup](https://docs.pytorch.org/docs/2.15/notes/get_start_xpu.html)

```sh
cmake -S . -B build-mecha
cmake --build build-mecha --target mecha-game-assets
uv venv --python 3.12 .venv-motion
py=.venv-motion/bin/python
uv pip install --python "$py" torch==2.15.0 --index-url https://download.pytorch.org/whl/xpu
uv pip install --python "$py" numpy scipy Pillow
"$py" -m tools.mecha_motion doctor --device xpu:0
```

The probe lists the device names and runs a gradient and optimizer update. Use the index of your Arc, especially on systems with integrated graphics. Pass `--device cpu` with a CPU build of PyTorch for an offline rehearsal. The curve fit and prediction run on the requested torch device. Mesh processing, constrained joint solving and GLB export use NumPy/SciPy on the CPU.

```sh
out="$PWD/build-mecha/motion-work"
"$py" -m tools.mecha_motion inventory --json "$out/inventory.json"
"$py" -m tools.mecha_motion run --device xpu:0 --epochs 400 --output "$out"
"$py" -m tools.mecha_motion verify --all --output "$out"
```

`latest.json` points to the completed batch report. Open a GLB beside that report in Blender and scrub its animation. The sibling JSON gives per-frame contacts, root travel and per-part transforms. The report records the selected rigs, excluded rigs and reasons, mesh and binding hashes, texture hash, checkpoint digest, output checksums and contact measurements. A failed bake leaves the last completed batch available. Each successful bake gets its own directory.

## Update a fit or review a subset

Training accepts the checked-in Kimodo and MotionBricks reference curves or a regenerated reference set. Add normalized training or held-out clips under the reference directory. Resume restores the optimizer for the same data and treats the epoch count as an absolute target. Start a new output directory for a changed corpus. [Adapter training and clip format](training.md)

```sh
"$py" -m tools.mecha_motion train --device xpu:0 --epochs 600 --resume --output "$out"
"$py" -m tools.mecha_motion bake --device xpu:0 --checkpoint "$out/curve-adapter.pt" \
    --output "$out/spot-check" --only mecha_ace kaiju_stalker kaiju_tunnel_wurm
"$py" -m tools.mecha_motion verify --output "$out/spot-check"
```

To regenerate the teacher motions on Arc, use Kimodo through PyTorch XPU and the MotionBricks G1 ONNX graph through OpenVINO's GPU plug-in. Then add `--references /path/to/reference-set` to `train` or `run`. To change the Kimodo denoiser weights themselves, fine-tune them on G1 recordings and supply the resulting weights to the reference generator. MotionBricks provides the pose-conditioned brace reference from its released graph. [Arc reference recipes](arc-references.md) · [Kimodo denoiser fine-tuning](finetune.md)

## Roster and acceptance checks

The baker resolves published bindings in game lookup order, including player chassis, variants, Mission Manual and story actors. The current inventory has 73 mechanical rigs, 20 kaiju and four infantry squads. Twenty-one vehicle or installation rigs and two single-hull props have no applicable gait. The inventory is rebuilt from the supplied pack on every run. Use `--pack` for a nondefault published pack.

Footed rigs get world-space stance anchors, two-link leg solves, root weight transfer and morphology-scaled stride and compression. Both Ace variants carry the approved elbow split and forearm-mounted weapon. Jaw-equipped kaiju follow the coil with a bite and recovery, with delayed motion through tail segments. Serpentine rigs get axial motion with a ground or water-bed reference. The solver reduces amplitude on a rig that cannot reach its target or whose attached geometry crosses the floor. The clip records that reduction as `reach_scale`. The GLB verifier samples every animated joint at every frame and checks the exported parent-local transforms against the native track. These checks cover the authored level-ground sequence; slopes, live interruptions and network playback require a game-side trial. [Accepted control studies](../../art/mecha/motion-evaluation/README.md)

## Verification on the preparation host

A 400-epoch CPU fit baked all 97 applicable rigs. The GLB round trip reproduced the native joint transforms across 11,737 frames. Maximum measured floor penetration was 0.000001 m. The regression checks also cover resume, held-out data, device selection, Ace equipment parenting and Stalker jaw/tail timing. Run them after building the published pack.

```sh
"$py" tests/mecha_motion_training.py
"$py" tests/mecha_motion_bake.py
```

A separate CPU smoke test trained the four Kimodo output-layer parameters, resumed the optimizer and generated a new sway reference with the resulting weights. A one-window full-denoiser smoke test updated and exported all 408 parameter tensors. That tiny full-model test increased held-out error, so it demonstrates execution only. No tuned model or generated roster from those smoke runs is shipped here. Physical Arc dispatch, memory use and throughput still need verification on the intended machine.

## Render the full roster and expanded action library

Use the verified full-roster Arc bake as the input. The first command renders the original approach and brace or bite for every eligible rig. The second samples the checkpoint on the same compute device, builds capability-specific actions and renders the expanded library. Run from the repository root with the motion environment above. These steps also use ModernGL, an EGL graphics driver, and the FFmpeg tools (including ffprobe and the libx264 encoder).

```sh
cmake --build build-mecha --target mecha-game-assets mecha-catalog
uv pip install --python "$py" moderngl
review="$PWD/artifacts/arc-motion-review"
"$py" -m tools.mecha_motion.roster reference --motion "$out" --out "$review"
"$py" -m tools.mecha_motion.roster actions --motion "$out" --out "$review" --jobs 4
```

Open `index.html` in the review directory to browse actors, jump to an action and open its native tracks or animated GLB. The reference reel shows the original trained sequence. The action overview samples each distinct action name. Each actor's own video plays every applicable action in sequence.

The native NPZ contains joint names and topology in `parts_json`, contiguous float32 XYZW rotations and XYZ offsets in `poses`, world travel in `root`, and `names`, `offsets` and `fps` for slicing actions. The GLB has the same rigid parts and one independently selectable animation per action. The verifier reconstructs each exported node at every frame and compares it to the native file. Completed actor exports with matching inputs and checksums can be reused on a rerun.

These are offline skeletal motion studies. Weapon trails, projectiles, spawned actors and other effects are rendered separately in the game. Gallery timing shows each action in isolation. Terrain transitions, live input blending and multiplayer interruption require engine testing.

### Refine pacing against an accepted gallery

Retain the accepted gallery as training input and use a new directory for the revised clips. The action timing trainer reads the accepted native poses and root travel. It uses angular motion and world-space joint speed to give slower preparations and holds more screen time, while allowing sharp releases to carry through. The pacing targets are derived from the accepted performance and bounded to 75 ms of displacement before fitting. Identical-rate motion retains a linear target.

```sh
accepted="$PWD/artifacts/arc-motion-review"
revised="$PWD/artifacts/arc-motion-refinement"
"$py" -m tools.mecha_motion.action_timing --device xpu:0 --epochs 400 \
    --accepted "$accepted" --out "$revised/action-timing.json"
"$py" -m tools.mecha_motion.roster reference --motion "$out" --out "$revised"
"$py" -m tools.mecha_motion.roster actions --motion "$out" --out "$revised" --jobs 4 \
    --timing "$revised/action-timing.json"
"$py" -m tools.mecha_motion.compare_actions --accepted "$accepted" --revised "$revised"
```

The model learns a smooth rate curve per rig family and action. It holds out entire rig groups where available, keeps alternate skins together and reserves phase samples for single-rig actions as well. The JSON artifact stores the fitted curves, device, accepted manifest digest and held-out errors against the pacing targets. During export, a monotone interpolator preserves the exact author-specified times for weapon emissions, landings and other contacts. The revised gallery contains every action. The two-up comparison uses the same camera framing and clock for each version and the accompanying JSON indexes the example chapters.
