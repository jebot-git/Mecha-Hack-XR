# Mounts, passive pilots, trade post and vehicle prototype

This build uses network schema 38 and profile schema 9. Use matching clients and
server. Earlier schema-2/3 pilot files retain their level, attributes, owned gear,
qualities and supplies. Their mounts are refitted to the new restrictions on load;
items that do not fit stay owned in inventory. Weapons take priority over shields
when moving an old body-mounted gun into the arms. Profile writes remain atomic.

## Mounts and sights

Weapons and shields fit only the two arms. A heavy weapon normally occupies both arms, with its own primary fire control. The Vanguard's utility limb lets the other hand carry a shield or independently fire a one-handed weapon. Active
modules, sensors and consumable bindings fit only shoulders and hips. Passive
installed upgrades remain internal. Shoulder Hardpoint is adapted to an arm
stabilizer: handheld angular spread is reduced by 15%, without bypassing the
normal offhand grip requirement. It does not allow a body-mounted gun.

L/R reticles appear only for an independently aimed equipped arm weapon and a
working tracked arm. A linked heavy offhand is a support hand. H appears only if
a shoulder/hip item requires spatial aiming. Self/area actions such as Nano
Shroud, Booster Sprint, Siege/Vehicle modes, scan and concealment show no reticle.
Grenades and aimed repair bindings require stock. One-shot modules use their
selected subtype's targeting behavior. Targeting damage, ejection and vehicle
mode suppress the relevant sights. The original IS game's default sights are
preserved outside this separate RPG game.

Tracked guns, reticles and launched shots share the same authoritative original
weapon rig and recoil pose. Firing an unsupported heavy weapon kicks the barrel upwards. Bracing reduces the kick and keeps follow-up shots on target.
See [weapon aim validation](MECHA_WEAPON_AIM_FIX.md).

## Passive pilot bonuses

Pilot roles affect tracked combat and mobility through the pilot’s attributes.

| Role | Bonuses | Gameplay |
| --- | --- | --- |
| Commander | Power +2, Presence +1 | Tighter weapon dispersion and shorter stuns |
| Engineer | System +2, Mobility +1 | Better subsystem checks and faster movement |
| Maverick | Mobility +2, Power +1 | Faster movement, quicker boost recharge and tighter weapon dispersion |
| Quipster | Presence +2, System +1 | Shorter stuns and sensor blindness |
| Bionic | System +2, Mobility +1 | Better subsystem checks and faster movement |
| Merc | Power +2, Presence +1 | Tighter weapon dispersion and shorter stuns |
| Vet | Power +1, Presence +1, Mobility or System +1 | Select the third attribute during preparation |
| Pariah | Presence +2, Power, Mobility or System +1 | Select the second attribute during preparation |

Chassis and equipment abilities use their own controls. The Mission Manual roles use only their attribute bonuses (pp. 74–75).

## Mission Manual chassis

The four additional frames follow the Mission Manual (pp. 76–79). A right-grip press operates the chassis power with the same input as the core frames. Weapon names, starter modules, hull values and heat use the game's existing equipment and realtime rules.

| Chassis | Hit / damage / reactor dice | Chassis power |
| --- | --- | --- |
| Auxiliary | d6 / d6 / d8 | Aim at a teammate within 30 m to reinforce their next weapon attack by d6 and transfer up to 30% reactor heat to the Auxiliary. With no teammate selected the Auxiliary can reinforce its own attack. |
| Colossus | d10 / d6 / d6 | Toggle garrison to gain d4 weapon damage and absorb d4 incoming damage at reduced walking speed. A maximum reactor roll discharges kinetic energy into a visible nearby hostile. |
| Hybrid | d6 / d10 / d6 | A head-aimed maw beam fires to 180 m. Overheating vents the reactor in a berserk reaction that damages the chassis, drives it away from a threat or emits a damaging screech. Heavy weapons are its only handheld armaments. |
| Vanguard | d6 / d8 / d8 | Switch a die step between reactor capacity and weapon damage. Its third limb braces one heavy weapon, leaving a tracked hand available for a second item. Bracing with all three limbs tightens the firing cone. |

## Trade post and contracts

The equipment menu's **Open Trade Post** entry is available between missions,
including before the first mission. Use the left stick to select rows
to select an item, buy, sell, choose armor, buy/equip armor or sell unused armor.
Transactions are authoritative in the hangar and require completed debriefs.
Mission, briefing and combat states reject trade requests.

Purchases use rulebook prices. Sale returns half the purchase price, rounded down; weapons
must be unequipped first, and equipped armor cannot be sold. A consumable binding
may remain at zero stock. One-shot purchases and sales respect the currently
selected module subtype. Module choices earned at levels 1/3/6/9 are not bought
or sold. Normal equipment selection can switch only to owned armor.

Credits are awarded once for the complete contract, not for kills or intermediate
maps. Payout is shown on the mission card/briefing and at debrief. Base amounts
by objective are 80, 110, 120, 70, 110, 100, 100, 90, 110 and 140 credits in the
objective catalogue order; add four credits per planned enemy hit die and 50 per additional
connected map. This is a prototype mission-reward policy. The initial contract's
payout is retained across all maps and is not rerolled at a transition. Defeat
awards no completion credits. Each completing party member receives that payout.

## Vehicle prototype

Fit **Vehicle Mode** to a shoulder or hip and press its mount button. Entry and
exit use a reversible 1.6-second rigid-part sequence. The torso folds at its lower
hinge, legs form grounded skids and arm panels fold with the hull. All four
chassis reuse their exact original topology and atlas: 238–280 triangles each,
within the existing 376-triangle limit, with no added runtime meshes/textures.

The left joystick is throttle and reverse; left/right steers the
vehicle. Forward speed is capped at 24 m/s, reverse at 12 m/s, with acceleration
and braking. Right-stick torso turning is centred while driving. Tracking/focus
loss stops the vehicle. Transformation locks walking; driving and transformation
block attacks and other active equipment. Press the Vehicle Mode control again
to return to walking. The cockpit's instrument/glass layout is unchanged; its
world height eases lower during the sequence.

This is a ground-skimmer prototype, without wheels, suspension, animated tread
links or redesigned collision. The conservative standing mecha collision remains,
including interior height clearance and walls; visible folding is not permission
to enter a passage too small for the mecha. Wading limits remain enforced.

Native preview: `artifacts/validation/vehicle-transformation.mp4` and
`artifacts/validation/vehicle-gallery/contact.png`. Blender source:
`art/mecha/vehicle-prototype/vehicle-transformation.blend`. Blender uses pose
samples exported by the actual native animation function, with NLA actions for
each rigid part. Original source meshes, rigs and cockpit files are unmodified.

For an isolated VR prototype profile (leave your existing pilot file alone):

```sh
./build-mecha/mecha-hack-xr --xr --profile /tmp/mecha-vehicle-test.json \
  --loadout assets/rpg/vehicle-loadout.json
```

Vehicle Mode is on the right hip: **A** toggles it. This JSON is a development
import preset; normal profiles select the module through creation or level-up.

Verification: all 14 optimized Release suites passed in 42.94 seconds. Native
menu flows, seven sight configurations, 196 two-eye vehicle frames, source
geometry/cockpit hashes and Blender pose budgets passed. No new headset test.

Current menu controls and campaign generation rules are documented in
[MECHA_INTERFACE.md](MECHA_INTERFACE.md). A or right trigger selects, and B or
left trigger goes back. Options contains microphone and volume controls. Pause
offers field equipment and cockpit recentering.
The hangar provides separate mission, equipment, trade and profile entry points.


## Hazard fronts

Regional weather can add a hazardous front. A front has a short initial
grace period and warns before each pulse. Enclosed structures shelter
pilots and NPCs. Spore bursts, fire storms and meteors mark their impact
areas so a pilot can move clear. The host resolves damage and disruption
for the whole room.

- Ion storms interfere with sensors and add reactor heat on land or in vacuum.
- Corruption spores form damaging clouds on wilderness maps.
- Volcanic fire storms mark burning ground and stress exposed reactors.
- Meteor showers strike deserts, lunar maps and exterior space sections.
- Magnetic bursts disrupt sensors and mobility in susceptible environments,
  including underwater maps.
- Volcanic ash clouds reduce visibility and add reactor heat.
- Radioactive winds expose pilots in volcanic and contaminated battlefield or
  graveyard terrain.

Ordinary atmospheric weather remains separate from these mission modifiers.
