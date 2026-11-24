#include "rpg/enemy_tactics.hpp"
#include "rpg/session.hpp"
#include "searchlight.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
namespace iron::rpg {
namespace {
bool biological(const EnemySpec &s){return s.kind==EnemyKind::SmallKaiju||s.kind==EnemyKind::LargeKaiju;}
bool meleeUnit(const EnemySpec &s){return s.optimal<=12&&std::any_of(s.attacks.begin(),s.attacks.end(),[](auto &a){return a.range<=10;});}
bool intersects(Vec3 from,Vec3 to,Vec3 center,Vec3 half){
 float low=0,high=1;const auto delta=to-from;
 for(int axis=0;axis<3;++axis){
  float p=axis==0?from.x:axis==1?from.y:from.z;
  float d=axis==0?delta.x:axis==1?delta.y:delta.z;
  float c=axis==0?center.x:axis==1?center.y:center.z;
  float h=axis==0?half.x:axis==1?half.y:half.z;
  if(std::abs(d)<.00001f){if(p<c-h||p>c+h)return false;}
  else{float a=(c-h-p)/d,b=(c+h-p)/d;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);if(low>high)return false;}
 }
 return low<.999f&&high>.001f;
}
bool alive(const Session &s,size_t i){return i<s.enemies.size()&&i<s.world.targets.size()&&s.enemies[i].hp>0&&s.enemies[i].disabled<=0&&s.world.targets[i].active&&!s.world.targets[i].escaped;}
float rangeOf(const EnemySpec &s){float value=10;for(auto &a:s.attacks)value=std::max(value,a.range);return value;}
int faction(const EnemySpec &s){auto catalog=enemyCatalog();for(size_t i=0;i<catalog.size();++i)if(std::string_view(catalog[i].id)==s.id)return enemyFaction(i);for(const auto &boss:campaignBosses())if(std::string_view(boss.spec.id)==s.id)return boss.faction;return -1;}
bool nearby(const Session &s,size_t i,size_t j){
 if(s.enemies[i].group>=0&&s.enemies[j].group>=0&&s.enemies[i].group!=s.enemies[j].group)return false;
 if(i==j||!alive(s,j)||factionsAtWar(s,i,j)||!enemyUnitsCompatible(combatSpec(s.enemies[i]),combatSpec(s.enemies[j])))return false;
 auto &a=combatSpec(s.enemies[i]);auto &b=combatSpec(s.enemies[j]);
 float radius=(a.traits|b.traits)&(Commander|HiveMind|PairAegis|PairGladius)?100.f:70.f;
 return length(s.world.targets[i].origin-s.world.targets[j].origin)<=radius;
}
bool firingLaneFrom(const Session &session,size_t actor,Vec3 from,Vec3 to,int ignoredTarget){
  if(session.world.groundContact(from,to)<.999f)return false;
  for(size_t index=0;index<session.world.targets.size();++index){
    const auto &obstacle=session.world.targets[index];
    if(index==actor||int(index)==ignoredTarget||!obstacle.active||obstacle.health<=0||obstacle.escaped)continue;
    if(intersects(from,to,obstacle.center,obstacle.half+Vec3{.35f,.35f,.35f}))return false;
  }
  return true;
}
std::optional<Vec3> openFiringPosition(const Session &session,size_t actor,Vec3 victim){
  const auto &body=session.world.targets[actor];
  Vec3 forward=victim-body.origin;
  forward.y=0;
  if(length(forward)<.001f)return {};
  const auto lateral=cross(unit(forward),{0,1,0});
  const auto muzzleOffset=body.center+body.ai.gravity.up()*(body.ai.localHalf.y*.2f)-body.origin;
  const float step=std::clamp(std::max(body.half.x,body.half.z)*1.3f+4.f,8.f,16.f);
  const float preferred=actor%2?1.f:-1.f;
  const auto traits=combatSpec(session.enemies[actor]).traits;
  const bool airborne=(traits&(Flying|Hover))||((traits&Submersible)&&session.world.waterDepthAt(body.origin)>0);
  // Test both sides and a second width before committing the navigation cache.
  for(float width:{step,step*2})for(float sign:{preferred,-preferred}){
    auto position=body.origin+lateral*(sign*width);
    if(!airborne)position.y=session.world.surfaceHeight(position.x,position.z);
    if(!insideArea(position,session.missionRadius()*.98f))continue;
    if(!firingLaneFrom(session,actor,position+muzzleOffset,victim+Vec3{0,6,0},-1))continue;
    if(!enemyNavigationPassage(session.world,actor,body.origin,position,airborne,true))continue;
    return position;
  }
  return {};
}
}
bool enemyUnitsCompatible(const EnemySpec &a,const EnemySpec &b){
 if(biological(a)!=biological(b))return false;
 if(biological(a))return true;
 if(((a.traits&PairAegis)&&(b.traits&PairGladius))||((b.traits&PairAegis)&&(a.traits&PairGladius)))return true;
 const int left=faction(a),right=faction(b);
 return left>=0&&left==right;
}
bool enemyCanSeeMember(const Session &s,size_t i,const Member &m){
 if(!alive(s,i)||!factionAttacksPlayers(s,i)||!m.combatControls()||m.sheet.hidden||m.hiddenRemaining>0||m.body.lifeSpent)return false;
 const auto &body=s.world.targets[i];Pilot observer;observer.position=body.origin;
 if(hasInjury(s.enemies[i].injuries,MechaInjury::Sensors)&&length(m.body.position-body.origin)>32.f)return false;
 Vec3 from=(body.ai.gravity.active()?body.center:body.origin+Vec3{0,6,0}),to=m.body.bodyPoint(6);
 if(!visibilityInRange(s.world,observer,to,pilotRevealed(m))||s.world.groundContact(from,to)<.999f)return false;
 const auto &spec=combatSpec(s.enemies[i]);
 bool breacher=std::any_of(spec.attacks.begin(),spec.attacks.end(),[](auto &a){return a.range<=10;});
 for(size_t k=s.enemies.size();k<s.world.targets.size();++k){
  const auto &t=s.world.targets[k];if(!t.active||t.health<=0||t.escaped||t.spawnType==21)continue;
  // A close-combat actor can identify a contact through destructible cover and
  // choose to breach it. Structural hulls still hide contacts completely.
  if(breacher&&!t.invulnerable)continue;
  if(intersects(from,to,t.center,t.half))return false;
 }
 return true;
}
bool enemyFiringLaneClear(const Session &s,size_t i,Vec3 to,int ignoredTarget){
 if(i>=s.world.targets.size())return false;
 const auto &body=s.world.targets[i];
 return firingLaneFrom(s,i,body.center+body.ai.gravity.up()*(body.ai.localHalf.y*.2f),to,ignoredTarget);
}
Vec3 enemyRecoveryStrafe(const Session &session,size_t actor,Vec3 target){
  const auto &enemy=session.enemies[actor];
  if(enemy.tactic!=3&&enemy.tactic!=5&&enemy.tactic!=7)return {};
  if(enemy.fireClock<.46f||enemy.fireClock>1.05f)return {};
  const auto &spec=combatSpec(enemy);
  if(enemy.boss>=0||spec.kind==EnemyKind::Installation||biological(spec)||
     !(spec.traits&(Ace|Hover|Flying|Leap|Veriform)))return {};
  const auto origin=session.world.targets[actor].origin;
  auto toward=target-origin;
  toward.y=0;
  const float distance=length(toward);
  if(distance<8.f||distance>std::min(95.f,rangeOf(spec)))return {};
  toward=toward*(1.f/distance);
  const auto side=cross(toward,{0,1,0})*(actor%2?1.f:-1.f);
  // Keep the target in usable range so the next attack has a real opening.
  const auto movement=unit(side+toward*(distance>spec.optimal? .28f:-.28f));
  if(insideArea(origin+movement*8.f,session.missionRadius()*.98f))return movement;
  return {};
}
std::vector<EnemyTacticalDecision> planEnemyTactics(const Session &s,std::span<const uint32_t> previous){
 const size_t count=s.enemies.size();std::vector<EnemyTacticalDecision> result(count);
 std::vector<std::vector<bool>> visible(count,std::vector<bool>(s.members.size()));
 for(size_t i=0;i<count;++i)for(size_t m=0;m<s.members.size();++m)visible[i][m]=enemyCanSeeMember(s,i,s.members[m]);
 std::vector<size_t> order(count);std::iota(order.begin(),order.end(),0);
 std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return bool(combatSpec(s.enemies[a]).traits&(Commander|HiveMind))>bool(combatSpec(s.enemies[b]).traits&(Commander|HiveMind));});
 for(size_t i:order){
  if(!alive(s,i))continue;
  const auto &spec=combatSpec(s.enemies[i]);const auto origin=s.world.targets[i].origin;
  size_t groupSize=1;for(size_t j=0;j<count;++j)if(nearby(s,i,j))++groupSize;
  const int options=int(std::count(visible[i].begin(),visible[i].end(),true));
  const int focusCapacity=std::max(2,int(groupSize)/std::max(1,options)+1);
  float best=-1e20f;
  for(size_t m=0;m<s.members.size();++m){
   if(!visible[i][m])continue;
   const auto &victim=s.members[m];float distance=length(victim.body.position-origin);
   float reach=rangeOf(spec),score=1.f-distance/std::max(30.f,reach);
   // Health and visible weapon activity are observable signals. Reactor values,
   // inventory, hidden cooldowns and future player inputs never enter scoring.
   score+=.28f*(1-std::clamp(float(victim.sheet.hp)/std::max(1,victim.sheet.maxHp),0.f,1.f));
   if(std::any_of(victim.body.wasFire.begin(),victim.body.wasFire.end(),[](bool f){return f;}))score+=.12f;
   if(i<previous.size()&&previous[i]==victim.id)score+=.20f;
   int focused=0,shared=0;
   for(size_t j=0;j<count;++j)if(nearby(s,i,j)){
    if(visible[j][m])++shared;
    if(result[j].targetId==victim.id)++focused;
   }
   score+=std::min(3,shared)*.035f+std::min(2,focused)*.20f;
   score-=std::max(0,focused-focusCapacity+1)*.30f;
   if(!enemyFiringLaneClear(s,i,victim.body.bodyPoint(6)))score-=.10f;
   // Member IDs resolve exact ties regardless of lobby insertion order.
   if(score>best+.00001f||(std::abs(score-best)<=.00001f&&(result[i].memberIndex<0||victim.id<result[i].targetId))){best=score;result[i].memberIndex=int(m);result[i].targetId=victim.id;}
  }
 }
 for(size_t i=0;i<count;++i){
  auto &decision=result[i];if(decision.memberIndex<0)continue;
  const auto &spec=combatSpec(s.enemies[i]);const auto &body=s.world.targets[i];
  const auto &pilot=s.members[size_t(decision.memberIndex)].body;
  auto victim=pilot.position;
  decision.destination=victim;decision.fireLaneClear=enemyFiringLaneClear(s,i,pilot.bodyPoint(6));
  const auto &enemy=s.enemies[i];
  const bool rangedDoctrine=enemy.tactic==0||enemy.tactic==3||enemy.tactic==8;
  std::optional<Vec3> opening;
  if(!decision.fireLaneClear&&rangedDoctrine&&!enemy.cover&&spec.kind!=EnemyKind::Installation){
    opening=openFiringPosition(s,i,victim);
  }
  std::vector<size_t> group;
  Vec3 center=body.origin;
  for(size_t j=0;j<count;++j)if(nearby(s,i,j)&&result[j].targetId==decision.targetId){group.push_back(j);center=center+s.world.targets[j].origin;}
  if(group.empty()){
   if(opening){decision.destination=*opening;decision.positioning=true;continue;}
   // A disabled or incompatible ally still blocks physical shots. Precision
   // and ordinary ranged actors may clear that lane without a squad partner;
   // a clear lane preserves their stationary doctrine hold.
   if(!decision.fireLaneClear&&spec.kind!=EnemyKind::Installation&&
      (s.enemies[i].tactic==0||s.enemies[i].tactic==8)){
    auto forward=victim-body.origin;forward.y=0;
    auto side=cross(unit(forward),{0,1,0});if(length(side)<.001f)side={1,0,0};
    float offset=std::clamp(std::max(body.half.x,body.half.z)*1.3f+4.f,8.f,32.f);
    float sign=i%2?1.f:-1.f;
    for(float direction:{sign,-sign}){
     auto position=body.origin+side*(offset*direction);
     if(insideArea(position,s.missionRadius()*.98f)){
      decision.destination=position;decision.positioning=true;break;
     }
    }
   }
   continue;
  }
  decision.coordinated=true;center=center*(1.f/float(group.size()+1));
  // A support actor stays with a compatible front line, never an incompatible
  // biological/robotic neighbour. Partners are stable by actor identity.
  for(size_t j:group)if(decision.partner<0||length(s.world.targets[j].origin-body.origin)<length(s.world.targets[size_t(decision.partner)].origin-body.origin))decision.partner=int(j);
  if(opening){decision.destination=*opening;decision.positioning=true;continue;}
  if(spec.kind==EnemyKind::Installation)continue;
  group.push_back(i);std::sort(group.begin(),group.end());
  size_t slot=size_t(std::find(group.begin(),group.end(),i)-group.begin());
  Vec3 back=center-victim;back.y=0;if(length(back)<.001f)back={0,0,1};back=unit(back);
  Vec3 side=cross(back,{0,1,0});
  float offset=(float(slot)-float(group.size()-1)*.5f);
  const bool support=(spec.traits&(Commander|Carrier))!=0;
  if((meleeUnit(spec)&&!support)||s.enemies[i].tactic==1||s.enemies[i].tactic==6){
   float turn=std::clamp(offset*.65f,-1.3f,1.3f);
   float radius=std::clamp(std::max(body.half.x,body.half.z)*.65f+3.5f,5.f,8.f);
   decision.destination=victim+(back*std::cos(turn)+side*std::sin(turn))*radius;
   decision.positioning=length(victim-body.origin)>10||!decision.fireLaneClear;
  }else{
   float reach=rangeOf(spec);float distance=std::clamp(spec.optimal,12.f,std::max(12.f,reach*.85f));
   if(support)distance=std::min(reach*.9f,distance+12.f);
   float spacing=std::clamp(body.half.x*1.4f+3.f,6.f,16.f);
   decision.destination=victim+back*distance+side*std::clamp(offset*spacing,-24.f,24.f);
   // Doctrine (ambush, evasive, hit-and-run) remains authoritative. Only the
   // ordinary/cover/flanking/precision plans receive explicit firing positions.
   decision.positioning=(s.enemies[i].tactic==0||s.enemies[i].tactic==2||s.enemies[i].tactic==3||s.enemies[i].tactic==8)&&
     (length(decision.destination-body.origin)>5||!decision.fireLaneClear);
  }
  decision.destination.y=body.origin.y;
  if(!insideArea(decision.destination,s.missionRadius()*.98f))decision.positioning=false;
 }
 return result;
}
}
