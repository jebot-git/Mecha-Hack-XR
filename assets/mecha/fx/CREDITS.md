# Weapon effect atlas

The native atlas contains resized, white-tinted alpha masks. It is a 512 × 256
RGBA8888 image with eight columns of 64-pixel cells and transparent gutters.

| Cells | Source | Creator | License |
|---|---|---|---|
| 0–23 | [Plasma Electric Effect Animations](https://opengameart.org/content/plasma-electric-effect-animations), Electrical Zap, Loop Electrical Charging, Gravity Burst | Reactorcore | [CC0](https://creativecommons.org/publicdomain/zero/1.0/) |
| 24–26 | [Lens Flares and Particles](https://opengameart.org/content/lens-flares-and-particles), nova, corona and sparkle from `flares_20101219.zip` | hc / hackcraft.de | CC0 per the selected archive's LICENSE |
| 27–31 | [Masks for Particles](https://opengameart.org/content/masks-for-particles), masks 22, 24, 38, 28 and 1 | jellyfish0 | CC0 |

Repack from the source archives with `tools/mecha_assets/pack_weapon_fx.py`.

`fire-flipbook.rgba` is an original 256 × 256 RGBA8888 atlas with four frames
each for ballistic flashes, energy flashes, fireballs and smoke. Rebuild it
with `python3 tools/mecha_assets/build_fire_flipbook.py assets/mecha/fx/fire-flipbook.rgba`.

The WAV files directly under `audio/` are 32 kHz, 16-bit mono mixes built from licensed
recordings, with additional sub-bass, resonance and energy layers. Enemy
movement shares the source palette for servo, tread, turbine, submersible,
idle and creature loops, plus footfalls, thruster bursts and alert calls.

| Source clips | Source | Creator | License |
|---|---|---|---|
| `report-rifle`, `report-rifle-alt`, `report-heavy` | [Gunshot Sounds](https://opengameart.org/content/gunshot-sounds), trimmed from `sks.wav` at 0.341 s and 2.284 s and `mosin.wav` at 0.432 s | Vincent Sevedge (2009), uploaded by Tabasco | [CC BY 3.0, per the notice in the original archive](https://creativecommons.org/licenses/by/3.0/) |
| `servo-drive` | [Quick Drill Fix](https://opengameart.org/content/quick-drill-fix), `roboticsqueeze.flac` | qubodup | CC0 |
| `air-release` | [Air Hiss](https://freesound.org/people/Jofae/sounds/367125/), HQ preview | Jofae | CC0 |
| `clank1`, `lightclunk1`, `mechanical1`, `mechanical2`, `rattle1` | [Mechanical Sounds](https://opengameart.org/content/mechanical-sounds) | Brian MacIntosh | CC0 |
| `thud2`, `thud3` | [Metal Impact Sounds](https://opengameart.org/content/metal-impact-sounds) | Brian MacIntosh | CC0 |

The authoring clips are converted to 32 kHz mono under
`art/mecha/combat-foley/`. Rebuild the runtime mixes with
`tools/mecha_assets/build_combat_audio.py assets/mecha/fx/audio`.

## Mounted weapon recordings

The 45 files under `audio/weapons/` are the approved dry recording mixes, in 48 kHz, 16-bit mono PCM. Each of the five families has one ready cue, one looped idle, one charge, three firing reports and three mechanical returns. These masters use edited, filtered and layered CC0 field recordings. The following list credits the audio contained in the distributed WAVs. [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

| Layer | Recordists and original recordings |
|---|---|
| Automatic and heavy reports | TheGreenEyedGhost’s [range session](https://freesound.org/people/TheGreenEyedGhost/sounds/707777/) and t-man95’s [festival cannon](https://freesound.org/people/t-man95/sounds/553240/) |
| Launch and mechanical power | Dr. Macak’s [balloon burner](https://freesound.org/people/Dr.%20Macak/sounds/491300/), qubodup’s [firework launch](https://freesound.org/people/qubodup/sounds/840511/) and ldezem’s [impact wrench](https://freesound.org/people/ldezem/sounds/386315/) |
| Blade and metal accents | Jan Schupke (Vehicle), [Tinysized SFX](https://opengameart.org/content/fantasy-sound-effects-tinysized-sfx), plastic-tube and tin-whistle passes, seax and knife draws, and hammer strikes. The blade motor also uses qubodup’s [Quick Drill Fix](https://opengameart.org/content/quick-drill-fix). |
| Emitter | Joseph Sardin, [Short welds](https://bigsoundbank.com/short-welds-s0952.html), [metal barrier](https://bigsoundbank.com/grinding-metal-barrier-closure-2-s2359.html) and [automatic hatch](https://bigsoundbank.com/automatic-hatch-closing-s1804.html) |
| Running and charging machinery | Joseph Sardin, [sewing carriage](https://bigsoundbank.com/sewing-machine-fast-speed-s1116.html), [compressor](https://bigsoundbank.com/air-compressor-s1487.html), [screwdriver](https://bigsoundbank.com/electric-screwdriver-1-s1572.html), [strap tensioner](https://bigsoundbank.com/ratchet-tensioner-s1824.html), [refrigeration motor](https://bigsoundbank.com/refrigeration-unit-s3597.html), [computer fan](https://bigsoundbank.com/computer-2-ventilation-s0126.html) and [hand grinder](https://bigsoundbank.com/coffee-grinder-3-s2539.html). LePapy recorded the [slide projector](https://bigsoundbank.com/slide-projector-s0867.html). |
| Contacts and pressure release | Brian MacIntosh’s [clank](https://opengameart.org/content/mechanical-sounds) and [thud](https://opengameart.org/content/metal-impact-sounds), and Jofae’s [air hiss](https://freesound.org/people/Jofae/sounds/367125/) |

The source logs give the original excerpt times and recordings used for each take. [Reports and blade](../../../art/mecha/weapon-audio-prototype/recordings/reports/SOURCES.md) · [Mechanisms](../../../art/mecha/weapon-audio-prototype/recordings/mechanisms/SOURCES.md) · [Emitter](../../../art/mecha/weapon-audio-prototype/recordings/acoustics/README.md)

## NPC comms and kaiju vocals

Each faction has four original calls per situation for six situations. The
scripts distinguish allies greeting or supporting each other from enemy crews
spotting a contact, committing to an attack or calling for help. The game
plays 24 kHz mono PCM recorded offline. [Dialogue scripts](../../../tools/mecha_assets/npc_dialogue.json)

- **Aeonic Primacy and Neo Dominion.** Qwen3-TTS 1.7B VoiceDesign (Apache 2.0)
  supplied designed voices for mainland Mandarin and Québec French. The
  renders use the Q8_0 talker, F16 12 Hz codec and CrispASR 0.8.55.
  [Qwen model card](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-VoiceDesign) · [Talker conversion](https://huggingface.co/cstr/qwen3-tts-1.7b-voicedesign-GGUF/tree/e3646df398d00b77f0e907a19a44f1a3723af14d) · [Codec conversion](https://huggingface.co/cstr/qwen3-tts-tokenizer-12hz-GGUF/tree/344e279551177654762e5c7d39e217834325f222) · [Rendering engine](https://github.com/CrispStrobe/CrispASR/releases/tag/v0.8.55)
- **Terran Collective.** Data Craftsman Australia's `en_AU-librivox-medium`
  (CC BY 4.0) supplies Willow (`jane_bennett`) and Bindi (`jenno`). The model was
  fine-tuned from `en_GB-jenny_dioco-medium`, derived from the Jenny TTS dataset
  by **Jenny (Dioco)**. LibriVox volunteers recorded the public-domain training
  audio. We synthesized original dialogue and adjusted pace, filtering and level.
  [Model and full attribution](https://huggingface.co/DataCraftsmanAustralia/piper-en_AU-librivox-medium/blob/7f35faf19fe1789ece4b14233448f7c04e35a84f/ATTRIBUTION.md) · [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) · [Jenny TTS](https://github.com/dioco-group/jenny-tts-dataset)

- The native kaiju bank combines animal calls, breathing, jaw Foley, flight feathers, shell fractures, gravel and water strokes. It supplies combat calls, locomotion and canopy contact across the roster. [Recording credits and licenses](audio/kaiju/CREDITS.md)
- The legacy fallback kaiju vocals use bowed double-bass from stomachache’s
  [stringharmonics.wav](https://freesound.org/people/stomachache/sounds/47429/)
  (CC0), trimmed and converted to mono FLAC in art/mecha/combat-foley/.
  Variable-speed takes, staggered room reflections, and the original game
  growl as breath foley create the three fallback calls. The recording and
  variable-speed treatment are inspired by Akira Ifukube’s Toho-era process.

Rebuild the radio bank with FFmpeg, NumPy, Piper TTS 1.8.0, ONNX Runtime
1.32.0, CrispASR 0.8.55 and the linked authoring models.

```sh
python tools/mecha_assets/build_npc_audio.py --crispasr /path/to/crispasr \
  --model /path/to/qwen3-tts-12hz-1.7b-voicedesign-q8_0.gguf \
  --codec /path/to/qwen3-tts-tokenizer-12hz.gguf \
  --australian-model /path/to/en_AU-librivox-medium.onnx
```

## Weapon room responses

The yard and hangar weapon tails use Conner’s *Angled Loading Dock* and *Firenze Parking Garage* impulse responses, respectively, copyright 2022 Conner. Each response was trimmed, damped, convolved with a dry report, and folded to mono for positional playback. [Original IR library](https://github.com/johnmatter/ir-library/tree/07cbb6f8779a4a448d02355c355f0cef8916b78d) · [Included MIT license](audio/weapons/returns/LICENSE-Conner.txt)

## Environmental recordings

The live client mixes nine location beds with weather and underwater recordings. Thunder uses three separate recordings on the lightning clock. The recording ledgers identify the field recordists, equipment and source licenses. [Location ambience](audio/ambience/CREDITS.md) · [Weather and thunder](audio/weather/CREDITS.md)

## Earlier neural sound pass

The earlier bank used mono 32 kHz PCM rendered from three offline models. Weapon
transients use a neural time-frequency foley mask. Machinery and NPC steps use
a neural spectral restorer. Its weather and underwater beds used an autoregressive
spectral model fitted to recorded ambience. The game still drives storm
thunder and hazard warnings on its existing schedule.

Weapon and machinery training use the licensed combat recordings listed above.
The environmental authors, recordings, and licenses are listed in
`tools/mecha_assets/neural_environment_sources.json`.

The weapon and machinery trainers regenerate the authored dry bank by default.
Pass `--baseline` to `train_weapon_sfx.py` or `train_machine_sfx.py` to
train against a specific version of the dry WAV bank.
`train_neural_environment.py` takes a source directory containing the source
manifest and its downloaded recordings. The authoring scripts emit
checkpoints, audio, and validation separately from the game’s runtime assets.
