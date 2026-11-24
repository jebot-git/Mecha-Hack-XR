#include "rpg/mecha_ranged.hpp"
#include "rpg/session.hpp"
#include "rpg/enemy_effects.hpp"

namespace iron::rpg {
MechaRangedRole mechaRangedRole(const EnemySpec &spec){
  if(spec.kind!=EnemyKind::SmallMecha&&spec.kind!=EnemyKind::LargeMecha)return MechaRangedRole::None;
  // The camouflage sniper has a dedicated aimed shot and a long-range ambush doctrine.
  if(std::string_view(spec.id)=="mecha_sniper")return MechaRangedRole::None;
  const bool agile=spec.traits&(Leap|Flying|Hover|Ace|Veriform|Swarm);
  if(!agile&&(spec.plating>=2||spec.kind==EnemyKind::LargeMecha))return MechaRangedRole::MissileCarrier;
  return MechaRangedRole::Skirmisher;
}

void steerRangedMecha(MechaRangedRole role,const EnemySpec &spec,const Target &body,
                      Vec3 victim,float now,size_t index,TacticalPlan &plan,Vec3 &destination){
  if(role==MechaRangedRole::None)return;
  float reach=0;
  for(const auto &attack:spec.attacks)
    if(attack.effect==AttackEffect::Damage)reach=std::max(reach,attack.range);
  if(reach<=18.f)return;
  const Vec3 up=body.ai.gravity.up();
  Vec3 gap=victim-body.origin;
  gap=gap-up*dot(gap,up);
  const float distance=length(gap);
  const auto forward=unit(gap,direction(body.yaw,0));
  const bool skirting=role==MechaRangedRole::Skirmisher;
  const float standOff=std::min(skirting?44.f:64.f,reach*(skirting?.74f:.82f));
  const float radial=std::clamp((distance-standOff)/10.f,-1.f,1.f);
  // Reverse only at the end of a long strafe pass. A rack retreats directly
  // from attackers and otherwise uses the normal navigation/cover collision.
  const float directionSign=(int(now/(skirting?4.2f:7.f))+int(index))%2?-1.f:1.f;
  const auto side=unit(cross(forward,up));
  Vec3 course=forward*radial+side*(directionSign*(skirting?.97f:.16f));
  plan.direction=unit(course);
  plan.speed=(skirting?2.05f:1.05f)*std::min(1.f,length(course));
  destination=body.origin+plan.direction*26.f;
}

void Session::launchMechaLayer(size_t index,int damage){
  auto &enemy=enemies[index];
  const auto &pattern=enemy.heavyAttack;
  const auto &body=world.targets[index];
  const bool rack=pattern.pattern==HeavyPattern::Loft;
  const int count=rack?mechaRackTubes:mechaWaveColumns-1;
  // Emit a complete readable row or skip this pulse. Keep room for a party's
  // return fire so incoming patterns never consume the entire projectile pool.
  if(ordnance.size()+count>maxLiveProjectiles-24)return;
  const auto up=body.ai.gravity.up();
  const auto target=pattern.origin+pattern.aim*pattern.lockDistance;
  const float mirror=pattern.reverse?-1.f:1.f;
  const int hole=3+(1-pattern.pulse)*int(mirror);
  const int lanes=rack?mechaRackTubes:mechaWaveColumns;
  for(int lane=0;lane<lanes;++lane){
    if(!rack&&lane==hole)continue;
    Shot shot;
    auto &p=shot.body;
    p.owner=int(index);p.mount=-1;p.damage=float(damage);
    shot.reactor=pattern.attack.reactor;
    if(rack){
      const float bank=(pattern.pulse%2?-1.f:1.f)*mirror;
      const float spread=float(lane-1);
      const float shoulder=std::clamp(body.ai.localHalf.x*.72f,1.f,5.f);
      p.position=body.center+up*std::clamp(body.ai.localHalf.y*.84f,1.f,8.f)+
                 pattern.lateral*(bank*shoulder+spread*.65f);
      p.velocity=unit(up*1.28f+pattern.aim*.46f+pattern.lateral*(bank*.30f+spread*.30f))*17.f;
      p.kind=ProjectileKind::Rocket;p.weapon=Weapon::Rocket;p.hull=2.f;
      p.visual=ProjectileVisual::LoftMissile;
      if(!pattern.guidanceLost){shot.loftPilot=enemy.chargeTarget;shot.loftNpc=enemy.chargeWorldTarget;}
      shot.loftPoint=target;
      // Extra time accounts for the climb and turn around the top of the arc.
      p.life=1.8f+pattern.attack.range/21.f;
    }else{
      p.position=body.center+up*(body.ai.localHalf.y*.20f);
      const Vec3 endpoint=target+pattern.lateral*((lane-3)*mechaWaveSpacing);
      const float depth=std::max(6.f,dot(target-p.position,pattern.aim));
      // Equal forward speed keeps the gaps aligned in the locked target plane.
      p.velocity=(endpoint-p.position)*(22.f/depth);
      p.life=pattern.attack.range/22.f+.18f;
      p.visual=(pattern.pulse%2)?ProjectileVisual::WaveB:ProjectileVisual::WaveA;
      p.weapon=Weapon::Rifle;
      const std::string_view name=pattern.attack.name;
      if(name.find("Beam")!=name.npos||name.find("Particle")!=name.npos||name.find("Plasma")!=name.npos)
        shot.quality=Quality::Beam;
    }
    launch(std::move(shot));
  }
}

void Session::guideLoftMissile(Shot &shot,float dt){
  auto &p=shot.body;
  const float previousAge=shot.loftAge;
  shot.loftAge+=dt;
  // Integrate only the part of the tick after booster ejection.
  const float burn=shot.loftAge-std::max(previousAge,.48f);
  if(shot.loftPilot){
    auto target=std::find_if(members.begin(),members.end(),[&](const Member &pilot){return pilot.id==shot.loftPilot;});
    if(target==members.end()||target->ejected||target->sheet.hp<=0||target->sheet.hidden||
       target->hiddenRemaining>0||target->body.lifeSpent)shot.loftPilot=0;
    else shot.loftPoint=target->body.bodyPoint(6);
  }
  if(shot.loftNpc>=0){
    const size_t npc=size_t(shot.loftNpc);
    if(npc>=enemies.size()||!world.targets[npc].active||world.targets[npc].escaped||
       enemies[npc].hp<=0||!factionsAtWar(*this,size_t(p.owner),npc))shot.loftNpc=-1;
    else shot.loftPoint=world.targets[npc].center;
  }
  if(burn<=0)return;
  const auto forward=unit(p.velocity);
  const auto offset=shot.loftPoint-p.position;
  const float distance=length(offset);
  if(distance<10.f){shot.loftPilot=0;shot.loftNpc=-1;}
  // After crossing the target plane, fly straight. A late dodge breaks the
  // terminal intercept and a missile cannot circle back into the same pilot.
  if(shot.loftAge>1.15f&&dot(offset,forward)<=0)return;
  const auto aim=unit(offset,forward);
  const float cosine=std::clamp(dot(forward,aim),-1.f,1.f);
  const float angle=std::acos(cosine);
  const float turn=std::min(angle,burn*(shot.loftAge<1.45f?2.65f:1.02f));
  const auto tangent=unit(aim-forward*cosine,forward);
  const auto heading=turn>=angle?aim:unit(forward*std::cos(turn)+tangent*std::sin(turn),forward);
  p.velocity=heading*std::min(21.f,length(p.velocity)+7.f*burn);
}
}
