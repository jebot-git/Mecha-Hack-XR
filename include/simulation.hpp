#pragma once
#include <array>
#include <algorithm>
#include <vector>
#include <cmath>
#include <string>
#include <filesystem>
#include <memory>
#include <optional>
#include <map>
#include <cstdint>
#include "vector.hpp"
#include "animation.hpp"
#include "mecha_injuries.hpp"
#include "procedural.hpp"
#include "environment.hpp"
#include "procedural_terrain.hpp"
#include "rpg/voidmaw.hpp"
#include "rpg/voidmaw_pose.hpp"
#include "rpg/surface_gravity.hpp"
namespace iron {
enum Mount { LeftHand, RightHand, LeftShoulder, RightShoulder, LeftHip, RightHip, MountCount };
inline std::array<bool,MountCount> mountButtons(bool leftTrigger,bool rightTrigger,
                                                 bool a,bool b,bool x,bool y) {
    return {leftTrigger,rightTrigger,y,b,x,a};
}
enum class Weapon { Empty, Rifle, Rocket, Cutter, Gatling, Grenade, Rail, Shield, Cruise, Shotgun, Homing, HeavyMachinegun, AutoGrenade, Count };
struct WeaponInfo { const char* name; const char* shortName; unsigned mounts,capacity; float cooldown,speed,damage; unsigned burst=0; };
const WeaponInfo& weaponInfo(Weapon,int game=0);
bool compatible(Weapon,int mount);
const char* weaponModel(Weapon);
inline int heldModel(Weapon w){return w==Weapon::Rifle?1:w==Weapon::Cutter?2:w==Weapon::Shotgun?3:w==Weapon::HeavyMachinegun?4:0;}
inline unsigned campaignWeaponMask(int game){return game?(1u<<int(Weapon::Count))-1:(1u<<9)-1;}
// Arming module $192AE, in native weapon order (Empty has no restriction).
inline unsigned weaponLimit(Weapon w){constexpr unsigned limits[]={6,1,4,1,2,2,1,1,2,1,2,1,1};unsigned n=unsigned(w);return n<std::size(limits)?limits[n]:0;}
inline float statusLampFill(float percent,unsigned segment){
    return std::clamp(percent/20-float(segment),0.f,1.f);
}
inline Vec3 armorLamp(float health,unsigned lamp){
    const float charge=statusLampFill(health,lamp);
    return charge<=0?Vec3{.006f,.008f,.005f}:charge<=1.f/3?Vec3{1,.015f,.003f}:charge<=2.f/3?Vec3{1,.65f,.005f}:Vec3{.03f,1,.008f};
}
using Loadout=std::array<Weapon,MountCount>;

struct Input {
    float steer=0, forward=0, turn=0, headYaw=0, headPitch=0;
    bool focused=true, headValid=true;
    bool searchlight=false;
    bool thrust=false, ascend=false, fireMode=false; // RPG stick clicks, sampled as held inputs
    float rpgWalkScale=1; // set by the authoritative session
    std::array<bool,MountCount> fire{};
    std::array<bool,2> ping{}; // left grip: identify self; right grip: head-aimed map ping
    std::array<bool,2> punch{}; // desktop/test equivalents of gestures
    std::array<bool,2> handValid{};
    std::array<HandPose,2> handPoses{{{{-.4f,-.13f,-.75f},{},{},true},{{.4f,-.13f,-.75f},{},{},true}}};
    std::array<Vec3,2> hands{}; // calibrated tracking-space positions, physical metres; independent of head
};
// Texture selectors:0 colour,1 gameplay atlas,2 original PSX atlas,
// 3 procedural trim repeats (u/v in repeats, oneSided carries tile index0..63).
struct WorldVertex { Vec3 position,color; float u=0,v=0,textured=0,oneSided=0; };
using WorldMesh=std::vector<WorldVertex>;
struct GroundTriangle {Vec3 a,b,c;float denominator;size_t firstVertex=0;};
enum class AIState { Dormant, Patrol, Pursue, Attack, Retreat, Evade, BombRun, Egress, Melee, Finished };
enum class EnemyMotor { Silent, Walker, Tracked, Turbine, Submersible, Creature, Machinery, CreatureWing, CreatureSwim, CreatureCrawl };
struct EnemyAI {
    EnemyMotor rpgMotor=EnemyMotor::Silent;
    EnemyCombatMotion combatMotion;
    rpg::VoidmawPoseCue voidmawPose{};
    rpg::SurfaceGravity gravity;
    uint32_t rpgGrapplePilot=0;
    AIState state=AIState::Dormant;
    std::array<unsigned,16> words{}; // Original parameters after the 32-bit target/flags word.
    int cargoGroup=-1;bool airborne=false;
    bool rpgFallEnabled=false;
    float fallSpeed=0;
    int playerTarget=-1; float retarget=0,gaitPhase=0;
    int targetSlot=0,identity=-1,parent=-1,generator=-1,attachment=-1;
    float stateTime=0,alert=0,lastHealth=0,maxHealth=0,respawnRange=0,spawnYaw=0;
    float weaponYaw=0,weaponPitch=0,bank=0,pitch=0,blocked=0,muzzleFlash=0;
    float secondClock=0,spawnDelay=0;unsigned burst=0,secondBurst=0,passes=0,side=0;
    Vec3 spawn{},localCenter{},localHalf{},velocity{},attachmentOffset{};
    bool boundsReady=false,registered=false,rocketUsed=false,rpgCrushing=false;
};
enum class Team { Neutral, Attackers, Defenders };
inline Vec3 teamColour(Team t){return t==Team::Attackers?Vec3{1,.38f,.08f}:t==Team::Defenders?Vec3{.08f,.72f,1}:Vec3{1,1,1};}
inline const char* teamName(Team t){return t==Team::Attackers?"ATTACKERS":t==Team::Defenders?"DEFENDERS":"NEUTRAL";}
struct Target {
    std::string generatorAsset; // Immutable generated-model identity, rebuilt from seed.
    Vec3 center,half; float health=100;
    Vec3 origin{}; float yaw=0; int type=-1,tag=255,spawnType=-1;
    uint32_t action=0; bool active=true,processed=false,protectedTarget=false,escaped=false,invulnerable=false;
    unsigned trigger=0,spawnFlags=0,parameters=0,deathFlag=0; float activationRange=10000,attackClock=0,speed=0;
    bool generated=false;
    EnemyAI ai;
    std::vector<Vec3> route; size_t waypoint=0;
    std::shared_ptr<WorldMesh> mesh,wreck;
    Vec3 fragmentSpan{};float fragmentStep=0;std::vector<unsigned> fragmentModels;
    int assetGame=-1; // Prototype provenance; campaign targets retain their existing IDs.
    bool prototypeObjective=false;
    bool rpgDecoration=false;
    float rpgWeakReveal=0;
    unsigned rpgEnemyInjuries=0,rpgEnemyMissingArms=0;
    Vec3 rpgTint{1,1,1}; // Derived campaign-boss appearance; default leaves other games unchanged.
    float rpgDeathAt=-1,rpgInitialHealth=0,rpgBurn=0,rpgRestY=0;
    bool rpgImpactSpent=false,rpgSecondarySpent=false;
    int proceduralZone=-1;
    Team team=Team::Neutral;
};
struct Generator {
    Target prototype;Vec3 origin;unsigned trigger=0,flags=0,remaining=0,burst=0,burstSize=0;
    float range=0,clock=0,interval=0,burstInterval=0;
    std::vector<size_t> slots;
    int source=-1;
};
struct ProceduralSpawnSite {
    Target prototype;
    Vec3 forward{0,0,-1};
    float roadHalfWidth=0;
};
struct ProceduralSpawner {
    ProceduralSpawnProfile profile=ProceduralSpawnProfile::Armour;
    int zone=0;
    std::vector<ProceduralSpawnSite> sites;
    size_t cursor=0;
    float clock=0;
    uint64_t spawned=0;
};
struct ProceduralSpawnSlot {size_t target=0;float cooldown=0;bool wasAlive=false;};
struct ProceduralPopulation {
    bool enabled=false;
    unsigned groundLimit=64,airLimit=8,groundPerZone=8,airPerZone=1;
    float interval=18,activationRange=600;
    std::vector<ProceduralSpawner> spawners;
    std::vector<ProceduralSpawnSlot> groundSlots,airSlots;
    size_t nextSpawner=0;
    uint64_t spawned=0;
};
struct CargoFlight {size_t carrier=0;std::vector<Target> choices;std::vector<size_t> slots;unsigned group=0,passes=0,release=0,salvo=0;float interval=0;};
enum class MissionStatus { Range, Active, Complete, Ejecting, Failed };
inline constexpr int rpgSafeBoxPickup=32;
struct Pickup { Vec3 position; int kind=0; bool collected=false; unsigned collectedBy=0; };
inline bool unclaimedSalvage(const Pickup &pickup,unsigned slot){
    return pickup.kind==rpgSafeBoxPickup&&slot<32&&!(pickup.collectedBy&(1u<<slot));
}
struct SoundEvent { enum Kind { Shot, Punch, Explosion, PickupSound, Damage, Complete, Failed, CutterMotor, LargeExplosion, Footstep, Special } kind; int weapon=0; Vec3 position{}; Weapon audioWeapon=Weapon::Empty; int transmitterActor=-1; };

struct WeaponEffect { enum Kind { Muzzle, Impact, Blast, Sparks, Debris, Smoke } kind=Impact;Vec3 position{},direction{};Weapon weapon=Weapon::Empty;float age=0,size=1;unsigned model=0;float damageRadius=0,damageRate=0; };
enum class ProjectileKind { Bullet, Rocket, Bomb };
enum class ProjectileVisual : uint8_t { Standard, WaveA, WaveB, LoftMissile };
struct Projectile { Vec3 position,velocity; float life=4,damage=25; int mount=0;
    ProjectileKind kind=ProjectileKind::Bullet;int owner=-1,victim=-1;float radius=0; Weapon weapon=Weapon::Empty;int shooter=-1,homingTarget=-1;
    float hull=-1;bool intercepted=false;
    ProjectileVisual visual=ProjectileVisual::Standard;
};
struct Missile {
    bool active=false;
    Vec3 position{};
    float yaw=0,pitch=0,life=0,launchYaw=0,headYaw=0,headPitch=0;
};
struct Gesture {
    Vec3 previous{}; bool valid=false,armed=true; float cooldown=0;
    bool sample(Vec3 position,bool tracked,float dt);
    void reset() { valid=false; armed=true; }
};
struct Pilot {
    Vec3 position{0,0,35};
    Vec3 transitDestination{};float transitCooldown=0;bool inTransit=false;
    float chassisYaw=0,torsoYaw=0,aimYaw=0,aimPitch=0; // torsoYaw is relative, limited to +/-90 degrees
    float facing() const { return wrap(chassisYaw+torsoYaw); }
    std::array<float,MountCount> cooldown{};
    std::array<unsigned,MountCount> shots{};
    std::array<unsigned,MountCount> burstCount{};
    // Test-range loadout capacities; original cartridge values are not decoded yet.
    std::array<unsigned,MountCount> ammo{400,400,40,40,60,8};
    Loadout weapons{Weapon::Rifle,Weapon::Rifle,Weapon::Rocket,Weapon::Rocket,Weapon::Grenade,Weapon::Cruise};
    ArmRig armRig{};
    std::array<Arm,2> arms{};
    std::array<unsigned,2> punches{};
    std::array<float,2> fistFlash{},punchRecovery{};
    std::array<Gesture,2> gestures{};
    std::array<bool,MountCount> wasFire{};
    std::array<bool,2> wasPunch{};
    std::array<bool,2> wasPing{};
    Missile missile;
    bool lifeSpent=false; float ejectionTime=0,health=100;
    std::array<float,8> damageUntil{};
    void markDamage(Vec3 towardSource,float now) {
        const auto q=gravityFacing();
        const auto local=rotate(Quaternion{-q.x,-q.y,-q.z,q.w},towardSource);
        if(!std::isfinite(local.x)||!std::isfinite(local.z)||std::hypot(local.x,local.z)<.05f)return;
        const int octant=int(std::lround(std::atan2(local.x,-local.z)*4/pi));
        damageUntil[(octant+8)%8]=now+1.15f;
    }
    MissionStatus missionStatus=MissionStatus::Range;
    std::array<float,MountCount> ammoRemainder{};
    unsigned playerSlot=0;
    float gaitPhase=0,walkingSpeed=0;
    float selfPingUntil=0,aimPingUntil=0;std::array<float,2> nextPing{};
    int pingTarget=-1,homingTarget=-1;Vec3 pingPosition{};
    Team team=Team::Neutral;
    bool searchlightOn=false,wasSearchlight=false;
    // RPG presentation is rebuilt from the replicated character sheet.
    int rpgChassis=-1;bool rpgHullVariant=false;
    unsigned rpgAimMask=7;
    float rpgVehicleBlend=0,rpgLanding=0,rpgBodyRecoil=0;
    Vec3 rpgThrust{}; // cockpit-local surge, rise and lateral roll
    float rpgThrustCharge=0;
    int rpgThrustCapacity=0;
    uint32_t rpgInjuries=0;
    unsigned rpgMissingArms=0;
    rpg::Knockdown rpgKnockdown;
    rpg::SurfaceGravity gravity;
    Quaternion gravityFacing() const {return compose(gravity.frame,angles(facing()));}
    Vec3 bodyPoint(float height) const {return position+gravity.up()*height;}
    bool injured(MechaInjury injury) const {return hasInjury(rpgInjuries,injury);}
    unsigned missingArms() const {return rpgMissingArms|(injured(MechaInjury::LostArm)&&!rpgMissingArms?1u:0u);}
    bool armAvailable(int hand) const {return hand>=0&&hand<2&&!(missingArms()&(1u<<hand));}
    bool sensorsWorking() const {return !injured(MechaInjury::Sensors);}
    bool targetingWorking() const {return !injured(MechaInjury::Targeting);}
    // Normalized pose in the authored weapon cycle. Zero is the ready position.
    std::array<float,MountCount> rpgWeaponPhase{};
    std::array<int,MountCount> rpgWeaponHandling{};
    std::array<std::string,MountCount> rpgModelNames{};
    std::array<std::string,2> rpgStashedModels{};
};
struct PassiveRadar {
    static constexpr float range=.18f/.0011f;
    static constexpr int sectors=12;
    float strength=0;
    bool exposesPilot=false;
    uint32_t sweep=0;
    size_t enemyPrefix=0;
    std::vector<Vec3> contacts,contactColors;
    std::vector<Vec3> objectives,allies;
    struct Arrival {Vec3 position;float progress;};
    std::vector<Arrival> arrivals;
    uint16_t distantBearings=0;
};
// Returns the contact speed once on touchdown. Downward speed is positive.
float stepMechaDescent(Vec3 &feet,float floor,float &downSpeed,float waterLevel,float dt);
struct World : Pilot {
    PassiveRadar rpgRadar;
    std::array<Weapon,2> rpgAudioWeapons{}; // Local unstowed weapon models before the generic hand-rig override.
    std::vector<Vec3> rpgSalvageDepots;
    struct RpgDrone {Vec3 position{};float yaw=0;unsigned owner=0;};
    std::vector<RpgDrone> rpgDrones;
    bool rpgDroneView=false;
    Vec3 rpgDroneEye{};
    float rpgDroneYaw=0,rpgDronePitch=0,rpgDroneRemaining=0;
    // Copies share a scene; loading a mission gives the renderer a fresh identity.
    std::shared_ptr<const int> renderIdentity=std::make_shared<const int>(0);
    bool cooperative=false;
    bool assault=false;
    bool rpgPrototype=false;
    SkyKind skyKind=SkyKind::Legacy;
    float rpgPoseLead=0; // Render-only time between simulation or network ticks.
    uint32_t rpgLocalPilot=0;
    int rpgCanopyAttacker=-1;
    bool rpgTransitActive=false;
    Vec3 rpgTransitPosition{};
    int rpgTransitReady=0,rpgTransitRequired=0;
    // Derived local presentation from the replicated RPG mission opener clock.
    int rpgInsertion=0;
    float rpgInsertionTime=0,rpgInsertionGround=0;
    std::array<std::string,MountCount> rpgReadouts{};
    int rpgShield=0,rpgAp=0,rpgMaxAp=0,rpgReactor=0,rpgPhase=0;
    std::string rpgPilotStatus;
    float rpgHeat=0,rpgArmor=100;bool rpgOverheated=false;
    EnvironmentSettings environment;
    Climate climate=Climate::Temperate;
    float environmentSeconds=-1;
    Weather previousSurfaceWeather=Weather::Clear;
    bool environmentEnabled() const {return assault||proceduralLayout||rpgPrototype;}
    bool exposedToPrecipitation(Vec3 point) const;
    EnvironmentSettings outdoorEnvironment() const;
    EnvironmentSettings environmentAt(Vec3) const;
    EnvironmentSettings activeEnvironment() const {return environmentAt(position);}
    float weatherTime() const {return environmentEnabled()&&environmentSeconds>=0?environmentSeconds:elapsed;}
    bool visibilityRules() const {return environmentEnabled();}
    bool canPerceive(const Pilot&,Vec3,bool revealed=false,int ignoredTarget=-1)const;
    int combatGame() const { return assault?1:game; }
    std::vector<Pilot> mapPilots; // snapshot-owned copies; never simulation pointers
    struct AimHit { Vec3 position; int target=-1; };
    AimHit traceAim(Vec3 origin,Vec3 ray,float range=1000,bool respectVisibility=true)const;
    void lockHoming();
    bool crushable(const Target&)const;
    void movePilot(Vec3 proposed);
    void stomp(unsigned foot);
    bool footContact(Vec3 center,const Target&)const;
    void pingMap(unsigned kind);
    Vec3 mapPingPosition(const Pilot&)const;
    enum class Difficulty { Normal, Hard } difficulty=Difficulty::Normal;
    std::vector<Pilot*> teammates;
    void damagePilot(Pilot&,float,Vec3 towardSource={});
    void collectPickups();
    void stepProjectiles(float dt);
    void configure(const Loadout&,const ArmRig& rig=ArmRig{});
    std::vector<Target> targets;
    std::vector<Generator> generators;
    std::vector<CargoFlight> cargoFlights;
    void dropCargo(size_t);
    void stepGenerators(float dt);
    void stepEnemies(float dt);
    void updateEnemyBounds(Target&);
    void resetEnemy(Target&);
    void retireEnemy(Target&,bool escaped=false);
    Vec3 enemyDestination(const Target&,int& victim) const;
    std::array<float,6> difficultyValues{}; // decoded projectile, impact and actor-health pairs
    float difficultyRatio=1;
    float nearestPlayerRange(Vec3) const;
    uint32_t randomState=0x49535852u;
    void selectPlayerTargets(float dt);
    bool enemyLineOfSight(Vec3 from,Vec3 to,int owner,int victim) const;
    bool moveEnemy(size_t,Vec3,float,bool flying=false,bool crushing=false,bool tracked=false);
    void enemyFire(size_t,Vec3,int,ProjectileKind,float,float,float=0,Vec3={});
    bool triggerReady(unsigned trigger) const;
    std::array<int,32> targetSlots{};
    std::vector<Projectile> projectiles;
    std::vector<WeaponEffect> effects;
    void strike(Vec3,Vec3,Weapon,float,bool sound=true);
    void destruction(Vec3,float,Weapon,float damageRadius=0,float damageRate=0);
    bool effect(WeaponEffect::Kind,Vec3,Vec3,Weapon,float=1);
    unsigned destroyed=0;
    int game=0; bool night=false;
    bool missionPrototype=false;
    bool proceduralLayout=false;
    bool proceduralTerrain=false;
    ProceduralMission procedural;
    ProceduralPopulation proceduralPopulation;
    uint32_t prototypeSeed=0;
    std::array<unsigned,23> actorDeaths{};
    std::vector<std::array<float,4>> objectiveRegions;
    int objectiveStage=0;
    void arrivedEnemy(Target&);
    int mission=-1,horizon=0,missionBuildingCount=0;
    float elapsed=0; uint32_t missionFlags=0; std::array<int,3> counters{};
    void stepEjection(float dt);
    Vec3 pilotPosition() const;
    Quaternion pilotOrientation() const;
    Vec3 groundColor{.13f,.20f,.19f},skyColor{.055f,.095f,.13f};
    Vec3 missionLight{-.3f,1,.6f}; // authored map direction in the deployed cockpit frame
    std::map<unsigned,std::vector<Vec3>> debrisVertices;
    std::array<WorldMesh,5> pickupMeshes;
    std::array<unsigned,32> pickupTypes{};
    void loadDecorations(const std::filesystem::path&);
    float debrisFloor(const WeaponEffect&) const;
    void indexTerrain();
    float surfaceHeight(float x,float z) const;
    float walkableHeight(Vec3 feet,float step=.85f) const;
    float pilotSupportHeight(Vec3 feet,float step=.85f) const;
    float waterLevelAt(Vec3 point) const;
    float deckContact(Vec3 from,Vec3 to) const;
    bool hasWater(float x,float z,float radius=0) const;
    bool groundPassable(Vec3 position,float radius=3.5f,bool allowDrop=false) const;
    float waterDepthAt(Vec3 position,float radius=0) const;
    float wadingDepthAt(Vec3 feet,float radius=0) const;
    float waterMovementScale(Vec3 position,float radius=0) const;
    float waterTraversalLimitAt(Vec3 position,float radius=0) const;
    bool indoors(Vec3 position) const;
    float interiorCeilingAt(Vec3 position,float radius=0) const;
    float groundContact(Vec3 from,Vec3 to) const;
    Vec3 pickupPosition(const Pickup&) const;
    Vec3 wreckPosition(const Target&) const;
    struct TerrainPatch { Vec3 low{},high{};size_t first=0,count=0; };
    struct MapGeometry {
        WorldMesh terrain,waterMesh,interiorDeckMesh;
        std::vector<GroundTriangle> groundTriangles;
        std::map<std::pair<int,int>,std::vector<size_t>> groundTiles;
        std::vector<TerrainPatch> terrainPatches;
    };
private:
    std::shared_ptr<MapGeometry> mapGeometry=std::make_shared<MapGeometry>();
public:
    const MapGeometry& geometry() const { return *mapGeometry; }
    MapGeometry& editGeometry() {
        // A renderer or a sibling world may still hold the previous map.
        if(renderIdentity.use_count()!=1)renderIdentity=std::make_shared<const int>(0);
        if(mapGeometry.use_count()!=1)mapGeometry=std::make_shared<MapGeometry>(*mapGeometry);
        return *mapGeometry;
    }
    std::array<float,4> movementBounds{-1000,-1000,1000,1000};
    std::vector<ProceduralWater> waterRegions;
    float maxWadeDepth=0;
    struct InteriorRegion {std::array<float,4> bounds{};float floor=0,ceiling=48;};
    std::vector<InteriorRegion> interiorRegions;
    struct InteriorDeck {
        std::array<float,4> bounds{};
        Vec3 start{},end{};
        float height(float x,float z) const;
    };
    std::vector<InteriorDeck> interiorDecks;
    std::vector<unsigned> interiorTierNodes;
    std::vector<std::array<float,4>> submergedRoutes;
    std::vector<Vec3> interiorNavNodes;
    std::vector<std::array<unsigned,2>> interiorNavLinks;
    Vec3 missionEntry{};
    bool hasMissionEntry=false;
    std::vector<std::array<float,4>> waterCrossings;
    std::vector<Pickup> pickups; std::vector<SoundEvent> sounds;
    std::string objective;
    void loadMission(const std::filesystem::path&,int,int campaign=0,bool sharedAssets=true);
    void loadMissionPrototype(const std::filesystem::path&,uint32_t seed);
    void loadProceduralPrototype(const std::filesystem::path&,const std::filesystem::path& recipe);
    void loadProceduralRecipe(const std::filesystem::path&,const nlohmann::json& recipe);
    void initIS2();void evaluateIS2();void destroyedIS2(Target&);
    void stepMission(float dt);
    void evaluateMission();
    bool destructionPending()const;
    void applyMissionAction(uint32_t);
    void targetDestroyed(Target&);
    void hurt(float damage,Vec3 towardSource={});

    World();
    void step(const Input&,float dt,bool shared=true);
    void resetTracking();
    rpg::KnockdownPose knockdownPose() const {return rpg::sampleKnockdown(rpgKnockdown,elapsed+rpgPoseLead);}
    Quaternion cockpitOrientation() const {return compose(gravityFacing(),knockdownPose().orientation);}
    Vec3 cockpitOrigin() const {return bodyPoint(12)+rotate(gravityFacing(),knockdownPose().offset);}
    Vec3 eye() const {
        return rpgDroneView?rpgDroneEye:cockpitOrigin()-gravity.up()*(3.7f*rpgVehicleBlend*rpgVehicleBlend*(3-2*rpgVehicleBlend));
    }
    void explode(Vec3 center,float radius,float damage,bool friendly=false);
};
}
