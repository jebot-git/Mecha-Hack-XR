#include "rpg/manual_missions.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/bosses.hpp"
#include <algorithm>
#include <cmath>
namespace iron::rpg {
std::span<const ManualMission> manualMissionCatalog(){
  static constexpr ManualMission data[]={
    {"anathema","ANATHEMA",12,{4,0,5},{"RECOVER THE MISSING EXPERIMENTAL PILOT.","CLEAR HOSTILE WITNESSES FROM THE UTILITY DISTRICT.","DEMOLISH THE GANG COMMAND INFRASTRUCTURE."},0,0,45},
    {"assault_moroo","ASSAULT ON MOROO",13,{4,9,8},{"EXTRACT SURVIVORS FROM THE CRASHED TRANSPORT.","DISABLE THE WARLORD AND SECURE THE PILOT.","RELIEVE THE CITY GARRISON AND HOLD ITS CENTER."},0,0,60},
    {"breaking_blockade","BREAKING THE BLOCKADE",14,{3,9,5},{"MAP AND DISRUPT THE HOSTILE RADAR NETWORK.","TAKE THE BLOCKADE COMMANDER ALIVE.","DESTROY THE ARTILLERY STORES FEEDING THE BLOCKADE."},2,0,60},
    {"chaos_stratus","CHAOS AT STRATUS",15,{4,2,1},{"RETRIEVE THE PROTOTYPE ENGINE FROM THE STATION.","COVER THE RESEARCH TEAM TRANSPORT TO EXTRACTION.","KEEP THE STATION CORE INTACT UNTIL RELIEF ARRIVES."},6,360,60},
    {"clash_titans","CLASH OF THE TITANS",16,{9,7,8},{"DISABLE THE RIVAL PILOT WITHOUT DESTROYING THE COCKPIT.","KEEP THE RIVAL OCCUPIED WHILE ALLIES COMPLETE THEIR TASK.","REACH THE RIVAL AND HOLD A JOINT DEFENSIVE POSITION."},2,0,75},
    {"cozen_job","THE COZEN JOB",17,{4,3,5},{"SECURE THE JETTISONED CARGO AND EXTRACT IN YOUR MECHA.","SCAN THE FALLEN CARGO FOR FLEET NAVIGATION DATA.","DESTROY THE EXPOSED SHIP STORES TO COVER YOUR ESCAPE."},7,300,45},
    {"defending_kuro","DEFENDING KURO STATION",18,{1,2,6},{"PROTECT THE BIOFUEL INSTALLATION FROM ASSAULT.","ESCORT THE CIVILIAN EVACUATION CONVOY.","PREVENT THE ENEMY FROM REACHING THE CAPTURED OFFICER."},6,0,105},
    {"drop_leviathan","DROP ZONE: LEVIATHAN",19,{8,4,3},{"REACH THE FRONTLINE SQUAD AND PROVIDE COVERING FIRE.","RECOVER THE PROTOTYPE MISSILES FROM THE BATTERY.","SURVEY THE REFINERY EXPERIMENT SITES."},2,0,60},
    {"enter_irondeath","ENTER IRONDEATH ARENA",20,{9,3,7},{"DISABLE THE OPPOSING CHAMPION IN THE ARENA.","SCAN RIVAL MACHINES FOR THEIR COMBAT DATA.","SURVIVE THE ARENA SPECTACLE UNTIL EXTRACTION OPENS."},2,0,60},
    {"elsewhere_infinitum","ELSEWHERE INFINITUM",21,{3,4,5},{"FIND A STABLE ROUTE THROUGH THE DISTORTED TERRAIN.","RECOVER STRANDED ALLIES AND RETURN TO THE PORTAL.","DESTROY THE ANCHORS SUSTAINING THE HOSTILE RIFT."},9,420,45},
    {"eruption_extinction","ERUPTION EXTINCTION",22,{5,1,4},{"ERASE THE EXPOSED RESEARCH STORES BEFORE ERUPTION.","DEFEND THE CONTROL STATION WHILE THE VOLCANO STABILIZES.","RECOVER SEALED KAIJU SAMPLES FROM THE HOT ZONE."},3,300,90},
    {"phoenix_peak","FALL OF PHOENIX PEAK",23,{4,4,1},{"EXTRACT THE SHELTERED VIP FROM THE RUINED DISTRICT.","RECOVER THE PROTOTYPE FROM THE VOLCANO-SIDE DEPOT.","PROTECT THE UPLINK THROUGH THE EMERGENCY TRANSMISSION."},0,360,75},
    {"foundry","THE FOUNDRY",24,{4,0,5},{"RECOVER A SURVIVING ALIEN MACHINE FOR STUDY.","REMOVE THE RIVAL SALVAGE FORCE FROM THE FOUNDRY.","DESTROY THE ASSEMBLY STORES BEFORE THE GIANT IS COMPLETED."},5,0,60},
    {"grand_theft_mariner","GRAND THEFT MARINER",25,{5,3,4},{"SABOTAGE THE SUBMERGED BASE USING YOUR MECHA WEAPONS.","SCAN THE BASE RELAYS FOR ACCESS CODES.","RECOVER ALLIED ESCAPE CAPSULES FROM THE FLOODED BASE."},8,360,45},
    {"heavens_fall","HEAVENS FALL",26,{0,4,1},{"CLEAR HOSTILE MACHINES AMONG THE FALLEN SHIP FRAGMENTS.","SECURE THE GRAVITY CORES BEFORE THE WRECK BREAKS APART.","PROTECT THE SHIP POWER CORE DURING EMERGENCY RECOVERY."},7,300,60},
    {"verglas","INSURGENCY AT VERGLAS",27,{2,4,5},{"ESCORT THE SCIENTISTS THROUGH THE FROZEN INDUSTRIAL ZONE.","RECOVER EVIDENCE CACHES BEFORE THEY ARE DESTROYED.","NEUTRALIZE THE EXPLOSIVE STORES THREATENING THE STATION."},6,0,60},
    {"interception_red_zone","INTERCEPTION: RED ZONE",28,{5,2,4},{"DESTROY THE RUNAWAY CONVOY POWER RELAYS FROM YOUR COCKPIT.","ESCORT THE INTERCEPTED TRANSPORT TO A SAFE SIDING.","RETRIEVE THE TRAPPED CREW CAPSULES FROM THE CONVOY ROUTE."},2,240,45},
    {"jailbreak","JAILBREAK: HANGAR BREAKOUT",29,{4,3,5},{"EXTRACT THE PRISONER CAPSULE AFTER THE MECHA BREAKOUT.","MAP THE PRISON DEFENSES FOR THE NEXT RESCUE OPERATION.","DESTROY THE LOCKDOWN GENERATORS TO OPEN THE ESCAPE ROUTE."},7,300,45},
    {"leviathan_mist","LEVIATHAN IN THE MIST",30,{3,4,0},{"SURVEY THE OBSCURED GIANT AND LOCATE ITS WEAK POINTS.","RETRIEVE THE LOST SURVEY TEAM CAPSULES.","ELIMINATE THE GIANT AND ITS DEFENDERS."},9,0,75},
    {"long_live_king","LONG LIVE THE KING",31,{8,9,2},{"REACH THE RESISTANCE AND SECURE ITS RALLYING POINT.","DISABLE THE ROYAL COMMAND MACHINE AND RECOVER ITS CONTROLLER.","ESCORT THE CAPTURED FREIGHTER OUT OF THE ROYAL TERRITORY."},1,0,60},
    {"mecha_league","MECHA LEAGUE",32,{4,1,9},{"RECOVER THE SCORING CORES AND BRING THEM HOME.","DEFEND YOUR GOAL INSTALLATION FROM THE RIVAL LEAGUE.","DISABLE THE OPPOSING TEAM CAPTAIN."},2,240,60},
    {"meltdown","MELTDOWN",33,{5,4,1},{"DISABLE THE UNSTABLE REACTOR STORES BEFORE CRITICALITY.","RECOVER PERSONNEL CAPSULES FROM THE REACTOR EXCLUSION ZONE.","HOLD THE COOLING STATION THROUGH THE SHUTDOWN SEQUENCE."},6,240,90},
    {"orions_arrow","ORIONS ARROW",34,{5,4,1},{"DESTROY THE ARMED WARHEAD STORES BEFORE IMPACT.","RECOVER THE GUIDANCE PACKAGE FROM THE ASTEROID FACILITY.","DEFEND THE INTERCEPTION BATTERY UNTIL IT CAN FIRE."},9,300,60},
    {"phase_line_talon","PHASE LINE: TALON",35,{4,3,6},{"EXTRACT SURVIVOR PODS FROM THE INFECTED DISTRICT.","SAMPLE THE CONTAMINATED SITES WITH MECHA SCANNERS.","STOP INFECTED MACHINES CROSSING THE QUARANTINE BOUNDARY."},0,0,75},
    {"academy_aeonic","PILOT ACADEMY: AEONIC PRIMACY",36,{3,9,1},{"COMPLETE THE SCANNER COURSE IN YOUR OWN MECHA.","DISABLE THE ACADEMY ACE DURING THE LIVE EXERCISE.","PROTECT THE SIMULATED UPLINK UNDER TRAINING FIRE."},6,0,60},
    {"academy_neo","PILOT ACADEMY: NEO DOMINION",37,{9,7,8},{"WIN THE KNIGHTLY TRIAL BY DISABLING THE RIVAL MACHINE.","HOLD THE TOURNAMENT MARKER WHILE RIVALS ATTACK.","REACH THE ALLIED TRAINEE AND DEFEND THE RALLY POINT."},2,0,75},
    {"academy_terran","PILOT ACADEMY: TERRAN COLLECTIVE",38,{4,0,2},{"RECOVER USEFUL SALVAGE FROM THE LIVE-FIRE SCRAPYARD.","CLEAR THE HOSTILE MACHINES THREATENING THE EXERCISE.","BRING THE TRAINING CONVOY THROUGH THE SCRAPYARD."},5,0,60},
    {"race_to_bottom","A RACE TO THE BOTTOM",39,{8,5,3},{"REACH THE WORKERS BEFORE THE RIVAL SALVAGE FORCE.","DESTROY THE RIVAL EXTRACTION STORES.","SURVEY THE LOWER CAVERN ACTIVITY FROM YOUR COCKPIT."},5,300,75},
    {"return_irondeath","RETURN TO IRONDEATH ARENA",40,{3,7,9},{"PASS EVERY CHECKPOINT BEFORE THE ARENA CLOCK EXPIRES.","HOLD THE FINAL CHECKPOINT AGAINST THE RIVAL RACERS.","DISABLE THE REIGNING CHAMPION ALONG THE RACE ROUTE."},2,180,45},
    {"revenge_irondeath","REVENGE OF IRONDEATH ARENA",41,{3,9,0},{"RECOVER THE ARENA SPONSOR RECORDS FROM GUARDED RELAYS.","DISABLE THE ENEMY CAPTAIN WITHOUT KILLING THE PILOT.","DEFEAT THE RINGLEADER FIRETEAM AND END THE GAMES."},2,0,60},
    {"nanovore","RISE OF THE NANOVORE",42,{8,3,4},{"REACH THE FACTORY CONTROL UNIT AND SECURE ITS RESTART.","TRACE THE NANOTECH OUTBREAK THROUGH THE FACILITY.","RECOVER THE SURVIVORS FROM SEALED EVACUATION PODS."},6,360,75},
    {"rise_up","RISE UP",43,{7,9,8},{"OCCUPY THE COMMUNICATIONS ARRAY DURING THE UPRISING.","BRING DOWN THE ENFORCER COMMANDER AND TAKE THE PILOT.","REACH THE LABOR FORCE AND HOLD ITS RALLY POINT."},5,0,75},
    {"salvaging_eclipse","SALVAGING THE ECLIPSE",44,{4,3,2},{"RECOVER THE CAPITAL-SHIP WEAPON SALVAGE.","SCAN BARRIER CONTROLLERS FOR USABLE ACCESS DATA.","ESCORT THE SURVIVOR TRANSPORT OUT OF THE WRECK."},7,0,60},
    {"search_rescue","SEARCH AND RESCUE",45,{4,4,3},{"COLLECT CIVILIAN EVACUATION PODS FROM THE RUBBLE.","RECOVER THE VIP CAPSULE FROM THE RUINED LABORATORY.","SCAN THE KAIJU REMAINS USING SEALED MECHA INSTRUMENTS."},0,360,60},
    {"sewer_duty","SEWER DUTY",46,{3,0,1},{"CONFIRM THE SEWER POWER GENERATORS ARE OPERATIONAL.","CLEAR THE MACHINES AND CREATURES OBSTRUCTING THE TUNNELS.","DEFEND THE FUSION SERVICE UNIT AGAINST TUNNEL INCURSIONS."},0,0,75},
    {"seize_ronin","SEIZE THE RONIN",47,{1,2,4},{"KEEP THE PRISON SHIP POWER CORE INTACT DURING CAPTURE.","COVER THE ESCAPE TRANSPORT THROUGH THE SHIPYARD.","RETRIEVE THE RESTRAINED PRISONER CAPSULE FROM THE HULL."},7,300,75},
    {"severed_connection","SEVERED CONNECTION",48,{5,9,3},{"DESTROY THE EXPOSED NETWORK HUB POWER CENTERS.","DISABLE THE HOSTILE INTERCEPTOR AND SECURE ITS PILOT.","DOWNLOAD RELAY DATA WITH THE MECHA SCANNER."},6,360,60},
    {"symbiotic_superdestroyer","SYMBIOTIC SUPERDESTROYER",49,{0,7,3},{"DESTROY THE HOSTILE NANOBOT CONTROLLER AND ITS DEFENDERS.","HOLD THE SAMPLING BEACON WHILE THE INSTRUMENTS WORK.","TRACE THE SYNTHETIC INFECTION THROUGH THE SEALED TEST SITE."},8,300,75},
    {"stars_torn_asunder","THE STARS TORN ASUNDER",50,{2,3,4},{"ESCORT THE FACTION DELEGATES OUT OF THE BATTLE ZONE.","SCAN THE ALIEN WARSHIP POWER NETWORK.","RECOVER THE DIPLOMATIC ARCHIVE CAPSULES."},9,0,90},
    {"frozen_king","TOMB OF THE FROZEN KING",54,{4,3,5},{"EXTRACT THE MISSING RESEARCHER CAPSULE FROM THE ICE.","SCAN PRESERVED KAIJU SPECIMENS WITH SEALED ONBOARD PROBES.","DESTROY THE CULT RESEARCH STORES AND CONTAINMENT EQUIPMENT."},3,360,75},
    {"voidbringer","VOIDBRINGERS TORMENT",55,{3,9,0},{"NAVIGATE THE SIMULATED MECHA MAZE TO THE ESCAPE MARKERS.","DISABLE THE SIMULATOR COMMAND MACHINE.","DEFEAT THE GIANT AND ITS PROJECTED DEFENDERS."},6,360,75},
    {"zura_station","RESCUE ZURA STATION",51,{1,2,4},{"DEFEND STATION LIFE SUPPORT UNTIL IT STABILIZES.","ESCORT THE STATION EVACUATION TRANSPORT.","RECOVER THE TRAPPED DIPLOMATIC CAPSULES."},6,300,90},
    {"stop_skirmish","STOP THE SKIRMISH",51,{9,8,7},{"DISABLE THE RIVAL COMMANDER BEFORE THE BATTLE SPREADS.","REACH THE ALLIED CEASEFIRE BEACON AND SECURE THE AREA.","HOLD THE BROADCAST RELAY WHILE THE TRUCE IS TRANSMITTED."},2,0,75},
    {"dethrone_queen","DETHRONE THE QUEEN",52,{0,5,6},{"DEFEAT THE BROODQUEEN DEFENDERS.","DESTROY THE SPAWNING RESOURCE NODES.","STOP THE SPAWNED CREATURES REACHING THE CIVILIAN BOUNDARY."},9,0,90},
    {"precious_cargo","DEFEND PRECIOUS CARGO",52,{2,1,6},{"ESCORT THE MEDICINE CONVOY THROUGH THE FLEET DEBRIS.","PROTECT THE STRANDED CIVILIAN CARGO DEPOT.","INTERCEPT ATTACKERS BEFORE THEY REACH THE FREIGHTERS."},9,0,90},
    {"face_keros","FACE THE KEROS",52,{5,3,1},{"DESTROY THE CANNON POWER NODES BEFORE THE NEXT DISCHARGE.","SCAN THE CANNON NETWORK TO EXPOSE THE FEEDBACK ROUTE.","DEFEND THE ALLIED TARGETING STATION THROUGH ITS FIRING CYCLE."},9,360,90},
    {"halt_verilian","HALT VERILIAN REINFORCEMENTS",53,{5,6,7},{"DESTROY THE CARRIER LAUNCH-SUPPORT STORES.","STOP THE NEWLY LAUNCHED UNITS AT THE DEFENSE LINE.","HOLD THE CARRIER HANGAR MOUTH TO PREVENT FRESH LAUNCHES."},7,360,75},
    {"take_aces","TAKE OUT THE ACES",53,{9,0,8},{"DISABLE THE ALIEN ACE BEFORE ESCAPE.","DEFEAT THE COMMAND FIRETEAM AND ITS ESCORT.","REACH THE PINNED ALLIES AND WITHSTAND THE ACE ATTACK."},2,300,75},
  };return data;
}
std::span<const MissionChoice> instantActions(){
  static constexpr MissionChoice data[]={
    {"attack","ATTACK"},
    {"disable","DISABLE"},
    {"scout","SCOUT"},
    {"acquire","ACQUIRE"},
    {"sabotage","SABOTAGE"},
    {"outrun","OUTRUN"},
    {"escort","ESCORT"},
    {"destroy","DESTROY"},
    {"reach","REACH"},
    {"hack","HACK"},
    {"defend","DEFEND"},
    {"escape","ESCAPE"},
    {"deceive","DECEIVE"},
    {"scan","SCAN"},
    {"repair","REPAIR"},
    {"infiltrate","INFILTRATE"},
    {"explore","EXPLORE"},
    {"salvage","SALVAGE"},
    {"claim","CLAIM"},
    {"investigate","INVESTIGATE"},
  };return data;
}
std::span<const MissionChoice> instantFoci(){
  static constexpr MissionChoice data[]={
    {"resources","RESOURCES"},
    {"reactor","REACTOR"},
    {"base","BASE"},
    {"bomb","BOMB"},
    {"planet","PLANET"},
    {"weapon","WEAPON"},
    {"signal","SIGNAL"},
    {"mecha","MECHA"},
    {"cargo","CARGO"},
    {"shields","SHIELDS"},
    {"alliance","ALLIANCE"},
    {"enemy","ENEMY"},
    {"fuel","FUEL"},
    {"moon","MOON"},
    {"kaiju","KAIJU"},
    {"starship","STARSHIP"},
    {"intel","INTEL"},
    {"experiment","EXPERIMENT"},
    {"station","STATION"},
    {"asteroid","ASTEROID"},
  };return data;
}
std::span<const MissionChoice> manualComplications(){
  static constexpr MissionChoice data[]={
    {"reinforcement_ambush","REINFORCEMENT AMBUSH"},
    {"communications_blackout","COMMUNICATIONS BLACKOUT"},
    {"thruster_surge","THRUSTER SURGE"},
    {"environmental_hazard","ENVIRONMENTAL HAZARD"},
    {"distant_bombardment","DISTANT BOMBARDMENT"},
    {"elite_arrival","ELITE ARRIVAL"},
  };return data;
}
std::span<const MissionChoice> manualOpportunities(){
  static constexpr MissionChoice data[]={
    {"allied_support","ALLIED SUPPORT"},
    {"exposed_defenses","EXPOSED DEFENSES"},
    {"auxiliary_reactor_power","AUXILIARY REACTOR POWER"},
    {"sabotaged_enemy_weapons","SABOTAGED ENEMY WEAPONS"},
    {"reactor_supply_drop","REACTOR SUPPLY DROP"},
    {"capital_ship_fire_support","CAPITAL-SHIP FIRE SUPPORT"},
  };return data;
}
std::span<const MissionChoice> manualBriefings(){
  static constexpr MissionChoice data[]={
    {"drop_craft_alert","DROP-CRAFT ALERT"},
    {"hangar_orders","HANGAR ORDERS"},
    {"encrypted_audio_order","ENCRYPTED AUDIO ORDER"},
    {"holographic_dossier","HOLOGRAPHIC DOSSIER"},
    {"remote_command_link","REMOTE COMMAND LINK"},
    {"broker_contract","BROKER CONTRACT"},
    {"orbital_briefing","ORBITAL BRIEFING"},
    {"dead_drop_dossier","DEAD-DROP DOSSIER"},
    {"squad_assembly","SQUAD ASSEMBLY"},
    {"mechanics_checklist","MECHANICS CHECKLIST"},
  };return data;
}
std::span<const MissionChoice> planetoidFeatures(){
  static constexpr MissionChoice data[]={
    {"craggy_surface","CRAGGY SURFACE"},
    {"volcanic_terrain","VOLCANIC TERRAIN"},
    {"cavern_network","CAVERN NETWORK"},
    {"water_world","WATER WORLD"},
    {"minor_rocky_body","MINOR ROCKY BODY"},
    {"gas_giant_vicinity","GAS GIANT VICINITY"},
    {"dense_rocky_world","DENSE ROCKY WORLD"},
    {"plateau_landscape","PLATEAU LANDSCAPE"},
    {"pulsar_vicinity","PULSAR VICINITY"},
    {"asteroid_belt","ASTEROID BELT"},
    {"arid_surface","ARID SURFACE"},
    {"living_ecosystem","LIVING ECOSYSTEM"},
  };return data;
}
std::span<const MissionChoice> planetoidHazards(){
  static constexpr MissionChoice data[]={
    {"dust_veil","DUST VEIL"},
    {"magnetic_storm","MAGNETIC STORM"},
    {"comet_passage","COMET PASSAGE"},
    {"void_lightning","VOID LIGHTNING"},
    {"unusual_constellation","UNUSUAL CONSTELLATION"},
    {"moon_debris","MOON DEBRIS"},
    {"nebula_interference","NEBULA INTERFERENCE"},
    {"solar_wind","SOLAR WIND"},
    {"drifting_debris","DRIFTING DEBRIS"},
    {"meteor_shower","METEOR SHOWER"},
    {"crowded_stellar_sky","CROWDED STELLAR SKY"},
    {"black_hole_proximity","BLACK-HOLE PROXIMITY"},
  };return data;
}
std::span<const MissionChoice> salvageForms(){
  static constexpr MissionChoice data[]={
    {"shattered","SHATTERED"},
    {"aeonic","AEONIC"},
    {"ancient","ANCIENT"},
    {"partly_working","PARTLY WORKING"},
    {"bioengineered","BIOENGINEERED"},
    {"terran","TERRAN"},
    {"rare_alloy","RARE-ALLOY"},
    {"volatile","VOLATILE"},
    {"dominion","DOMINION"},
    {"unique","UNIQUE"},
    {"modified","MODIFIED"},
    {"verilian","VERILIAN"},
  };return data;
}
std::span<const MissionChoice> salvageFeatures(){
  static constexpr MissionChoice data[]={
    {"prototype_weapon","PROTOTYPE WEAPON"},
    {"reactor_component","REACTOR COMPONENT"},
    {"mecha_wreckage","MECHA WRECKAGE"},
    {"vehicle_parts","VEHICLE PARTS"},
    {"power_source","POWER SOURCE"},
    {"ship_frame","SHIP FRAME"},
    {"support_equipment","SUPPORT EQUIPMENT"},
    {"ammunition","AMMUNITION"},
    {"restricted_technology","RESTRICTED TECHNOLOGY"},
    {"module_parts","MODULE PARTS"},
    {"computer_core","COMPUTER CORE"},
    {"intact_chassis","INTACT CHASSIS"},
  };return data;
}
std::span<const MissionChoice> mechaPaints(){
  static constexpr MissionChoice data[]={
    {"blood_red","BLOOD RED"},
    {"acid_green","ACID GREEN"},
    {"electric_blue","ELECTRIC BLUE"},
    {"flame_orange","FLAME ORANGE"},
    {"deep_violet","DEEP VIOLET"},
    {"toxic_yellow","TOXIC YELLOW"},
    {"candy_pink","CANDY PINK"},
    {"night_black","NIGHT BLACK"},
    {"gunmetal","GUNMETAL"},
    {"iridescent","IRIDESCENT"},
    {"silver_mirror","SILVER MIRROR"},
    {"combined_paint","COMBINED PAINT"},
  };return data;
}
std::span<const MissionChoice> mechaFeatures(){
  static constexpr MissionChoice data[]={
    {"energy_channels","ENERGY CHANNELS"},
    {"vent_network","VENT NETWORK"},
    {"horned_armor","HORNED ARMOR"},
    {"spiked_shoulders","SPIKED SHOULDERS"},
    {"crested_armor","CRESTED ARMOR"},
    {"tracked_undercarriage","TRACKED UNDERCARRIAGE"},
    {"scrap_patchwork","SCRAP PATCHWORK"},
    {"sensor_antennae","SENSOR ANTENNAE"},
    {"folded_thrusters","FOLDED THRUSTERS"},
    {"concealed_hardpoints","CONCEALED HARDPOINTS"},
    {"faction_banners","FACTION BANNERS"},
    {"combined_fittings","COMBINED FITTINGS"},
  };return data;
}
std::span<const MissionChoice> kaijuLooks(){
  static constexpr MissionChoice data[]={
    {"insectoid","INSECTOID"},
    {"reptilian","REPTILIAN"},
    {"mammalian","MAMMALIAN"},
    {"arachnid","ARACHNID"},
    {"cephalopod","CEPHALOPOD"},
    {"hybrid_anatomy","HYBRID ANATOMY"},
  };return data;
}
std::span<const MissionChoice> kaijuFeatures(){
  static constexpr MissionChoice data[]={
    {"extra_limbs","EXTRA LIMBS"},
    {"tentacle_growths","TENTACLE GROWTHS"},
    {"membrane_wings","MEMBRANE WINGS"},
    {"bone_horns","BONE HORNS"},
    {"luminous_veins","LUMINOUS VEINS"},
    {"combined_growths","COMBINED GROWTHS"},
  };return data;
}
std::span<const MissionChoice> kaijuNamePrefixes(){
  static constexpr MissionChoice data[]={
    {"doom","DOOM"},
    {"meat","MEAT"},
    {"ultra","ULTRA"},
    {"gloom","GLOOM"},
    {"kill","KILL"},
    {"hell","HELL"},
    {"warp","WARP"},
    {"double","DOUBLE"},
    {"bone","BONE"},
    {"razor","RAZOR"},
    {"rip","RIP"},
    {"shatter","SHATTER"},
  };return data;
}
std::span<const MissionChoice> kaijuNameSuffixes(){
  static constexpr MissionChoice data[]={
    {"jaw","JAW"},
    {"crusher","CRUSHER"},
    {"mantle","MANTLE"},
    {"head","HEAD"},
    {"hammer","HAMMER"},
    {"reaper","REAPER"},
    {"scraper","SCRAPER"},
    {"knife","KNIFE"},
    {"spine","SPINE"},
    {"face","FACE"},
    {"maw","MAW"},
    {"fury","FURY"},
  };return data;
}
std::span<const MissionChoice> friendlyChatter(){
  static constexpr MissionChoice data[]={
    {"watch_your_flank","WATCH YOUR FLANK."},
    {"clear_the_crossing","CLEAR THE CROSSING."},
    {"you_have_my_support","YOU HAVE MY SUPPORT."},
    {"stay_on_the_task","STAY ON THE TASK."},
    {"incoming_fire","INCOMING FIRE."},
    {"more_contacts_inbound","MORE CONTACTS INBOUND."},
    {"radar_contact_confirmed","RADAR CONTACT CONFIRMED."},
    {"new_hostile_signatures","NEW HOSTILE SIGNATURES."},
    {"cover_requested","COVER REQUESTED."},
    {"advance_as_a_unit","ADVANCE AS A UNIT."},
    {"keep_moving","KEEP MOVING."},
    {"my_position_is_compromised","MY POSITION IS COMPROMISED."},
    {"trust_your_training","TRUST YOUR TRAINING."},
    {"spread_out_and_surround","SPREAD OUT AND SURROUND."},
    {"taking_the_leader","TAKING THE LEADER."},
    {"commit_all_batteries","COMMIT ALL BATTERIES."},
    {"covering_your_advance","COVERING YOUR ADVANCE."},
    {"hold_formation","HOLD FORMATION."},
    {"target_hit_confirmed","TARGET HIT CONFIRMED."},
    {"finish_the_operation","FINISH THE OPERATION."},
  };return data;
}
std::span<const MissionChoice> enemyChatter(){
  static constexpr MissionChoice data[]={
    {"your_armor_will_fail","YOUR ARMOR WILL FAIL."},
    {"the_signal_is_breaking_up","THE SIGNAL IS BREAKING UP."},
    {"close_and_engage","CLOSE AND ENGAGE."},
    {"target_marked_for_pursuit","TARGET MARKED FOR PURSUIT."},
    {"face_our_guns","FACE OUR GUNS."},
    {"advance_on_my_mark","ADVANCE ON MY MARK."},
    {"surrender_the_machine","SURRENDER THE MACHINE."},
    {"weapons_free","WEAPONS FREE."},
    {"fire_for_effect","FIRE FOR EFFECT."},
    {"transmission_lost","TRANSMISSION LOST."},
    {"meet_me_at_blade_range","MEET ME AT BLADE RANGE."},
    {"identify_yourself","IDENTIFY YOURSELF."},
    {"your_systems_are_outdated","YOUR SYSTEMS ARE OUTDATED."},
    {"your_orders_conceal_the_truth","YOUR ORDERS CONCEAL THE TRUTH."},
    {"power_down_and_surrender","POWER DOWN AND SURRENDER."},
    {"is_that_your_full_firepower","IS THAT YOUR FULL FIREPOWER?"},
    {"your_formation_has_a_weakness","YOUR FORMATION HAS A WEAKNESS."},
    {"only_one_squad_leaves","ONLY ONE SQUAD LEAVES."},
    {"our_line_will_hold","OUR LINE WILL HOLD."},
    {"no_retreat_permitted","NO RETREAT PERMITTED."},
  };return data;
}
std::span<const MissionChoice> techPrefixes(){
  static constexpr MissionChoice data[]={
    {"radiation","RADIATION"},
    {"adaptive","ADAPTIVE"},
    {"power","POWER"},
    {"auxiliary","AUXILIARY"},
    {"hydraulic","HYDRAULIC"},
    {"thermal","THERMAL"},
    {"nuclear","NUCLEAR"},
    {"fuel","FUEL"},
    {"bionic","BIONIC"},
    {"oscillating","OSCILLATING"},
    {"orbital","ORBITAL"},
    {"energy","ENERGY"},
  };return data;
}
std::span<const MissionChoice> techSuffixes(){
  static constexpr MissionChoice data[]={
    {"coupler","COUPLER"},
    {"magnetizer","MAGNETIZER"},
    {"compressor","COMPRESSOR"},
    {"motivator","MOTIVATOR"},
    {"readout","READOUT"},
    {"transformer","TRANSFORMER"},
    {"drive","DRIVE"},
    {"illuminator","ILLUMINATOR"},
    {"connector","CONNECTOR"},
    {"activator","ACTIVATOR"},
    {"conduit","CONDUIT"},
    {"pistons","PISTONS"},
  };return data;
}
std::span<const MissionChoice> criticalCockpitEffects(){
  static constexpr MissionChoice data[]={
    {"console_fire","CONSOLE FIRE"},
    {"reserve_sparks","RESERVE SPARKS"},
    {"viewport_vapor","VIEWPORT VAPOR"},
    {"arcing_cables","ARCING CABLES"},
    {"thruster_switch_failure","THRUSTER SWITCH FAILURE"},
    {"thrust_inversion","THRUST INVERSION"},
    {"comms_feedback","COMMS FEEDBACK"},
    {"cockpit_deformation","COCKPIT DEFORMATION"},
    {"alarm_cascade","ALARM CASCADE"},
    {"pilot_blackout","PILOT BLACKOUT"},
  };return data;
}
std::span<const MissionChoice> criticalExteriorEffects(){
  static constexpr MissionChoice data[]={
    {"limb_fracture","LIMB FRACTURE"},
    {"ammo_detonation","AMMO DETONATION"},
    {"system_puncture","SYSTEM PUNCTURE"},
    {"feed_mechanism_jam","FEED MECHANISM JAM"},
    {"burning_chassis","BURNING CHASSIS"},
    {"weapon_overheat","WEAPON OVERHEAT"},
    {"armor_separation","ARMOR SEPARATION"},
    {"thermal_overload","THERMAL OVERLOAD"},
    {"severed_limb","SEVERED LIMB"},
    {"cockpit_breach","COCKPIT BREACH"},
  };return data;
}
uint32_t manualRandom(uint32_t seed,ManualRng channel,unsigned index){
  uint32_t v=seed^(uint32_t(channel)+1)*0x9e3779b9u^(index+1)*0x85ebca6bu;
  v=(v^(v>>16))*0x7feb352du;v=(v^(v>>15))*0x846ca68bu;return v^(v>>16);
}
ManualAppearance manualAppearance(uint32_t seed,unsigned index,bool kaiju){
  Dice d{manualRandom(seed,ManualRng::Appearance,index)};ManualAppearance result;
  static constexpr Vec3 palette[]={{.85f,.23f,.20f},{.40f,.80f,.24f},{.25f,.52f,.95f},{.95f,.43f,.20f},
    {.54f,.28f,.78f},{.88f,.80f,.24f},{.90f,.43f,.67f},{.30f,.33f,.40f},{.57f,.61f,.64f},{.65f,.75f,.86f},{.85f,.88f,.92f}};
  unsigned paint=d.next()%12;result.paint=paint==11?int(d.next()%11):int(paint);
  if(paint==11)result.accent=int(d.next()%11);
  result.tint=palette[result.paint];if(result.accent>=0)result.tint=(result.tint+palette[result.accent])*.5f;
  unsigned feature=d.next()%unsigned(kaiju?6:12);unsigned direct=unsigned(kaiju?5:11);
  result.feature=feature==direct?int(d.next()%direct):int(feature);
  if(feature==direct)result.extraFeature=int(d.next()%direct);
  unsigned look=d.next()%6;result.look=look==5?int(d.next()%5):int(look);
  if(look==5)result.extraLook=int(d.next()%5);
  if(kaiju){result.nickname=kaijuNamePrefixes()[d.next()%12].name;
    result.nickname+=kaijuNameSuffixes()[d.next()%12].name;}
  return result;
}
std::string manualTechnobabble(uint32_t seed,unsigned index){
  Dice d{manualRandom(seed,ManualRng::Chatter,index)^0x874bc61du};
  return std::string(techPrefixes()[d.next()%12].name)+" "+techSuffixes()[d.next()%12].name;
}
void expandManualMission(MissionRecipe &r,Dice &){
  // Each subsystem owns a salted stream: cosmetic and briefing draws cannot
  // perturb encounters, loot, combat or one another when tables grow.
  Dice mission{manualRandom(r.seed,ManualRng::Mission)};
  std::vector<int> choices,local;
  const auto catalog=manualMissionCatalog();
  for(size_t i=0;i<catalog.size();++i){const auto &item=catalog[i];
    std::string_view id=item.id;
    if(id=="grand_theft_mariner"&&r.environment!=2)continue;
    if((id=="verglas"||id=="frozen_king")&&r.environment!=6)continue;
    if(id=="eruption_extinction"&&r.environment!=4)continue;
    if(std::find(item.objectives.begin(),item.objectives.end(),r.objective)==item.objectives.end())continue;
    choices.push_back(int(i));if(item.preferredLocation==r.location)local.push_back(int(i));}
  // Prefer a fitting physical site without making other operational adaptations
  // unreachable. The board keeps independently shuffled activity/site decks.
  const auto &pool=!local.empty()&&mission.next()%3?local:choices;
  if(pool.empty())return;
  r.manualMission=pool[mission.next()%pool.size()];const auto &item=catalog[r.manualMission];
  std::vector<int> variants;for(int i=0;i<3;++i)if(item.objectives[i]==r.objective)variants.push_back(i);
  r.manualVariant=variants[mission.next()%variants.size()];
  r.objectiveSeconds=float(item.hold);
  r.countdownSeconds=float(item.countdown);
  if(r.objective==3||r.objective==5)r.objectiveCount=3+int(mission.next()%3);
  else if(r.objective==4)r.objectiveCount=2+int(mission.next()%3);
  else if(r.objective==1||r.objective==2||r.objective==7||r.objective==8)r.objectiveCount=1;
  r.rechargeSeconds=18.f+float(mission.next()%4)*6.f;
  r.complication=int(manualRandom(r.seed,ManualRng::Complication)%6);
  r.opportunity=int(manualRandom(r.seed,ManualRng::Opportunity)%6);
  r.briefingStyle=int(manualRandom(r.seed,ManualRng::Briefing)%10);
  Dice planet{manualRandom(r.seed,ManualRng::Planetoid)};
  r.planetoidFeature=int(planet.next()%12);r.planetoidHazard=int(planet.next()%12);
  // Setting constraints win over a planet roll, as they do over insertion rolls.
  if(r.environment==2)r.planetoidFeature=3;
  else if(r.environment==0)r.planetoidFeature=10;
  else if(r.environment==4)r.planetoidFeature=1;
  else if(r.environment==5)r.planetoidFeature=11;
  else if(r.location==9)r.planetoidFeature=9;
  Dice instant{manualRandom(r.seed,ManualRng::Instant)};
  static constexpr unsigned actionObjectives[]={
    (1u<<0)|(1u<<6),(1u<<5)|(1u<<6)|(1u<<9),1u<<3,1u<<4,1u<<5,
    (1u<<2)|(1u<<3),1u<<2,(1u<<0)|(1u<<5),1u<<8,(1u<<3)|(1u<<7),
    (1u<<1)|(1u<<6),(1u<<2)|(1u<<4),1u<<7,1u<<3,(1u<<1)|(1u<<8),
    (1u<<3)|(1u<<4),1u<<3,1u<<4,(1u<<7)|(1u<<8),1u<<3};
  std::vector<int> actions;for(int i=0;i<20;++i)if(actionObjectives[i]&(1u<<r.objective))actions.push_back(i);
  r.instantAction=actions[instant.next()%actions.size()];r.instantFocus=int(instant.next()%20);
  if((r.instantAction==5||r.instantAction==11)&&r.countdownSeconds==0)r.countdownSeconds=360;
  if(r.countdownSeconds>0)r.countdownSeconds=std::max(r.countdownSeconds,120.f+50.f*r.objectiveCount+r.objectiveSeconds);
  Dice salvage{manualRandom(r.seed,ManualRng::Salvage)};
  r.salvageForm=int(salvage.next()%12);r.salvageFeature=int(salvage.next()%12);
  // Greater workload and time pressure have concrete contract compensation.
}
std::string manualMissionName(const MissionRecipe &r){
  if(r.storyArc>=0)return storyMissionTitle(r);
  if(r.finale)return r.boss>=0?std::string("FINALE: ")+campaignBoss(r.boss).spec.name:"FINALE: DECISIVE BATTLE";
  if(r.manualMission>=0&&size_t(r.manualMission)<manualMissionCatalog().size())return manualMissionCatalog()[r.manualMission].name;
  return missionObjectives()[r.objective].name;
}
std::string manualMissionTask(const MissionRecipe &r){
  if(r.finale)return r.boss>=0?std::string("DESTROY ")+campaignBoss(r.boss).spec.name+" TO END THE CAMPAIGN.":"DEFEAT THE FACTION FIELD ARMY IN A DECISIVE BATTLE.";
  if(r.manualMission<0||size_t(r.manualMission)>=manualMissionCatalog().size()||r.manualVariant<0||r.manualVariant>2)return {};
  const auto &mission=manualMissionCatalog()[r.manualMission];
  if(mission.objectives[r.manualVariant]!=r.objective)return {};
  return mission.tasks[r.manualVariant];
}
float objectiveDuration(const MissionRecipe &r,float fallback){return r.objectiveSeconds>0?r.objectiveSeconds:fallback;}
bool validManualRecipe(const MissionRecipe &r){
  if(!validStoryRecipe(r)||!std::isfinite(r.radius)||(r.radius!=0&&(r.radius<500||r.radius>1000))||r.depotCount< -1||r.depotCount>3)return false;
  auto valid=[](int value,size_t count){return value>=-1&&(value<0||size_t(value)<count);};
  if(!valid(r.manualMission,manualMissionCatalog().size())||r.manualVariant<0||r.manualVariant>2||
     !valid(r.complication,6)||!valid(r.opportunity,6)||!valid(r.briefingStyle,10)||!valid(r.planetoidFeature,12)||
     !valid(r.planetoidHazard,12)||!valid(r.instantAction,20)||!valid(r.instantFocus,20)||!valid(r.salvageForm,12)||!valid(r.salvageFeature,12))return false;
  if(r.campaignStage<1||r.campaignStage>10||r.employer< -1||r.employer>=voidmawFaction||r.adversary< -1||r.adversary>3||r.factionPressure<0||r.factionPressure>20||r.campaignIntel<0||r.campaignIntel>18||r.campaignSupply<0||r.campaignSupply>18||r.boss< -1||r.boss>=int(campaignBosses().size())||(r.boss>=0&&!r.finale)||(r.finaleBattle&&!r.finale))return false;
  if(r.finale&&(r.maps!=1||r.objective!=0||r.siteCount||((r.boss>=0)==r.finaleBattle)))return false;
  if(r.objectiveCount<0||r.objectiveCount>5)return false;
  for(float value:{r.objectiveSeconds,r.countdownSeconds,r.rechargeSeconds})if(!std::isfinite(value)||value<0||value>3600)return false;
  return r.manualMission<0||manualMissionCatalog()[r.manualMission].objectives[r.manualVariant]==r.objective;
}
}
