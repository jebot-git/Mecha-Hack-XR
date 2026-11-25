#pragma once
#include "coop.hpp"
#include "rpg/npc_audio.hpp"
#include "rpg/catalog.hpp"
#include "rpg/mission.hpp"
#include "rpg/rules.hpp"
#include "rpg/insertion.hpp"
#include "rpg/voidmaw_assault.hpp"
#include "rpg/mecha_duel.hpp"
#include "rpg/enemy_navigation.hpp"
#include "rpg/enemy_tactics.hpp"
#include "rpg/bosses.hpp"
namespace iron::rpg {
inline constexpr int schema = 38;
struct ThrusterState {
  Vec3 velocity{};
  float remaining=0, charge=4, altitude=0, pulseDuration=.32f, liftRemaining=0, downSpeed=0;
  bool thrustHeld=true, ascendHeld=true;
  bool combined=false;
};
inline constexpr size_t maxLiveProjectiles = 256, maxLiveEffects = 256;
inline constexpr uint16_t defaultPort = 29607;
enum class Phase { Hangar, Combat, Complete, Defeat, Briefing };
enum class VehiclePassPhase { Approach, Warning, Volley, Egress };
struct VehiclePass {
  VehiclePassPhase phase=VehiclePassPhase::Approach;
  Vec3 aim{}, course{};
  float timer=0;
  int pulse=0;
  bool reverse=false;
  FormationRun soloRun;
};
enum class HeavyPattern { None, Crossfire, MissileCurtain, Beam, Weave, Loft };
struct HeavyAttack {
  HeavyPattern pattern=HeavyPattern::None;
  EnemyAttack attack{};
  Vec3 origin{}, aim{}, lateral{};
  float timer=0;
  int pulse=0;
  bool reverse=false, redeployed=false;
  float lockDistance=0;
  bool guidanceLost=false;
};
struct Enemy {
  int spec = 0, hp = 8, maxHp = 8, tactic = 0;
  bool cover = false, ambushed = false, transformed=false, combined=false;
  int spawned=0;float manualClock=0;
  int group=-1;
  int boss=-1,bossPhase=0;
  VehiclePass vehiclePass;
  HeavyAttack heavyAttack;
  VoidmawAssaultState voidmawAssault{};
  MechaDuel duel{};
  float bossTell=0;
  // Host-only attack commitment. The windup is presented as a world effect.
  uint32_t chargeTarget = 0;
  int chargeWorldTarget = -1;
  int chargedAttack = -1;
  bool chargedTransformed = false;
  float chargeRemaining = 0;
  float boostEffectClock = 0;
  bool ambient=false;
  uint32_t corrosionOwner=0;
  unsigned ambientWave=0;
  float airstrikeUntil=0;
  float arrivalDelay=0;
  bool burrowReady=false,burrowImpact=false;
  uint32_t grapplePilot=0;
  int grappleDamage=0;
  float grappleSide=0,grappleCooldown=0,grappleAt=0,grappleYaw=0;
  Vec3 grappleReturn{},grappleMuzzle{};
  unsigned patrolStep=0;
  // Host-only cadence state for patrols beyond all living pilots and drones.
  bool distantPatrol=false;
  float pendingPatrolTime=0, radioReady=0;
  float heat=0,ventRemaining=0;
  bool overheated=false;
  unsigned injuries=0,missingArms=0;
  std::array<int,5> subsystemDamage{};
  ThrusterState thrusters;
  float decisionClock = 0, fireClock = 1, disabled = 0, corrosion = 0,
        regenClock = 0, anchorX = 0, anchorZ = 0;
};
bool canBurrow(const EnemySpec &);
inline const EnemySpec &combatSpec(const Enemy &e){return e.boss<0?enemySpec(e.spec):campaignBoss(e.boss).spec;}
struct WeaponBurst {
  Gear gear=Gear::Empty;
  Quality quality=Quality::None;
  int remaining=0,damage=0;
  float untilNext=0;
};
struct RecoveredItem {
  Gear gear=Gear::Empty;
  WeaponRoll roll{};
  Rarity rarity=Rarity::Common;
  bool operator==(const RecoveredItem &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RecoveredItem,gear,roll,rarity)
enum class HullEmergency { None, LastWind, AwaitRepair };
struct NaniteHold {
  uint32_t target=0;
  int slot=-1;
  double started=0;
};
// The action clock is shared by host, replicas and both XR eyes.
struct WeaponCycle {
  enum Stage { Stroke, Charge, Release };
  float started=0, duration=0;
  Stage stage=Stroke;
  bool active(float now) const {return duration>0 && (stage==Charge || now-started<duration);}
  float pose(float now) const {
    if(!active(now))return 0;
    const float fraction=std::clamp((now-started)/duration,0.f,1.f);
    // Frame 9 of the authored 1–25 cycle is the firing/charged pose.
    if(stage==Charge)return fraction/3.f;
    if(stage==Release)return (1.f+2.f*fraction)/3.f;
    return fraction;
  }
};
struct Member {
  uint32_t id = 0;
  std::string name;
  bool ready = false, inSortie = false;
  Pilot body;
  Sheet sheet;
  Input input;
  float inputAge = 100;
  ThrusterState thrusters;
  unsigned injuries = 0;
  unsigned missingArms = 0;
  bool debriefAccepted=false;
  bool configured=false,campaignAwarded=false;
  uint32_t configuration=0;
  std::array<bool,3> heavySupported{};
  std::array<bool,2> handSupported{};
  std::array<bool,2> handStashed{}, stashGripHeld{};
  Vec3 dronePosition{};
  float droneYaw=0,dronePitch=0,droneLaunchYaw=0,droneHeadYaw=0,droneHeadPitch=0;
  float scanRemaining=0,scanDuration=0,scanExposureRemaining=0,weatherJam=0;
  std::vector<std::array<float,3>> scannedContacts;
  std::vector<size_t> scannedActors;
  uint16_t scannedBearings=0;
  uint32_t scanSweep=0;
  unsigned activeScanners=0;
  float nextSensorPulse=0;
  bool fireModeHeld=true,rifleSemi=false;
  bool droneControl=false,droneGripHeld=false,droneDetonateHeld=true,droneArc=false;
  std::vector<RecoveredItem> pendingRecovery;
  std::vector<Gear> recoveredGear;
  bool fieldAmmo=false;
  int pendingDepotSalvage=0;
  float vehicleBlend=0,vehicleSpeed=0;
  bool ejected = false, criticalFaultRolled=false;
  HullEmergency emergency=HullEmergency::None;
  double ejectAt=0, faultNoticeUntil=0;
  int lastFault=0;
  NaniteHold repairHold;
  bool recoveryPending() const {
    return emergency!=HullEmergency::None || (ejected && body.ejectionTime<7);
  }
  bool combatControls() const { return !ejected && (sheet.hp>0 || emergency==HullEmergency::LastWind); }
  float hiddenRemaining = 0, shieldRemaining = 0, stun = 0, blind = 0,
        boost = 0, explosive = 0, ongoing = 0, ongoingTick = 0, drone = 0,
        chassisCooldown = 0;
  int shield = 0, ongoingDamage = 0;
  float frameShieldDelay=0;
  bool funnels=false, shieldNodes=false, veriform=false;
  float manualClock=0, precog=0, immobile=0, mobilityDisrupt=0, systemDisrupt=0;
  float nodeMobility=0, rangeExtension=0, pistonCooldown=0;
  int precogTarget=-1;
  std::array<bool,MountCount> busterReady{};
  std::array<bool,2> lanceReady{};
  std::array<WeaponBurst,2> bursts{};
  std::array<WeaponCycle,2> weaponCycles{};
  std::array<int,MountCount> beltTarget{-1,-1,-1,-1,-1,-1};
  bool garrison=false, reinforced=false;
  float berserkRetreat=0;
  bool siege = false, barricade = false, vehicle = false, chassisHeld = true;
  std::array<bool, MountCount> blocked{}, held{};
  std::array<float, MountCount> recoil{};
  std::array<bool, 2> punchHeld{};
  std::array<float,2> parryWindow{};
  Vec3 swingMotion{},lastMeleeMotion{};
  float comboRemaining=0,riposteRemaining=0,criticalRemaining=0;
  int comboTarget=-1,comboHand=-1,comboCount=0,riposteTarget=-1;
  std::array<Gesture, 2> gestures{};
  bool mine = false, turret = false;
  Vec3 minePosition{}, turretPosition{};
};
struct Shot {
  Projectile body;
  Quality quality = Quality::None;
  AttackEffect effect = AttackEffect::Damage;
  bool reactor = false;
  int maxTargets=0;
  bool meleeStrike=false, coverSensitiveBlast=false, npcCanHitPilots=true;
  Vec3 launchOrigin{};
  Vec3 guardAxis{}; // Host-only orientation of a directed melee guard.
  int ricochets=0;
  bool feedMisfire=false;
  uint32_t loftPilot=0;
  int loftNpc=-1;
  float loftAge=0;
  Vec3 loftPoint{};
};
enum class SafeBoxKind { Dungeon, Garrison, Depot, FieldSupplies, ModuleCache };
struct SupplyDrop {
  size_t pickup=0;
  SafeBoxKind kind=SafeBoxKind::Dungeon;
  uint32_t seed=0;
  int difficulty=0;
};
inline constexpr size_t maxRecoveryItems=256;
RecoveredItem safeBoxEquipment(const SupplyDrop &,uint32_t pilot);
Gear safeBoxReward(const SupplyDrop &,uint32_t pilot);
bool pilotRevealed(const Member &);
struct Trap {
  Vec3 position;
  bool spent = false;
};
class Session {
  struct SectorPreparation;
  std::shared_ptr<SectorPreparation> nextSector;
  void prefetchNextSector();
  void loadSectorEnvironment(std::vector<DungeonSite> *sites);
  Dice dice;
  using Navigation = EnemyNavigationState;
  struct EnemyMemory {uint32_t target=0;Vec3 position{};float remaining=0;};
  std::vector<EnemyTacticalDecision> tacticalDecisions;
  std::vector<int> factionContacts;
  std::vector<EnemyMemory> enemyMemory;
  float tacticalClock=0,encounterClock=10,npcRadioReady=0,ambientKaijuReady=12;
  float routineRadioReady=0;
  unsigned radioSequence=0;
  std::array<std::array<RadioRotation,size_t(RadioLine::Count)>,npcRadioFactions> radioRotation{};
  unsigned ambientWaves=0;
  enum class AmbientKind { Patrol, Armor, Airstrike };
  struct AmbientReserve {unsigned wave;AmbientKind kind;float due,expires;};
  std::vector<AmbientReserve> ambientReserves;
  // Host bookkeeping for live random encounters. Only the earned total is replicated.
  struct AmbientBounty { unsigned wave; int remaining; bool partyKill=false; };
  std::vector<AmbientBounty> ambientBounties;
  void finishAmbientActor(size_t index,bool destroyed,bool partyKill=false);
  size_t navigationTurn=0;
  std::vector<Navigation> navigation;
  struct EscortRoute {
    std::vector<Vec3> points;
    std::vector<float> distance;
    float covered=0, speed=0;
    Vec3 at(float) const;
  } escort;
  void prepareEscort(Vec3 from, Vec3 exit);
  bool stepEscort(float dt, bool accompanied);
  void positionEscort(float dt);
  struct WeatherFront {
    Weather kind=Weather::Clear;
    uint32_t seed=0;
    int lastPulse=-1,linger=0;
    float impactAt=-1;
    size_t count=0;
    std::array<Vec3,4> cells{};
  } weatherFront;
  void log(const std::string &);
  void prepareMission(bool continuation=false);
  void enterSortie(Member &,bool enter);
  void stepMission(float);
  void checkpointCampaign();
  void advanceCampaign();
  void recordCampaignVictory();
  void stepBosses(float);
  void npcRadio(size_t actor, RadioLine);
  void ambientKaijuCall(std::span<const Vec3> observers);
  void stageOpener(float);
  InsertionKind openerKind() const;
  bool insertionLocked() const;
  void activate(Member &, int, bool punch = false);
  void stepSensors(Member &,float dt);
  void scanSensors(Member &,float duration,bool exposesPilot);
  bool networkGame=false;
  void startHullEmergency(Member &);
  void rollFault(Member &,bool allowModuleLoss);
  Member &naniteTarget(Member &);
  void advanceRepair(Member &,int slot);
  void useRepairNanites(Member &user,Member &target,bool emergency=false);
  void criticalHit(Member &,size_t,Vec3,int &damage);
  bool parryStrike(Member &,const Shot &);
  int absorbShield(Member &,int);
  void stepChassis(Member &,float dt);
  void takeDamage(Member &, int, bool plasma = false, Vec3 towardSource = {},bool energy=false);
  void detonateDrone(Member &);
  bool activateManual(Member &,int,Gear,Vec3,Vec3);
  void stepManual(Member &,float);
  void bankDepotSalvage(Member &);
  void stepManualEnemies(float);
  void reinforceManualEnemies();
  bool spawnManualEnemy(int,Vec3,int tactic=0,unsigned arrival=0,Vec3 approach={},bool leadArrival=true,FactionRole factionRole=FactionRole::Contract,size_t *actorIndex=nullptr);
  void stepAmbientEncounters(float);
  int spawnAmbientWave(const Member &,int count,unsigned wave,AmbientKind,bool reinforcing=false);
  void spawnFactionDetachments();
  bool stepFactionWithdrawal(size_t,float,EnemyNavigationBudget &);
  void followFactionPilots(size_t,float,EnemyNavigationBudget &);
  void noteFactionAttack(size_t);
  void noteFactionKill(size_t);
  void applyDisabledResult(Member &, int);
  bool injuryCheck(Member &,int attribute,bool attack=false);
  void eject(Member &);
  void updateEnvironment();
  void stepWeatherHazards();
  void useChassis(Member &);
  void hitEnemy(Member &, size_t, int, Quality, bool melee);
  void damageScenery(size_t, float);
  void damageNpc(size_t,int,AttackEffect,Quality);
  void crushFoot(Vec3, Member *);
  void moveRpgEnemy(size_t,Vec3,float,bool airborne);
  bool stepVoidmawAssault(size_t,Member &,float);
  bool stepMechaDuel(size_t,const Member *,int npc,float);
  bool startVoidmawGrapple(size_t,Member &);
  void positionVoidmawGrapple(size_t,const Member &);
  void damageVoidmawGrapple(size_t,int);
  void releaseVoidmawGrapple(size_t);
  bool startVoidmawBurrow(size_t,const Member &);
  void voidmawEruption(size_t);
  bool boostMember(Member &,const Input &,bool movementAllowed,float dt);
  void beginDestruction(size_t);
  void finishEnemyDeath(Member *,size_t);
  void damageEnemyEnvironment(size_t,int);
  void stepDestruction(float);
  void blastScenery(Vec3, float, float);
  bool placeSafeBox(Vec3, SafeBoxKind, unsigned source, int difficulty);
  void collectSupplies();
  void bankRecoveredGear(Member &);
  void stepDrone(Member &,const Input &,float);
  void enemiesAct(float);
  bool startHeavyAttack(size_t, const EnemyAttack &, const Member &, int attackIndex, Vec3 muzzle,int npcTarget);
  void stepHeavyAttack(size_t, float dt);
  void launchMechaLayer(size_t,int damage);
  void guideLoftMissile(Shot &,float dt);
  bool redeployCarrierAllies(size_t, float targetRange);
  bool stepVehicleAttack(size_t,const Member *,int objectiveTarget,float,EnemyNavigationBudget &);
  void patrolEnemy(size_t,float,EnemyNavigationBudget *);
  void stepDungeonSites();
  void stepProjectiles(float);
  void blast(Member &, Vec3, float, int, Quality, bool self = false,int maxTargets=0,AttackEffect effect=AttackEffect::Damage);
  void trapsStep(float);
  bool fire(Member &, int, Vec3, Vec3, int, Quality, float range = 600, bool followup=false);
  void stepWeaponBurst(Member &, int slot, float dt);
  bool launch(Shot);
  bool addEffect(WeaponEffect);

public:
  uint32_t host = 0;
  uint64_t epoch = 0, sequence = 0;
  Phase phase = Phase::Hangar;
  uint32_t seed = 1, environmentSeed = 0, boardRevision = 0;
  CampaignState campaign;
  int missionNumber = 0, selectedMission = 0, salvage = 0, biome = 3;
  EnvironmentCycle environmentCycle;
  MissionState mission;
  double clock = 0;
  bool difficult = false;
  std::string terrain = "URBAN";
  GameSettings settings;
  World world;
  std::vector<Member> members;
  std::vector<Enemy> enemies;
  std::vector<Shot> ordnance;
  std::vector<Trap> traps;
  std::vector<SupplyDrop> supplies;
  std::vector<std::string> events;
  bool zeroGravity() const {return biome==1||mission.recipe.location==9;}
  void stepPilotFall(Member &,float dt);
  float missionRadius() const { return mission.recipe.radius>0?mission.recipe.radius:settings.extent; }
  explicit Session(uint32_t seed = 1,bool networkGame=false);
  Member &member(uint32_t);
  void add(uint32_t, const std::string &);
  void remove(uint32_t);
  void command(uint32_t, const Json &);
  void playerCommand(uint32_t, const Json &);
  void step(float);
  Json snapshot(const coop::SceneryBaseline *baseline=nullptr) const;
  void readSnapshot(const Json &,const Json *scenery=nullptr);
  World view(uint32_t, const ArmRig & = {}, float poseLead=0) const;
  std::vector<std::string> hud(uint32_t) const;
};
void buildMissionEnvironment(World &, uint32_t, int biome, float radius,
                             std::span<const int> enemyIds, int location = 0,
                             int objective = 0, int region = 0, int opener = -1,int landform = -1,int dungeonCount=0,std::vector<DungeonSite> *sites=nullptr,const MissionRecipe *story=nullptr);
Vec3 missionEntryPosition(const World &, unsigned slot);
Vec3 missionNavigablePosition(const World &, Vec3 desired, float radius = 3.5f,
                             int ignore = -1, float maximumDepth = -1);
std::vector<Vec3> missionEscortRoute(const World &, Vec3 from, Vec3 destination,
                                     size_t enemyCount);
Vec3 missionNavigationWaypoint(const World &, Vec3 from, Vec3 destination,
                              float radius = 3.5f);
Climate missionClimate(int biome, int location = 0);
bool missionWeatherAllowed(const MissionRecipe &,Weather);
EnvironmentSample missionEnvironment(const MissionRecipe &, const EnvironmentCycle &);
bool insideArea(Vec3, float radius);
bool clearPosition(const World &, Vec3, float radius = 3, int ignore = -1);
} // namespace iron::rpg
