#pragma once
#include "rpg/catalog.hpp"
namespace iron::rpg {
class Session;
struct MissionRecipe;
enum class EncounterRole {Patrol,Garrison,Ambush,Assault,AirWing,Pack};
enum class FactionRole { Contract, Neutral, Allied };
// Host-only shared geometry and timings for a surface unit attack run.
struct FormationRun {
  Vec3 axis{},mark{};
  float breakAt=0,regroupAt=0;
  bool left=false;
};
struct EncounterGroup {
  int role=0,site=-1,cursor=0;
  std::vector<int> actors;
  std::vector<std::array<float,3>> route;
  float alert=0;
  bool rewardPlaced=false;
  FactionRole factionRole=FactionRole::Contract;
  int faction=-1;
  bool withdrawing=false,retaliating=false;
  std::array<float,3> withdrawal{};
  FormationRun run;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EncounterGroup,role,site,cursor,actors,route,alert,rewardPlaced,factionRole,faction,withdrawing,retaliating,withdrawal)
struct DungeonRoom {
  std::array<float,3> position{};
  int kind=0;
  bool visited=false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DungeonRoom,position,kind,visited)
struct DungeonSite {
  int style=0,core=-1;
  std::array<float,4> bounds{};
  std::array<float,3> entrance{};
  std::vector<DungeonRoom> rooms;
  std::vector<std::array<int,2>> links;
  bool cleared=false,cacheClaimed=false;
  bool underground=false;
  int moduleCacheRoom=-1;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DungeonSite,style,core,bounds,entrance,rooms,links,cleared,cacheClaimed,underground,moduleCacheRoom)
const char *encounterRoleName(int);
int actorFaction(const Session &,size_t);
const EncounterGroup *factionDetachment(const Session &,size_t);
bool factionAttacksPlayers(const Session &,size_t);
bool factionsAtWar(const Session &,size_t,size_t);
std::vector<int> factionCombatContacts(const Session &);
void planMissionEncounters(Session &);
void stepEncounterGroups(Session &,float);
void validateEncounterState(const Session &,size_t sceneTargets);
std::vector<DungeonSite> buildDungeonSites(World &,const nlohmann::json &layout,int biome,int location,int region,int count);
}
