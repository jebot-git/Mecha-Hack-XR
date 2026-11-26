# Voidmaw Stalker motion study

The Stalker walks 8.3 metres, plants four claws, coils, bites and settles. The comparison uses the published 748-triangle creature on both sides, with the same clock, root path, ground and orthographic tracking camera. [Video](stalker-comparison.mp4) · [Bite frame](stalker-review.jpg) · [Editable Blender scene](stalker-rig.blend)

The live side calls the game's C++ pose function using distance-driven gait phase, speed and timed melee cues. The study gives each of the four feet a series of world-space holds and toe-clearance arcs. Two-link inverse kinematics keeps each knee on its outward bend plane and preserves segment lengths. The root sways toward the supporting claws. A delay down the tail carries the torso's motion through the last segment.

During preparation the pelvis, thorax, jaw and tail follow the normalized knee-flexion timing sampled from the generated G1 MotionBricks brace. The creature has its own amplitudes, forward snap and recovery. Kimodo's sparse root and end-effector control approach informs the authored path and contact targets. All 19 native joints retain their existing rigid mesh assignments. Edit the contact intervals and joint responses in the solver to rebake the sequence. The Blender scene contains both baked armatures, timeline markers and four animated contact guides. [Generated timing](../reference/brace.json) · [Solver](solve.py)

### Measurements

The comparison tracks a lowest-sole vertex of each claw over the **same authored hold windows** on both performances. Mean accumulated horizontal travel per hold was 0.903 m live and below 0.00001 m in the study. The worst hold measured 3.055 m live. At least two claws support the study throughout the stride. Both performances kept the sampled toe markers above the flat floor. These measurements cover the authored forward route on level ground. [Recorded metrics](metrics.json)

### Rebuild

Run from the repository root with the published runtime assets and Linux CMake build available. The renderer uses Blender Workbench. Video assembly uses Pillow with FreeType and FFmpeg. Supply an installed TrueType font through the optional `--font` argument.

```sh
study=art/mecha/motion-evaluation/kaiju
work=$(mktemp -d)
c++ -std=c++20 -O2 -Iinclude -I.deps/usr/include "$study/sample_live.cpp" \
  -o "$work/sample-live" -Lbuild-mecha -lmecha-rpg-core -liron-net -liron-core \
  -L.deps/usr/lib/x86_64-linux-gnu -lenet
blender -b --factory-startup --python-exit-code 1 -P "$study/solve.py" -- "$work"
blender -b "$study/stalker-rig.blend" -o "$work/frame" -a
python3 "$study/preview.py" "$work"
```
