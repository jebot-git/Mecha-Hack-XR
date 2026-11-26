# Mecha and kaiju motion evaluation

| Study | Watch the comparison | Inspect the animation |
| --- | --- | --- |
| Mecha Ace walks, brakes and braces | [Side-by-side MP4](mecha/mecha-ace.mp4) | [Animated GLB](mecha/mecha-ace.glb) |
| Voidmaw Stalker advances, coils, bites and recovers | [Side-by-side MP4](kaiju/stalker-comparison.mp4) | [Blender scene and actions](kaiju/stalker-rig.blend) |

Watch the feet against the floor as the body passes over them. On the Ace, watch the forearms and pelvis as the step gives way to the brace. On the Stalker, watch the jaw and tail during recovery while the claws support the body. The left actor in each comparison uses the game's compiled pose solver. The right actor uses the prototype. Each pair shares a mesh, root path and clock.

Kimodo generated the Ace’s sideways pelvis motion between three keyed poses. MotionBricks generated the timing of a two-legged brace. The Ace uses the brace for pelvis compression and shoulder movement. The Stalker transfers its timing to the coil before the bite, with a separate four-legged contact solve. Both models ran offline on CPU. [Generation recipes and sources](methods.md)

## Interpreting the measurements

Both studies measure world-space foot motion during the prototype's scheduled support intervals. For the Ace the measure is the RMS horizontal speed of the sole in metres per second. For the Stalker it is the distance a sole travels across an entire hold in metres. Compare the two versions of each actor within its own report. The live solver does not publish per-foot contact intervals, so walking phase differences contribute to the baseline values. [Ace measurements](mecha/metrics.json) · [Stalker measurements](kaiju/metrics.json)

Using each gait’s own near-floor frames during walking, Ace horizontal sole speed fell from 2.51 to 0.013 m/s RMS. Mean Stalker claw travel over a scheduled hold fell from 0.90 m to below 0.00001 m.

The clips exercise level terrain with a prescribed trajectory. Slopes, direction changes under player input, interrupted transitions and multiplayer pose reconstruction would need an engine trial. The assets and generation scripts in this directory are for offline evaluation. [Motion sources and reproduction](methods.md)

The [full-roster retraining and bake workflow](../../../tools/mecha_motion/README.md) applies the accepted motion approach to the published actor rigs.
