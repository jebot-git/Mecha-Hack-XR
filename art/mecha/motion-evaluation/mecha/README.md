# Mecha Ace movement study

The Ace takes three strides, brakes over the last stride, plants both feet and lowers its centre of mass into a guard. The comparison covers 13.5 metres in 7.9 seconds at 30 fps. The game pose solver and the prototype receive the same root travel, speed and action timing. [Comparison](mecha-ace.mp4) · [Editable animated GLB](mecha-ace.glb)

The prototype reuses all 748 triangles. Two new elbow hinges bind the forearm, hand and attached equipment as rigid assemblies. The existing thigh and shin lengths feed a two-link leg solve with the knee directed toward its authored bend plane. Each foot stays level, travels to its next support point during swing and holds that point in world coordinates during stance. The solver shortens the final steps as root speed falls.

The Kimodo waypoint walk supplies asymmetric contact durations and separate left/right ankle clearance curves. A new Kimodo G1 inbetween supplies the pelvis weight-transfer curve. The MotionBricks G1 transition supplies pelvis descent and shoulder timing for the brace. Both channels are reversed and stretched for the authored recovery. The renderer follows both actors with the same orthographic camera and uses the published colour and UV data. [Motion sources](../methods.md)

The extra ground-contact measure detects contacts independently in each version. It counts consecutive sample pairs while root speed exceeds 0.2 m/s and all bind-sole vertices are within 2.5 cm of the floor, then measures RMS horizontal sole speed. It includes low-clearance swing samples close to touchdown. [Measurements](metrics.json)

## Rebuild

Build the normal `mecha-render-probe` CMake target with Ninja so that the published asset pack, C++ libraries and build flags are available. The sampler links to these libraries and calls `poseEnemy` directly. The following command uses EGL for offscreen OpenGL and requires FFmpeg with libx264.

```sh
uv run --no-project --with numpy --with scipy --with Pillow --with moderngl --with glcontext python art/mecha/motion-evaluation/mecha/study.py
```

Set `--build` to use another build directory or `--output` to write the GLB, preview and measurements elsewhere.
