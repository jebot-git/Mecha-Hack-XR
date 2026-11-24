#include "rpg/session.hpp"
#include "rpg/voidmaw_pose.hpp"
#include "rpg/kaiju_audio.hpp"

namespace iron::rpg {
namespace {
using AssaultPhase=VoidmawAssaultPhase;
struct AssaultPace {
  float engage, pursuit, dodge, coil, dash, launchRange, recovery;
};

// The flea crosses the sightline in a single spring. Broader drones and barons
// need more space to turn their mass and retain their ranged attacks outside it.
AssaultPace paceFor(const EnemySpec &spec){
  const std::string_view id=spec.id;
  if(id=="kaiju_tunnel_wurm")return {32,9,7,1.05f,27,15,1.65f};
  if(id=="kaiju_flea")return {58,18,28,.52f,46,16,.82f};
  if(id=="kaiju_spawnling")return {54,16,23,.62f,39,17,.96f};
  return {23,12,16,.80f,33,15,1.15f};
}
}

bool Session::stepVoidmawAssault(size_t i,Member &pilot,float dt){
  auto &enemy=enemies[i];
  const auto &spec=combatSpec(enemy);
  const bool wurm=std::string_view(spec.id)=="kaiju_tunnel_wurm";
  if((spec.kind!=EnemyKind::SmallKaiju&&!wurm)||enemy.boss>=0||enemy.transformed)return false;
  auto &state=enemy.voidmawAssault;
  auto &body=world.targets[i];
  const float now=world.elapsed;
  const auto pace=paceFor(spec);
  const auto offset=pilot.body.position-body.origin;
  const Vec3 horizontal{offset.x,0,offset.z};
  const float range=length(horizontal);
  const auto towards=unit(horizontal,direction(body.yaw,0));

  // Other combat, climbing, navigation or a target switch can own this actor
  // for an arbitrary interval. A suspended dash cannot resume after that gap.
  if(state.phase!=AssaultPhase::Idle &&
     (state.target!=pilot.id || now-state.lastUpdate>dt+.002f)){
    state.phase=AssaultPhase::Idle;
    state.readyAt=now+.6f;
    enemy.grappleCooldown=std::max(enemy.grappleCooldown,.6f);
    body.ai.voidmawPose={};
  }
  if(enemy.chargedAttack>=0||enemy.grappleCooldown>0||body.ai.gravity.active()||
     pilot.body.gravity.active()||std::abs(offset.y)>7.f||pilot.vehicle||
     pilot.vehicleBlend>.01f||dt<=0){
    if(state.phase!=AssaultPhase::Idle){
      state.phase=AssaultPhase::Idle;
      body.ai.voidmawPose={};
    }
    return false;
  }

  auto begin=[&](AssaultPhase next,VoidmawPose pose,float seconds){
    state.phase=next;
    state.remaining=seconds;
    body.ai.voidmawPose={pose,now,seconds,state.side};
  };
  auto face=[&](Vec3 heading,float turnRate){
    const auto yaw=std::atan2(-heading.x,-heading.z);
    body.yaw=wrap(body.yaw+std::clamp(wrap(yaw-body.yaw),-turnRate*dt,turnRate*dt));
    body.ai.weaponYaw=body.yaw;
    world.updateEnemyBounds(body);
  };
  auto move=[&](Vec3 heading,float speed,float seconds){
    const auto before=body.origin;
    const auto displacement=heading*(speed*seconds);
    if(insideArea(before+displacement,missionRadius()*.98f))
      moveRpgEnemy(i,displacement,seconds,false);
    return dot(body.origin-before,heading)>.15f*speed*seconds;
  };
  auto recover=[&](){
    begin(AssaultPhase::Recover,VoidmawPose::Recover,pace.recovery);
    enemy.fireClock=std::max(enemy.fireClock,pace.recovery+.25f);
  };
  auto coil=[&](){
    state.course=towards;
    begin(AssaultPhase::Coil,VoidmawPose::Coil,pace.coil);
    body.ai.combatMotion.play(EnemyAction::MeleeWindup,now,pace.coil);
    emitKaijuSound(world,kaijuCall(spec),body.center);
  };

  if(state.phase==AssaultPhase::Idle){
    if(now<state.readyAt||range>pace.engage||(wurm&&range<=10.f&&enemy.fireClock<=0)||
       !enemyFiringLaneClear(*this,i,pilot.body.bodyPoint(6)))return false;
    state.target=pilot.id;
    state.side=((i+state.passes++)%2==0)?1.f:-1.f;
    if(range<=pace.launchRange)coil();
    else begin(AssaultPhase::Pursuit,VoidmawPose::Pursuit,.38f+.08f*float(i%3));
  }
  state.lastUpdate=now;
  const auto lateral=cross(towards,Vec3{0,1,0})*state.side;
  const float step=std::min(dt,state.remaining);
  state.remaining=std::max(0.f,state.remaining-dt);
  switch(state.phase){
  case AssaultPhase::Pursuit:{
    const auto heading=unit(towards+lateral*.28f);
    face(heading,6);
    if(range<=pace.launchRange)coil();
    else if(!move(heading,pace.pursuit,step)||state.remaining<=0){
      state.course=unit(towards*.42f+lateral);
      begin(AssaultPhase::Dodge,VoidmawPose::Dodge,.29f);
    }
    break;
  }
  case AssaultPhase::Dodge:
    face(towards,6);
    if(!move(state.course,pace.dodge,step)||state.remaining<=0){
      if(range<=pace.launchRange+2.f)coil();
      else if(body.ai.blocked>0){
        state.phase=AssaultPhase::Idle;
        state.readyAt=now+1.f;
        body.ai.voidmawPose={};
        return false;
      }else{
        state.side=-state.side;
        begin(AssaultPhase::Pursuit,VoidmawPose::Pursuit,.95f);
      }
    }
    break;
  case AssaultPhase::Coil:
    face(state.course,9);
    if(state.remaining<=0){
      const float duration=(pace.launchRange+7.f)/pace.dash;
      begin(AssaultPhase::Tackle,VoidmawPose::Tackle,duration);
      body.ai.combatMotion.play(EnemyAction::Strike,now,duration);
      emitKaijuSound(world,CombatCue::KaijuSwipe,body.center);
    }
    break;
  case AssaultPhase::Tackle:{
    // Resolve contact along the actual collision-tested path. Sampling at one
    // metre also preserves a narrow hit lane during a long simulation frame.
    const int samples=std::max(1,int(std::ceil(pace.dash*step)));
    const float slice=step/float(samples);
    const float reach=std::clamp(body.ai.localHalf.x*.48f+2.f,3.f,5.f);
    for(int n=0;n<samples;++n){
      const auto gap=pilot.body.position-body.origin;
      const float along=dot(gap,state.course);
      const auto crossLane=gap-state.course*along;
      if(along>=-.35f&&along<=reach&&std::hypot(crossLane.x,crossLane.z)<reach*.86f&&
         std::abs(gap.y)<7.f&&enemyFiringLaneClear(*this,i,pilot.body.bodyPoint(6))){
        Shot strike{};
        strike.meleeStrike=true;
        strike.body.owner=int(i);
        strike.body.velocity=state.course*pace.dash;
        strike.body.position=pilot.body.bodyPoint(7)-state.course*2.f;
        if(!parryStrike(pilot,strike)){
          takeDamage(pilot,std::clamp(2+spec.hd,3,8),false,body.origin-pilot.body.position);
          if(startVoidmawGrapple(i,pilot)){
            state.phase=AssaultPhase::Idle;
            body.ai.voidmawPose={VoidmawPose::Cling,now,0.f,enemy.grappleSide};
            return true;
          }
          // A deflected tackle already has its parry report. Grabs contact the
          // glass at the end of the canopy approach, handled in enemiesAct().
          emitKaijuSound(world,CombatCue::KaijuImpact,strike.body.position);
        }
        recover();
        break;
      }
      if(!move(state.course,pace.dash,slice)){recover();break;}
    }
    if(state.phase==AssaultPhase::Tackle&&state.remaining<=0)recover();
    break;
  }
  case AssaultPhase::Recover:
    if(state.remaining>pace.recovery*.52f)
      move(state.course,3.f,step); // Carry the missed lunge forward before planting.
    if(state.remaining<=0){
      state.phase=AssaultPhase::Idle;
      state.readyAt=now+.65f;
      // During this interval normal ranged and special attacks get a turn.
      enemy.grappleCooldown=std::max(enemy.grappleCooldown,.65f);
      body.ai.voidmawPose={};
    }
    break;
  case AssaultPhase::Idle:break;
  }
  return true;
}
}
