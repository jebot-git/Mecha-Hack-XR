# Player interface

The interface separates the hangar, nine procedural missions, equipment, trade,
pilot profiles and options. The menus retain the native bitmap/OpenGL presentation
and VR-only input. Desktop mode remains a passive preview.

## Controls

Use the left stick to move between rows and change values. A or the right
trigger confirms. B or the left trigger goes back. On equipment pages, A
confirms the selected mount and B goes back, while the triggers serve as arm
mount shortcuts. Hold either grip with the mount button to remove equipment.
Press Menu to pause in a mission. Left grip + Y and both grips + left
trigger also open pause. Recenter from the pause menu. Tap the right stick down
to toggle the headlight in any environment. Tap the right stick up to switch
equipped rifles between automatic fire and lower-heat precision semi-auto.
A held firing input must be released when resuming from pause.

Click and tilt the left stick to boost forward, backward or sideways. The
cockpit meter shows one to four charges. Mobility governs capacity, recharge
rate, boost thrust and walking speed. Right stick click spends a charge to
ascend. A mech that boosts off a ledge or loses a supporting roof falls until
its feet reach another surface. A landing sends a short shockwave across that
surface. Water slows the descent, while a mech in zero G stays aloft. Grounded
footfalls can destroy small scenery and crush light foes. Enemy mecha use boosts and can crush small scenery, and
aware enemies evade an approaching footfall. Equip a scanner and activate its
mount button to scan. Radar contacts fade over ten seconds with Sensor Scan or
twelve with Long-Range Scanners. Distant contacts show bearings and sound one
cockpit sonar ping. Sensor Scan exposes the pilot at headlight detection
range for the duration of the scan. Long-Range Scanners scan covertly. Damaged
sensors disable scanning. Radar contacts preserve visual camouflage.

Polar snowstorms can mark a patch of ground for a hail strike. The warning
gives the pilot 2.5 seconds to clear its 12-metre radius or take overhead
cover. On exposed terrain, roadways and decks, snow, sand, ash and spore
residue build up over 30 seconds. Rain wets the surface and fire storms
scorch it. When a front clears, its surface effects recede over 30 seconds.

Swing a tracked blade across a telegraphed close-range strike to parry it.
Successful parries stagger the attacker and open a riposte. Chain hits with
alternating hands or reversed swing directions for up to three strikes.
A scan, PRECOG mark or exposed armour shows visible enemy weak points.
Optics, rear reactor, arms and drive hits deal critical damage and can
disable the corresponding subsystem.

The centre computer shows the cockpit bearing on a 360° compass. The right
computer shows reactor heat and venting status. The bird’s-eye display shows
the mecha schematic and target count.

Each chassis displays hull, armour and shield on its own condition gauge.
Orange shows hull integrity as a percentage, blue shows current and maximum armour
points, and green shows the remaining energy shield charge. Compact instruments
label these channels HP, AP and SH.

The overhead red lamp brightens as the reactor heats up. Steam and a warning tone
build in the cockpit, with a faster flash and sustained venting at overheat.

Weapons, boosts and modules add heat. Venting temporarily disables weapons
and thrust with reduced walking speed. The same heat threshold applies to
enemies, which can withdraw while venting. Armour absorbs damage but adds mass.
Scout frames move and cool faster. Coolant and modules provide other ways to
manage heat.

Selecting a mount row equips the chosen item. Selecting an already mounted
item preserves its upgrade. Weapons occupy arms; modules and supplies occupy
shoulders/hips. Heavy weapons retain both-arm support.

The pause menu provides Resume, field equipment, Options, cockpit recenter and
Exit. Options contains sound/music volume and, online, microphone control.
Volume changes are saved to the game's data directory in `audio.json`. Pilot
roles provide passive bonuses. Creation shows the gameplay effects of the
selected role.

## Preparation

Choose a chassis, one of four illustrated pilot roles, and a callsign, then select
Create Pilot. Each new mech starts with its chassis weapons, scanner and one repair
nanite charge fitted to the left hip. Four profiles retain separate equipment and progression.

Equipment fits owned gear and armor, assigns the six mounts, and installs
modules bought with contract credits. Inventory browses stock and identifies mounted gear and fitted
armor. The Trade Post buys, sells and crafts stock. Buying armor adds it to
Inventory, and Equipment fits it. Existing weapon qualities are retained.

Select a mission and open the Sortie Room from its briefing or the hangar. The
host opens the room. Every pilot enters and readies up separately. The room
shows who is in the hangar, present, or ready. Once everyone is ready, the host
deploys from the mission briefing. Unreadying returns the party to the room.

## Procedural campaign

One campaign seed supplies nine offers. The generator chooses objectives,
environment, insertion, enemy roster/tactics, encounter budget and one to three
connected sectors, adapting enemy strength to the mission tier, faction pressure and party size. There are
no player controls for enemies, counts, tactics, factions, maps or other generator
overrides. Briefings show operational information, not unused narrative-table
hooks or source-rule terminology.

The host has exactly one generation action: **New Campaign / Regenerate All 9**,
available before the first victory. Once progression begins, missions update
automatically after the whole party accepts debrief. Completed slots remain
archived; all remaining slots receive new seeds and increased party-scaled
resistance, shaped by the selected contracts, faction standings, intelligence,
supplies and structural losses. There are no per-offer rerolls or seed fields.

Nine victories unlock an additional final operation with a faction boss or
decisive battle. Missions pay the quoted credits and change faction standings.
Depots offer optional salvage. After the finale, each pilot chooses a chassis
with left or right and confirms the award to receive its starter equipment.
Callsign, role, mission history, credits, scrap, faction standing and honours
carry forward. The whole party must accept before returning to the hangar.
In the pilot roster, press right on a slot and confirm to start that pilot over.

Campaign history and pending debriefs are saved with each pilot. A host restores
its chosen profile's campaign; guests join that shared progression. Failed
missions and canceled briefings preserve the existing offers and standings.
Connected sectors still move the party together, grant a short rest, and count
as only one contract. See [adaptive campaigns](MECHA_CAMPAIGN.md) for the full
consequence rules, six boss designs, save behavior and reset policy.

The network and interactive local client use `Session::playerCommand`, which
rejects generator settings, free quality edits and retired manual-rest/next-mission
commands. Seed regeneration accepts no override fields. Direct `Session::command`
settings remain available to deterministic developer fixtures. Client generation
flags are restricted to offline desktop captures with a frame limit; they cannot
be used for an XR, hosted or joined game.

## Validation and compatibility

This build uses network protocol 38. For multiplayer, use matching client/server
binaries. Profile schema 9 migrates earlier saves to fixed chassis stats while retaining stock, modules, and campaign history. Completion flags are replicated and
checked for valid nine-bit state. The tests cover navigation edges, diagonal
input, focus loss, host/guest action sets, bounded volume controls, creation,
owned inventory, saved qualities, automatic mission lengths, stable campaign
completion and real UDP rejection of host override requests.

Native review:

```
cmake --build build-mecha --target mecha-render-probe -j4
SDL_VIDEODRIVER=wayland build-mecha/mecha-render-probe --menus --out artifacts/validation/menus/gallery
```

The gallery verifies OpenGL status,
unchanged animation time between eyes and visible menu highlights. It includes
host/guest hangars, online/offline options, profiles, creation, equipment pages,
trade, adaptive missions, final selection, victory/award/reset, briefing and pause.
It also renders all six boss rigs in normal and charging appearance. Headset input testing
remains separate from these native captures and controller-state regressions.
