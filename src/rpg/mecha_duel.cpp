#include "rpg/mecha_duel.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/enemy_tactics.hpp"
#include "rpg/manual_enemy_ai.hpp"
#include "rpg/weapon_audio.hpp"
#include <string_view>

namespace iron::rpg {
const EnemyAttack *duelWeapon(const EnemySpec &spec){
  if(spec.kind!=EnemyKind::SmallMecha&&spec.kind!=EnemyKind::LargeMecha)return nullptr;
  for(const auto &attack:spec.attacks){
    if(attack.minimum>0||attack.recharge>0||attack.dice<=0)continue;
    if(attack.effect!=AttackEffect::Damage&&attack.effect!=AttackEffect::Push)continue;
    if(attack.range<=12||std::string_view(attack.name).starts_with("Thermal Hatchet"))return &attack;
  }
  return nullptr;
}

bool Session::stepMechaDuel(size_t i,const Member *victim,int npc,float dt){
  auto &enemy=enemies[i];
  auto &duel=enemy.duel;
  const auto &spec=combatSpec(enemy);
  const auto *weapon=duelWeapon(spec);
  auto &body=world.targets[i];
  auto &motion=body.ai.combatMotion;
  const float now=world.elapsed;
  auto cancel=[&]{
    if(duel.phase==DuelPhase::Windup){
      std::erase_if(world.effects,[&](const WeaponEffect &fx){
        return fx.model==rpgDuelGuardTell&&length(fx.position-duel.guardPoint)<.02f;
      });
    }
    if(duel.phase!=DuelPhase::Idle){
      enemy.fireClock=std::max(enemy.fireClock,.38f);
      enemy.bossTell=0;
      if(motion.action!=EnemyAction::Stagger&&(motion.technique==duel.cut||motion.technique==MechaTechnique::Rush))
        motion.play(EnemyAction::None,now,0);
    }
    duel.phase=DuelPhase::Idle;duel.until=now;
  };
  // Finish the committed burn against the world before a duel can steer the hull.
  if(!weapon||enemy.thrusters.remaining>0||enemy.transformed||enemy.missingArms==3||enemy.overheated||enemy.chargedAttack>=0||
     !victim||(!victim->id&&npc<0)){
    if(duel.phase!=DuelPhase::Idle)cancel();
    return false;
  }
  // Missed simulation ticks signal an intervening stagger, withdrawal or other interrupt.
  if(duel.phase!=DuelPhase::Idle&&(duel.pilot!=victim->id||duel.npc!=npc||now-duel.lastStep>dt+.006f)){
    cancel();return false;
  }
  duel.lastStep=now;
  const Vec3 up=body.ai.gravity.up();
  const Vec3 offset=victim->body.position-body.origin;
  const Vec3 ground=offset-up*dot(offset,up);
  const float distance=length(ground);
  const Vec3 advance=unit(ground,rotate(compose(body.ai.gravity.frame,angles(body.yaw)),{0,0,-1}));
  const float reach=8.f;
  const float engage=spec.optimal<=16?36.f:spec.optimal>=40?16.f:23.f;
  auto recovery=[&](float seconds){
    duel.phase=DuelPhase::Recover;duel.until=now+seconds;enemy.bossTell=0;
    motion.play(EnemyAction::None,now,seconds);
  };
  if(duel.phase==DuelPhase::Recover){
    if(now>=duel.until){duel.phase=DuelPhase::Idle;enemy.fireClock=std::max(enemy.fireClock,.28f);}
    return true;
  }
  if(duel.phase==DuelPhase::Idle){
    // Move into reach while the weapon cools. Ranged hulls use the shorter
    // engagement radius and will still fire their normal pattern outside it.
    if(now<duel.until||distance>engage||std::abs(dot(offset,up))>8||enemy.fireClock>3.f)return false;
    // One committed exchange per defender keeps every guard readable in a crowd.
    for(size_t j=0;j<enemies.size();++j){
      const auto &other=enemies[j];
      if(j!=i&&other.hp>0&&other.disabled<=0&&other.duel.phase!=DuelPhase::Idle&&
         other.duel.phase!=DuelPhase::Recover&&other.duel.pilot==victim->id&&other.duel.npc==npc){
        duel.until=now+.28f;enemy.fireClock=std::max(enemy.fireClock,.32f);return false;
      }
    }
    if(!enemyFiringLaneClear(*this,i,victim->body.bodyPoint(6),npc))return false;
    duel.pilot=victim->id;duel.npc=npc;duel.beat=0;++duel.sequence;
    duel.phase=DuelPhase::Rush;duel.until=now+1.55f;
    if(distance>reach+1){
      motion.play(EnemyAction::MeleeWindup,now,duel.until-now,MechaTechnique::Rush);
      addEffect({WeaponEffect::Sparks,body.center-advance*2,advance,Weapon::Empty,0,1.6f,rpgEnemyBoost});
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyThrust),body.center});
      addReactorHeat(enemy.heat,enemy.overheated,enemy.ventRemaining,.08f);
    }
  }
  if(duel.phase==DuelPhase::Rush||duel.phase==DuelPhase::Guard){
    const Vec3 local=rotate(gravityInverse(body.ai.gravity.frame),advance);
    body.yaw=std::atan2(-local.x,-local.z);body.ai.weaponYaw=body.yaw;
    world.updateEnemyBounds(body);
    if(duel.phase==DuelPhase::Guard&&now<duel.until)return true;
    if(duel.phase==DuelPhase::Rush&&distance>reach+.5f&&now<duel.until){
      const float speed=hasInjury(enemy.injuries,MechaInjury::Hobbled)?12.f:26.f;
      const Vec3 step=advance*std::min(distance-reach,speed*dt);
      if(insideArea(body.origin+step,missionRadius()*.98f))moveRpgEnemy(i,step,dt,(spec.traits&(Flying|Hover))!=0);
      return true;
    }
    if(distance>reach+2||std::abs(dot(offset,up))>8||!enemyFiringLaneClear(*this,i,victim->body.bodyPoint(6),npc)){
      recovery(.55f);return true;
    }
    if(enemy.fireClock>0){
      const float guardTime=std::min(enemy.fireClock,4.f);
      duel.phase=DuelPhase::Guard;duel.until=now+guardTime;
      motion.play(EnemyAction::Guard,now,guardTime);
      return true;
    }
    const bool overhead=duel.beat==2;
    const bool fromRight=enemy.missingArms?bool(enemy.missingArms&2u):bool((i+duel.sequence+duel.beat)&1u);
    duel.cut=overhead?MechaTechnique::Overhead:fromRight?MechaTechnique::RightCut:MechaTechnique::LeftCut;
    const Vec3 right=unit(cross(up,advance),{1,0,0});
    const float side=overhead?0.f:fromRight?1.f:-1.f;
    // Intersect the upper hull on a descending cut. Its guard point sits in
    // the clear forward glass above the dashboard, in tracked-hand reach.
    const Vec3 target=npc>=0?world.targets[size_t(npc)].center:victim->body.bodyPoint(11.5f)+right*(side*.85f);
    duel.origin=body.origin+up*(overhead?14.2f:13.6f)+right*(side*1.35f);
    duel.ray=unit(target-duel.origin,advance);
    duel.guardPoint=target-duel.ray*3.4f;
    duel.guardAxis=overhead?right:unit(right*(-side*.74f)+up*.68f);
    const float warning=overhead?.88f:duel.beat?.67f:.78f;
    if(!addEffect({WeaponEffect::Sparks,duel.guardPoint,duel.guardAxis,Weapon::Empty,
                   enemyEffectLife(rpgDuelGuardTell)-warning,1.45f,rpgDuelGuardTell})){
      recovery(.5f);return true;
    }
    duel.phase=DuelPhase::Windup;duel.until=now+warning;
    if(enemy.boss>=0)enemy.bossTell=warning;
    motion.play(EnemyAction::MeleeWindup,now,warning,duel.cut);
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),duel.origin,Weapon::Cutter});
    return true;
  }
  if(duel.phase==DuelPhase::Windup&&enemy.boss>=0)enemy.bossTell=std::max(0.f,duel.until-now);
  if(now<duel.until)return true;
  if(duel.phase==DuelPhase::Windup){
    Shot shot;
    shot.body.owner=int(i);shot.body.mount=-1;shot.body.weapon=Weapon::Empty;
    shot.body.position=duel.origin;shot.body.velocity=duel.ray*90.f;
    shot.body.life=(length(duel.guardPoint-duel.origin)+4.2f)/90.f;
    int damage=manualEnemyDamageBonus(*this,i);
    for(int d=0;d<weapon->dice+manualEnemyExtraDice(*this,i);++d)damage+=dice.roll(weapon->sides);
    shot.body.damage=std::max(1.f,std::round(float(damage)*(duel.beat==2?.95f:.75f)));
    shot.reactor=weapon->reactor;shot.effect=weapon->effect;shot.meleeStrike=true;shot.guardAxis=duel.guardAxis;
    if(!launch(shot)){recovery(.55f);return true;}
    addEffect({WeaponEffect::Sparks,duel.guardPoint,unit(cross(duel.ray,duel.guardAxis)),Weapon::Empty,0,1.9f,rpgDuelCut});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Melee),duel.origin,Weapon::Cutter});
    addReactorHeat(enemy.heat,enemy.overheated,enemy.ventRemaining,.10f);
    duel.phase=DuelPhase::FollowThrough;duel.until=now+.39f;
    motion.play(EnemyAction::Strike,now,.39f,duel.cut);
    return true;
  }
  const bool advanced=(spec.traits&Ace)||spec.hd>=8;
  const int beats=enemy.missingArms?1:advanced?3:2;
  if(++duel.beat>=beats||distance>reach+2){recovery(1.1f);return true;}
  duel.phase=DuelPhase::Guard;duel.until=now+.25f;
  motion.play(EnemyAction::Guard,now,.25f,duel.cut);
  return true;
}
}
