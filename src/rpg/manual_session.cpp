#include "rpg/session.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/weapon_audio.hpp"
#include <algorithm>
namespace iron::rpg {
bool Session::activateManual(Member &m,int slot,Gear gear,Vec3 origin,Vec3 ray){
  if(!manualGear(gear))return false;
  auto &s=m.sheet;const auto stats=weaponStats(s,gear,equippedWeaponCopy(s,slot));
  auto used=[&](bool reactor=false){m.body.cooldown[slot]=stats.cooldown;s.stress(dice,stats.heat);if(reactor)s.reactorTest(dice);if(consumable(gear))--s.inventory[size_t(gear)];};
  const auto baseRange=gearInfo(gear).range;
  const float moduleRange=(m.rangeExtension>0?90.f:30.f)*(baseRange>0?stats.range/baseRange:1.f);
  auto target=[&](float range=30.f){return world.traceAim(origin,ray,range);};
  switch(gear){
  case Gear::FunnelDrones:case Gear::ShieldNodes:{
    if(m.held[slot])return true;
    bool &state=gear==Gear::FunnelDrones?m.funnels:m.shieldNodes;
    used();state=!state;m.manualClock=0;break;
  }
  case Gear::MicroMissiles:{
    if(!m.body.targetingWorking())break;
    int lock=-1;
    float best=.975f;
    for(size_t i=0;i<enemies.size();++i){
      const auto &enemy=world.targets[i];
      if(!enemy.active||enemy.escaped||enemies[i].hp<=0||!factionAttacksPlayers(*this,i))continue;
      const auto offset=enemy.center-origin;
      const float distance=length(offset),aim=dot(unit(offset),ray);
      if(distance>stats.range||aim<=best)continue;
      if(world.traceAim(origin,unit(offset),distance+.05f,false).target!=int(i))continue;
      lock=int(i);best=aim;
    }
    used(true);
    Shot shot;
    shot.body.position=origin;shot.body.velocity=unit(ray)*86;shot.body.homingTarget=lock;
    shot.body.damage=float(dice.roll(4)+dice.roll(4)+dice.roll(4));shot.body.life=stats.range/86;
    shot.body.radius=7;shot.body.hull=2;shot.body.shooter=int(m.id);shot.body.mount=slot;
    shot.body.weapon=Weapon::Homing;shot.body.kind=ProjectileKind::Rocket;
    if(launch(std::move(shot))){
      addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rocket,0,1.f,rpgRocketIgnition});
      world.sounds.push_back({SoundEvent::Shot,int(Weapon::Homing),origin});
    }
    break;
  }
  case Gear::PrecogSystem:{auto hit=target(moduleRange);if(hit.target>=0&&size_t(hit.target)<enemies.size()&&enemies[hit.target].hp>0){used(true);m.precog=6;m.precogTarget=hit.target;}break;}
  case Gear::VerilianBattery:if(!s.batteryDie){used();s.batteryDie=6;}break;
  case Gear::MechaManacle:{
    auto hit=target(10);
    if(hit.target>=0&&size_t(hit.target)<enemies.size()){
      auto &enemy=enemies[hit.target];auto kind=enemySpec(enemy.spec).kind;
      if(enemy.hp>0&&enemy.hp<=enemy.maxHp/4&&(kind==EnemyKind::SmallMecha||kind==EnemyKind::LargeMecha||kind==EnemyKind::Labor)){
        used();enemy.disabled=std::max(enemy.disabled,60.f);
      }
    }
    break;
  }
  default:break;
  }
  return true;
}
void Session::bankDepotSalvage(Member &m){
  if(!m.pendingDepotSalvage)return;
  const int caches=std::exchange(m.pendingDepotSalvage,0);
  auto &s=m.sheet;const auto &r=mission.recipe;
  const int previousScrap=s.inventory[size_t(Gear::Scrap)];
  Dice reward{manualRandom(r.seed,ManualRng::Salvage,m.id)};
  auto stock=[&](Gear gear,int count=1){s.inventory[size_t(gear)]=std::min(99,s.inventory[size_t(gear)]+count);};
  stock(Gear::Scrap,caches*(1+std::max(0,r.salvageForm)%3));
  // The first recovered cache carries the contract-specific salvage feature.
  switch(r.salvageFeature){
  case 0:{static constexpr Gear weapons[]={Gear::Blade,Gear::Rifle,Gear::HeavyBlade,Gear::Cannon};auto g=weapons[reward.next()%4];s.owned[size_t(g)]=std::min(99,s.owned[size_t(g)]+1);break;}
  case 1:case 4:stock(Gear::VerilianBattery);break;
  case 2:stock(Gear::Scrap,3);break;
  case 3:stock(Gear::BoostKit);break;
  case 5:stock(Gear::Scrap,5);break;
  case 6:stock(Gear::Repair);s.repairKits=s.inventory[size_t(Gear::Repair)];break;
  case 7:stock(Gear::FragGrenade);break;
  case 8:stock(Gear::ArcGrenade);break;
  case 9:stock(Gear::Scrap,4);break;
  case 10:stock(Gear::FragGrenade);break;
  case 11:stock(Gear::Scrap,8);break;
  default:break;
  }
  log(m.name+" / DEPOT SALVAGE BANKED / +"+
      std::to_string(s.inventory[size_t(Gear::Scrap)]-previousScrap)+" SCRAP");
}
void Session::stepManual(Member &m,float dt){
  for(auto *timer:{&m.precog,&m.immobile,&m.mobilityDisrupt,&m.systemDisrupt,&m.nodeMobility,&m.rangeExtension})*timer=std::max(0.f,*timer-dt);
  if(m.precog==0)m.precogTarget=-1;
  if(m.ejected||!m.sheet.canAct()||phase!=Phase::Combat)return;
  auto installed=[&](Module module){return std::find(m.sheet.equipment.begin(),m.sheet.equipment.end(),moduleGear(module))!=m.sheet.equipment.end();};
  if(!installed(Module::FunnelDrones))m.funnels=false;
  if(!installed(Module::ShieldNodes))m.shieldNodes=false;
  m.manualClock-=dt;
  if(m.manualClock>0||(!m.funnels&&!m.shieldNodes))return;
  m.manualClock=3;
  if(m.funnels){size_t target=enemies.size();float nearest=(m.rangeExtension>0?90.f:30.f)*mountedModuleOutput(m.sheet,Gear::FunnelDrones);
    for(size_t i=0;i<enemies.size();++i){float distance=length(world.targets[i].center-(m.body.position+Vec3{0,6,0}));if(enemies[i].hp>0&&distance<nearest){auto hit=world.traceAim(m.body.position+Vec3{0,6,0},unit(world.targets[i].center-m.body.position-Vec3{0,6,0}),distance+1);if(hit.target==int(i)){target=i;nearest=distance;}}}
    if(target<enemies.size()){Shot shot;shot.body.position=m.body.position+Vec3{0,8,0};shot.body.velocity=unit(world.targets[target].center-shot.body.position)*100;shot.body.damage=float(dice.roll(8));shot.body.life=nearest/100+.05f;shot.body.shooter=int(m.id);shot.body.weapon=Weapon::Rifle;if(launch(std::move(shot)))world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),m.body.position+Vec3{0,8,0}});m.sheet.reactorTest(dice);}
  }
  if(m.shieldNodes){
    const float radius=(m.rangeExtension>0?90.f:30.f)*mountedModuleOutput(m.sheet,Gear::ShieldNodes);
    for(auto &ally:members)
      if(!ally.ejected&&ally.sheet.hp>0&&length(ally.body.position-m.body.position)<=radius){
        ally.shield=std::max(ally.shield,3);
        ally.shieldRemaining=std::max(ally.shieldRemaining,3.2f);
      }
    m.sheet.reactorTest(dice);
  }
}
}
