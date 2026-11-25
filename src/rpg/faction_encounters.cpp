#include "rpg/session.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/weapon_audio.hpp"
#include "searchlight.hpp"
#include <algorithm>

namespace iron::rpg {
namespace {
constexpr int alliedStanding=4, hostileStanding=-4;
constexpr float assistanceRange=125.f, engagementRange=165.f;
Vec3 point(const std::array<float,3> &p){return {p[0],p[1],p[2]};}
std::array<float,3> pack(Vec3 p){return {p.x,p.y,p.z};}
bool active(const Session &s,size_t i){
  return i<s.enemies.size()&&s.enemies[i].hp>0&&s.world.targets[i].active&&!s.world.targets[i].escaped;
}
const Member *nearbyPilot(const Session &s,Vec3 origin,float radius){
  const Member *best=nullptr;
  for(const auto &m:s.members){
    if(m.ejected||m.sheet.hp<=0||m.body.lifeSpent)continue;
    const float distance=length(m.body.position-origin);
    if(distance<radius){radius=distance;best=&m;}
  }
  return best;
}
Vec3 withdrawalPoint(const Session &s,Vec3 from){
  const float edge=s.missionRadius()*.965f;
  Vec3 best=from;float distance=1e9f;
  for(bool alongX:{true,false})for(float side:{-1.f,1.f})for(float offset:{0.f,40.f,-40.f,85.f,-85.f}){
    auto p=from;
    if(alongX){p.x=edge*side;p.z=std::clamp(from.z+offset,-edge,edge);}
    else {p.z=edge*side;p.x=std::clamp(from.x+offset,-edge,edge);}
    p.y=s.world.surfaceHeight(p.x,p.z);
    if(length(p-from)>=distance||!clearPosition(s.world,p,10))continue;
    best=p;distance=length(p-from);
  }
  return best;
}
}

int actorFaction(const Session &s,size_t i){
  const auto &e=s.enemies[i];
  return e.boss>=0?campaignBoss(e.boss).faction:enemyFaction(size_t(e.spec));
}
const EncounterGroup *factionDetachment(const Session &s,size_t i){
  const auto index=s.enemies[i].group;
  if(index<0||size_t(index)>=s.mission.groups.size())return nullptr;
  const auto &group=s.mission.groups[size_t(index)];
  return group.factionRole==FactionRole::Contract?nullptr:&group;
}
bool factionAttacksPlayers(const Session &s,size_t i){
  if(actorFaction(s,i)==voidmawFaction)return true;
  const auto *group=factionDetachment(s,i);
  if(!group)return true;
  if(group->factionRole==FactionRole::Neutral)return s.mission.offendedNeutral&(1u<<group->faction);
  return group->retaliating;
}
bool factionsAtWar(const Session &s,size_t i,size_t j){
  if(i==j)return false;
  const int a=actorFaction(s,i),b=actorFaction(s,j);
  if(a==b)return false;
  if(a==voidmawFaction||b==voidmawFaction)return true;
  const auto *left=factionDetachment(s,i),*right=factionDetachment(s,j);
  // Different factions on opposite sides of the operation are rivals.
  if(bool(left)!=bool(right))return true;
  if(!left)return false;
  return factionAttacksPlayers(s,i)!=factionAttacksPlayers(s,j);
}
std::vector<int> factionCombatContacts(const Session &s){
  std::vector<int> result(s.enemies.size(),-1);
  for(size_t i=0;i<s.enemies.size();++i){
    if(!active(s,i)||s.enemies[i].disabled>0)continue;
    const auto *group=factionDetachment(s,i);
    const auto &body=s.world.targets[i];
    const bool allied=group&&group->factionRole==FactionRole::Allied;
    const auto *pilot=allied?nearbyPilot(s,body.origin,assistanceRange):nullptr;
    float closest=engagementRange;
    Pilot observer;observer.position=body.origin;
    for(size_t j=0;j<s.enemies.size();++j){
      if(!active(s,j)||!factionsAtWar(s,i,j))continue;
      const auto &other=s.world.targets[j];
      const float distance=length(other.origin-body.origin);
      // The escort radius applies to helping pilots against other human forces.
      // Each detachment defends itself from local kaiju, including on withdrawal.
      if(allied&&actorFaction(s,j)!=voidmawFaction&&
         (group->withdrawing||!pilot||length(other.origin-pilot->body.position)>assistanceRange+25))continue;
      if(distance>=closest||
         (hasInjury(s.enemies[i].injuries,MechaInjury::Sensors)&&distance>32.f)||
         !visibilityInRange(s.world,observer,other.center)||
         !s.world.enemyLineOfSight(body.center,other.center,int(i),int(j)))continue;
      result[i]=int(j);closest=distance;
    }
  }
  return result;
}

void Session::spawnFactionDetachments(){
  const auto &recipe=mission.recipe;
  if(settings.enemy>=0||settings.enemyCount>0||recipe.finale||recipe.objective==9||
     settings.world>=2||recipe.location==7||biome==1||biome==2||biome==7)return;
  Dice random{recipe.seed^0x394cd582u};
  unsigned combatants=0;
  for(size_t i=0;i<enemies.size();++i)combatants|=1u<<actorFaction(*this,i);
  struct Candidate {int faction;std::vector<int> roster;};
  std::vector<Candidate> neutral,allied;
  for(int f=0;f<voidmawFaction;++f){
    const unsigned bit=1u<<f;
    if((combatants&bit)||f==recipe.adversary||campaign.standings[f]<=hostileStanding||
       (mission.withdrawnAllies&bit))continue;
    Candidate candidate{f,{}};
    for(size_t i=0;i<enemyCatalog().size();++i){
      const auto &spec=enemySpec(i);
      if(enemyFaction(i)==f&&spec.hd<=std::min(4,encounterThreatLimit(recipe.difficulty,recipe.campaignStage))&&
         spec.kind!=EnemyKind::Installation&&!(spec.traits&(Flying|Hover|Submersible|Carrier|Veriform|Combiner))&&
         std::any_of(spec.attacks.begin(),spec.attacks.end(),[](const auto &attack){return attack.range>12&&attack.dice>0;}))
        candidate.roster.push_back(int(i));
    }
    if(candidate.roster.empty())continue;
    if(!(mission.offendedNeutral&bit)&&(campaign.standings[f]>=alliedStanding||
       (f==recipe.employer&&campaign.standings[f]>=0)))allied.push_back(std::move(candidate));
    else neutral.push_back(std::move(candidate));
  }
  std::stable_sort(allied.begin(),allied.end(),[&](const auto &a,const auto &b){
    const int left=campaign.standings[a.faction]+(a.faction==recipe.employer?2:0);
    const int right=campaign.standings[b.faction]+(b.faction==recipe.employer?2:0);
    return left>right;
  });
  const auto budget=std::min<size_t>(8,enemies.size()<maxInitialEnemies?maxInitialEnemies-enemies.size():0);
  size_t spawned=0;
  std::vector<Vec3> anchors;
  auto place=[&](const Candidate &candidate,FactionRole role){
    if(spawned>=budget)return false;
    const bool escort=role==FactionRole::Allied;
    std::vector<Vec3> locations;
    for(auto p:world.interiorNavNodes){
      const float d=length(p-world.missionEntry);
      if(d<(escort?52.f:135.f)||d>(escort?245.f:missionRadius()*1.6f)||
         world.indoors(p+Vec3{0,10,0})||!insideArea(p,missionRadius()*.89f)||
         world.waterDepthAt(p,10)>world.maxWadeDepth||
         std::any_of(anchors.begin(),anchors.end(),[&](Vec3 a){return length(a-p)<85.f;}))continue;
      if(std::any_of(enemies.begin(),enemies.end(),[&](const Enemy &e){
        const auto i=size_t(&e-enemies.data());
        return e.hp>0&&length(world.targets[i].origin-p)<42.f;
      }))continue;
      locations.push_back(p);
    }
    if(locations.empty())return false;
    if(escort)std::stable_sort(locations.begin(),locations.end(),[&](Vec3 a,Vec3 b){
      return length(a-world.missionEntry)<length(b-world.missionEntry);
    });
    for(size_t tries=0;tries<std::min(size_t(10),locations.size());++tries){
      const auto p=escort?locations[tries]:locations[(random.next()+tries)%locations.size()];
      EncounterGroup group;
      group.factionRole=role;group.faction=candidate.faction;
      group.role=int(EncounterRole::Patrol);
      group.route.push_back(pack(p));
      const int index=int(mission.groups.size());
      for(size_t n=0;n<2&&spawned<budget;++n){
        const int spec=candidate.roster[random.next()%candidate.roster.size()];
        const auto actor=enemies.size();
        if(!spawnManualEnemy(spec,p+Vec3{float(n)*23,0,0},3,0,{},true,role))continue;
        enemies[actor].group=index;
        group.actors.push_back(int(actor));++spawned;
      }
      if(group.actors.empty())continue;
      if(!escort){
        const auto center=world.targets[size_t(group.actors.front())].origin;
        for(float offset:{29.f,-29.f}){
          auto patrol=center+Vec3{offset,0,18};patrol.y=world.walkableHeight(patrol);
          if(clearPosition(world,patrol,5))group.route.push_back(pack(patrol));
        }
      }
      anchors.push_back(p);
      mission.supportedFactions|=1u<<candidate.faction;
      mission.groups.push_back(std::move(group));
      return true;
    }
    return false;
  };
  if(!allied.empty())place(allied.front(),FactionRole::Allied);
  if(!neutral.empty()){
    const unsigned count=1+random.next()%3;
    const size_t start=random.next()%neutral.size();
    for(unsigned n=0;n<count;++n)place(neutral[(start+n)%neutral.size()],FactionRole::Neutral);
  }
}

void Session::noteFactionAttack(size_t i){
  const auto *side=factionDetachment(*this,i);
  if(!side)return;
  auto &group=mission.groups[size_t(enemies[i].group)];
  const unsigned bit=1u<<group.faction;
  if(group.factionRole==FactionRole::Neutral){
    if(mission.offendedNeutral&bit)return;
    mission.offendedNeutral|=bit;
    log(std::string(factions[group.faction])+" / NEUTRALITY BROKEN");
  }else if(!group.withdrawing){
    group.withdrawing=true;
    for(int actor:group.actors){
      auto &e=enemies[size_t(actor)];
      e.decisionClock=0;e.heavyAttack={};e.chargedAttack=-1;e.vehiclePass={};
    }
    group.withdrawal=pack(withdrawalPoint(*this,world.targets[i].origin));
    mission.withdrawnAllies|=bit;
    log(std::string(factions[group.faction])+" / ALLIED GROUP WITHDRAWING");
  }
  tacticalClock=0;
}
void Session::noteFactionKill(size_t i){
  const int f=actorFaction(*this,i);
  auto &kills=mission.factionKills[size_t(f)];
  kills=std::min(kills+1,12);
  const auto *side=factionDetachment(*this,i);
  if(!side||side->factionRole!=FactionRole::Allied||side->retaliating)return;
  noteFactionAttack(i);
  auto &group=mission.groups[size_t(enemies[i].group)];
  group.retaliating=true;mission.lostAllies|=1u<<f;
  log(std::string(factions[f])+" / ALLIES RETURNING FIRE DURING WITHDRAWAL");
  tacticalClock=0;
}

bool Session::stepFactionWithdrawal(size_t i,float dt,EnemyNavigationBudget &budget){
  const auto *side=factionDetachment(*this,i);
  if(!side||!side->withdrawing)return false;
  auto &e=enemies[i];auto &body=world.targets[i];
  auto &motion=body.ai.combatMotion;
  const auto goal=point(side->withdrawal);
  auto toward=goal-body.origin;toward.y=0;
  // Timer is per actor, so scattered survivors have the same extraction window.
  e.decisionClock+=dt;
  if(length(toward)<10.f||e.decisionClock>=65.f){
    addEffect({WeaponEffect::Sparks,body.origin,unit(toward),Weapon::Empty,0,3.f,rpgArrivalWarp});
    body.active=false;body.escaped=true;body.ai.velocity={};
    return true;
  }
  e.fireClock=std::max(0.f,e.fireClock-dt);
  if(e.disabled>0||body.ai.gravity.active())return true;
  const auto &spec=combatSpec(e);
  const float pace=hasInjury(e.injuries,MechaInjury::Hobbled)?6.f:12.f;
  const auto heading=enemyNavigationDirection(world,i,unit(toward),goal,pace,dt,false,navigation[i],&budget);
  if(length(heading)>.01f){
    auto step=heading*(pace*dt);
    if(groundDriveTank(spec))step=tankDriveStep(world,i,heading,heading,navigation[i].priorDriveVelocity,pace,dt,hoverDriveTank(spec),false);
    else {body.yaw=std::atan2(-heading.x,-heading.z);body.ai.weaponYaw=body.yaw;}
    world.updateEnemyBounds(body);
    moveRpgEnemy(i,step,dt,false);
    motion.retreat=1.f;
  }
  const bool tank=groundDriveTank(spec);
  if((e.fireClock>0&&!tank)||e.overheated)return true;
  const auto *pilot=side->retaliating?nearbyPilot(*this,body.origin,75.f):nullptr;
  if(pilot&&!enemyCanSeeMember(*this,i,*pilot))pilot=nullptr;
  Vec3 target{};
  if(pilot)target=pilot->body.bodyPoint(6);
  int targetIndex=i<factionContacts.size()?factionContacts[i]:-1;
  if(targetIndex>=0&&active(*this,size_t(targetIndex))&&actorFaction(*this,size_t(targetIndex))==voidmawFaction&&
     (!pilot||length(world.targets[targetIndex].center-body.center)<length(target-body.center))){
    target=world.targets[targetIndex].center;
  }else{
    if(!pilot)return true;
    targetIndex=-1;
  }
  if(tank&&(!traverseTankGun(body,target,dt)||e.fireClock>0))return true;
  const float distance=length(target-body.center);
  const auto attack=std::find_if(spec.attacks.begin(),spec.attacks.end(),[&](const auto &a){
    return a.range>12&&a.dice>0&&distance>=a.minimum&&distance<=a.range;
  });
  if(attack==spec.attacks.end()||!enemyFiringLaneClear(*this,i,target,targetIndex))return true;
  const auto socket=tank?tankGunSocket(body,world.elapsed):std::nullopt;
  const auto muzzle=socket?socket->muzzle:body.center+body.ai.gravity.up()*(body.ai.localHalf.y*.2f);
  const auto ray=socket?socket->forward:unit(target-muzzle);
  Shot shot;
  shot.body.position=muzzle;shot.body.velocity=ray*70.f;shot.body.owner=int(i);
  shot.body.mount=-1;shot.body.life=attack->range/70.f;
  shot.body.weapon=Weapon::Rifle;
  int damage=0;
  for(int die=0;die<attack->dice;++die)damage+=dice.roll(attack->sides);
  shot.body.damage=float(std::max(1,damage));
  if(launch(std::move(shot))){
    body.ai.muzzleFlash=.12f;
    if(!tank)body.ai.weaponYaw=std::atan2(-ray.x,-ray.z);
    motion.play(EnemyAction::Fire,world.elapsed,.24f);
    addEffect({WeaponEffect::Sparks,muzzle,ray,Weapon::Rifle,0,1.5f,rpgEnemyKinetic});
    world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),muzzle});
    e.fireClock=1.1f;
    addReactorHeat(e.heat,e.overheated,e.ventRemaining,.27f);
  }
  return true;
}

void Session::followFactionPilots(size_t i,float dt,EnemyNavigationBudget &budget){
  auto &body=world.targets[i];
  const auto *pilot=nearbyPilot(*this,body.origin,assistanceRange);
  if(!pilot){patrolEnemy(i,dt,&budget);return;}
  npcRadio(i, RadioLine::FriendlyContact);
  auto delta=pilot->body.position-body.origin;delta.y=0;
  const float spacing=25.f+float(i%3)*6.f;
  if(length(delta)<spacing)return;
  const float pace=6.f;
  auto destination=pilot->body.position-unit(delta)*spacing;
  const auto heading=enemyNavigationDirection(world,i,unit(delta),destination,pace,dt,false,navigation[i],&budget);
  if(length(heading)<.01f)return;
  const auto &spec=combatSpec(enemies[i]);
  auto step=heading*(pace*dt);
  if(groundDriveTank(spec))step=tankDriveStep(world,i,heading,heading,navigation[i].priorDriveVelocity,pace,dt,hoverDriveTank(spec),false);
  else body.yaw=std::atan2(-heading.x,-heading.z);
  body.ai.weaponYaw=body.yaw;
  world.updateEnemyBounds(body);
  moveRpgEnemy(i,step,dt,false);
}
}
