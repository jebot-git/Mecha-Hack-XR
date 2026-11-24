# Adaptive campaigns

[Branching story frameworks](MECHA_CAMPAIGN_STORIES.md) extend this progression
with authored choices, story-driven maps, continuing aftermath and offline legacy.

A campaign has nine successful contracts followed by a boss encounter or a
decisive battle. The final sector of a contract advances the campaign. Moving
between sectors grants a short rest. A failed contract can be retried.

After each victory the mission selector archives the completed slot. Once the
whole party has accepted debrief, the remaining offers regenerate from the
host’s faction standings, campaign stage and party size. Pilots acquire modules
from trade or recovered safe boxes.

## Consequences

Choosing a contract commits to its named employer and adversary. Completing it
normally gives the employer +2 standing and the adversary -2. Capturing a leader
costs the adversary only -1. Every eight non-objective concrete structures lost
during the operation reduces the employer gain by one, up to three. War damage
includes enemy destruction; objective stores and protected assets are excluded.
Losses accumulate across connected sectors. Standings are bounded to -20..20.

Reconnaissance and captures each provide two intelligence; sabotage provides one.
Defense, escort, rescue and reinforcement contracts each secure two supplies.
Intelligence and supplies each cap at 18. Intelligence reduces future encounter
counts by up to 15% and grants a short concealed insertion. Supplies improve
contract compensation and grant up to six temporary shield points at insertion.
Hostile faction relations increase encounter pressure. Friendly factions are
more likely to offer contracts, while hostile factions are more likely to appear
as opponents. Contract selection, outcome and structural losses also salt the
next board's seeds, so different mission paths produce different campaigns.

The final operation targets the human faction with the lowest negative standing.
The campaign seed breaks ties. If all three human factions have neutral or
positive standings, the final operation faces a Voidmaw incursion. Its support and enemy pressure retain the campaign's
intelligence, supplies and relationships. Authored campaigns choose boss/battle form from accumulated story priorities;
legacy procedural campaigns retain seeded selection. The host has no override. Its arena is an open surface battlefield, avoiding
interior clearance and underwater-only boss placement problems.

## Faction detachments

Eligible dry surface operations can deploy up to three neutral groups and
one allied detachment within the initial actor limit. A faction that fields
the contract force cannot also appear as a non-hostile detachment. Allies
require at least +4 standing, or nonnegative standing with the employer.
Only the Aeonic Primacy, Terran Collective and Neo Dominion have standings.
Voidmaw are always hostile to humans. Human forces can engage visible kaiju
regardless of their relationship to the pilots or their distance from the party.

Neutral groups fight opposing NPCs. A player hit turns every neutral group
of that faction against the party for the rest of the operation. Allied
groups assist nearby pilots and withdraw when hit by the party. Surviving
allies return fire while withdrawing if the party destroys one of their units.
Neither type is part of the contract's required target count.

Debrief applies standing changes once, including kills and friendly-fire
incidents accumulated across connected sectors. Campaign offer generation
uses the resulting standings. Active radar scans show allies in cyan,
neutral contacts in amber and hostile contacts in red.

## Final enemies

These are original encounter definitions using existing authored models and rigs.
No mesh, animation membership or approved triangle budget is enlarged. Bosses
have distinct hull colors and flash during a weapon charge; the cockpit objective
shows their identity, hull, phase and charge warning.

| Enemy | Faction / rig | Combat design | Response |
| --- | --- | --- | --- |
| Prism Regent | Aeonic / Aeon Primarch | Shielded beam hunter, funnel crossfire, close repulsor, shock armor | Break sightlines during charge; close inside lance range |
| Null Choir | Aeonic / Ion Disruptor Installation | Stationary long beam and sensor disruption with point defense | Advance through cover into the beam's minimum range |
| Ironwake Marshal | Terran / Terran Mammoth | Plated siege commander, push mortar and rotary batteries | Flank and get inside mortar range; clear supporting grunts |
| Red Horizon | Terran / Dreadnought | Airborne broadside carrier with seeker screen | Strafe charged broadsides; clear the two air wings |
| Crownless Duelist | Neo Dominion / Neo Paladin | Fast melee champion with knockback cleave and reactor challenge | Keep clear of the cleave and use its recovery window |
| Gravesong Sovereign | Voidmaw / Kaiju Broodqueen | Regenerating biological giant with acid spines and pulling song | Burst through regeneration while managing brood waves |

Base hull is 160 solo, increasing by 60% per additional pilot (448 for four).
At two-thirds and one-third hull, the boss pauses for 1.5 seconds and summons
one wave of 3..6 appropriate escorts. Crossing both thresholds in one hit creates
both waves once. The last phase shortens recovery, with at least a half-second
recovery beyond the defined 1.4..2 second charge interval. Victory requires defeating the boss and its initial
command guard. The cockpit tracks the surviving guard. Summoned escorts are optional. Native endless carrier
spawning is not used by these boss definitions.

A decisive battle instead fields a larger faction roster and ends when its force
is eliminated. Carrier types are excluded to keep the victory finite. Both
finale types preserve the existing limits: 48 initial enemies, 64 live/recycled
enemy slots, 1,024 scene targets and 256 projectiles/effects. Ordinary campaign
generation still uses the unchanged 66-entry source enemy catalog; boss profiles
are a separate catalog and do not leak into early missions.

## Awards, reset and saves

Final victory opens campaign debrief. Each pilot can use left or right on the
stick to choose their next chassis. Accepting the award grants one campaign
point and issues the starter loadout for the selected chassis. Acquired weapons,
tuning, armour, modules and consumables are cleared. Each pilot keeps their
callsign, role, career, mission record, credits and scrap. Narrative legacy
carries the previous framework, ending and rebuilding pressure into the next
campaign. Saved claims prevent duplicate awards and equipment resets when a
party resumes debrief. The host returns the party to the hangar after every
remaining pilot accepts. Older profiles without a saved starter module fall
back to the chassis default when claiming a campaign award.

Each pilot starts as a Rookie, earns Veteran after five contracts, and earns
Campaign Ace after winning a campaign. A veteran who reaches +12 with a human
faction permanently earns its uniform ribbon and service title. The Primacy
grants Shield of the Primacy, Terra grants Warden of Terra, and the Dominion
grants Lion of the Dominion. Co-op uses the host’s standings for the operation,
and applies its outcome to each participant’s own record. A pilot can earn all
three ribbons. The pilot roster displays the uniform, standings and earned titles.

Campaign seed/revision, nine-bit completion, compact historical generator inputs,
relations, resources, consequence hash, progression random state and pending
debrief status are saved in the pilot profile. The host restores its chosen
profile's campaign when starting or switching profiles; guests participate in
that shared campaign and checkpoint the same campaign history alongside their
own training and awards. Saves are local files, not server accounts or an
anti-cheat boundary. A saved unclaimed final victory resumes at the award screen.

Whole-board regeneration remains the host's only manual generation action. It
is available before the first victory; after progression begins, remaining
missions update automatically. This prevents regenerating completed slots into
extra level/reward opportunities. A successful campaign starts a fresh board
automatically after everyone accepts their award.

Profile schema 9 reads schemas 2 through 8 and discards the old Voidmaw standing. Importing a legacy `default.json` preserves
that source; normal per-slot files upgrade atomically on their next save. Older
levelled profiles receive a neutral campaign checkpoint matching their existing
level and pending reward; unknown historical faction choices are not invented.
Client and server use protocol 38. Matching binaries are required. Shared campaign state is sent once per snapshot rather than repeated
inside each current participant's sheet. Configuration acknowledgments keep
an older network snapshot from overwriting a newly selected local profile.

## Validation

`mecha-campaign-progression` drives the actual objective/sector/debrief state
machines through nine contracts and the finale. It checks immutable completed
offers, renewed remaining offers, party barriers, faction consequences, retry
stability, save/resume during debrief, claimed/unclaimed final victory, exactly
one award, four-profile storage, preserved pilot identity and savings, every boss,
phase replication, bounded waves, and decisive-battle generation.

`mecha-render-probe --menus` includes native stereo campaign hangar, adaptive
offers, debrief, final selection, briefing, victory, claimed award and starter
equipment screens, plus all six bosses in normal and charging appearance. These
captures do not substitute for an in-headset playthrough or balance evaluation.
