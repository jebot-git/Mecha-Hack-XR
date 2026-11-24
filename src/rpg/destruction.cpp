#include "rpg/session.hpp"
#include "rpg/destruction.hpp"
#include "rpg/kaiju_audio.hpp"
#include "rpg/manual_missions.hpp"
#include <algorithm>
namespace iron::rpg {
namespace {
float age(const World &w,const Target &t){return t.rpgDeathAt<0?-1:std::max(0.f,w.elapsed-t.rpgDeathAt);}
unsigned destructionSeed(const World &w,size_t index){return w.prototypeSeed^unsigned(index*0x9e3779b9u);}
Vec3 splashPoint(const World &w,Vec3 p){for(const auto &water:w.waterRegions)if(proceduralWaterEdge(water,p.x,p.z)>=0)p.y=std::max(p.y,water.level+.05f);return p;}
}
void Session::beginDestruction(size_t index){
  if(index>=world.targets.size())return;
  auto &t=world.targets[index];
  if(!t.active||t.escaped||t.invulnerable||t.health>0||t.rpgDeathAt>=0)return;
  t.rpgRestY=world.wreckPosition(t).y;t.rpgDeathAt=world.elapsed;t.processed=true;t.ai.muzzleFlash=0;
  auto material=destructionMaterial(t.generatorAsset);unsigned random=destructionSeed(world,index);
  if(index>=enemies.size()&&std::none_of(mission.depots.begin(),mission.depots.end(),[&](const auto &d){return d.target==int(index);})&&material==DestructionMaterial::Concrete&&!t.protectedTarget&&
     std::find(mission.assets.begin(),mission.assets.end(),int(index))==mission.assets.end())mission.structuresLost=std::min(4000,mission.structuresLost+1);
  bool organic=material==DestructionMaterial::Organic,wood=material==DestructionMaterial::Wood;
  if(index<enemies.size()){
    const auto &spec=combatSpec(enemies[index]);
    if(spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju)
      emitKaijuSound(world,kaijuCall(spec),t.center);
  }
  float span=std::clamp(length(t.half)*2,3.f,36.f);
  Vec3 floor=t.origin;floor.y=world.walkableHeight(floor);
  bool wet=world.waterDepthAt(floor)>1;
  if(material==DestructionMaterial::Metal)addEffect({WeaponEffect::Blast,t.center,{},Weapon::Empty,0,std::min(span,16.f)});
  addEffect({WeaponEffect::Smoke,wet?splashPoint(world,floor):floor+Vec3{0,1,0},{0,1,0},Weapon::Empty,0,std::min(span,18.f),wet?rpgSplash:rpgDust});
  int count=std::clamp(4+int(span/2),6,18);
  for(int n=0;n<count;++n){float a=float(n)*2.39996f+float(random%6284)/1000;
    auto kind=material==DestructionMaterial::Concrete&&n%4==0?DestructionMaterial::Glass:material;
    Vec3 p=t.origin+Vec3{std::sin(a)*std::min(8.f,t.half.x*.5f),std::min(12.f,t.half.y*(.2f+float(n%3)*.2f)),std::cos(a)*std::min(8.f,t.half.z*.5f)};
    WeaponEffect chip{WeaponEffect::Debris,p,{std::sin(a)*(3+n*.4f),5+float(n%5),std::cos(a)*(3+n*.4f)},Weapon::Empty,0,
      (.55f+float(n%4)*.3f)*std::clamp(span/20,.15f,1.f)*(organic?.5f:1.f),rpgDebrisBase+unsigned(kind)};
    chip.position.y=std::max(chip.position.y,world.debrisFloor(chip));addEffect(chip);
  }
  if(!wet&&(material==DestructionMaterial::Metal||volatileScenery(t.generatorAsset))){
    // Short-lived presentation is evictable; the burn/detonation mechanics are
    // stored on the target and cannot disappear under particle pressure.
    if(volatileScenery(t.generatorAsset))t.rpgBurn=10;
    else if(index<enemies.size())t.rpgBurn=5;
    world.sounds.push_back({span>15?SoundEvent::LargeExplosion:SoundEvent::Explosion,0,t.center});
  }else if(wood&&!wet)t.rpgBurn=8;
  if(!wet){
    size_t marks=std::count_if(world.effects.begin(),world.effects.end(),[](const auto &fx){return fx.model==rpgScorch;});
    if(marks>=12){auto old=std::find_if(world.effects.begin(),world.effects.end(),[](const auto &fx){return fx.model==rpgScorch;});world.effects.erase(old);}
    addEffect({WeaponEffect::Impact,floor,{},Weapon::Empty,0,std::clamp(span*.22f,1.5f,7.f),rpgScorch});
  }
}
void Session::finishEnemyDeath(Member *credit,size_t index){
  auto &e=enemies[index];auto &t=world.targets[index];if(t.rpgDeathAt>=0)return;
  releaseVoidmawGrapple(index);
  const auto &spec=combatSpec(e);++world.destroyed;t.ai.muzzleFlash=0;
  finishAmbientActor(index,true,credit&&factionAttacksPlayers(*this,index));
  if (credit && factionAttacksPlayers(*this,index)) {
    if(credit->emergency==HullEmergency::LastWind && clock<=credit->ejectAt+1e-6){
      credit->emergency=HullEmergency::None;credit->ejectAt=0;
      credit->sheet.hp=(credit->sheet.maxHp+3)/4;
      credit->body.health=100.f*credit->sheet.hp/credit->sheet.maxHp;
      log(credit->name+" / LAST WIND / HULL RESTORED");
      rollFault(*credit,true);
    }
    npcRadio(index, RadioLine::EnemyDistress);
    if(world.elapsed>=npcRadioReady)for(size_t ally=0;ally<enemies.size();++ally){
      const auto *group=factionDetachment(*this,ally);
      if(group&&group->factionRole==FactionRole::Allied&&!group->withdrawing&&
         enemies[ally].hp>0&&world.targets[ally].active&&!world.targets[ally].escaped&&
         length(world.targets[ally].center-credit->body.position)<100.f){
        npcRadio(ally, RadioLine::FriendlyConfirm);
        if(world.elapsed<npcRadioReady)break;
      }
    }
  }
  if(credit&&credit->sheet.has(Module::DismantlerArms)&&length(t.origin-credit->body.position)<=10){credit->sheet.ap=std::min(credit->sheet.maxAp,credit->sheet.ap+spec.hd);credit->sheet.reactorTest(dice);}
  if(spec.deathDice){int explosion=0;for(int d=0;d<spec.deathDice;++d)explosion+=dice.roll(spec.deathSides);
    for(auto &ally:members)if(!ally.ejected&&length(ally.body.position-t.origin)<=10)takeDamage(ally,explosion,false,t.origin-ally.body.position);}
  auto label=spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju?manualAppearance(mission.recipe.seed,unsigned(index),true).nickname:std::string(spec.name);
  log(label+" DESTROYED");beginDestruction(index);
}
void Session::damageEnemyEnvironment(size_t index,int amount){
  auto &e=enemies[index];if(e.hp<=0)return;
  const int oldHp=e.hp;
  e.hp=std::max(0,e.hp-std::max(0,amount-combatSpec(e).plating));
  if(mission.recipe.objective==9&&index==0&&!mission.recovered&&e.hp<=e.maxHp/4){e.hp=std::max(1,e.hp);e.disabled=99999;}
  world.targets[index].health=float(e.hp);damageVoidmawGrapple(index,oldHp-e.hp);
  if(e.hp==0)finishEnemyDeath(nullptr,index);
}
void Session::stepDestruction(float dt){
  if(!world.rpgPrototype)return;
  // Age cosmetic state even during debriefing/defeat. Gravity is analytic over
  // each step; horizontal friction and low-energy settling stop endless jitter.
  for(auto &fx:world.effects){fx.age+=dt;
    if(fx.kind==WeaponEffect::Debris){fx.position=fx.position+fx.direction*dt+Vec3{0,-9*dt*dt,0};fx.direction.y-=18*dt;
      float floor=world.debrisFloor(fx);if(fx.position.y<floor){fx.position.y=floor;
        if(fx.direction.y<0)fx.direction.y*=rpgDebris(fx.model)?-.22f:-.5f;
        if(rpgDebris(fx.model)){fx.direction.x*=.55f;fx.direction.z*=.55f;if(std::abs(fx.direction.y)<.8f)fx.direction.y=0;}}
    }else if(fx.model==rpgDust){fx.position=fx.position+fx.direction*dt;}
  }
  std::erase_if(world.effects,[](const auto &fx){return fx.age>=destructionEffectLife(fx.model,int(fx.kind));});
  bool quarter=int(world.elapsed*4)>int(std::max(0.f,world.elapsed-dt)*4);
  bool puff=int(world.elapsed*5)>int(std::max(0.f,world.elapsed-dt)*5);
  std::vector<size_t> burning;
  for(size_t i=0;i<world.targets.size();++i){auto &t=world.targets[i];if(!t.active||t.escaped||t.invulnerable)continue;
    if(t.health>0&&t.rpgInitialHealth<=0)t.rpgInitialHealth=t.health;
    if(t.health<=0&&t.rpgDeathAt<0&&!(i==0&&mission.recipe.objective==9&&mission.recovered))beginDestruction(i);
    float elapsed=age(world,t);
    float impactAt=std::max(.8f,std::sqrt(std::max(0.f,t.origin.y-world.walkableHeight(t.origin))/9));
    if(elapsed>=impactAt&&!t.rpgImpactSpent){t.rpgImpactSpent=true;Vec3 p=t.origin;p.y=world.walkableHeight(p);
      bool wet=world.waterDepthAt(p)>1;addEffect({WeaponEffect::Smoke,wet?splashPoint(world,p):p+Vec3{0,1,0},{0,.8f,0},Weapon::Empty,0,std::clamp(t.half.y,3.f,20.f),wet?rpgSplash:rpgDust});
      if(i<enemies.size()&&combatSpec(enemies[i]).kind==EnemyKind::LargeKaiju)
        emitKaijuSound(world,wet?CombatCue::KaijuSpray:CombatCue::KaijuImpact,wet?splashPoint(world,p):p);
    }
    if(t.rpgBurn>0){
      if(world.waterDepthAt(t.origin)>1)t.rpgBurn=0;
      else {t.rpgBurn=std::max(0.f,t.rpgBurn-dt);burning.push_back(i);
        if(puff){Vec3 p=t.origin;p.y=world.walkableHeight(p)+1.5f;
          addEffect({WeaponEffect::Blast,p,{0,1,0},Weapon::Empty,0,2.f,rpgFire});
          if(int(world.elapsed*5)%3==0)addEffect({WeaponEffect::Smoke,p+Vec3{0,2,0},{0,1.3f,0},Weapon::Empty,0,3.f,rpgDust});}}
    }
  }
  if(phase!=Phase::Combat)return;
  // Only a bounded set can deal heat damage in one tick, with a rotating start
  // so a large burning district does not starve later sources permanently.
  if(quarter&&!burning.empty())for(size_t n=0;n<std::min<size_t>(32,burning.size());++n){
    size_t source=burning[(n+size_t(world.elapsed*4)*32)%burning.size()];auto center=world.targets[source].origin;
    if(world.targets[source].health>0)damageScenery(source,1);
    for(size_t i=enemies.size();i<world.targets.size();++i)if(i!=source&&world.targets[i].active&&world.targets[i].health>0&&length(world.targets[i].origin-center)<6)damageScenery(i,1);
    for(size_t i=0;i<enemies.size();++i)if(enemies[i].hp>0&&length(world.targets[i].origin-center)<6)damageEnemyEnvironment(i,1);
    for(auto &m:members)if(!m.ejected&&m.sheet.hp>0&&length(m.body.position-center)<6&&!m.sheet.has(Module::ExothermicPlating))takeDamage(m,1,true);
  }
  unsigned detonations=0;
  for(size_t i=enemies.size();i<world.targets.size();++i){auto &t=world.targets[i];if(!t.active||t.health>0||t.rpgDeathAt<0||t.rpgSecondarySpent||!volatileScenery(t.generatorAsset)||age(world,t)<.45f)continue;
    if(detonations++>=8)break;
    t.rpgSecondarySpent=true;if(world.waterDepthAt(t.origin)>1)continue;
    auto center=t.origin+Vec3{0,2,0};addEffect({WeaponEffect::Blast,center,{},Weapon::Grenade,0,10});
    world.sounds.push_back({SoundEvent::Explosion,0,center});blastScenery(center,16,12);
    for(size_t n=enemies.size();n<world.targets.size();++n)if(n!=i&&world.targets[n].health>0&&volatileScenery(world.targets[n].generatorAsset)&&length(world.targets[n].origin-center)<16)
      damageScenery(n,world.targets[n].health+1);
    for(size_t n=0;n<enemies.size();++n)if(length(world.targets[n].origin-center)<16)damageEnemyEnvironment(n,4);
    for(auto &m:members)if(!m.ejected&&m.sheet.hp>0&&length(m.body.position-center)<16)takeDamage(m,4,false,center-m.body.position);
  }
}
}
