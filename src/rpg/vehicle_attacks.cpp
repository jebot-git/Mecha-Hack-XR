#include "rpg/session.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/weapon_audio.hpp"

namespace iron::rpg {
namespace {
enum class VehicleRole { None, Raider, Gunner, Bomber, Drone };
VehicleRole vehicleRole(const EnemySpec &spec){
  const std::string_view id=spec.id;
  if(id=="bomber")return VehicleRole::Bomber;
  if(id=="interceptor"||id=="gunboat"||id=="swarm_drone"||id=="cyclone_drone"||
     id=="deepdiver_submarine")return VehicleRole::Drone;
  if(spec.kind!=EnemyKind::Conventional||spec.traits&(Flying|Carrier|Submersible))return VehicleRole::None;
  return groundDriveTank(spec)?VehicleRole::Gunner:VehicleRole::Raider;
}
Vec3 flat(Vec3 v){v.y=0;return v;}
float planar(Vec3 v){return std::hypot(v.x,v.z);}
struct FormationRank {
  FormationRun *run=nullptr;
  size_t leader=0;
  int slot=0,members=0;
  float spacing=10.f,furthestPeer=0;

  Vec3 station(bool withdrawing) const {
    const auto across=cross(run->axis,Vec3{0,1,0});
    const int row=(slot+1)/2;
    // Fan the ranks around the mark so the outer infantry files stay in range.
    const float range=26.f+float(row%2)*2.f;
    const float angle=(slot%2?1.f:-1.f)*row*std::min(.75f,spacing/26.f);
    auto position=run->mark-run->axis*(range*std::cos(angle))+across*(range*std::sin(angle));
    if(withdrawing)position=position-run->axis*28.f+across*(run->left?-19.f:19.f);
    return position;
  }
};

FormationRank surfaceFormation(Session &s,size_t index,bool participating){
  FormationRank rank;
  if(!participating)return rank;
  auto &enemy=s.enemies[index];
  rank.run=&enemy.vehiclePass.soloRun;
  rank.leader=index;
  auto add=[&](size_t id){
    if(id>=s.enemies.size()||s.enemies[id].hp<=0||!s.world.targets[id].active)return;
    const auto &peer=combatSpec(s.enemies[id]);
    if(!groundDriveTank(peer)&&!antiMechaInfantry(peer))return;
    if(!rank.members){
      rank.leader=id;
      rank.run=&s.enemies[id].vehiclePass.soloRun;
    }
    if(id==index)rank.slot=rank.members;
    const auto &body=s.world.targets[id];
    rank.furthestPeer=std::max(rank.furthestPeer,planar(body.origin-s.world.targets[index].origin));
    const auto half=body.ai.boundsReady?body.ai.localHalf:body.half;
    rank.spacing=std::max(rank.spacing,2.f*std::max(half.x,half.z)+3.f);
    ++rank.members;
  };
  if(enemy.group>=0&&size_t(enemy.group)<s.mission.groups.size()){
    auto &group=s.mission.groups[size_t(enemy.group)];
    for(int id:group.actors)if(id>=0)add(size_t(id));
    if(rank.members>1)rank.run=&group.run;
  }else if(enemy.ambientWave){
    for(size_t id=0;id<s.enemies.size();++id)
      if(s.enemies[id].ambientWave==enemy.ambientWave)add(id);
  }
  return rank;
}
Vec3 bombCell(Vec3 aim,Vec3 course,int index){
  return aim+course*(float(index-1)*14.f);
}
constexpr float bombRadius=5.5f,bombTravel=1.20f;
}
bool Session::stepVehicleAttack(size_t index,const Member *victim,int objectiveTarget,
                                float dt,EnemyNavigationBudget &budget){
  auto &enemy=enemies[index];
  auto &body=world.targets[index];
  const auto &spec=combatSpec(enemy);
  const auto role=vehicleRole(spec);
  if(role==VehicleRole::None||enemy.boss>=0||enemy.transformed||body.ai.gravity.active())return false;
  const bool bomber=role==VehicleRole::Bomber;
  const bool drone=role==VehicleRole::Drone;
  const bool gunner=role==VehicleRole::Gunner;
  const bool hoverDrive=hoverDriveTank(spec),infantry=antiMechaInfantry(spec);
  const bool rockets=gunner||std::string_view(spec.id)=="deepdiver_submarine";
  const bool underwater=(spec.traits&Submersible)!=0;
  if(underwater&&(biome!=2||world.waterDepthAt(body.origin)<1)){
    enemy.vehiclePass={};
    body.ai.airborne=false;
    return false;
  }
  auto &pass=enemy.vehiclePass;
  auto &motion=body.ai.combatMotion;
  const float side=(bool(index&1u)!=pass.reverse)?1.f:-1.f;
  const bool airborne=!gunner&&!infantry&&((spec.traits&(Flying|Hover|Submersible))||drone);
  body.ai.airborne=airborne;
  const float approachRange=bomber?59.f:drone?47.f:gunner?65.f:39.f;
  const auto rank=surfaceFormation(*this,index,gunner||infantry);
  auto *formation=rank.run;
  if(formation&&formation->regroupAt>0&&world.elapsed>=formation->regroupAt)
    *formation={};
  const float altitude=bomber?32.f:std::string_view(spec.id)=="interceptor"?fighterCruiseHeight:(spec.traits&Flying)?19.f:drone?(underwater?2.f:8.f):3.f;
  const float speed=(bomber?25.f:drone?13.5f:gunner?10.f:15.f)*
      (hasInjury(enemy.injuries,MechaInjury::Hobbled)?.55f:1.f)*
      (airborne?1.f:environmentCatalog()[biome].speed);
  auto muzzle=[&]{return body.center+Vec3{0,body.ai.localHalf.y*.2f,0};};
  auto aimTurret=[&](Vec3 point){
    if(gunner)return traverseTankGun(body,point,dt);
    const auto ray=point-muzzle();
    body.ai.weaponYaw=std::atan2(-ray.x,-ray.z);
    body.ai.weaponPitch=std::atan2(ray.y,std::max(.001f,planar(ray)));
    return true;
  };
  auto move=[&](Vec3 wanted,float speedScale,Vec3 facing){
    const float edge=missionRadius()*.975f;
    auto destination=body.origin+flat(wanted);
    destination.x=std::clamp(destination.x,-edge,edge);
    destination.z=std::clamp(destination.z,-edge,edge);
    auto heading=unit(flat(destination-body.origin),{});
    const float pace=formation?std::min(speed,10.f):speed;
    const float moveSpeed=std::min(pace*speedScale,planar(wanted)/std::max(dt,.0001f));
    if(length(heading)>.01f)
      heading=enemyNavigationDirection(world,index,heading,destination,
                 moveSpeed,dt,airborne,navigation[index],&budget);
    auto displacement=flat(heading)*(moveSpeed*dt);
    if(gunner){
      displacement=tankDriveStep(world,index,heading,facing,navigation[index].priorDriveVelocity,moveSpeed,dt,hoverDrive,
                                 pass.phase==VehiclePassPhase::Egress||dot(facing,heading)<0);
    }else if(length(heading)>.01f){
      const float yaw=std::atan2(-heading.x,-heading.z);
      const float turn=wrap(yaw-body.yaw);
      body.yaw=wrap(body.yaw+std::clamp(turn,-dt*2.8f,dt*2.8f));
      if(airborne){
        const float bank=underwater?0.f:std::clamp(turn*.40f,-.40f,.40f);
        body.ai.bank+=std::clamp(bank-body.ai.bank,-dt*1.9f,dt*1.9f);
      }
    }
    if(airborne){
      const auto p=body.origin+displacement;
      const float floor=world.walkableHeight(p);
      const float water=world.waterDepthAt(p);
      const float top=body.ai.localCenter.y+body.ai.localHalf.y;
      const float ceiling=world.interiorCeilingAt({p.x,floor+1,p.z})-top-.6f;
      const float flightCeiling=underwater?std::min(ceiling,floor+water-top):ceiling;
      const float desired=std::max(floor,std::min(flightCeiling,floor+altitude+(underwater?0.f:water)));
      const float verticalRate=spec.traits&Hover?3.f:14.f;
      displacement.y=std::clamp(desired-body.origin.y,-verticalRate*dt,verticalRate*dt);
      if(underwater&&water<top+.5f)displacement={0,displacement.y,0};
    }
    if(!insideArea(body.origin+displacement,missionRadius()*.98f))return;
    moveRpgEnemy(index,displacement,dt,airborne);
    world.updateEnemyBounds(body);
  };
  auto retreat=[&]{
    pass.phase=VehiclePassPhase::Egress;
    pass.timer=bomber?2.25f:gunner?2.5f:2.0f;
    enemy.fireClock=pass.timer+1.0f+.16f*float(index%5);
  };
  if(formation&&formation->breakAt>0&&world.elapsed>=formation->breakAt&&pass.phase!=VehiclePassPhase::Egress)
    retreat();
  if(pass.phase==VehiclePassPhase::Egress){
    if(formation&&rank.members>1&&world.elapsed<formation->breakAt){
      // A unit that finished its salvo covers the remaining files until
      // the shared break. Every survivor then peels off together.
      move({},0.f,pass.aim-body.origin);
      aimTurret(pass.aim);
      return true;
    }
    pass.timer-=dt;
    const auto radial=unit(flat(body.origin-pass.aim),pass.course);
    const auto lateral=cross(radial,Vec3{0,1,0})*side;
    const auto travel=bomber?pass.course:hoverDrive?unit(radial+lateral*.80f):gunner?radial:unit(radial+lateral*(drone?1.50f:.72f));
    auto withdrawal=travel*25.f;
    if(formation&&formation->regroupAt>0){
      withdrawal=flat(rank.station(true)-body.origin);
      pass.timer=formation->regroupAt-world.elapsed;
    }
    move(withdrawal,1.22f,pass.aim-body.origin);
    if(gunner)aimTurret(victim?victim->body.bodyPoint(6):pass.aim);
    motion.retreat=1.f;
    if(enemy.airstrikeUntil>0){
      if(planar(body.origin-pass.aim)>=145.f||
         std::max(std::abs(body.origin.x),std::abs(body.origin.z))>=missionRadius()*.965f){
        enemy.hp=0;body.health=0;body.active=false;body.escaped=true;
      }
    }else if(pass.timer<=0){
      pass.phase=VehiclePassPhase::Approach;pass.reverse=!pass.reverse;
    }
    return true;
  }
  if(pass.phase==VehiclePassPhase::Approach){
    if(!victim&&enemy.airstrikeUntil<=0)return false;
    const auto designated=enemy.airstrikeUntil>0?pass.aim:
        objectiveTarget>=0?world.targets[size_t(objectiveTarget)].center:victim->body.bodyPoint(6);
    const auto delta=flat(designated-body.origin);
    const auto toward=unit(delta,direction(body.yaw,0));
    const auto lateral=cross(toward,Vec3{0,1,0})*side;
    const float range=planar(delta);
    const bool indoors=bomber&&world.indoors(designated);
    if(formation&&length(formation->axis)<.5f){
      formation->left=(enemy.group>=0?unsigned(enemy.group):enemy.ambientWave?enemy.ambientWave:unsigned(rank.leader))%2!=0;
      formation->axis=toward;
      formation->mark=designated;
    }
    if(formation&&index==rank.leader&&formation->breakAt<=0)formation->mark=designated;
    // Each rank approaches its own firing position on a fixed axis. A small
    // hold radius absorbs waypoint noise while the weapons recover.
    const auto toSlot=formation?flat(rank.station(false)-body.origin):Vec3{};
    const bool outOfPlace=formation&&rank.members>1&&length(toSlot)>4.f&&formation->breakAt<=0;
    if(outOfPlace||indoors||enemy.overheated||enemy.fireClock>0||range>approachRange||(!bomber&&!drone&&range<13.f)||
       (enemy.airstrikeUntil<=0&&!enemyFiringLaneClear(*this,index,designated,objectiveTarget))){
      auto offset=range>approachRange*.72f?toward:lateral+toward*(range<approachRange*.46f?-.62f:.12f);
      if(enemy.overheated||(range<13.f&&!bomber&&!drone))offset=lateral*.65f-toward;
      const auto &decision=tacticalDecisions[index];
      if(enemy.airstrikeUntil<=0&&!decision.fireLaneClear&&decision.positioning&&!indoors)
        offset=unit(flat(decision.destination-body.origin),unit(offset));
      auto travel=unit(offset)*21.f;
      if(formation&&(length(toSlot)>5.f||decision.fireLaneClear||!decision.positioning))
        travel=length(toSlot)>2.f?toSlot:Vec3{};
      move(travel,1.f,delta);
      aimTurret(designated);
      return true;
    }
    // Freeze the world-space target at the warning. Cover and player movement
    // can now foil the pass without a last-frame aim correction.
    if(formation&&formation->breakAt<=0){
      const float muster=rank.members>1?std::clamp(rank.furthestPeer/8.f,1.f,5.f):0.f;
      formation->breakAt=world.elapsed+2.8f+muster;
      formation->regroupAt=formation->breakAt+2.8f;
    }
    pass.aim=designated;
    pass.course=bomber?toward:unit(lateral+toward*.12f);
    pass.phase=VehiclePassPhase::Warning;
    pass.timer=(bomber?.82f:drone?.68f:.60f)+.07f*float(index%4);
    pass.pulse=0;
    motion.play(EnemyAction::RangedWindup,world.elapsed,pass.timer);
    aimTurret(designated);
    if(bomber){
      for(int cell=0;cell<3;++cell){
        auto mark=bombCell(pass.aim,pass.course,cell);
        mark.y=world.surfaceHeight(mark.x,mark.z)+.18f;
        addEffect({WeaponEffect::Impact,mark,pass.course,Weapon::Grenade,0,bombRadius,rpgVehicleBombWarning});
      }
    }else{
      const auto socket=gunner?tankGunSocket(body,world.elapsed):std::nullopt;
      const auto origin=socket?socket->muzzle:muzzle();
      const auto ray=designated-origin;
      addEffect({WeaponEffect::Sparks,origin,unit(ray),drone?Weapon::Rail:rockets?Weapon::Rocket:Weapon::Rifle,
                 0,std::min(70.f,length(ray)),rpgVehicleVolleyWarning});
    }
    if(world.sounds.size()<96)
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),muzzle(),
                              bomber?Weapon::AutoGrenade:rockets?Weapon::Rocket:drone?Weapon::Rail:Weapon::Gatling});
    return true;
  }
  if(enemy.overheated){retreat();return true;}
  const bool brace=(gunner&&!hoverDrive)||infantry;
  const auto firingTravel=brace?Vec3{}:formation?formation->axis*2.f:pass.course*25.f;
  move(firingTravel,bomber?1.f:.56f,pass.aim-body.origin);
  auto lanePoint=pass.aim;
  if(gunner){
    const auto lateral=cross(unit(flat(pass.aim-body.origin)),Vec3{0,1,0})*side;
    lanePoint=lanePoint+lateral*(float(pass.pulse-1)*7.5f);
  }
  const bool aligned=aimTurret(lanePoint);
  pass.timer-=dt;
  if(pass.timer>0)return true;
  if(!aligned){
    // A crossing target can exceed the mount's elevation or traverse rate.
    // Give it one half-turn of recovery time, then seek another firing pass.
    if(pass.timer<=-1.2f)retreat();
    return true;
  }
  if(gunner)pass.timer=0;
  pass.phase=VehiclePassPhase::Volley;
  const auto socket=gunner?tankGunSocket(body,world.elapsed):std::nullopt;
  const auto origin=socket?socket->muzzle:muzzle();
  const auto aim=socket?socket->forward:unit(lanePoint-origin);
  const auto spread=cross(unit(flat(aim)),Vec3{0,1,0})*side;
  const float damage=std::clamp(float(2+spec.hd/3),2.f,4.f);
  const auto type=bomber?Weapon::AutoGrenade:rockets?Weapon::Rocket:drone?Weapon::Rail:Weapon::Gatling;
  auto emit=[&](Vec3 velocity,float radius){
    Shot shot;
    shot.body.position=origin;shot.body.velocity=velocity;
    shot.body.damage=bomber?damage+2.f:damage;
    shot.body.owner=int(index);shot.body.weapon=type;
    shot.body.kind=bomber?ProjectileKind::Bomb:rockets?ProjectileKind::Rocket:ProjectileKind::Bullet;
    if(rockets)shot.body.hull=2.f;
    shot.body.life=bomber?bombTravel+.23f:2.8f;
    shot.body.radius=radius;
    shot.coverSensitiveBlast=bomber;
    return launch(std::move(shot));
  };
  const size_t waveSize=drone?(pass.pulse==0?3:2):1;
  if(ordnance.size()+waveSize>maxLiveProjectiles){
    std::erase_if(ordnance,[](const Shot &shot){return shot.body.life<=0;});
    if(ordnance.size()+waveSize>maxLiveProjectiles){retreat();return true;}
  }
  bool fired=false;
  if(bomber){
    auto point=bombCell(pass.aim,pass.course,pass.pulse);
    point.y=world.surfaceHeight(point.x,point.z);
    if(insideArea(point,missionRadius()*.98f)){
      const auto velocity=(point-origin)*(1.f/bombTravel)+Vec3{0,9.f*bombTravel,0};
      fired=emit(velocity,bombRadius);
    }
  }else if(drone){
    const float fan=pass.pulse==0?.24f:.13f;
    for(float angle:{-fan,fan})fired|=emit(unit(aim+spread*angle)*37.f,0);
    if(pass.pulse==0)fired|=emit(aim*37.f,0);
  }else if(gunner){
    fired=emit(aim*47.f,0);
  }else{
    const float lane=float(pass.pulse-1)*5.f;
    fired=emit(unit(pass.aim+spread*lane-origin)*60.f,0);
  }
  if(fired){
    body.ai.muzzleFlash=.16f;
    motion.play(EnemyAction::Fire,world.elapsed,.30f);
    addEffect({WeaponEffect::Sparks,origin,aim,type,0,1.8f,
               rockets?rpgRocketIgnition:drone?rpgEnemyEnergy:rpgEnemyKinetic});
    if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::Shot,int(type),origin});
  }
  ++pass.pulse;
  if(pass.pulse==(drone?2:3)){
    if(fired)addReactorHeat(enemy.heat,enemy.overheated,enemy.ventRemaining,bomber?.39f:.30f);
    retreat();
  }else pass.timer+=bomber?.26f:drone?.34f:.23f;
  return true;
}
}
