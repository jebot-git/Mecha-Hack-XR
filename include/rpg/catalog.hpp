#pragma once
#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <span>
namespace iron::rpg {
inline constexpr int encounterThreatLimit(int tier,int stage=1){return tier>=2?12:4+3*tier+(stage-1)/3;}
inline constexpr int maxInitialEnemies=48,maxEncounterEnemies=64,maxEncounterTargets=1536;
enum class EnemyKind {
  Conventional,
  SmallMecha,
  Installation,
  Labor,
  SmallKaiju,
  LargeKaiju,
  LargeMecha
};
enum class AttackEffect {
  Damage,
  Reactor,
  Stun,
  Blind,
  Pull,
  Ongoing,
  DoubleReactor,
  Consume,
  Push,
  Immobilize,
  DisruptMobility,
  DisruptSystems
};
enum EnemyTrait : unsigned {
  Hover = 1,
  Flying = 2,
  Leap = 4,
  Camo = 8,
  Commander = 16,
  Ace = 32,
  Regenerate = 64,
  Carrier = 128,
  ShockArmor = 256,
  Barricaded = 512,
  Starship = 1024, Submersible = 2048, Swarm = 4096, Ambush = 8192,
  HiveMind = 16384, ShieldWall = 32768, UnstableReactor = 65536,
  Elite = 131072, Dismantler = 262144, Combiner = 524288,
  PairAegis = 1048576, PairGladius = 2097152, Veriform = 4194304
};
struct EnemyAttack {
  const char *name;
  int dice = 1, sides = 4, targets = 1;
  float minimum = 0, range = 30;
  int recharge = 0;
  AttackEffect effect = AttackEffect::Damage;
  bool reactor = false;
  int defense = 1;
  int alternateDefense = -1;
};
struct EnemySpec {
  const char *id;
  const char *name;
  int hd;
  EnemyKind kind;
  float optimal;
  int plating = 0, shield = 0, acid = 0;
  unsigned traits = 0;
  std::vector<EnemyAttack> attacks;
  int shieldDice = 1, deathDice = 0, deathSides = 6;
  const char *model = nullptr;
};
std::span<const EnemySpec> enemyCatalog();
const EnemySpec &enemySpec(size_t);
const char *enemyModelId(size_t);
inline bool groundDriveTank(const EnemySpec &spec){
  const std::string_view id=spec.id;
  return id=="anti_mecha_hovertank"||id=="heavy_hover_tank";
}
struct EnvironmentSpec {
  const char *id;
  const char *name;
  float speed = 1, sensors = 1;
  Vec3 color;
};
std::span<const EnvironmentSpec> environmentCatalog();
int enemyFaction(size_t);
inline constexpr std::array<const char *, 4> factions = {
    "Aeonic Primacy", "Terran Collective", "Neo Dominion", "Voidmaw"};
inline constexpr std::array<const char *, 4> locations = {
    "Aeon Prime", "Slipstar", "Fortress Husqvarna", "Verilian Tunnels"};
struct GameSettings {
  int faction = 1, world = 0, theme = -1, objective = -1, location = -1,
      opener = -1, tactic = -1, environment = -1, enemy = -1;
  float extent = 1000;
  int enemyCount = 0; // Automatic party-scaled encounter budget; positive is an explicit override.
  int maps = 0; // Procedural length; positive values are developer-fixture overrides.
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GameSettings, faction, world, theme,
                                   objective, location, opener, tactic,
                                   environment, enemy, extent, enemyCount,maps)
void validateSettings(const GameSettings &);
} // namespace iron::rpg
