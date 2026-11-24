#include "rpg/catalog.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
namespace {
void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
const EnemySpec &named(const char *name){for(const auto &s:enemyCatalog())if(std::string_view(s.id)==name)return s;throw std::runtime_error(std::string("Missing enemy ")+name);}
}
int main(){try{
  setOriginalAssetRoot(MECHA_ASSETS);
  require(enemyCatalog().size()==66,"Mission Manual must add all32 enemies to original34");
  require(std::string_view(enemySpec(0).id)=="jump_pod"&&std::string_view(enemySpec(33).id)=="kaiju_obliterator","Original enemy indices changed");
  const std::map<std::string,int> source={
    {"bomber",1},{"cyclone_drone",1},{"interceptor",1},{"kaiju_flea",1},{"sentry_gun",1},{"swarm_drone",1},
    {"aeon_diode",2},{"neo_knight",2},{"terran_grunt",2},{"deepdiver_submarine",3},{"veriform_mecha",3},{"verilian_guardian",3},
    {"funnel_drone_mecha",4},{"gunboat",4},{"kaiju_dismantler",4},{"abyssal_kaiju",5},{"heavy_hover_tank",5},{"carrier",5},
    {"artillery_battery",6},{"thunder_cannon_mecha",6},{"verilian_goliath",6},{"combiner_mecha",7},{"cruiser",7},{"kaiju_broodqueen",7},
    {"aeon_primarch",8},{"neo_paladin",8},{"terran_mammoth",8},{"aegis_mecha",9},{"gladius_mecha",9},{"kaiju_tunnel_wurm",9},
    {"dreadnought",10},{"verilian_gargantuan",10}};
  std::set<std::string> seen;
  for(size_t i=34;i<enemyCatalog().size();++i){const auto &s=enemySpec(i);
    require(source.at(s.id)==s.hd,"Manual HD differs for "+std::string(s.id));
    require(seen.insert(s.id).second,"Duplicate manual enemy");
    auto mesh=originalMesh(enemyModelId(i));
    require(!mesh->empty()&&bool(enemyAnimationRig(std::filesystem::path(MECHA_ASSETS)/"models",enemyModelId(i),mesh->size())),"Manual enemy lacks an available animated model rig");
    require(!s.attacks.empty(),"Manual enemy has no attacks");
    for(const auto &a:s.attacks)require(a.dice>=0&&a.sides>=4&&a.minimum<a.range&&a.defense>=0&&a.defense<=3&&a.targets>=1&&a.targets<=8,"Malformed attack for "+std::string(s.id));
    if(s.traits&Starship)require((s.traits&Flying)!=0,"Starship cannot fly");
    if(s.traits&UnstableReactor)require(s.deathDice>0&&s.deathSides>=6,"Unstable reactor has no death pulse");
    if(s.kind==EnemyKind::SmallKaiju||s.kind==EnemyKind::LargeKaiju)require(enemyFaction(i)==3,"Kaiju assigned to human faction");
  }
  require(named("bomber").attacks[1].alternateDefense==3&&named("interceptor").attacks[1].alternateDefense==2&&named("neo_knight").attacks[0].alternateDefense==0&&named("neo_paladin").attacks[0].alternateDefense==0,"Alternative pilot defense choices lost");
  const auto &dread=named("dreadnought");require(dread.shield==10&&dread.shieldDice==2&&(dread.traits&Elite),"Dreadnought lost2d10 shields/elite cadence");
  const auto &garg=named("verilian_gargantuan");require(garg.plating==8&&garg.deathDice==2&&garg.deathSides==10,"Gargantuan defenses/reactor pulse differ from source");
  require(named("kaiju_dismantler").attacks[1].effect==AttackEffect::Immobilize,"Caustic web lost movement-only immobilization");
  require(named("deepdiver_submarine").attacks[1].effect==AttackEffect::DisruptMobility,"Sonar lost mobility disruption");
  require(named("aeon_primarch").attacks[2].effect==AttackEffect::DisruptSystems,"Disruption field lost system disruption");
  require(named("verilian_gargantuan").attacks[2].defense==0,"Power defense overwritten by legacy reactor heuristic");
  require(named("kaiju_broodqueen").attacks[2].defense==3,"Presence defense overwritten by legacy reactor heuristic");
  require((named("aegis_mecha").traits&PairAegis)&&(named("gladius_mecha").traits&PairGladius),"Paired mecha lost synergy traits");
  std::cout<<"All32 Mission Manual enemies retain source combat values, existing indices, faction identity and available model rigs\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
