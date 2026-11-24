#include "rpg/snapshot.hpp"
#include "rpg/session.hpp"

namespace iron::rpg {
namespace {
void transfer(Json &from,Json &to,const char *key){
  auto it=from.find(key);
  if(it!=from.end()){
    to[key]=std::move(*it);
    from.erase(it);
  }
}

// Ordered common columns. Preserve optional and future fields in a trailing
// object so that the canonical snapshot reader retains all animation and FX data.
constexpr const char *enemyColumns[]={"spec","hp","maxHp","tactic","cover","ambushed","decisionClock",
  "fireClock","disabled","corrosion","corrosionOwner","regenClock","anchorX","anchorZ","transformed",
  "spawned","manualClock","combined","group","boss","bossPhase","bossTell","heat","ventRemaining",
  "overheated","injuries","missingArms","subsystemDamage"};
constexpr const char *projectileColumns[]={"position","velocity","life","damage","mount","kind","owner",
  "victim","shooter","radius","weapon","homingTarget","hull","intercepted"};

template<size_t N> void packRows(Json &rows,const char *const (&columns)[N]){
  for(auto &object:rows){
    Json row=Json::array();
    auto &values=row.get_ref<Json::array_t&>();
    values.reserve(N+1);
    for(auto column:columns){
      auto it=object.find(column);
      if(it==object.end())throw std::logic_error("Missing compact snapshot column");
      values.push_back(std::move(*it));object.erase(it);
    }
    if(!object.empty())values.push_back(std::move(object));
    object=std::move(row);
  }
}
template<size_t N> void unpackRows(Json &rows,const char *const (&columns)[N],size_t limit){
  if(!rows.is_array()||rows.size()>limit)throw std::runtime_error("Invalid compact snapshot row count");
  for(auto &row:rows){
    if(!row.is_array()||(row.size()!=N&&row.size()!=N+1))throw std::runtime_error("Invalid compact snapshot row");
    Json obj=row.size()==N?Json::object():std::move(row[N]);
    if(!obj.is_object())throw std::runtime_error("Invalid compact snapshot extensions");
    for(size_t i=0;i<N;++i)obj[columns[i]]=std::move(row[i]);
    row=std::move(obj);
  }
}
}
Json packRpgSnapshot(Json &state){
  Json baseline=Json::object();
  for(auto key:{"campaign","settings"})transfer(state,baseline,key);
  auto &mission=state.at("mission");
  for(auto key:{"recipe","assets","sites","depots","exit","mapCount","contractReward"})
    transfer(mission,baseline["mission"],key);
  baseline["sheets"]=Json::object();
  for(auto &player:state.at("players")){
    auto &sheet=player.at("sheet");
    Json live=Json::object();
    for(auto key:{"hp","ap","reactor","overheated","reactorStress","ventRemaining","kineticCharge",
                  "targetLock","hidden","repairKits","batteryDie"})
      transfer(sheet,live,key);
    baseline["sheets"][std::to_string(player.at("id").get<uint32_t>())]=std::move(sheet);
    sheet=std::move(live);
  }
  packRows(state.at("enemies"),enemyColumns);
  packRows(state.at("world").at("projectiles"),projectileColumns);
  return baseline;
}
void unpackRpgSnapshot(Json &state,const Json &baseline){
  for(auto key:{"campaign","settings"})state[key]=baseline.at(key);
  state.at("mission").update(baseline.at("mission"));
  for(auto &p:state.at("players"))
    p.at("sheet").update(baseline.at("sheets").at(std::to_string(p.at("id").get<uint32_t>())));
  unpackRows(state.at("enemies"),enemyColumns,maxEncounterTargets);
  unpackRows(state.at("world").at("projectiles"),projectileColumns,maxLiveProjectiles);
}
}
