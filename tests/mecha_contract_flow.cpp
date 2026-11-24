#include "rpg/mission_board.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
static void require(bool valid,const char *message){if(!valid)throw std::runtime_error(message);}
int main(){
  Session host(42);host.add(1,"Host");host.add(2,"Guest");
  auto original=generateMissionBoard(host);
  host.command(2,{{"op","ready"},{"value",true}});
  assert(rejects([&]{host.command(2,{{"op","reroll_missions"}});}));
  assert(host.boardRevision==0&&host.member(2).ready);
  host.command(1,{{"op","reroll_missions"}});
  assert(host.boardRevision==1&&host.selectedMission==0);
  for(const auto &m:host.members)assert(!m.ready);
  auto refreshed=generateMissionBoard(host);
  for(int i=0;i<missionBoardSize;++i)assert(refreshed[i].seed!=original[i].seed);
  Session guest;guest.readSnapshot(host.snapshot());
  assert(guest.boardRevision==host.boardRevision);
  assert(Json(generateMissionBoard(guest))==Json(refreshed));
  host.command(1,missionSelectionCommand(4));
  host.command(1,{{"op","ready"},{"value",true}});host.command(2,{{"op","ready"},{"value",true}});
  assert(Json(host.mission.recipe)==Json(refreshed[4]));
  assert(rejects([&]{host.command(1,{{"op","reroll_missions"}});}));
  for(size_t i=0;i<host.enemies.size();++i)assert(host.enemies[i].tactic==host.mission.recipe.tactics[i]);
  host.phase=Phase::Complete;
  host.command(1,{{"op","hangar"}});assert(host.boardRevision==1&&host.selectedMission==0);
  host.phase=Phase::Complete;
  host.command(1,{{"op","next"}});assert(host.boardRevision==1&&host.phase==Phase::Briefing);

  // Prefer the marked street even when the direct line is navigable.
  World street;
  street.targets.clear();
  street.interiorNavNodes={{0,0,120},{38,0,120},{38,0,-120},{0,0,-120}};
  street.interiorNavLinks={{0,1},{1,2},{2,3}};
  const auto streetRoute=missionEscortRoute(street,{0,0,120},{0,0,-120},0);
  require(std::any_of(streetRoute.begin(),streetRoute.end(),[](Vec3 p){return p.x>37;}),
          "Escort cut across an open lot instead of following a connected road");

  // The last road node is beyond the exit. Do not drive past extraction
  // and reverse along the same lane with the rest of the convoy in tow.
  street.interiorNavNodes={{0,0,120},{0,0,0},{0,0,-300}};
  street.interiorNavLinks={{0,1},{1,2}};
  const auto exitRoute=missionEscortRoute(street,{0,0,120},{0,0,-252},0);
  require(std::none_of(exitRoute.begin(),exitRoute.end(),[](Vec3 p){return p.z<-265;}),
          "Convoy route doubled back down the extraction lane");

  // A convoy must follow corridors and ramps rather than the old straight Z lane.
  Session convoy(817);convoy.add(1,"Escort");
  GameSettings settings;settings.maps=1;settings.extent=500;settings.world=3;settings.location=7;
  settings.environment=3;settings.objective=2;settings.opener=5;settings.enemy=3;settings.enemyCount=1;
  convoy.command(1,{{"op","settings"},{"settings",settings}});
  convoy.command(1,{{"op","ready"},{"value",true}});convoy.command(1,{{"op","deploy"}});
  // Keep reinforcement insertion active to cover asset-index repair, while
  // isolating route traversal from hostile damage to this stationary escort.
  convoy.mission.recipe.complication=0;
  auto asset=convoy.mission.assets[0];
  Vec3 start=convoy.world.targets[asset].origin;
  const std::array<const char *,3> models{"escort_command","escort_cargo","escort_support"};
  require(convoy.mission.assets.size()==models.size(),"Escort failed to spawn all three vehicles");
  Session lateJoin;
  lateJoin.readSnapshot(convoy.snapshot());
  for(size_t i=0;i<models.size();++i){
    const auto index=convoy.mission.assets[i];
    require(convoy.world.targets[index].generatorAsset==models[i]&&
            lateJoin.world.targets[index].generatorAsset==models[i],
            "Convoy variant diverged on a joining client");
  }
  convoy.member(1).body.position={450,0,450};
  convoy.step(.1f);
  require(length(convoy.world.targets[convoy.mission.assets[0]].origin-start)<.001f,
          "Convoy continued without an escort");
  float maximumSpeed=0;
  for(int tick=0;tick<6000&&convoy.phase==Phase::Combat;++tick){
    for(size_t i=0;i<convoy.enemies.size();++i){
      convoy.enemies[i].disabled=100000;convoy.world.targets[i].active=false;
    }
    // Live reinforcements grow the enemy prefix and shift every scenery index.
    // The authoritative mission reference follows that insertion.
    auto &target=convoy.world.targets[convoy.mission.assets[0]];
    convoy.member(1).body.position=target.origin+Vec3{20,0,0};
    convoy.step(.1f);
    const auto &assets=convoy.mission.assets;
    maximumSpeed=std::max(maximumSpeed,length(convoy.world.targets[assets[0]].ai.velocity));
    for(size_t i=1;i<assets.size();++i){
      const auto &ahead=convoy.world.targets[assets[i-1]];
      const auto &behind=convoy.world.targets[assets[i]];
      require(length(ahead.origin-behind.origin)>9.f,"Convoy vehicles overlapped while turning");
    }
  }
  const auto &transport=convoy.world.targets[convoy.mission.assets[0]];
  if(convoy.phase!=Phase::Complete){
    const auto p=transport.origin;
    std::cerr<<"Convoy stalled: "<<p.x<<','<<p.y<<','<<p.z<<" from "<<start.x<<','<<start.z
      <<" phase "<<int(convoy.phase)<<" elapsed "<<convoy.mission.manualElapsed
      <<" deadline "<<convoy.mission.recipe.countdownSeconds<<'\n';
  }
  require(convoy.phase==Phase::Complete&&transport.health>0&&convoy.mission.progress==1,
          "Convoy did not reach extraction intact");
  require(convoy.mission.assets[0]>asset&&convoy.enemies.size()>1,
          "Escort test failed to exercise reinforcement index shifts");
  require(maximumSpeed>=10.f,"Convoy failed to reach its faster cruising pace");
  Vec3 exit{convoy.mission.exit[0],convoy.mission.exit[1],convoy.mission.exit[2]};
  require(length(transport.origin-exit)<1,"Lead vehicle did not stop at extraction");
  std::cout<<"Fresh host-authoritative contracts, replication and connected convoy routes verified\n";
}
