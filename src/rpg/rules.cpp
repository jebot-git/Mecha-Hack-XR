#include "rpg/rules.hpp"
#include "rpg/chassis_visual.hpp"
#include <stdexcept>
namespace iron::rpg {
const ChassisInfo &chassisInfo(Chassis c) {
  static constexpr ChassisInfo data[] = {{"BRAWLER", 8, 8, 6, 3},
                                         {"SCOUT", 6, 6, 6, 3},
                                         {"STRIKER", 8, 8, 8, 3},
                                         {"TITAN", 10, 6, 6, 5},
                                         {"AUXILIARY", 6, 6, 8, 3},
                                         {"COLOSSUS", 10, 6, 6, 5},
                                         {"HYBRID", 6, 10, 6, 5},
                                         {"VANGUARD", 6, 8, 8, 3}};
  static_assert(std::size(data)==size_t(Chassis::Count));
  int i = int(c);
  if (i < 0 || i >= int(Chassis::Count))
    throw std::runtime_error("Unknown chassis");
  return data[i];
}
const char *chassisId(Chassis c){
  static_assert(chassisIds.size()==size_t(Chassis::Count));
  return chassisIds.at(size_t(c));
}
const char *chassisPowerDescription(Chassis c){
  static constexpr const char *descriptions[]={
    "Close on an aimed target and strike.", "Cloak briefly and ambush.",
    "Vent your reactor or a nearby ally's.", "Raise a temporary shield.",
    "Reinforce an ally's next shot and share their reactor load.",
    "Toggle garrison for heavier hits and armor at reduced speed.",
    "Fire a maw beam. Overheating triggers a berserk reaction.",
    "Switch between weapon power and reactor cooling. An auxiliary limb steadies heavy weapons."};
  static_assert(std::size(descriptions)==size_t(Chassis::Count));
  chassisInfo(c);return descriptions[int(c)];
}
const char *pilotId(PilotKind p){
  static constexpr std::array ids={"commander","engineer","maverick","quipster"};
  static_assert(ids.size()==playablePilotCount);return ids.at(size_t(playablePilot(p)));
}
PilotKind playablePilot(PilotKind p){
  switch(p){
  case PilotKind::Bionic:return PilotKind::Engineer;
  case PilotKind::Merc:return PilotKind::Maverick;
  case PilotKind::Vet:return PilotKind::Commander;
  case PilotKind::Pariah:return PilotKind::Quipster;
  default:
    if(int(p)<0||int(p)>=playablePilotCount)throw std::runtime_error("Unknown pilot");
    return p;
  }
}
int pilotBonusChoices(PilotKind p){return p==PilotKind::Vet?2:p==PilotKind::Pariah?3:0;}
const char *pilotName(PilotKind p) {
  static constexpr std::array names={"COMMANDER","ENGINEER","MAVERICK","QUIPSTER"};
  return names.at(size_t(playablePilot(p)));
}
const char *pilotRoleDescription(PilotKind pilot) {
  static constexpr const char *descriptions[]={
    "Tighter weapon spread and shorter stuns.",
    "Faster movement and shorter sensor-blind effects.",
    "Faster movement and tighter weapon spread.",
    "Shorter sensor-blind effects and stuns."};
  static_assert(std::size(descriptions)==playablePilotCount);
  return descriptions[int(playablePilot(pilot))];
}
const char *moduleId(Module m) {
  static const char *ids[] = {
      "rocket_fists",         "jump_jets",          "targeting_array",
      "nano_shroud",          "shoulder_hardpoint", "super_reactor",
      "chest_beam",           "grappler",           "missile_barrage",
      "quake_generator",      "siege_mode",         "static_cloud",
      "barricade_mode",       "exothermic_plating", "particle_barrier",
      "reflective_shielding", "retractable_shield", "booster_sprint",
      "frag_mines",           "sentry_turret",      "vehicle_mode",
      "dismantler_arms", "energy_modulator", "funnel_drones", "geist_system",
      "glide_thrusters", "long_range_scanners", "micro_missiles", "micro_reactor",
      "piston_fists", "precog_system", "shield_nodes", "lodium_alloy", "veriform_frame"};
  static_assert(std::size(ids)==size_t(Module::Count));
  int i = int(m);
  if (i < 0 || i >= int(Module::Count))
    throw std::runtime_error("Unknown module");
  return ids[i];
}
const char *moduleName(Module m) {
  static const char *names[] = {
      "Rocket Fists",         "Jump Jets",          "Targeting Array",
      "Nano Shroud",          "Shoulder Hardpoint", "Super Reactor",
      "Chest Beam",           "Grappler",           "Missile Barrage",
      "Quake Generator",      "Siege Mode",         "Static Cloud",
      "Barricade Mode",       "Exothermic Plating", "Particle Barrier",
      "Reflective Shielding", "Retractable Shield", "Booster Sprint",
      "Frag Mines",           "Sentry Turret",      "Vehicle Mode",
      "Dismantler Arms", "Energy Modulator", "Funnel Drones", "GEIST System",
      "Glide Thrusters", "Long-Range Scanners", "Homing Missile", "Micro-Reactor",
      "Piston Fists", "PRECOG System", "Shield Nodes", "Lodium Alloy", "Veriform Frame"};
  static_assert(std::size(names)==size_t(Module::Count));
  moduleId(m);
  return names[int(m)];
}
const char *qualityName(Quality q) {
  static const char *names[] = {"None",      "Arc",    "Chainblade",
                                "Corrosive", "Deadly", "Plasma",
                                "Quick",     "Sniper", "Thrown",
      "Artillery", "Automated", "Beam", "Buster", "Belt-Fed", "Bouncing",
      "Dual", "Sawed-Off", "Scrap-Fed", "Lodium", "Thermal", "Unstable"};
  static_assert(std::size(names)==size_t(Quality::Count));
  if (int(q) < 0 || q >= Quality::Count)
    throw std::runtime_error("Unknown quality");
  return names[int(q)];
}
const char* moduleDescription(Module module){
  static const char* text[]={"Increase unarmed impact damage.","Jump to a far clear location; generates heat.","Improve the next attack; generates heat.","Restore 1-4 armor points; generates heat.","Arm stabilizer: reduce handheld angular spread.","Increase reactor capacity.","Close beam: 1-8 damage and push; generates heat.","Pull a near enemy close for 1-4 damage; generates heat.","Fire four short-range missiles; generates heat.","Damage and push two close enemies; generates heat.","Immobile ranged damage +2; generates heat on activation.","Shock two close enemies and hide; generates heat.","Immobile; halve damage and cover nearby allies.","Retaliate against close attackers; generates heat.","Deploy or dismiss one physical cover barrier.","Retaliate against ranged attackers; generates heat.","Integrated energy and weather shield that recharges after depletion.","Double movement briefly; generates heat.","Plant a mine; next activation detonates it.","Deploy an automated sentry; ammo is limited.","Transform to a joystick-driven ground skimmer.",
    "Close kills recover armor equal to target strength; generates heat.",
    "Increase chassis weapon damage.",
    "Deploy drones for periodic near-target damage; reactor upkeep.",
    "Use Presence to protect the reactor; failure hurts the chassis.",
    "Short lateral glide while acting; reactor-limited cooldown.",
    "Toggle covert radar pulses and extend module range. The pulses do not reveal your position.",
    "Launch a 520 m missile that homes on an aimed hostile; generates heat.",
    "Compact reactor with improved heat recovery.",
    "Unarmed impacts trigger an extra strike with +2 damage; reactor cost.",
    "Mark one near target to improve tracking and attacks; generates heat.",
    "Refresh energy and weather shielding for nearby allies; reactor upkeep.",
    "Increase maximum armor by 2 AP.",
    "Transform for faster movement over difficult terrain; generates heat."};
  static_assert(std::size(text)==size_t(Module::Count));
  moduleId(module);return text[int(module)];
}
const GearInfo &gearInfo(Gear g) {
  static const GearInfo data[] = {
      {"empty", "Empty", 0, 0, 0, 0},
      {"blade", "Arc Saber", 20, .7f, .14f, 10},
      {"rifle", "Service Rifle", 40, .45f, .22f, 600},
      {"cannon", "Siege Cannon", 100, 1.4f, .48f, 900},
      {"repair", "Repair Nanites", 10, 5, .2f, 10},
      {"jump_jets", "Jump Jets", 0, 8, .5f, 60, Module::JumpJets},
      {"targeting_array", "Targeting Array", 0, 4, .25f, 0,
       Module::TargetingArray},
      {"nano_shroud", "Nano Shroud", 0, 6, .5f, 0, Module::NanoShroud},
      {"scan", "Sensor Scan", 0, 5, .2f, 900},
      {"heavy_blade", "Mass Blade", 50, 1.2f, .3f, 12},
      {"shield", "Shield", 25, 0, 0, 0},
      {"comlink", "Comlink", 10, 0, 0, 0},
      {"boost_kit", "Boost Kit", 18, 6, 0, 0},
      {"explosive_rounds", "High-Pressure Ammo", 15, 5, 0, 0},
      {"arc_grenade", "Arc Strike Drone", 22, 2, .2f, 60},
      {"frag_grenade", "Frag Strike Drone", 18, 2, .2f, 60},
      {"one_shot_module", "One-Shot Module", 125, 4, 0, 30},
      {"scouting_drone", "Scouting Drone", 80, 5, .1f, 900},
      {"sensor_jammer", "Sensor Jammer", 50, 5, 0, 30},
      {"chest_beam", "Chest Beam", 0, 4, .3f, 10, Module::ChestBeam},
      {"grappler", "Grappler", 0, 5, .3f, 30, Module::Grappler},
      {"missile_barrage", "Missile Barrage", 0, 6, .5f, 90,
       Module::MissileBarrage},
      {"quake_generator", "Quake Generator", 0, 6, .4f, 10,
       Module::QuakeGenerator},
      {"siege_mode", "Siege Mode", 0, 1, .1f, 0, Module::SiegeMode},
      {"static_cloud", "Static Cloud", 0, 6, .4f, 10, Module::StaticCloud},
      {"barricade_mode", "Barricade Mode", 0, 1, .1f, 0, Module::BarricadeMode},
      {"particle_barrier", "Particle Barrier", 0, 5, .3f, 10,
       Module::ParticleBarrier},
      {"booster_sprint", "Booster Sprint", 0, 6, .3f, 0, Module::BoosterSprint},
      {"frag_mines", "Frag Mines", 0, 3, .3f, 10, Module::FragMines},
      {"sentry_turret", "Sentry Turret", 0, 3, .3f, 30, Module::SentryTurret},
      {"vehicle_mode", "Vehicle Mode", 0, 1, .1f, 0, Module::VehicleMode},
      {"funnel_drones", "Funnel Drones", 0, 1, .3f, 30, Module::FunnelDrones},
      {"glide_thrusters", "Glide Thrusters", 0, 4, .3f, 30, Module::GlideThrusters},
      {"long_range_scanners", "Long-Range Scanners", 0, 6, .3f, 90, Module::LongRangeScanners},
      {"micro_missiles", "Homing Missile", 0, 4, .35f, 520, Module::MicroMissiles},
      {"precog_system", "PRECOG System", 0, 4, .3f, 30, Module::PrecogSystem},
      {"shield_nodes", "Shield Nodes", 0, 1, .3f, 30, Module::ShieldNodes},
      {"veriform_frame", "Veriform Frame", 0, 1, .3f, 0, Module::VeriformFrame},
      {"fusion_grenade", "Fusion Grenade", 75, 3, 0, 30},
      {"scrap", "Scrap", 0, 3, 0, 0},
      {"verilian_battery", "Verilian Battery", 12, 1, 0, 0},
      {"holoprojector", "Holoprojector", 50, 5, .05f, 30},
      {"camera_drone", "Camera Drone", 50, 5, .05f, 900},
      {"welding_beam", "Welding Beam", 50, .5f, .1f, 10},
      {"cockpit_trinket", "Cockpit Trinket", 25, 0, 0, 0},
      {"rear_thrusters", "Rear Thrusters", 50, 4, .05f, 0},
      {"air_compressor", "Air Compressor", 25, 4, 0, 10},
      {"utility_knife", "Utility Knife", 25, .7f, 0, 10},
      {"mecha_manacle", "Mecha Manacle", 50, 4, .05f, 10},
      {"cable_winch", "Cable and Winch", 50, 4, .05f, 30},
      {"foot_treads", "Foot Treads", 50, 4, .05f, 0},
      {"wrist_drill", "Wrist Drill", 50, .8f, .05f, 10},
      {"squadron_emblem", "Squadron Emblem", 25, 0, 0, 0},
      {"fusionic_runner_lights", "Fusionic Runner Lights", 25, 1, 0, 0},
      {"galvanic_voidglass", "Galvanic Voidglass", 50, 0, 0, 0},
      {"on_board_ai", "On-Board A.I.", 50, 5, 0, 900},
      {"chrome_plating", "Chrome Plating", 25, 0, 0, 0},
      {"sonisphere_amplifiers", "Sonisphere Amplifiers", 25, 3, 0, 90},
      {"banshee_fenders", "Banshee Fenders", 25, 3, 0, 30},
      {"infamous_part", "Infamous Part", 25, 0, 0, 0},
      {"cargo_hullpack", "Cargo Hullpack", 50, 3, 0, 10},
      {"cargo_spider", "Cargo Spider", 75, 5, .05f, 30},
      {"chain_cutter", "Chain Cutter", 65, 0.56f, 0.18f, 8},
      {"scattergun", "Scattergun", 80, 0.9f, 0.3f, 120},
      {"rotary", "Rotary Gun", 105, 0.17f, 0.14f, 415},
      {"grenade_lobber", "Grenade Lobber", 105, 1.5f, 0.39f, 350},
      {"rocket_rack", "Rocket Rack", 115, 1.65f, 0.48f, 480},
      {"seeker_pod", "Seeker Pod", 125, 1.75f, 0.44f, 510},
      {"pulse_carbine", "Pulse Carbine", 90, 0.5f, 0.26f, 405},
      {"rail_lance", "Rail Lance", 145, 1.75f, 0.53f, 850},
      {"blade_uncommon", "Arc Saber Duelist", 27, 0.581f, 0.115f, 11},
      {"blade_rare", "Arc Saber Overdrive", 36, 0.791f, 0.158f, 11},
      {"rifle_uncommon", "Service Rifle Marksman", 54, 0.373f, 0.18f, 642},
      {"rifle_rare", "Service Rifle Storm", 72, 0.508f, 0.249f, 672},
      {"cannon_uncommon", "Siege Cannon Breach", 135, 1.162f, 0.394f, 963},
      {"cannon_rare", "Siege Cannon Longbow", 180, 1.582f, 0.542f, 1008},
      {"heavy_blade_uncommon", "Mass Blade Cleaver", 68, 0.996f, 0.246f, 13},
      {"heavy_blade_rare", "Mass Blade Judgement", 90, 1.356f, 0.339f, 13},
      {"chain_cutter_uncommon", "Chain Cutter Ripper", 88, 0.465f, 0.148f, 9},
      {"chain_cutter_rare", "Chain Cutter Ravager", 117, 0.633f, 0.203f, 9},
      {"scattergun_uncommon", "Scattergun Choke", 108, 0.747f, 0.246f, 128},
      {"scattergun_rare", "Scattergun Breachfire", 144, 1.017f, 0.339f, 134},
      {"rotary_uncommon", "Rotary Gun Ratchet", 142, 0.141f, 0.115f, 444},
      {"rotary_rare", "Rotary Gun Cyclone", 189, 0.192f, 0.158f, 465},
      {"grenade_lobber_uncommon", "Grenade Lobber Demolition", 142, 1.245f, 0.32f, 374},
      {"grenade_lobber_rare", "Grenade Lobber Rainmaker", 189, 1.695f, 0.441f, 392},
      {"rocket_rack_uncommon", "Rocket Rack Barrage", 155, 1.369f, 0.394f, 514},
      {"rocket_rack_rare", "Rocket Rack Firestorm", 207, 1.864f, 0.542f, 538},
      {"seeker_pod_uncommon", "Seeker Pod Interceptor", 169, 1.452f, 0.361f, 546},
      {"seeker_pod_rare", "Seeker Pod Swarm", 225, 1.977f, 0.497f, 571},
      {"pulse_carbine_uncommon", "Pulse Carbine Capacitor", 122, 0.415f, 0.213f, 433},
      {"pulse_carbine_rare", "Pulse Carbine Starfire", 162, 0.565f, 0.294f, 454},
      {"rail_lance_uncommon", "Rail Lance Needle", 196, 1.452f, 0.435f, 910},
      {"rail_lance_rare", "Rail Lance Thunderhead", 261, 1.977f, 0.599f, 952}};
  static_assert(std::size(data)==size_t(Gear::Count));
  if (int(g) < 0 || g >= Gear::Count)
    throw std::runtime_error("Unknown equipment");
  return data[int(g)];
}
const char *gearName(Gear g) { return gearInfo(g).name; }
Gear moduleGear(Module m) {
  for (int g = 0; g < int(Gear::Count); ++g)
    if (gearInfo(Gear(g)).module == m)
      return Gear(g);
  return Gear::Empty;
}
Module moduleRead(const std::string &id) {
  for (int i = 0; i < int(Module::Count); ++i)
    if (id == moduleId(Module(i)))
      return Module(i);
  throw std::runtime_error("Unknown module: " + id);
}
Gear gearRead(const std::string &id) {
  for (int i = 0; i < int(Gear::Count); ++i)
    if (id == gearInfo(Gear(i)).id)
      return Gear(i);
  throw std::runtime_error("Unknown equipment: " + id);
}
namespace {
constexpr std::array arsenalFamilies = {Gear::Blade, Gear::Rifle, Gear::Cannon, Gear::HeavyBlade, Gear::ChainCutter, Gear::Scattergun, Gear::Rotary, Gear::GrenadeLobber, Gear::RocketRack, Gear::SeekerPod, Gear::PulseCarbine, Gear::RailLance};
constexpr int variantStart=int(Gear::BladeUncommon);
}
std::span<const Gear> weaponFamilies(){return arsenalFamilies;}
Gear weaponFamily(Gear gear){
  const int offset=int(gear)-variantStart;
  return offset>=0&&gear<Gear::Count?arsenalFamilies[size_t(offset/2)]:gear;
}
Rarity weaponRarity(Gear gear){
  return gear>=Gear::BladeUncommon&&gear<Gear::Count?
      Rarity(1+(int(gear)-variantStart)%2):Rarity::Common;
}
const char *rarityName(Rarity rarity){
  switch(rarity){
  case Rarity::Uncommon:return "UNCOMMON";
  case Rarity::Rare:return "RARE";
  default:return "COMMON";
  }
}
Gear weaponVariant(Gear family,Rarity rarity){
  family=weaponFamily(family);
  const auto match=std::find(arsenalFamilies.begin(),arsenalFamilies.end(),family);
  if(match==arsenalFamilies.end()||rarity==Rarity::Common)return family;
  return Gear(variantStart+2*int(match-arsenalFamilies.begin())+int(rarity)-1);
}
const WeaponTuning *weaponTuning(Gear gear){
  static constexpr WeaponTuning base[]={
    {Weapon::Cutter, AttackPattern::Melee, 0, 1, 0, 0, 0, 0, 0, {0, 0.1f, -1.65f}},
    {Weapon::Rifle, AttackPattern::Single, 0, 1, 180, 0.003f, 0, 0, 0, {0, 0.1f, -2.4f}},
    {Weapon::HeavyMachinegun, AttackPattern::Single, 0, 1, 140, 0.002f, 0, 0, 0, {0, 0.18f, -2.85f}},
    {Weapon::Cutter, AttackPattern::Melee, 1, 1, 0, 0, 0, 0, 0, {0, 0.2f, -2.35f}},
    {Weapon::Cutter, AttackPattern::Melee, -1, 1, 0, 0, 0, 0, 0, {0, 0.11f, -1.8f}},
    {Weapon::Shotgun, AttackPattern::Scatter, -1, 5, 150, 0.032f, 0, 0, 0, {0, 0.13f, -1.8f}},
    {Weapon::Gatling, AttackPattern::Single, -3, 1, 182, 0.008f, 0, 0, 0, {0, 0.19f, -2.1f}},
    {Weapon::AutoGrenade, AttackPattern::Lob, 0, 1, 52, 0.005f, 8, 18, 0, {0, 0.2f, -1.55f}},
    {Weapon::Rocket, AttackPattern::Rocket, 1, 1, 88, 0.007f, 6, 0, 0, {0, 0.22f, -1.24f}},
    {Weapon::Homing, AttackPattern::Seek, 0, 1, 94, 0.002f, 4, 0, 0, {0, 0.26f, -1.25f}},
    {Weapon::Rail, AttackPattern::Pulse, 0, 1, 147, 0.004f, 0, 0, 0, {0, 0.13f, -2.04f}},
    {Weapon::Rail, AttackPattern::Charge, 3, 1, 285, 0.0009f, 0, 0, 0.52f, {0, 0.21f, -2.95f}}};
  static_assert(std::size(base)==arsenalFamilies.size());
  static const auto tuned=[]{
    std::array<std::array<WeaponTuning,3>,arsenalFamilies.size()> values{};
    for(size_t i=0;i<values.size();++i){
      values[i]={base[i],base[i],base[i]};
      auto &uncommon=values[i][1],&rare=values[i][2];
      uncommon.dispersion*=.73f;uncommon.velocity*=1.10f;
      rare.damageBias+=2;rare.dispersion*=1.12f;
      switch(arsenalFamilies[i]){
      case Gear::Rifle:case Gear::PulseCarbine:
        rare.pattern=AttackPattern::Burst;rare.projectiles=3;rare.damageBias-=3;break;
      case Gear::Scattergun:
        rare.projectiles=7;rare.dispersion*=1.2f;break;
      case Gear::RocketRack:case Gear::SeekerPod:case Gear::GrenadeLobber:
        rare.projectiles=3;rare.damageBias-=2;rare.blast*=.67f;break;
      case Gear::RailLance:rare.windup=.85f;rare.velocity*=1.23f;break;
      case Gear::ChainCutter:rare.damageBias+=1;break;
      case Gear::Rotary:rare.pattern=AttackPattern::Burst;rare.projectiles=2;rare.damageBias-=3;break;
      default:break;
      }
    }
    return values;
  }();
  const auto family=weaponFamily(gear);
  const auto match=std::find(arsenalFamilies.begin(),arsenalFamilies.end(),family);
  return match==arsenalFamilies.end()?nullptr:&tuned[size_t(match-arsenalFamilies.begin())][size_t(weaponRarity(gear))];
}
bool ranged(Gear gear){const auto *tuning=weaponTuning(gear);return tuning&&tuning->pattern!=AttackPattern::Melee;}
bool melee(Gear gear){const auto *tuning=weaponTuning(gear);return tuning&&tuning->pattern==AttackPattern::Melee;}
bool heavy(Gear gear){
  switch(weaponFamily(gear)){
  case Gear::HeavyBlade:case Gear::Cannon:case Gear::Rotary:case Gear::GrenadeLobber:
  case Gear::RocketRack:case Gear::SeekerPod:case Gear::RailLance:return true;
  default:return false;
  }
}
bool retiredGear(Gear gear){
  switch(gear){
  case Gear::Holoprojector:case Gear::CameraDrone:case Gear::WeldingBeam:
  case Gear::CockpitTrinket:case Gear::AirCompressor:case Gear::UtilityKnife:
  case Gear::CableWinch:case Gear::FootTreads:case Gear::WristDrill:case Gear::SquadronEmblem:
  case Gear::FusionicRunnerLights:case Gear::GalvanicVoidglass:case Gear::OnBoardAI:
  case Gear::ChromePlating:case Gear::SonisphereAmplifiers:case Gear::BansheeFenders:
  case Gear::InfamousPart:case Gear::CargoHullpack:case Gear::CargoSpider:
  case Gear::BoosterSprint:case Gear::GlideThrusters:return true;
  case Gear::FusionGrenade:case Gear::OneShotModule:case Gear::SensorJammer:case Gear::ScoutingDrone:
  case Gear::JumpJets:case Gear::RearThrusters:case Gear::TargetingArray:case Gear::SentryTurret:
  case Gear::FragMines:case Gear::VeriformFrame:return true;
  default:return false;
  }
}
bool retiredModule(Module module){
  return module==Module::GeistSystem||module==Module::BoosterSprint||module==Module::GlideThrusters||
      module==Module::JumpJets||module==Module::LodiumAlloy||module==Module::TargetingArray||
      module==Module::SentryTurret||module==Module::FragMines||module==Module::VeriformFrame;
}
bool frameUpgrade(Module module){
  switch(module){
  case Module::RocketFists:case Module::ShoulderHardpoint:case Module::SuperReactor:
  case Module::ExothermicPlating:case Module::ReflectiveShielding:case Module::RetractableShield:
  case Module::DismantlerArms:case Module::EnergyModulator:case Module::MicroReactor:
  case Module::PistonFists:return true;
  default:return false;
  }
}
int fittedModuleCount(const Sheet &sheet){
  return int(std::count_if(sheet.modules.begin(),sheet.modules.end(),[](Module module){return !frameUpgrade(module);}));
}
bool chassisArmorAllowed(Chassis c,Armor armor){
  return armor!=Armor::Heavy||(c!=Chassis::Auxiliary&&c!=Chassis::Vanguard);
}
bool pairedWeapon(Chassis c,Gear gear){return heavy(gear)&&c!=Chassis::Vanguard;}
bool chassisGearAllowed(Chassis c,Gear gear){
  return c!=Chassis::Hybrid||(gear!=Gear::Shield&&(!(melee(gear)||ranged(gear))||heavy(gear)));
}
bool mountAllowed(Gear g,int slot){
  if(slot<0||slot>=MountCount||g==Gear::Comlink||g==Gear::Scrap||retiredGear(g))return false;
  if(g==Gear::Empty)return true;
  return (ranged(g)||melee(g)||g==Gear::Shield)?slot<LeftShoulder:slot>=LeftShoulder;
}
bool targetedGear(Gear g,Module oneShot){
  if(g==Gear::OneShotModule)g=moduleGear(oneShot);
  return ranged(g)||melee(g)||g==Gear::ChestBeam||g==Gear::Grappler||g==Gear::MissileBarrage||g==Gear::JumpJets||g==Gear::Repair||g==Gear::ArcGrenade||g==Gear::FragGrenade||g==Gear::FusionGrenade||g==Gear::MicroMissiles||g==Gear::PrecogSystem||g==Gear::WeldingBeam||g==Gear::UtilityKnife||g==Gear::WristDrill||g==Gear::MechaManacle||g==Gear::CableWinch||g==Gear::SentryTurret||g==Gear::ParticleBarrier;
}
bool consumable(Gear g) {
  return g==Gear::Repair||g==Gear::BoostKit||g==Gear::ExplosiveRounds||g==Gear::ArcGrenade||
      g==Gear::FragGrenade||g==Gear::VerilianBattery;
}
bool gadget(Gear g){return g>=Gear::Holoprojector&&g<=Gear::CargoSpider;}
bool manualGear(Gear g){return g>=Gear::FunnelDrones&&g<=Gear::CargoSpider;}
bool manualModule(Module m){return m>=Module::DismantlerArms&&m<Module::Count;}
bool qualityAllowed(Gear g,Quality q){
  if(int(q)<0||q>=Quality::Count)return false;
  if(q==Quality::None)return true;
  if(!melee(g)&&!ranged(g))return false;
  switch(q){
  case Quality::Chainblade:case Quality::Lodium:case Quality::Thermal:return melee(g);
  case Quality::Sniper:case Quality::Beam:case Quality::SawedOff:case Quality::ScrapFed:return ranged(g);
  case Quality::Thrown:return weaponFamily(g)==Gear::Blade;
  case Quality::Quick:case Quality::Dual:return !heavy(g);
  case Quality::Artillery:case Quality::BeltFed:return heavy(g)&&ranged(g);
  case Quality::Buster:return heavy(g);
  case Quality::Bouncing:return ranged(g)&&!heavy(g);
  default:return true;
  }
}
const char *gearVisualId(Gear g){
  gearInfo(g);
  if(g==Gear::ArcGrenade||g==Gear::FragGrenade)return "weapon_scouting_drone";
  static const auto ids=[] {std::array<std::string,size_t(Gear::Count)> out;
    for(size_t n=0;n<out.size();++n)out[n]=std::string(weaponTuning(Gear(n))?"arsenal_":"weapon_")+gearInfo(Gear(n)).id;
    return out;}();
  return ids[size_t(g)].c_str();
}
int manualSourcePage(Module m){return manualModule(m)?(m<=Module::MicroReactor?81:82):0;}
int manualSourcePage(Quality q){return q>=Quality::Artillery&&q<Quality::Count?83:0;}
int manualSourcePage(Gear g){
  if(!manualGear(g))return 0;
  if(gadget(g))return 85;
  if(g>=Gear::FusionGrenade&&g<=Gear::VerilianBattery)return 84;
  return manualSourcePage(gearInfo(g).module);
}
const char *qualityDescription(Quality q){
  qualityName(q);
  static const char *text[]={
    "Long-range heavy fire adds 1-4 damage; close fire has greater aim deviation.",
    "An independent fire controller sustains the weapon while its trigger is held.",
    "Ranged impacts deal one additional damage.",
    "Aimed heavy fire charges reactor energy for a precise +4 damage shot.",
    "Sustained heavy fire repeats while the target remains exposed; feed failures require reload time.",
    "A light ranged near miss can ricochet for unarmed damage at reactor cost.",
    "Matched pair: +1 damage per gun, +2 per paired volley.",
    "Close and near shots deal +1; far-range aim deviation is increased.",
    "Consume scrap, or one AP, to add three damage to a ranged impact.",
    "Exceptional melee impacts restore one armor point.",
    "Melee impacts deal one additional damage.",
    "Critical impacts deal extra damage; misfires damage the wielder."};
  return q>=Quality::Artillery?text[int(q)-int(Quality::Artillery)]:"Standard weapon upgrade.";
}
const char *gearDescription(Gear g){
  const auto &info=gearInfo(g);
  if(g==Gear::Scan)return "Toggle radar pulses. Each pulse briefly exposes your position.";
  if(g==Gear::Repair)return "Repairs hull damage. Automatically spends a charge at critical hull.";
  if(g==Gear::Shield)return "Wide melee parry when swung across an incoming strike.";
  if(g==Gear::ExplosiveRounds)return "Adds ranged damage for the operation. Load once per sortie.";
  if(g==Gear::ArcGrenade||g==Gear::FragGrenade)return "Look to steer. Right trigger detonates.";
  if(info.module!=Module::Count)return moduleDescription(info.module);
  if(const auto *tuning=weaponTuning(g)){
    switch(tuning->pattern){
    case AttackPattern::Melee:return "Close-range strikes, parries and alternating combos.";
    case AttackPattern::Burst:return "A timed burst follows your aim through the entire volley.";
    case AttackPattern::Scatter:return "Wide pellet spread for close-range engagements.";
    case AttackPattern::Lob:return "Arcing shells can reach positions behind cover.";
    case AttackPattern::Rocket:return "Direct-fire rockets with an explosive impact.";
    case AttackPattern::Seek:return "Missiles track the enemy under the reticle at launch.";
    case AttackPattern::Pulse:return "Rapid energy impacts bypass ordinary armor.";
    case AttackPattern::Charge:return "Hold the trigger through the charge before firing.";
    default:return "Direct-fire ballistic rounds.";
    }
  }
  if(!manualGear(g))return info.name;
  static const char *text[]={
    "Fusion burst: 2-12 damage to a nearby target and up to two close neighbors.",
    "Spend one to restore 2 AP; craft equipment worth 25 credits per scrap in preparation.",
    "Load a reserve battery to absorb reactor heat until depleted.",
    "Project a nearby hull-sized hologram to distract hostile sensors.",
    "Observe the surrounding area using a remote sensor camera.",
    "Cut destructible material or weld an allied chassis at close range.",
    "A personal keepsake retained with this saved mecha.",
    "Rapidly recover from a disabled stance using rear-mounted thrust.",
    "Clear debris and visual interference from the chassis.",
    "A short-range cutting and prying tool.",
    "Immobilize a disabled or unpiloted nearby mecha.",
    "Pull nearby salvage or a disabled target with a cable.",
    "Build momentum from rest using retractable treads.",
    "Drill through debris to recover nearby scrap.",
    "A saved squadron insignia for this chassis.",
    "Switch hull-mounted running lights and external illumination.",
    "Transparent cockpit armor finish retained with this chassis.",
    "An integrated sensor assistant for nearby scan information.",
    "A reflective hull finish retained with this chassis.",
    "Project external comms and a sensor-decoy sound.",
    "Emit a moving acoustic distraction for nearby enemies.",
    "A recognizable salvaged component retained with this chassis.",
    "Carry and recover nearby salvage using hull-mounted storage.",
    "Deploy an automated salvage carrier with a local sensor link."};
  return text[int(g)-int(Gear::FusionGrenade)];
}
uint32_t Dice::next() {
  state += 0x9e3779b9u;
  uint32_t n = state;
  n = (n ^ (n >> 16)) * 0x85ebca6bu;
  n = (n ^ (n >> 13)) * 0xc2b2ae35u;
  return n ^ (n >> 16);
}
int Dice::roll(int sides) {
  if (sides < 2 || sides > 20)
    throw std::runtime_error("Unsupported die");
  uint32_t limit = UINT32_MAX - UINT32_MAX % uint32_t(sides), n;
  do {
    n = next();
  } while (n >= limit);
  return 1 + int(n % uint32_t(sides));
}
Test test(Dice &d, int attr, int modifier, int advantage) {
  int n = d.roll(20);
  if (advantage) {
    int other = d.roll(20);
    n = advantage > 0 ? std::min(n, other) : std::max(n, other);
  }
  return {n, n + modifier, n + modifier < attr, n == 1};
}
Test test(Dice &d,const Sheet &sheet,int attribute,uint32_t injuries,bool attack,int modifier,int advantage) {
  if(attribute<0||attribute>=4)throw std::runtime_error("Invalid test attribute");
  if(injuryDisadvantage(injuries,attribute,attack))advantage=advantage>0?0:-1;
  return test(d,sheet.attributes[size_t(attribute)],modifier,advantage);
}
bool Sheet::has(Module m) const {
  return std::find(modules.begin(), modules.end(), m) != modules.end();
}
static int smaller(int n) {
  switch (n) {
  case 20:
    return 12;
  case 12:
    return 10;
  case 10:
    return 8;
  case 8:
    return 6;
  default:
    return 4;
  }
}
static int larger(int n) {
  switch (n) {
  case 4:
    return 6;
  case 6:
    return 8;
  case 8:
    return 10;
  case 10:
    return 12;
  default:
    return 20;
  }
}
void addReactorHeat(float &heat,bool &overheated,float &vent,float amount){
  heat=std::clamp(heat+std::max(0.f,amount),0.f,1.f);
  if(heat>=1.f&&!overheated){
    overheated=true;
    vent=1.15f;
  }
}
void stepReactorHeat(float &heat,bool &overheated,float &vent,float dt,float cooling){
  heat=std::max(0.f,heat-dt*(overheated?.38f:cooling));
  vent=std::max(0.f,vent-dt);
  if(overheated&&vent==0&&heat<=.56f)overheated=false;
}
void Sheet::reactorTest(Dice &d) {
  stress(d,.16f);
  if(chassis==Chassis::Colossus&&d.roll(std::max(4,maxReactor))==maxReactor)kineticCharge=true;
}
void Sheet::stress(Dice &, float amount) {
  const int coolingDie=std::clamp(maxReactor-2*powerRouting,4,12);
  float efficiency=std::clamp(9.f/float(coolingDie),.62f,1.45f);
  if(has(Module::MicroReactor))efficiency*=.8f;
  if(batteryDie>0&&amount>=.10f&&reactorStress>.24f){
    --batteryDie;
    efficiency*=.45f;
  }
  addReactorHeat(reactorStress,overheated,ventRemaining,amount*efficiency);
}
void Sheet::stepThermal(float dt) {
  const float mobilityCooling=chassis==Chassis::Scout?.24f:chassis==Chassis::Titan?.14f:.18f;
  const float cooling=mobilityCooling+(has(Module::MicroReactor)?.08f:0.f);
  stepReactorHeat(reactorStress,overheated,ventRemaining,dt,cooling);
}
void Sheet::coolant(float amount){
  reactorStress=std::max(0.f,reactorStress-amount);
  if(reactorStress<=.36f){overheated=false;ventRemaining=0;}
}
int Sheet::damage(int amount) {
  amount = std::max(0, amount);
  int armor = std::min(ap, amount);
  ap -= armor;
  int loss = std::min(hp, amount - armor);
  hp -= loss;
  return loss;
}
void Sheet::rest(Dice &d, bool full) {
  ap = maxAp;
  hp = full ? maxHp : std::min(maxHp, hp + d.roll(chassisInfo(chassis).hit));
  reactor = maxReactor;
  overheated = false;
  reactorStress = ventRemaining = 0;
  targetLock = hidden = false;
}
void normalizeCombatProfile(Sheet &s){
  validateRefinements(s);
  const auto &info=chassisInfo(s.chassis);
  s.pilot=playablePilot(s.pilot);
  s.pilotBonusChoice=0;
  static constexpr int characteristics[][4]={{15,11,9,11},{10,16,12,10},
                                              {13,12,13,11},{14,8,12,12},
                                              {10,11,14,13},{15,8,10,14},
                                              {14,14,9,10},{14,11,14,9}};
  static_assert(std::size(characteristics)==size_t(Chassis::Count));
  static constexpr int bonuses[][4]={{2,0,0,1},{0,1,2,0},{1,2,0,0},{0,0,1,2}};
  static_assert(std::size(bonuses)==playablePilotCount);
  for(int i=0;i<4;++i)s.attributes[i]=characteristics[int(s.chassis)][i]+bonuses[int(s.pilot)][i]+s.refinements[i+1];

  s.level=1;s.pendingLevels=0;s.moduleChoices=0;
  s.maxHp=8+info.hit+s.refinements[size_t(PilotRefinement::Hull)];
  s.hp=std::min(s.hp,s.maxHp);
}
Sheet createSheet(Chassis c, PilotKind p, Dice &d, int /*legacyLevel*/) {
  Sheet s;
  s.creationSeed=d.next();
  s.chassis=c;s.pilot=p;
  normalizeCombatProfile(s);
  startingEquipment(s,false);
  validate(s);
  return s;
}
void prepare(Sheet &s) {
  s.maxAp = s.armor == Armor::Heavy ? 8 : s.armor == Armor::Light ? 4 : 0;
  s.ap = s.maxAp;
  s.hp = s.maxHp;
  s.maxReactor = chassisInfo(s.chassis).reactor;
  if (s.has(Module::SuperReactor))
    s.maxReactor = larger(s.maxReactor);
  if(s.has(Module::MicroReactor))s.maxReactor=smaller(s.maxReactor);
  s.batteryDie=0;
  s.reactor = s.maxReactor;
  s.powerRouting=0;s.kineticCharge=false;
  s.overheated = s.targetLock = s.hidden = false;
  s.reactorStress = s.ventRemaining = 0;
}
void destroyModule(Sheet &s,Module module) {
  auto gear=moduleGear(module);
  int lostCopy=-1;
  for(int slot=LeftShoulder;slot<MountCount;++slot)if(gear!=Gear::Empty&&s.equipment[slot]==gear)
    lostCopy=equippedWeaponCopy(s,slot);
  if(gear!=Gear::Empty&&s.owned[size_t(gear)]>0&&
     (lostCopy>=0?lostCopy<s.owned[size_t(gear)]:!s.has(module))){
    if(lostCopy<0)lostCopy=s.owned[size_t(gear)]-1;
    --s.owned[size_t(gear)];
    std::erase_if(s.weaponCopies,[&](const auto &it){return it.gear==gear&&it.copy==lostCopy;});
    for(auto &it:s.weaponCopies)if(it.gear==gear&&it.copy>lostCopy)--it.copy;
  }else std::erase(s.modules,module);
  for(int slot=0;slot<MountCount;++slot) {
    auto mounted=s.equipment[slot];
    if(gearInfo(mounted).module==module ||
       (module==Module::ShoulderHardpoint&&slot>=LeftShoulder&&ranged(mounted))) {
      s.equipment[slot]=Gear::Empty;s.qualities[slot]=Quality::None;
      if(slot>=LeftShoulder)s.moduleCopies[slot-LeftShoulder]=-1;
    }
  }
  if(module==Module::TargetingArray)s.targetLock=false;
  // Derive remaining passive capacities without healing/refilling the survivor.
  Sheet capacities=s;prepare(capacities);
  normalizeHeavyEquipment(s);
  s.maxAp=capacities.maxAp;s.ap=std::min(s.ap,s.maxAp);
  s.maxReactor=capacities.maxReactor;s.reactor=std::min(s.reactor,s.maxReactor);
}
void validateWeaponCopies(const Sheet &s){
  std::array<std::array<bool,99>,size_t(Gear::Count)> seenCopies{};
  for(const auto &item:s.weaponCopies){
    const bool mod=gradedModule(item.gear);
    if((!weaponTuning(item.gear)&&!mod)||item.rarity<Rarity::Common||item.rarity>Rarity::Rare||
       (mod?item.tuning!=WeaponRoll{}:item.rarity!=Rarity::Common)||item.copy<0||item.copy>=99||item.copy>=s.owned[size_t(item.gear)]||
       seenCopies[size_t(item.gear)][size_t(item.copy)])
      throw std::runtime_error("Invalid or duplicate weapon copy");
    seenCopies[size_t(item.gear)][size_t(item.copy)]=true;
    if(std::any_of(item.roll.begin(),item.roll.end(),[](int value){return value< -1||value>1;})||
       std::any_of(item.tuning.begin(),item.tuning.end(),[](int value){return value<0||value>weaponTuneLimit;})||
       weaponTuneCount(&item)>weaponTuneLimit)
      throw std::runtime_error("Invalid weapon stat tuning");
  }
  for(int slot=LeftShoulder;slot<MountCount;++slot){
    const int copy=s.moduleCopies[slot-LeftShoulder];
    const auto gear=s.equipment[slot];
    if(copy< -1||(copy>=0&&(!gradedModule(gear)||copy>=s.owned[size_t(gear)])))
      throw std::runtime_error("Invalid mounted module copy");
  }
  for(int hand=0;hand<2;++hand){
    const auto gear=s.equipment[hand];
    if(s.fittedCopies[hand]< -1||(s.fittedCopies[hand]>=0&&
       (!weaponTuning(gear)||equipmentOwner(s,hand)!=hand||s.fittedCopies[hand]>=99||s.fittedCopies[hand]>=s.owned[size_t(gear)]||
        weaponCopyFitted(s,gear,s.fittedCopies[hand],hand))))
      throw std::runtime_error("Invalid mounted weapon copy");
  }
}
void validateRefinements(const Sheet &s){
  if(s.campaignAwards<0||s.campaignAwards>1000000||
     s.campaignAwardsSpent<0||s.campaignAwardsSpent>s.campaignAwards)
    throw std::runtime_error("Invalid campaign award balance");
  const auto hull=s.refinements.front();
  if(hull<0||hull>pilotRefinementLimit||
     std::any_of(s.refinements.begin()+1,s.refinements.end(),[](int rank){return rank<0||rank>1;})||
     pilotRefinementCount(s)>pilotRefinementLimit||(!s.created&&pilotRefinementCount(s)>0))
    throw std::runtime_error("Invalid pilot refinements");
}
void validateRoster(const Sheet &s){
  chassisInfo(s.chassis);
  pilotName(s.pilot);
  if(s.powerRouting< -1||s.powerRouting>1||(s.chassis!=Chassis::Vanguard&&s.powerRouting!=0)||(s.chassis!=Chassis::Colossus&&s.kineticCharge))throw std::runtime_error("Invalid chassis power state");
  if(s.pilotBonusChoice<0||s.pilotBonusChoice>=std::max(1,pilotBonusChoices(s.pilot)))throw std::runtime_error("Invalid pilot bonus choice");
}
const char *factionHonour(int faction) {
  static constexpr std::array<const char *,3> honours{"SHIELD OF THE PRIMACY","WARDEN OF TERRA","LION OF THE DOMINION"};
  return honours.at(size_t(faction));
}
void awardServiceHonours(Sheet &s) {
  if(s.missionsCompleted<5)return;
  for(unsigned f=0;f<3;++f)if(s.career.standings[f]>=12)s.career.heroFactions|=1u<<f;
}
void importLegacyCareer(Sheet &s) {
  s.career.standings=s.campaign.standings;
  awardServiceHonours(s);
}
int uniformRank(const Sheet &s) {
  if(s.career.heroFactions)return 3;
  if(s.campaignAwards)return 2;
  return s.missionsCompleted>=5?1:0;
}
std::string serviceTitle(const Sheet &s) {
  if(s.career.heroFactions){
    int best=-1;
    for(int f=0;f<3;++f)if((s.career.heroFactions&(1u<<f))&&(best<0||s.career.standings[f]>s.career.standings[best]))best=f;
    return factionHonour(best);
  }
  if(s.campaignAwards)return "CAMPAIGN ACE";
  return s.missionsCompleted>=5?"VETERAN":"ROOKIE";
}
void validatePilotCareer(const PilotCareer &career) {
  if(career.heroFactions>7 || std::any_of(career.standings.begin(),career.standings.end(),[](int value){return value < -20 || value > 20;}))
    throw std::runtime_error("Invalid pilot career");
}
void validate(const Sheet &s) {
  validateCampaign(s.campaign);
  validatePilotCareer(s.career);
  validateRefinements(s);
  if(
     (s.lastCampaignAward[0]!=1&&s.lastCampaignAward!=CampaignAwardKey{})||
     (s.lastCampaignAward[0]==1&&s.campaignAwards==0))
    throw std::runtime_error("Invalid campaign award record");
  if(s.starterModule!=Module::Count){
    moduleId(s.starterModule);
    if(retiredModule(s.starterModule))throw std::runtime_error("Invalid starter module");
  }
  validateRoster(s);
  if(s.batteryDie<0||s.batteryDie>6)throw std::runtime_error("Invalid reserve battery die");
  if (moduleGear(s.oneShotModule) == Gear::Empty)
    throw std::runtime_error("One-shot module must be active");
  if(s.moduleChoices!=0||s.missionsCompleted<0||s.pendingLevels!=0||s.record.size()>160)throw std::runtime_error("Invalid progression state");
  if(!((s.creationSwap[0]==-1&&s.creationSwap[1]==-1)||(s.creationSwap[0]>=0&&s.creationSwap[0]<4&&s.creationSwap[1]>=0&&s.creationSwap[1]<4)))throw std::runtime_error("Invalid creation attribute swap");
  for(auto value:s.oneShotStock)if(value<0||value>99)throw std::runtime_error("Invalid one-shot module stock");
  for(auto value:s.owned)if(value<0||value>99)throw std::runtime_error("Invalid equipment stock");
  validateWeaponCopies(s);
  if(s.created&&(int(s.armor)<0||int(s.armor)>2||!s.ownedArmor[int(s.armor)]))throw std::runtime_error("Armor is not owned");
  if(!chassisArmorAllowed(s.chassis,s.armor))throw std::runtime_error("Chassis requires light armor");
  if (s.level != 1 ||
      fittedModuleCount(s) > moduleLimit || s.maxHp < 1 ||
      s.maxHp > 8 + chassisInfo(s.chassis).hit + s.refinements.front() || int(s.armor) < 0 ||
      int(s.armor) > 2 || s.credits < 0 || s.credits > 1000000)
    throw std::runtime_error("Invalid character bounds");
  for (auto v : s.attributes)
    if (v < 3 || v > 32)
      throw std::runtime_error("Invalid attributes");
  for (auto m : s.modules) {
    moduleId(m);
    if (std::count(s.modules.begin(), s.modules.end(), m) > 1)
      throw std::runtime_error("Duplicate module");
  }
  for (auto n : s.inventory)
    if (n < 0 || n > 99)
      throw std::runtime_error("Invalid inventory");
  if(s.chassis==Chassis::Vanguard&&(s.heavyOwners[0]!=-1||(heavy(s.equipment[LeftHand])&&heavy(s.equipment[RightHand]))))
    throw std::runtime_error("One utility appendage supports one heavy weapon");
  for (int slot = 0; slot < MountCount; ++slot) {
    auto g = s.equipment[slot];
    auto &info = gearInfo(g);
    auto q = s.qualities[slot];
    qualityName(q);
    if (info.module != Module::Count && availableEquipment(s,g)==0)
      throw std::runtime_error("Module not installed");
    if(!chassisGearAllowed(s.chassis,g))throw std::runtime_error("Hybrid requires heavy handheld weapons");
    if(!mountAllowed(g,slot))throw std::runtime_error("Weapons require arms; modules and supplies require shoulders or hips");
    if(g==Gear::Comlink)throw std::runtime_error("Comlink is integrated and cannot occupy a mount");
    if(pairedWeapon(s.chassis,g)&&(s.equipment[slot^1]!=g||(s.heavyOwners[slot/2]!=slot&&s.heavyOwners[slot/2]!=(slot^1))||s.qualities[slot^1]!=q))throw std::runtime_error("Heavy weapon requires both mirrored mounts");
    if(s.created&&g!=Gear::Empty&&!consumable(g)&&equipmentOwner(s,slot)==slot){
      int count=0;for(int i=0;i<MountCount;++i)if(s.equipment[i]==g&&equipmentOwner(s,i)==i)++count;
      if(count>availableEquipment(s,g))throw std::runtime_error("Equipped item is not owned");
    }
    if(!qualityAllowed(g,q))throw std::runtime_error("Incompatible weapon quality");
    if(q==Quality::Dual&&(slot>=LeftShoulder||s.equipment[slot^1]!=g||s.qualities[slot^1]!=Quality::Dual))throw std::runtime_error("Dual needs matching weapons in both hands");
  }
}
void customize(Sheet &s, const Json &recipe) {
  Sheet candidate = s;
  candidate.modules.clear();
  candidate.fittedCopies.fill(-1);
  for (auto &value : recipe.at("modules"))
    candidate.modules.push_back(moduleRead(value.get<std::string>()));
  static const char *mounts[] = {"left_hand",     "right_hand",
                                 "left_shoulder", "right_shoulder",
                                 "left_hip",      "right_hip"};
  auto &equipment = recipe.at("equipment");
  if (equipment.size() != MountCount)
    throw std::runtime_error("Specify all six mounts");
  for (int i = 0; i < MountCount; ++i) {
    candidate.equipment[i] = gearRead(equipment.at(mounts[i]));
    candidate.qualities[i] = Quality::None;
    if (recipe.contains("qualities") &&
        recipe["qualities"].contains(mounts[i])) {
      auto text = recipe["qualities"][mounts[i]].get<std::string>();
      bool found = false;
      for (int q = 0; q < int(Quality::Count); ++q) {
        auto name = std::string(qualityName(Quality(q)));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (name == text) {
          candidate.qualities[i] = Quality(q);
          found = true;
        }
      }
      if (!found)
        throw std::runtime_error("Unknown weapon quality");
    }
  }
  // Older loadout recipes repeat a single braced gun in both hand slots.
  if(candidate.chassis==Chassis::Vanguard&&heavy(candidate.equipment[LeftHand])&&
     candidate.equipment[LeftHand]==candidate.equipment[RightHand]){
    candidate.equipment[RightHand]=Gear::Empty;
    candidate.qualities[RightHand]=Quality::None;
  }
  if (recipe.contains("armor")) {
    auto a = recipe.at("armor").get<std::string>();
    if (a != "none" && a != "light" && a != "heavy")
      throw std::runtime_error("Unknown armor");
    candidate.armor = a == "heavy"   ? Armor::Heavy
                      : a == "light" ? Armor::Light
                                     : Armor::None;
  }
  if (recipe.contains("one_shot_module"))
    candidate.oneShotModule = moduleRead(recipe.at("one_shot_module"));
  normalizeCombatProfile(candidate);
  migrateProfileSheet(candidate);
  // A loadout recipe can fit a recovered variant, but cannot grant another copy.
  for(size_t i=size_t(Gear::BladeUncommon);i<candidate.owned.size();++i)
    candidate.owned[i]=s.owned[i];
  refitMounts(candidate);
  validate(candidate);
  prepare(candidate);
  s = candidate;
}
int damageDie(const Sheet &sheet){
  int die=chassisInfo(sheet.chassis).damage;
  if(sheet.has(Module::EnergyModulator))die=std::min(12,larger(die));
  return std::clamp(die+2*sheet.powerRouting,4,12);
}
float spreadRadians(const Sheet &s, Gear gear, float movement,
                    bool injured) {
  float base = (heavy(gear) ? .012f : .003f) *
               std::clamp(16.f / std::max(3, s.attributes[0]), .45f, 3.f);
  base *= 1 + std::abs(movement) * .7f;
  if (s.chassis == Chassis::Scout && heavy(gear))
    base *= 1.8f;
  if (injured)
    base *= 2;
  if (s.targetLock)
    base *= .35f * std::clamp(14.f / std::max(3, s.attributes[2]), .5f, 2.f);
  return std::clamp(base, .001f, .3f);
}
Vec3 deviated(Dice &dice, Vec3 forward, float angle) {
  forward = unit(forward);
  auto right = unit(cross(forward, {0, 1, 0}), {1, 0, 0});
  auto up = unit(cross(right, forward));
  float u = float(dice.next() >> 8) / 16777216.f,
        v = float(dice.next() >> 8) / 16777216.f;
  float radius = std::sqrt(u) * std::tan(angle), azimuth = v * 2 * pi;
  return unit(forward + right * (radius * std::cos(azimuth)) +
              up * (radius * std::sin(azimuth)));
}
} // namespace iron::rpg
