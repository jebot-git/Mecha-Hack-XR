# Mecha Hack XR campaign score

The game uses the seven originals from the Mecha Hack Suno playlist and eight
local arrangements. The [tracklist](SUNO-TRACKLIST.md) maps every cue to its
source. The game uses a short excerpt of the original victory track; the
listening album includes the full mix.

Run `python3 tools/mecha_assets/pack_suno_ost.py <source> <output>` to rebuild
the looping Opus banks and one-shots. The source directory contains the
15 WAVs named by cue, `selection.json` and `TRACKLIST.md`.

## Schism Tracker score

The melodies, arrangements, and tonal instruments were composed for Mecha
Hack XR. The score takes its direction from the mechanical pulse of Armored
Core and the compact tracker-era phrasing of Iron Soldier. All 15 pieces have
editable Impulse Tracker modules in `art/mecha/ost/`. Schism Tracker renders
those modules for the tracker edition. Both editions use stereo Opus at 48 kHz.

The revised score uses a shared pilot melody for the title, hangar, finale,
and victory. Each faction carries a short melodic cell from its briefing into
combat. The combat cue adds a distinct rhythm section and a quieter middle
passage before the full arrangement returns. The boss cue alternates a low
guitar figure with higher synth responses. Victory and defeat resolve on a
held chord, while mission and menu cues return to their opening pulse.

The Armored Core OSTs channel supplied soundtrack references for this revision,
including the early series anniversary disc and the recurring “Artificial Sky”
cues in Armored Core 3. The cues informed the use of recurring themes and
contrasting menu and combat arrangements.
[Anniversary Box, Disc 01](https://www.youtube.com/watch?v=Ojtl4UdCDYM) ·
[Artificial Sky I](https://www.youtube.com/watch?v=-huNmW3uY-E) ·
[Artificial Sky III](https://www.youtube.com/watch?v=A9x8FSVbMjY)

The sampled kit is [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums),
performed by Austin McMahon and recorded by Versilian Studios and Karoryfer
Samples. The six kit hits come from [ferrosintesis-samples-drumkit 0.2.0](https://docs.rs/crate/ferrosintesis-samples-drumkit/0.2.0),
a CC0 distribution pinned to source revision
`9f04cf9a734527edfbb0a4eee1f674e45bbf71bc`. They are resampled to 22,050 Hz
mono for the modules.

| Filename | Track | In-game use |
| --- | --- | --- |
| main | The Iron Between Stars | Title and profile selection |
| intermission | Standby at Lodestar | Hangar, equipment and pause |
| aeonic | Prism Protocol | Aeonic Primacy combat |
| terran | Mass Driver | Terran Collective combat |
| neo | Oath of the Red Crown | Neo Dominion combat |
| voidmaw | Maw Below the Signal | Voidmaw combat |
| aeonic-brief | Cold Relay | Aeonic briefing |
| terran-brief | Factory at Dawn | Terran briefing |
| neo-brief | The Unlit Banner | Neo briefing |
| voidmaw-brief | A Mouth in the Dark | Voidmaw briefing |
| boss | Break the War Machine | Boss encounters |
| finale | All Fronts Open | Decisive battle finale |
| victory | Signal: Fireteam Alive | Mission victory |
| clear | One More Sunrise | Campaign victory |
| defeat | Cold Cockpit | Defeat |

Rebuild with `python3 tools/mecha_assets/compose_ost.py --listen /tmp/mecha-ost-album`.
This requires NumPy, Schism Tracker, and FFmpeg with libopus. Pass
`--modules-only` to regenerate just the editable modules.
