#pragma once
#include "simulation.hpp"
#include "rpg/campaign.hpp"
#include <nlohmann/json.hpp>
#include <span>
namespace iron::rpg {
using Json = nlohmann::json;
enum class Chassis { Brawler, Scout, Striker, Titan, Auxiliary, Colossus, Hybrid, Vanguard, Count };
enum class PilotKind { Commander, Engineer, Maverick, Quipster, Bionic, Merc, Vet, Pariah, Count };
enum class Module {
  RocketFists,
  JumpJets,
  TargetingArray,
  NanoShroud,
  ShoulderHardpoint,
  SuperReactor,
  ChestBeam,
  Grappler,
  MissileBarrage,
  QuakeGenerator,
  SiegeMode,
  StaticCloud,
  BarricadeMode,
  ExothermicPlating,
  ParticleBarrier,
  ReflectiveShielding,
  RetractableShield,
  BoosterSprint,
  FragMines,
  SentryTurret,
  VehicleMode,
  DismantlerArms, EnergyModulator, FunnelDrones, GeistSystem,
  GlideThrusters, LongRangeScanners, MicroMissiles, MicroReactor,
  PistonFists, PrecogSystem, ShieldNodes, LodiumAlloy, VeriformFrame,
  Count
};
enum class Gear {
  Empty,
  Blade,
  Rifle,
  Cannon,
  Repair,
  JumpJets,
  TargetingArray,
  NanoShroud,
  Scan,
  HeavyBlade,
  Shield,
  Comlink,
  BoostKit,
  ExplosiveRounds,
  ArcGrenade,
  FragGrenade,
  OneShotModule,
  ScoutingDrone,
  SensorJammer,
  ChestBeam,
  Grappler,
  MissileBarrage,
  QuakeGenerator,
  SiegeMode,
  StaticCloud,
  BarricadeMode,
  ParticleBarrier,
  BoosterSprint,
  FragMines,
  SentryTurret,
  VehicleMode,
  FunnelDrones, GlideThrusters, LongRangeScanners, MicroMissiles,
  PrecogSystem, ShieldNodes, VeriformFrame,
  FusionGrenade, Scrap, VerilianBattery,
  Holoprojector, CameraDrone, WeldingBeam, CockpitTrinket, RearThrusters,
  AirCompressor, UtilityKnife, MechaManacle, CableWinch, FootTreads,
  WristDrill, SquadronEmblem, FusionicRunnerLights, GalvanicVoidglass,
  OnBoardAI, ChromePlating, SonisphereAmplifiers, BansheeFenders,
  InfamousPart, CargoHullpack, CargoSpider,
  ChainCutter,
  Scattergun,
  Rotary,
  GrenadeLobber,
  RocketRack,
  SeekerPod,
  PulseCarbine,
  RailLance,
  BladeUncommon, BladeRare,
  RifleUncommon, RifleRare,
  CannonUncommon, CannonRare,
  HeavyBladeUncommon, HeavyBladeRare,
  ChainCutterUncommon, ChainCutterRare,
  ScattergunUncommon, ScattergunRare,
  RotaryUncommon, RotaryRare,
  GrenadeLobberUncommon, GrenadeLobberRare,
  RocketRackUncommon, RocketRackRare,
  SeekerPodUncommon, SeekerPodRare,
  PulseCarbineUncommon, PulseCarbineRare,
  RailLanceUncommon, RailLanceRare,
  Count
};
enum class Quality {
  None,
  Arc,
  Chainblade,
  Corrosive,
  Deadly,
  Plasma,
  Quick,
  Sniper,
  Thrown,
  Artillery, Automated, Beam, Buster, BeltFed, Bouncing, Dual, SawedOff,
  ScrapFed, Lodium, Thermal, Unstable,
  Count
};
enum class Armor { None, Light, Heavy };
struct GearInfo {
  const char *id;
  const char *name;
  int cost;
  float cooldown, stress, range;
  Module module = Module::Count;
};
enum class Rarity { Common, Uncommon, Rare };
enum class AttackPattern { Melee, Single, Burst, Scatter, Lob, Rocket, Seek, Pulse, Charge };
struct WeaponTuning {
  Weapon visual;
  AttackPattern pattern;
  int damageBias, projectiles;
  float velocity, dispersion, blast, lift, windup;
  Vec3 muzzle; // Relative to the tracked grip, with forward along -Z.
};
const GearInfo &gearInfo(Gear);
Gear weaponFamily(Gear);
Rarity weaponRarity(Gear);
const char *rarityName(Rarity);
const WeaponTuning *weaponTuning(Gear);
std::span<const Gear> weaponFamilies();
Gear weaponVariant(Gear family,Rarity);
bool retiredGear(Gear);
bool retiredModule(Module);

Gear moduleGear(Module);
const char *moduleId(Module);
const char *moduleName(Module);
const char *moduleDescription(Module);
const char *qualityName(Quality);
Module moduleRead(const std::string &);
Gear gearRead(const std::string &);
bool ranged(Gear);
bool melee(Gear);
bool heavy(Gear);
bool consumable(Gear);
bool gadget(Gear);
bool manualGear(Gear);
bool manualModule(Module);
bool qualityAllowed(Gear,Quality);
const char *gearVisualId(Gear);
const char *gearDescription(Gear);
const char *qualityDescription(Quality);
int manualSourcePage(Gear);
int manualSourcePage(Module);
int manualSourcePage(Quality);
bool mountAllowed(Gear,int slot);
bool chassisArmorAllowed(Chassis,Armor);
bool chassisGearAllowed(Chassis,Gear);
bool pairedWeapon(Chassis,Gear);
bool targetedGear(Gear,Module oneShot=Module::ChestBeam);
struct ChassisInfo {
  const char *name;
  int hit, damage, reactor, armor;
};
const ChassisInfo &chassisInfo(Chassis);
const char *chassisId(Chassis);
const char *chassisPowerDescription(Chassis);
const char *pilotId(PilotKind);
const char *pilotName(PilotKind);
int pilotBonusChoices(PilotKind);
int pilotBonusAttribute(PilotKind,int choice);
const char *pilotRoleDescription(PilotKind);
Module chassisStarterModule(Chassis);
const char *gearName(Gear);
struct Dice {
  uint32_t state = 1;
  uint32_t next();
  int roll(int sides);
};
struct Test {
  int natural = 0, total = 0;
  bool success = false, critical = false;
};
Test test(Dice &, int attribute, int modifier = 0, int advantage = 0);
void addReactorHeat(float &heat, bool &overheated, float &vent, float amount);
void stepReactorHeat(float &heat, bool &overheated, float &vent, float dt, float cooling);
enum class WeaponStat { Cycle, Heat, Handling, Count };
enum class PilotRefinement { Hull, Power, Mobility, System, Presence, Count };
inline constexpr int pilotRefinementLimit=2;
using WeaponRoll=std::array<int,size_t(WeaponStat::Count)>;
inline constexpr int weaponTuneLimit=2;
struct WeaponCopy {
  Gear gear=Gear::Empty;
  int copy=0;
  WeaponRoll roll{}, tuning{};
  Rarity rarity=Rarity::Common; // Module grade; weapon grades are encoded by Gear.
  bool operator==(const WeaponCopy &) const = default;
};
inline void to_json(Json &j,const WeaponCopy &w){j=Json::array({w.gear,w.copy,w.roll,w.tuning});if(w.rarity!=Rarity::Common)j.push_back(w.rarity);}
inline void from_json(const Json &j,WeaponCopy &w){
  if(!j.is_array()||(j.size()!=4&&j.size()!=5)||!j[0].is_number_integer()||!j[1].is_number_integer())
    throw std::runtime_error("Invalid weapon copy");
  for(int i:{2,3})
    if(!j[i].is_array()||j[i].size()!=size_t(WeaponStat::Count)||
       !std::all_of(j[i].begin(),j[i].end(),[](const auto &entry){return entry.is_number_integer();}))
      throw std::runtime_error("Invalid weapon stat roll");
  j.at(0).get_to(w.gear);j.at(1).get_to(w.copy);j.at(2).get_to(w.roll);j.at(3).get_to(w.tuning);w.rarity=j.size()==5?j.at(4).get<Rarity>():Rarity::Common;
}
struct WeaponStats {
  float cooldown=0, heat=0, spreadFactor=1, range=0, output=1;
};
struct PilotCareer {
  HumanStandings standings{};
  unsigned heroFactions=0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PilotCareer,standings,heroFactions)

struct Sheet {
  Chassis chassis = Chassis::Striker;
  PilotKind pilot = PilotKind::Engineer;
  int pilotBonusChoice=0;
  int powerRouting=0; // Vanguard: -1 reactor, +1 weapons. Reset on deployment.
  bool kineticCharge=false;
  int level = 1, hp = 1, maxHp = 1, ap = 3, maxAp = 3, reactor = 8,
      maxReactor = 8;
  std::array<int, 4> attributes{};
  std::vector<Module> modules;
  Module starterModule=Module::Count;
  std::array<Gear, MountCount> equipment{};
  Armor armor = Armor::Light;
  std::array<Quality, MountCount> qualities{};
  std::array<int, size_t(Gear::Count)> inventory{};
  int credits = 120;
  std::array<int,size_t(Gear::Count)> owned{};
  std::vector<WeaponCopy> weaponCopies;
  std::array<int,2> fittedCopies{-1,-1};
  std::array<int,4> moduleCopies{-1,-1,-1,-1}; // Explicit recovered copy, or factory default.
  std::array<int,size_t(Module::Count)> oneShotStock{};
  std::array<bool,3> ownedArmor{true,false,false};
  std::array<int,3> heavyOwners{-1,-1,-1};
  int missionsCompleted=0,pendingLevels=0,moduleChoices=0;
  int campaignAwards=0;
  int campaignAwardsSpent=0;
  std::array<int,size_t(PilotRefinement::Count)> refinements{};
  CampaignAwardKey lastCampaignAward{};
  CampaignState campaign;
  PilotCareer career;
  bool created=false,startingCredits=false;
  uint32_t creationSeed=0;
  std::array<int,2> creationSwap{-1,-1};
  std::string record="Veteran of the frontier";
  Module oneShotModule = Module::ChestBeam;
  int repairKits = 2;
  int batteryDie = 0;
  bool overheated = false, targetLock = false, hidden = false;
  float reactorStress = 0, ventRemaining = 0;
  bool has(Module) const;
  bool canAct() const { return hp > 0 && !overheated; }
  void stress(Dice &, float amount);
  void reactorTest(Dice &);
  void stepThermal(float dt);
  void coolant(float amount);
  int damage(int amount); // Prototype policy automatically spends available AP.
  void rest(Dice &, bool longRest = false);
};
Sheet createSheet(Chassis, PilotKind, Dice &, int level = 1);
inline constexpr int moduleLimit=4,modulePurchaseCost=160;
void normalizeCombatProfile(Sheet &);
void retireObsoleteEquipment(Sheet &);
void validate(const Sheet &);
void validateRoster(const Sheet &);
void validateWeaponCopies(const Sheet &);
void customize(Sheet &, const Json &);
void prepare(Sheet &);
void destroyModule(Sheet &,Module);
void startingEquipment(Sheet &,bool credits);
void normalizeHeavyEquipment(Sheet &);
int equipmentOwner(const Sheet &,int slot);
int availableEquipment(const Sheet &,Gear);
void equipGear(Sheet &,int slot,Gear,int copy=-1);
bool weaponCopyFitted(const Sheet &,Gear,int copy,int except=-1);
int equippedWeaponCopy(const Sheet &,int slot);
const WeaponCopy *weaponCopy(const Sheet &,Gear,int copy);
WeaponRoll rollWeaponStats(Dice &);
void recoverWeapon(Sheet &,Gear,const WeaponRoll &,Rarity grade=Rarity::Common);
bool gradedModule(Gear);
int traderPrice(Gear);
int equipmentCopyCount(const Sheet &,Gear);
Rarity equipmentRarity(const Sheet &,Gear,int copy);
float mountedModuleOutput(const Sheet &,Gear);
WeaponStats weaponStats(const Sheet &,Gear,int copy);
int weaponTuneCount(const WeaponCopy *);
int weaponTuneCost(Gear,int steps);
void tuneWeapon(Sheet &,Gear,int copy,WeaponStat,bool useAward=false);
int availableCampaignAwards(const Sheet &);
int pilotRefinementCount(const Sheet &);
int pilotRefinementCost(const Sheet &);
bool canRefinePilot(const Sheet &,PilotRefinement);
void refinePilot(Sheet &,PilotRefinement,bool useAward=false);
void validateRefinements(const Sheet &);
bool frameUpgrade(Module);
int fittedModuleCount(const Sheet &);
inline constexpr int frameUpgradeCost=230;
void upgradeFrame(Sheet &,Module);
void equipQuality(Sheet &,int slot,Quality);
void purchase(Sheet &,Gear);
void craftFromScrap(Sheet &,Gear,int count=1);
int damageDie(const Sheet &);
void purchaseArmor(Sheet &,Armor);
void installModule(Sheet &,Module);
void finishCreation(Sheet &);
void migrateProfileSheet(Sheet &);
void awardServiceHonours(Sheet &);
void importLegacyCareer(Sheet &);
void validatePilotCareer(const PilotCareer &);
int uniformRank(const Sheet &);
std::string serviceTitle(const Sheet &);
const char *factionHonour(int faction);
void refitMounts(Sheet &);
void sellEquipment(Sheet &,Gear,int copy=-1);
void sellArmor(Sheet &,Armor);
Test test(Dice &,const Sheet &,int attribute,uint32_t injuries,bool attack=false,
          int modifier=0,int advantage=0);
float spreadRadians(const Sheet &, Gear, float movement,
                    bool injured = false);
Vec3 deviated(Dice &, Vec3, float angle);
std::filesystem::path defaultProfilePath();
void saveProfile(const std::filesystem::path &, const Sheet &,
                 const std::string &callsign = "Pilot");
Sheet loadProfile(const std::filesystem::path &,
                  std::string *callsign = nullptr);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Sheet, chassis, pilot, pilotBonusChoice, powerRouting, kineticCharge, level, hp, maxHp, ap,
                                   maxAp, reactor, maxReactor, attributes,
                                   modules, starterModule, equipment, repairKits, overheated,
                                   targetLock, hidden, armor, qualities,
                                   inventory, credits, oneShotModule,
                                   reactorStress, ventRemaining,owned,weaponCopies,fittedCopies,moduleCopies,oneShotStock,ownedArmor,
                                   heavyOwners,missionsCompleted,pendingLevels,
                                   moduleChoices,created,startingCredits,record,
                                   creationSeed,creationSwap,batteryDie,campaignAwards,campaignAwardsSpent,refinements,lastCampaignAward,campaign,career)
} // namespace iron::rpg
