# Nine-mission campaigns and connected maps

A campaign supplies nine procedural offers from one seed. The host may regenerate
all nine before progression begins. After each party debrief, completed slots
remain archived while the generator adapts the remaining offers to the campaign
record and host faction standings. Nine victories unlock a separate finale.
Winning grants a campaign award and issues starter equipment for each pilot’s
chosen chassis. Credits, scrap and personal standings carry into the next campaign.
See [adaptive campaigns](MECHA_CAMPAIGN.md).

Objectives, locations and tactical doctrines use independently shuffled decks.
Initial opposition draws from varied themes; later offers use the faction
relationships produced by the party's prior contract choices.
Climate choices depend on the site, and arrival
choices respect enclosed, underwater and vacuum settings. Enemy groups draw
without replacement before reusing a type, filter for party level, and assign
individual tactical roles. The first contact retains the advertised doctrine.
All ten objectives, ten locations, ten themes, six arrivals, eight environments,
ten doctrines and 34 enemy definitions remain available.

## Contract pacing

Regular contracts span one to three sectors. A short deadline, a defense line,
a convoy or a hold objective concentrates play in sectors 1.00–1.30 km across.
Each such sector can contain one optional dungeon and one salvage depot.
Open operations use 1.45–2.00 km sectors and request two or three of each,
subject to route and clearance checks. Terrain speed and return travel influence
the primary route and sector count. Exploring optional sites can extend a
contract beyond the 5–15 minute direct-route target. Finales use one compact sector.

## Physical map building

Generation assembles terrain and site connections before assigning mission
assets. Recon, recovery, resource and reinforcement goals use distinct existing
navigation sites where possible, then resolve a clear physical position. Recovery
markers use the real insertion site. Convoys and grounded enemies follow the
same passage graph as players, including exterior districts.

Exterior maps choose among five connected layout families: rings, spokes,
branching spines, courtyards and organic networks. Four deterministic candidates
are scored for connectivity, coverage, loops and usable parcel sizes. Six seeded
height profiles provide terraces, ridges, calderas, ravines, mesas and coastal
shelves, requesting 18–60 metres of relief before traversal grading. A final
heightfield grading pass limits steep edges around roads and facility approaches;
measured final relief can differ from the requested profile.

Facilities vary their main halls, side rooms and open concourses. Underground,
submerged and surface facilities use fitted floors, graded approaches and portals
with full mecha clearance. Partial sections coexist with outdoor terrain. Designated
submerged routes permit large mechas to traverse deeper water; ordinary coastlines
retain their existing depth restrictions.

Dressing mixes small props, mecha-sized cover and oversized landmarks. Themed
clusters are distributed across the map rather than exhausted in the first plots.
Actual mesh and collision bounds scale together; oversized buildings and wrecks
require larger parcels. Placement protects routes, portals and overhead clearance,
and omits decorative instances without a supported foundation.

The movement engine uses a heightfield: these are connected raised districts,
not overlapping floors at identical horizontal coordinates. Actual terrain and
collision queries support the tiers. Mission, enemy and scenery generation share
the resulting heights and navigation anchors. Scenery stays within the existing
1,152-target construction budget and preserves enemy-prefix and deployable-slot ordering.

## HEXROLL 2E references

Research used the official documentation and public SCROLL model. The C++
implementation applies the documented generation ideas to mecha combat:

- [Generating a Realm](https://docs.hexroll.app/building-a-sandbox/generating-a-realm/):
  build geography and sites first; choose inhabitants within encounter bounds;
  let later activities refer to those sites.
- [SCROLL tutorial, part II](https://docs.hexroll.app/scroll/scroll-tutorial-part-2/):
  inherit shared context, reuse generated collections, and select distinct entities
  when repeated choices would weaken variety.
- [Sandbox graph behaviour](https://docs.hexroll.app/hexroll-internals/graph-behavior/):
  keep objective destinations consistent with the connected location model.
- [Official source model](https://github.com/hexroll/hexroll-osr-scrolls), reviewed
  at commit `8aac8babcc7507278667e3b3b75d171ff82774a2`: `realms/realm.scroll`,
  `dungeons/dungeon.scroll`, and `hooks/quests/escort.scroll` illustrate staged
  sites, entrance/passage relations and destination-linked activities.

The implementation is original C++ and uses the existing Mecha Hack catalog.
No HEXROLL runtime, tables, fantasy text or generated content is packaged.

## Verification

`mecha-mission-board` checks deterministic refreshes, coverage and compatible
encounters. `mecha-contract-flow` checks host authority, readiness reset, board
replication, matching deployment, fresh operations and convoy navigation.
`mecha-preparation` checks owned equipment, earned module choices and actionable
menu rows. `mecha-locations` checks graph connectivity and actual player movement
up graded floors; `mecha-clearance` samples the real model envelopes along routes.
Run these through CTest without launching a game window.

Network schema is 19; host, client and dedicated server must use matching builds.
Profile schema 6 also stores adaptive campaign checkpoints; see MECHA_CAMPAIGN.md.

The bounded generation audit exercises 24 seeded worlds across all layout,
height, section, location and biome families. It measures actual route clearance,
walkable grades, active asset size bands and generation cost. This is deterministic
candidate scoring and regression-driven calibration, not neural-network training.
Run `build-mecha/mecha-generation-audit --report /tmp/generation.json` to inspect
its complete results.

The generated-map AI soak retains each map's terrain, collision, navigation and
enemy doctrines. It places a stationary test pilot at a verified clear, visible,
walkable point inside an existing enemy weapon's range; the original deployment
can legitimately remain outside every firing envelope for a ten-second sample.
The report records the selected enemy, weapon range, approach samples and original
deployment, and still requires observed attacks, moving enemies, a full simulation
window and bounded runtime pools. The separate 340-case enemy/doctrine matrix is
unchanged by this fixture placement.

Validation on 2026-11-19: the 24-world audit passes 73,145 physical route samples
and actual graded pilot walks, with 24 distinct navigation geometries and 75
active asset variants. It includes 315 small, 601 mecha-scale and four oversized
scenery instances, plus 15 facilities, seven underground and three submerged
worlds. Terrain ranges from 5,520 to 16,358 triangles per world. Results are saved
in `art/mecha/generation-validation.json`; build times depend on machine load.

The current generation build passes all 48 CTest suites with the Release client,
server and developer tools built.

Generation version 4 adds natural road ribbons, shaped shores, coherent districts
and varied dungeon footprints. See [Procgen Arcana adaptations](MECHA_PROCGEN_ARCANA.md)
for current behavior and validation. The numerical measurements above describe
the earlier generation checkpoint.

Generation version 10 enforces 320 decorations and 48 large objects per map and
adds below-grade dungeon sites. See [density and underground sites](MECHA_DENSITY_AND_UNDERGROUND.md).
