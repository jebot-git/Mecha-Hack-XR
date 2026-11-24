# Scaled encounters, patrols and enterable sites

This pass builds on upstream `98dd7e0`, including its 66-enemy roster, Mission
Manual content, squad decisions and bounded navigation. Its unchanged baseline
passed all 46 Release suites before this implementation.

## Offered missions and difficulty

Automatic encounter size is the default. Preparation shows **AUTO / PARTY
SCALED**; an explicit count from 1 to 48 remains available. Selecting a specific
enemy retains the isolated development/test deployment path.

The nine offers contain three repeating threat tiers: Patrol, Frontline and
Dangerous. Briefings and cards identify the tier, contact count and party size.
The effective level is the connected party's mean level rounded upward, bounded
to 1–10. One to four pilots contribute to the following target HD budget:

```
round((8 + 3 * level) * (1 + 0.75 * (partySize - 1)) * (0.8 + 0.2 * tier))
```

The roster is selected from contextual inhabitants with HD no higher than party
level + 1. When a theme has no suitable low-tier inhabitant, a lower-tier
mechanical/biological proxy is used. Automatic count divides the target budget
by the eligible pool's mean HD and is bounded to 2–48 actors. This is a target
budget, not an exact sum: distinct stat blocks are dealt before reuse, retaining
their actual damage, HP and capabilities. Explicit counts override density.

In the fixed militant test sample, solo offers average 7.67 contacts at level 1
and 10 at level 10; a level-10 four-pilot party averages 33.33 contacts. These
examples vary by thematic pool. Larger automatic contracts receive additional
deadline allowance when contacts per pilot exceed six. Credit rewards still
belong to mission completion and account for the actual roster's HD.

The host fixes the recipe at deployment. Offers refresh for the current roster
and progression; joining/leaving does not rescale enemies already in combat.
Connected maps retain the contract's difficulty column and roll new sites.

## Natural groups and patrols

Ground detachments contain up to three compatible actors; air wings contain up
to four. Biological packs, faction-compatible mechanical units, installations
and submersibles retain separate group constraints. Installations hold their
posts, while support traits continue to use the upstream tactical behavior.

Groups receive terrain-valid anchors rather than the old isolated spawn spiral.
They form patrols, hunting packs, air wings, garrisons, ambushes or assault forces.
Objective assets receive nearby defenders; capture leaders retain their role;
invaders start behind the approach line. Submersibles select water habitats.
At least the first detachment remains available for exterior activity instead
of assigning every compatible actor to a building.

Patrols walk connected navigation edges in repeatable outward/return loops.
Followers keep spacing; garrisons retain individual posts. Detection interrupts
patrolling with the existing physical line-of-sight and doctrine decisions.
Groups can investigate an eligible ally's last observed contact. Concealment,
death and ejection revoke that contact; hidden current player coordinates are
not a pursuit destination. Groups resume their routes when contact expires.

Patrol edges are checked against every squad member after site/objective
placement; indoor formations use the shared corridor. Ambush traps follow the
new actor anchors.

Navigation keeps the existing four rotating planning grants per simulation
tick. Runtime reinforcements are capped at 64 actors and the scene at 1,024
targets. Reused slots lose their former group/memory; children join compatible
nearby groups. Insertion repairs mission assets and dungeon-core indices.
Projectile and effect caps remain 256 each.

## HEXROLL 2E adaptation

The native, offline generation layer follows HEXROLL 2E's documented dependency
order: physical terrain and sites first, then inhabitants and keyed activities.
Biome, usable parcels and existing facilities determine placement. A small room
graph uses a connected spanning tree with optional loops; dead ends and extra
links vary from seed to seed. This uses original C++ generation rather than a
HEXROLL service or SCROLL interpreter.

Surface maps request up to two optional facilities. A usable parcel receives a
coherent foundation, a roof, four, five or six rooms, two exterior doors and a route
into the existing road graph. Shells use the current authored hangar, tunnel or
bulkhead pieces and shared textures. Existing hull/tunnel maps key their actual
compartments. Wet/obstructed parcels are rejected; actual site count can be lower
than requested. Normal new doorways are 40 m wide with 32 m vertical clearance;
rooms have 40 m headroom. All dimensions are world metres for the existing
12 m cockpit eye height.

Room keys distinguish entry, guards, cache and power-core areas. Suitable groups
occupy separate rooms; larger actors remain outside. The core is a physical
destructible object. Clearing its assigned guards and destroying the core secures
the site. Visiting the cache then releases one repair/boost consumable crate,
using the existing bounded pickup system. Release, collection and room visits
are authoritative and replicated, with no repeat award on re-entry. The cockpit
overview shows local site status. Main mission beacons are kept out of the site
and its approaches so they cannot obstruct a doorway.

This remains cockpit-scale FPS exploration. The adaptation does not add an
on-foot mode, a separate world hex UI, narrative NPC/rumor simulation or a full
fantasy rules conversion. Physical aiming, movement and armor continue to
resolve combat; a new random to-hit roll is not added.

## Verification and limits

```sh
cmake --build build-mecha -j4
ctest --test-dir build-mecha --output-on-failure
./build-mecha/mecha-encounters-tests artifacts/validation/encounters-after.json
cmake --build build-mecha --target mecha-render-probe -j4
SDL_VIDEODRIVER=wayland ./build-mecha/mecha-render-probe --encounters \
  --out artifacts/validation/encounter-gallery
```

The focused audit covers level/party scaling, reproducibility, count limits,
eight terrain/location families, all ten objectives, compatible membership,
collision-free deployment, graph attachment, actual pilot doorway traversal,
headroom, snapshot reconstruction, unalerted patrol movement and single-award
salvage. Combat probes run 12, 24 and 48 initial actors for ten simulated seconds
each, including normal reinforcement opportunities. The final report records
CPU distribution and uncompressed snapshot sizes. These CPU timings exclude
rendering and are desktop measurements, not Quest results.

Final Release validation passes all **47 suites** and **12 focused encounter
checks**. The stationary four-pilot combat sample measured:

| Initial / final enemies | Mean tick | P95 tick | Worst tick | Raw snapshot |
|---|---:|---:|---:|---:|
| 12 / 15 | 0.090 ms | 0.172 ms | 0.269 ms | 78,149 bytes |
| 24 / 26 | 0.127 ms | 0.268 ms | 0.455 ms | 85,942 bytes |
| 48 / 50 | 0.258 ms | 0.410 ms | 0.679 ms | 98,292 bytes |

The harness refreshes disposable pilots so defeat does not end the measurement.
It includes AI, navigation and projectile simulation, and records snapshot size
before transport framing. It is a bounded smoke sample, not a frame-rate or
network-capacity guarantee.

Native renderer QA captures entrance, interior, cutaway and overview views for
urban, desert and hull settings: twelve captures and twenty-four eye draws.
The cutaway disables roofs only in its copied presentation world. Current
headset interaction, long-session balance and four-headset networking require
separate playtesting.

Network schema **13** carries party difficulty, groups, patrols and sites.
Profile schema **5** is unchanged. Install matching clients and server.

## References

* Local *The Mecha Hack Mission Manual v1.2*, printed p. 7 (adapting enemies to
  the fireteam) and p. 103 (mission complications/opportunities). The existing
  native Manual implementation supplies mission themes and equipment rules.
* [HEXROLL 2E generator guide](https://docs.hexroll.app/building-a-sandbox/generating-a-realm/):
  layered terrain, dungeon, dwelling and faction generation with contextual HD
  limits. [System compatibility notes](https://docs.hexroll.app/getting-started/faq/).
* [Micro RPG catalog](https://github.com/brunobord/micro-rpg-catalog) and its
  [small-dungeon example](https://github.com/brunobord/micro-rpg-catalog/blob/master/1d6-osr-ultra-lite-and-small-dungeon.md):
  compact encounter progression and physical-hit/armor-friendly interpretation.
  Referenced rule text and illustrations are not bundled into the client.

Current maps enforce decoration and large-structure floors, and may key a
below-grade facility alongside a surface building. See [density and underground sites](MECHA_DENSITY_AND_UNDERGROUND.md).
