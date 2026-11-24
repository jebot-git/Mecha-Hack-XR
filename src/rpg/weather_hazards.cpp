#include "rpg/session.hpp"
#include "rpg/enemy_effects.hpp"
#include "destruction_visuals.hpp"
#include <algorithm>

namespace iron::rpg {
namespace {
constexpr float frontGrace=4.f, fieldInterval=1.f;
bool exposed(const World &world,Vec3 p){
  const auto canopy=p+Vec3{0,10,0};
  return !world.indoors(p)&&!world.indoors(canopy)&&
         (world.environment.weather!=Weather::Snowstorm||world.exposedToPrecipitation(canopy));
}
bool withinImpact(const World &world,Vec3 center,Vec3 feet,float radius,int actor=-1){
  const auto gap=feet-center;
  return std::hypot(gap.x,gap.z)<=radius&&std::abs(gap.y)<=13.f&&
      exposed(world,feet)&&world.enemyLineOfSight(center+Vec3{0,1.8f,0},feet+Vec3{0,5,0},-1,actor);
}
}
void Session::stepWeatherHazards(){
  const auto kind=world.environment.weather;
  const auto *hazard=weatherHazard(kind);
  auto &front=weatherFront;
  if(front.kind!=kind||front.seed!=world.environment.seed){
    std::erase_if(world.effects,[](const auto &fx){
      return fx.model==rpgWeatherWarning||fx.model==rpgWeatherMeteor||
             fx.model==rpgWeatherSpore||fx.model==rpgWeatherFire||fx.model==rpgWeatherHail;
    });
    front={};front.kind=kind;front.seed=world.environment.seed;
  }
  if(!hazard)return;
  const bool local=hazard->radius>0;
  const auto live=[&](const Member &m){
    return !m.ejected&&m.sheet.hp>0&&!m.body.lifeSpent&&exposed(world,m.body.position);
  };
  const auto hit=[&](Vec3 from,int actor){
    if(!exposed(world,from))return false;
    for(size_t n=0;n<front.count;++n)
      if(withinImpact(world,front.cells[n],from,hazard->radius,actor))return true;
    return false;
  };
  if(front.impactAt>=0&&world.elapsed>=front.impactAt){
    const bool initial=front.linger==0;
    if(initial&&local){
      if(kind==Weather::CorruptionSporeBurst)front.linger=4;
      if(kind==Weather::FireStorm)front.linger=3;
      for(size_t n=0;n<front.count;++n){
        const auto p=front.cells[n];
        if(kind==Weather::MeteorShower){
          addEffect({WeaponEffect::Blast,p,{0,1,0},Weapon::Grenade,0,hazard->radius*.6f,rpgWeaponBlast});
          addEffect({WeaponEffect::Smoke,p,{0,1,0},Weapon::Empty,0,6.f,rpgDust});
          world.sounds.push_back({SoundEvent::LargeExplosion,0,p});
          for(size_t i=enemies.size();i<world.targets.size();++i){
            const auto &t=world.targets[i];
            if(t.active&&!t.protectedTarget&&!t.invulnerable&&withinImpact(world,p,t.origin,hazard->radius,int(i)))
              damageScenery(i,10.f);
          }
        }else if(kind==Weather::Snowstorm){
          addEffect({WeaponEffect::Sparks,p,{0,1,0},Weapon::Empty,0,hazard->radius,rpgWeatherHail});
        }else{
          const unsigned fx=kind==Weather::FireStorm?rpgWeatherFire:rpgWeatherSpore;
          addEffect({WeaponEffect::Smoke,p,{0,1,0},Weapon::Empty,0,hazard->radius,fx});
        }
      }
    }else if(initial){
      for(const auto &m:members)if(live(m))
        addEffect({WeaponEffect::Sparks,m.body.bodyPoint(6),{0,1,0},Weapon::Rail,0,5.f,rpgWeatherPulse});
    }
    for(auto &m:members){
      if(!live(m)|| (local&&!hit(m.body.position,-1)))continue;
      if(kind!=Weather::MeteorShower&&absorbShield(m,1)==0)continue;
      switch(kind){
        case Weather::IonStorm:
          m.weatherJam=std::max(m.weatherJam,3.f);
          m.systemDisrupt=std::max(m.systemDisrupt,3.3f);
          m.sheet.stress(dice,m.sheet.has(Module::ReflectiveShielding)?.17f:.35f);
          break;
        case Weather::CorruptionSporeBurst:
          m.systemDisrupt=std::max(m.systemDisrupt,1.6f);
          m.sheet.stress(dice,.07f);
          takeDamage(m,1);
          break;
        case Weather::FireStorm:
          if(!m.sheet.has(Module::ExothermicPlating)){m.sheet.stress(dice,.19f);takeDamage(m,2);}
          else m.sheet.stress(dice,.07f);
          break;
        case Weather::Snowstorm:takeDamage(m,2);break;
        case Weather::MeteorShower:takeDamage(m,7);break;
        case Weather::MagneticBurst:
          m.weatherJam=std::max(m.weatherJam,2.f);
          m.mobilityDisrupt=std::max(m.mobilityDisrupt,3.4f);
          m.thrusters.remaining=0;m.body.rpgThrust={};
          break;
        case Weather::AshCloud:m.sheet.stress(dice,.12f);break;
        case Weather::RadioactiveWind:
          m.sheet.stress(dice,.14f);
          takeDamage(m,1,true);
          break;
        default:break;
      }
    }
    for(size_t i=0;i<enemies.size();++i){
      const auto &body=world.targets[i];
      const auto &e=enemies[i];
      if(e.hp<=0||!body.active||body.escaped||!exposed(world,body.origin)||
         (local&&!hit(body.origin,int(i))))continue;
      switch(kind){
        case Weather::IonStorm:damageNpc(i,0,AttackEffect::DoubleReactor,Quality::None);break;
        case Weather::CorruptionSporeBurst:
          if(combatSpec(e).kind!=EnemyKind::SmallKaiju&&combatSpec(e).kind!=EnemyKind::LargeKaiju)
            damageNpc(i,1,AttackEffect::DisruptSystems,Quality::Plasma);
          break;
        case Weather::FireStorm:damageNpc(i,2,AttackEffect::Reactor,Quality::Plasma);break;
        case Weather::Snowstorm:damageNpc(i,2,AttackEffect::Damage,Quality::None);break;
        case Weather::MeteorShower:damageNpc(i,7,AttackEffect::Damage,Quality::None);break;
        case Weather::MagneticBurst:damageNpc(i,0,AttackEffect::DisruptMobility,Quality::None);break;
        case Weather::AshCloud:damageNpc(i,0,AttackEffect::Reactor,Quality::None);break;
        case Weather::RadioactiveWind:damageNpc(i,1,AttackEffect::Reactor,Quality::Plasma);break;
        default:break;
      }
    }
    // Spore clouds and fire cells apply once per second after impact.
    if(front.linger>1){--front.linger;front.impactAt+=fieldInterval;}
    else {front.linger=0;front.impactAt=-1;}
    return;
  }
  if(front.impactAt>=0||world.environmentSeconds<frontGrace)return;
  const float age=world.environmentSeconds-frontGrace;
  const int pulse=int(age/hazard->cadence);
  if(pulse==front.lastPulse||age-pulse*hazard->cadence>1.f)return;
  front.lastPulse=pulse;
  front.count=0;
  for(const auto &m:members){
    if(!live(m)||front.count>=front.cells.size())continue;
    if(!local){
      addEffect({WeaponEffect::Sparks,m.body.bodyPoint(5),{0,1,0},Weapon::Rail,3.f-hazard->warning,5.f,rpgWeatherWarning});
      ++front.count;
      continue;
    }
    const uint32_t index=unsigned(pulse)*23+m.body.playerSlot*5;
    const float angle=weatherNoise(front.seed,index)*2*pi;
    const float distance=hazard->radius*(.55f+.45f*weatherNoise(front.seed,index+1));
    for(float turn:{0.f,1.57f,-1.57f,pi}){
      auto p=m.body.position+Vec3{std::sin(angle+turn)*distance,0,std::cos(angle+turn)*distance};
      p.y=kind==Weather::Snowstorm?world.pilotSupportHeight(p):world.walkableHeight(p);
      if(!insideArea(p,missionRadius()*.96f)||!exposed(world,p)||std::abs(p.y-m.body.position.y)>10.f||
         (kind!=Weather::MeteorShower&&world.wadingDepthAt(p)>1.f)||
         std::any_of(front.cells.begin(),front.cells.begin()+front.count,[&](Vec3 prior){return length(prior-p)<hazard->radius;}))continue;
      const unsigned model=kind==Weather::MeteorShower?rpgWeatherMeteor:rpgWeatherWarning;
      if(addEffect({WeaponEffect::Sparks,p,{0,1,0},Weapon::Grenade,3.f-hazard->warning,hazard->radius,model}))
        front.cells[front.count++]=p;
      break;
    }
  }
  if(!local||front.count)front.impactAt=world.elapsed+hazard->warning;
}
}
