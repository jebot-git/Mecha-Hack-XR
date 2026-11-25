#include "rpg/manual_enemy_ai.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/enemy_tactics.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/weapon_audio.hpp"
#include "rpg/kaiju_audio.hpp"
#include <limits>
#include <stdexcept>
namespace iron::rpg {
namespace {
int findEnemy(std::string_view id){for(size_t i=0;i<enemyCatalog().size();++i)if(enemySpec(i).id==id)return int(i);throw std::runtime_error("Unknown manual reinforcement");}
std::string visualId(const Enemy &enemy,uint32_t seed,size_t index){
  const auto &spec=enemySpec(size_t(enemy.spec));
  const char *base=enemyModelId(size_t(enemy.spec));
  if((spec.traits&Veriform)&&enemy.transformed)base=enemyModelId(size_t(findEnemy("interceptor")));
  if((spec.traits&Combiner)&&enemy.combined)base="combiner_mecha_combined";
  return originalVariant(base,seed^uint32_t(index*0x9e3779b9u));
}
void shift(int &index,size_t at){if(index>=0&&size_t(index)>=at)++index;}
// Scenery keeps its map-relative index when the actor prefix changes size.
void shiftWorldIndices(World &world,size_t previous,size_t current){
  const size_t first=std::min(previous,current);
  auto removed=[&](size_t index){return index>=first&&index<previous;};
  auto offset=[&](size_t index){return index<first?index:index-previous+current;};
  auto remap=[&](int &index){
    if(index>=0)index=removed(size_t(index))?-1:int(offset(size_t(index)));
  };
  auto slots=[&](std::vector<size_t> &indices){
    std::erase_if(indices,removed);
    for(auto &index:indices)index=offset(index);
  };
  for(auto &target:world.targets){remap(target.ai.parent);remap(target.ai.attachment);}
  for(auto &slot:world.targetSlots)remap(slot);
  for(auto &generator:world.generators){remap(generator.source);slots(generator.slots);}
  std::erase_if(world.cargoFlights,[&](const auto &flight){return removed(flight.carrier);});
  for(auto &flight:world.cargoFlights){flight.carrier=offset(flight.carrier);slots(flight.slots);}
  auto population=[&](auto &list){
    std::erase_if(list,[&](const auto &slot){return removed(slot.target);});
    for(auto &slot:list)slot.target=offset(slot.target);
  };
  population(world.proceduralPopulation.groundSlots);population(world.proceduralPopulation.airSlots);
  for(auto &shot:world.projectiles){remap(shot.owner);remap(shot.homingTarget);}
  remap(world.pingTarget);remap(world.homingTarget);
}
bool clearSpawn(const World &world,const Target &body,Vec3 p,float radius,bool flying,int ignore){
  if(!insideArea(p,std::max(100.f,world.movementBounds[2])*.97f))return false;
  if(!flying)return clearPosition(world,p,radius,ignore);
  if(p.y+body.ai.localCenter.y+body.ai.localHalf.y>world.interiorCeilingAt(p,radius)-2)return false;
  for(size_t i=0;i<world.targets.size();++i){const auto &t=world.targets[i];
    if(int(i)==ignore||!t.active||t.health<=0||t.escaped)continue;
    if(std::abs(p.x-t.center.x)<radius+t.half.x&&std::abs(p.z-t.center.z)<radius+t.half.z&&
       p.y+body.ai.localCenter.y+body.ai.localHalf.y>t.center.y-t.half.y&&
       p.y+body.ai.localCenter.y-body.ai.localHalf.y<t.center.y+t.half.y)return false;
  }
  return true;
}
}
bool npcAllyInRange(const Session &s,size_t index,unsigned trait,float range){
  if(index>=s.enemies.size()||index>=s.world.targets.size())return false;
  for(size_t i=0;i<s.enemies.size();++i)if(i!=index&&s.enemies[i].hp>0&&s.world.targets[i].active&&!s.world.targets[i].escaped&&!factionsAtWar(s,index,i)&&
     (!trait||(combatSpec(s.enemies[i]).traits&trait))&&
     length(s.world.targets[i].origin-s.world.targets[index].origin)<=range)return true;
  return false;
}
float manualEnemySpeed(const EnemySpec &spec,int biome){return (spec.traits&Starship)||((spec.traits&Submersible)&&biome==2)?2.f:1.f;}
float manualEnemySpread(const EnemySpec &spec,float range){return (spec.traits&Starship)&&range>30?2.f:1.f;}
int manualEnemyDamageBonus(const Session &s,size_t index){return index<s.enemies.size()&&(combatSpec(s.enemies[index]).traits&HiveMind)&&npcAllyInRange(s,index,0,90)?2:0;}
int manualEnemyExtraDice(const Session &s,size_t index){
  if(index>=s.enemies.size())return 0;
  const auto &enemy=s.enemies[index];const auto traits=combatSpec(enemy).traits;
  return int(enemy.combined&&bool(traits&Combiner))+int(bool(traits&(PairAegis|PairGladius))&&npcAllyInRange(s,index,traits&PairAegis?PairGladius:PairAegis,90));
}
int manualEnemyArmorDamage(const EnemySpec &spec,int amount){return std::max(0,amount)*((spec.traits&Dismantler)?2:1);}
void syncManualEnemyModels(World &world,std::span<const Enemy> enemies,size_t previousCount,const MissionRecipe *story){
  if(previousCount>world.targets.size()||enemies.size()>maxEncounterEnemies||
     world.targets.size()-previousCount+enemies.size()>maxEncounterTargets)
    throw std::runtime_error("Invalid manual enemy prefix");
  if(previousCount!=enemies.size()){
    shiftWorldIndices(world,previousCount,enemies.size());
    if(previousCount<enemies.size())
      world.targets.insert(world.targets.begin()+previousCount,enemies.size()-previousCount,Target{});
    else
      world.targets.erase(world.targets.begin()+enemies.size(),world.targets.begin()+previousCount);
  }
  for(size_t i=0;i<enemies.size();++i){
    auto &body=world.targets[i];auto id=visualId(enemies[i],world.prototypeSeed,i);
    if(enemies[i].boss<0&&!enemies[i].transformed&&!enemies[i].combined)id=storyEnemyModel(story,enemies[i].spec,id);
    if(body.generatorAsset!=id)applyOriginalModel(body,id);
    body.spawnType=3;body.ai.maxHealth=float(enemies[i].maxHp);
  }
}
void Session::stepManualEnemies(float dt){
  if(!std::isfinite(dt)||dt<=0)return;
  for(size_t i=0;i<enemies.size();++i){auto &enemy=enemies[i];if(enemy.hp<=0)continue;
    if(enemy.arrivalDelay>0){
      enemy.arrivalDelay=std::max(0.f,enemy.arrivalDelay-dt);
      if(enemy.arrivalDelay>0)continue;
      world.targets[i].active=true;tacticalClock=0;
      if(enemy.burrowImpact){enemy.burrowImpact=false;voidmawEruption(i);}
    }
    const auto &spec=combatSpec(enemy);enemy.manualClock=std::max(0.f,enemy.manualClock-dt);
    if(spec.traits&Veriform){float nearest=std::numeric_limits<float>::infinity();
      auto &body=world.targets[i];
      for(const auto &m:members)if(!m.ejected&&m.sheet.hp>0){
        const auto offset=m.body.position-body.origin;
        nearest=std::min(nearest,std::hypot(offset.x,offset.z));
      }
      if(nearest>35)enemy.transformed=true;else if(nearest<20)enemy.transformed=false;
      body.ai.airborne=enemy.transformed;
      if(enemy.transformed)body.ai.fallSpeed=0;
    }
    if((spec.traits&Combiner)&&!enemy.combined&&enemy.disabled<=0){
      for(size_t j=i+1;j<enemies.size();++j)if(enemies[j].hp>0&&!enemies[j].combined&&
          (combatSpec(enemies[j]).traits&Combiner)&&!factionsAtWar(*this,i,j)&&length(world.targets[i].origin-world.targets[j].origin)<=10){
        enemy.hp=std::min(enemy.hp*2,enemy.maxHp*2);enemy.maxHp*=2;enemy.combined=true;
        enemies[j].hp=0;world.targets[j].health=0;world.targets[j].active=false;
        world.targets[i].health=float(enemy.hp);world.targets[i].ai.maxHealth=float(enemy.maxHp);
        log("HOSTILE COMBINER LINK ACTIVE");break;
      }
    }
  }
  syncManualEnemyModels(world,enemies,enemies.size(),&mission.recipe);
}
bool Session::spawnManualEnemy(int specIndex,Vec3 desired,int tactic,unsigned arrival,Vec3 approach,bool leadArrival,FactionRole factionRole,size_t *actorIndex){
  const auto &spec=enemySpec(size_t(specIndex));
  const bool kaiju=spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju;
  if(arrival&&kaiju&&arrival!=rpgArrivalBurrow)arrival=rpgArrivalKaijuRift;
  if(arrival==rpgArrivalBurrow&&!canBurrow(spec))return false;
  if(enemies.size()>maxEncounterEnemies||world.targets.size()>maxEncounterTargets)return false;
  const bool ambient=arrival!=0;
  size_t slot=enemies.size();
  if(factionRole==FactionRole::Contract)for(size_t i=0;i<enemies.size();++i)
    if(enemies[i].ambient==ambient&&enemies[i].hp<=0&&!world.targets[i].protectedTarget&&
       !factionDetachment(*this,i)){slot=i;break;}
  const bool insert=slot==enemies.size();
  if(insert&&(enemies.size()>=maxEncounterEnemies||world.targets.size()>=maxEncounterTargets))return false;
  if(insert&&!ambient&&world.proceduralTerrain&&settings.enemy<0&&settings.enemyCount==0&&
     std::count_if(enemies.begin(),enemies.end(),[](const Enemy &e){return !e.ambient;})>=maxEncounterEnemies-8)return false;
  Enemy fresh;fresh.spec=specIndex;fresh.hp=fresh.maxHp=spec.hd*8;fresh.tactic=std::clamp(tactic,0,9);
  fresh.fireClock=1;fresh.manualClock=12;fresh.ambient=ambient;
  fresh.arrivalDelay=arrival==rpgArrivalDropship?2.45f:arrival==rpgArrivalWarp?1.35f:
      arrival==rpgArrivalKaijuRift?1.72f:arrival==rpgArrivalBurrow?1.55f:.65f;
  if(!ambient)fresh.arrivalDelay=0;
  fresh.burrowImpact=arrival==rpgArrivalBurrow;
  fresh.burrowReady=canBurrow(spec)&&!fresh.burrowImpact;
  Target body;applyOriginalModel(body,storyEnemyModel(&mission.recipe,fresh.spec,visualId(fresh,world.prototypeSeed,slot)));
  const float radius=std::clamp(std::hypot(body.half.x,body.half.z)+1,3.5f,18.f);
  const bool flying=(spec.traits&Flying)!=0;bool placed=false;
  for(int attempt=0;attempt<32&&!placed;++attempt){
    float angle=float(attempt%8)*pi/4+float(slot%5)*.17f;
    float distance=attempt==0?0.f:float((attempt+7)/8)*(radius+8);
    auto p=desired+Vec3{std::cos(angle)*distance,0,std::sin(angle)*distance};
    p.y=world.surfaceHeight(p.x,p.z)+(flying?22.f:0.f);
    if(ambient){
      if(std::any_of(members.begin(),members.end(),[&](const Member &m){
        return !m.ejected&&m.sheet.hp>0&&std::hypot(p.x-m.body.position.x,p.z-m.body.position.z)<32.f;
      }))continue;
      if(!flying&&world.waterDepthAt(p,radius)>world.waterTraversalLimitAt(p,radius))continue;
      if((spec.traits&Submersible)&&world.waterDepthAt(p)<3.f)continue;
      bool reserved=false;
      for(size_t i=0;i<enemies.size();++i){
        if(enemies[i].arrivalDelay<=0)continue;
        const auto &pending=world.targets[i];
        if(std::hypot(p.x-pending.origin.x,p.z-pending.origin.z)<radius+std::hypot(pending.half.x,pending.half.z)+2.f){
          reserved=true;break;
        }
      }
      if(reserved)continue;
    }
    if(clearSpawn(world,body,p,radius,flying,insert?-1:int(slot))){body.origin=p;placed=true;}
  }
  if(!placed)return false;
  body.ai.airborne=flying;world.updateEnemyBounds(body);body.health=float(fresh.hp);body.ai.maxHealth=float(fresh.maxHp);
  if(ambient){
    body.yaw=std::atan2(-approach.x,-approach.z);
    body.ai.weaponYaw=body.yaw;
    const float phase=std::atan2(approach.z,approach.x)-float(slot%7)*.3f;
    fresh.patrolStep=unsigned((int(std::lround(phase/(pi*.5f)))+8)%4);
    world.updateEnemyBounds(body);
  }
  body.spawnType=3;body.active=!ambient;fresh.anchorX=body.origin.x;fresh.anchorZ=body.origin.z;
  if(insert){
    shiftWorldIndices(world,slot,slot+1);
    for(auto &index:mission.assets)if(index>=int(slot))++index;
    for(auto &enemy:enemies)shift(enemy.chargeWorldTarget,slot);
    for(auto &depot:mission.depots)if(depot.target>=int(slot))++depot.target;
    for(auto &site:mission.sites)if(site.core>=int(slot))++site.core;
    for(auto &shot:ordnance){shift(shot.body.owner,slot);shift(shot.body.homingTarget,slot);}
    for(auto &m:members){shift(m.body.pingTarget,slot);shift(m.body.homingTarget,slot);shift(m.precogTarget,slot);for(auto &target:m.beltTarget)shift(target,slot);}
    world.targets.insert(world.targets.begin()+slot,std::move(body));enemies.push_back(fresh);navigation.emplace_back();
  }else{
    for(auto &shot:ordnance)if(shot.body.homingTarget==int(slot))shot.body.homingTarget=-1;
    for(auto &enemy:enemies)if(enemy.chargeWorldTarget==int(slot))enemy.chargeWorldTarget=-1;
    world.targets[slot]=std::move(body);enemies[slot]=fresh;if(slot<navigation.size())navigation[slot]=Navigation{};
  }
  for(auto &group:mission.groups)std::erase(group.actors,int(slot));
  int group=-1;float nearest=120;
  for(size_t i=0;i<enemies.size();++i)if(factionRole==FactionRole::Contract&&!ambient&&i!=slot&&enemies[i].hp>0&&enemies[i].group>=0&&
     !factionDetachment(*this,i)&&enemyUnitsCompatible(spec,combatSpec(enemies[i]))){
    float distance=length(world.targets[i].origin-world.targets[slot].origin);if(distance<nearest){nearest=distance;group=enemies[i].group;}}
  enemies[slot].group=group;if(group>=0&&size_t(group)<mission.groups.size())mission.groups[size_t(group)].actors.push_back(int(slot));
  // A recycled slot is a new actor, including its disposable host intent.
  if(slot<enemyMemory.size())enemyMemory[slot]=EnemyMemory{};
  if(slot<tacticalDecisions.size())tacticalDecisions[slot]=EnemyTacticalDecision{};
  if(leadArrival&&factionRole==FactionRole::Contract)
    addEffect({WeaponEffect::Sparks,world.targets[slot].origin,{},Weapon::Empty,0,0,rpgRadarArrival});
  if(ambient&&(arrival!=rpgArrivalDropship||leadArrival)){
    const auto p=world.targets[slot].origin;
    addEffect({WeaponEffect::Sparks,p,approach,Weapon::Empty,0,4.f,arrival});
    const auto cue=arrival==rpgArrivalWarp?CombatCue::ArrivalWarp:
        arrival==rpgArrivalBurrow?CombatCue::ArrivalBurrow:
        arrival==rpgArrivalKaijuRift?kaijuCall(spec):
        arrival==rpgArrivalAirstrike?CombatCue::EnemyThrust:
        arrival==rpgArrivalDropship?CombatCue::ArrivalDropship:CombatCue::EnemyAlert;
    const auto source=p+Vec3{0,arrival==rpgArrivalDropship?12.f:
        (arrival==rpgArrivalWarp||arrival==rpgArrivalKaijuRift)?5.f:0.f,0};
    if(leadArrival&&world.sounds.size()<96)world.sounds.push_back({SoundEvent::Special,int(cue),source});
  }
  if(actorIndex)*actorIndex=slot;
  tacticalClock=0;
  return true;
}
void Session::reinforceManualEnemies(){
  // Take the original size: no newly spawned actor gets a second turn this tick.
  const size_t count=enemies.size();
  for(size_t i=0;i<count;++i){
    if(enemies[i].ambient||enemies[i].hp<=0||enemies[i].disabled>0||enemies[i].manualClock>0)continue;
    std::string_view id=combatSpec(enemies[i]).id;
    if(id!="carrier"&&id!="kaiju_broodqueen"&&id!="dreadnought")continue;
    enemies[i].manualClock=12;
    if(dice.roll(6)<5)continue;
    int amount=id=="dreadnought"?dice.roll(8)+2:dice.roll(4)+1;
    bool largeBrood=id=="kaiju_broodqueen"&&dice.roll(2)==2;if(largeBrood)amount=1;
    const auto origin=world.targets[i].origin;const int tactic=enemies[i].tactic;
    bool lead=true;
    for(int n=0;n<amount;++n){
      int child;
      if(id=="kaiju_broodqueen"){
        if(largeBrood){std::vector<int> choices;for(size_t c=0;c<enemyCatalog().size();++c){const auto &s=enemySpec(c);if((s.kind==EnemyKind::SmallKaiju||s.kind==EnemyKind::LargeKaiju)&&s.hd<=4)choices.push_back(int(c));}child=choices[size_t(dice.roll(int(choices.size()))-1)];}
        else child=findEnemy("kaiju_spawnling");
      }else child=findEnemy(dice.roll(2)==1?"interceptor":"bomber");
      float angle=float(n)*2.399963f;Vec3 desired=origin+Vec3{std::cos(angle)*32,0,std::sin(angle)*32};
      if(spawnManualEnemy(child,desired,tactic,0,{},lead)){
        if(lead&&id=="kaiju_broodqueen")emitKaijuSound(world,CombatCue::KaijuShriek,world.targets[i].center);
        ++enemies[i].spawned;
        lead=false;
      }
    }
  }
}
} // namespace iron::rpg
