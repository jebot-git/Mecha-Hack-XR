#include "net/snapshot_packet.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <iostream>
#include <stdexcept>

using namespace iron;
using namespace iron::rpg;
namespace {
void check(bool ok,const char *reason){if(!ok)throw std::runtime_error(reason);}
int indexOf(std::string_view id){
  const auto catalog=enemyCatalog();
  for(size_t i=0;i<catalog.size();++i)if(catalog[i].id==id)return int(i);
  throw std::runtime_error("fixture enemy missing");
}
Session arena(bool coop=false){
  Session s(73,coop);s.add(1,"Guard");
  if(coop){s.add(2,"Target");s.member(1).body.position={100,0,60};}
  s.phase=Phase::Combat;s.mission.recipe.opener=5;s.mission.openerTime=100;
  s.mission.recipe.objective=0;s.mission.recipe.environment=3;s.biome=3;
  s.settings.extent=500;s.world.movementBounds={-500,-500,500,500};
  auto &p=s.members.back();p.body.position={};p.sheet.ap=0;p.sheet.hp=p.sheet.maxHp=600;
  p.sheet.equipment.fill(Gear::Empty);
  Enemy enemy;enemy.spec=indexOf("mecha_ace");enemy.fireClock=0;enemy.hp=enemy.maxHp=500;
  s.enemies={enemy};s.mission.recipe.enemies={enemy.spec};
  Target body;body.spawnType=3;body.origin={0,0,-28};body.health=body.ai.maxHealth=500;
  applyOriginalModel(body,originalVariant(enemyModelId(enemy.spec),0));s.world.updateEnemyBounds(body);
  s.world.targets={body};return s;
}
void enterWindup(Session &s,float dt){
  s.step(dt);
  check(s.enemies.front().duel.phase==DuelPhase::Rush,"Equipped ace failed to charge");
  for(int n=0;n<120&&s.enemies[0].duel.phase!=DuelPhase::Windup;++n)s.step(dt);
  const auto &duel=s.enemies[0].duel;
  check(duel.phase==DuelPhase::Windup&&length(s.world.targets[0].origin-s.members.back().body.position)<10,
        "Rush failed to close into striking range");
  check(duel.pilot==s.members.back().id,"Rush targeted wrong co-op pilot");
  const auto &pilot=s.members.back();
  const auto guide=s.view(pilot.id).meleeGuide;
  check(guide.kind==MeleeGuide::Guard&&length(guide.position-duel.guardPoint)<.001f&&
        dot(guide.axis,duel.guardAxis)>.999f,"Pilot guard guide missed the required contact line");
  if(s.members.size()>1){
    Session peer;peer.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(s.snapshot())));
    check(peer.view(pilot.id).meleeGuide.kind==MeleeGuide::Guard&&
          peer.view(s.members.front().id).meleeGuide.kind==MeleeGuide::None,"Guard cue lost pilot ownership in snapshot");
  }
  check(duel.until-s.world.elapsed>.65f,"First cut has insufficient warning");
  const auto cue=std::find_if(s.world.effects.begin(),s.world.effects.end(),[](const auto &fx){return fx.model==rpgDuelGuardTell;});
  check(cue!=s.world.effects.end()&&length(cue->position-duel.guardPoint)<.001f&&dot(cue->direction,duel.guardAxis)>.999f,
        "Guard tell and authoritative guard disagree");
}
void guardInput(Session &s,Input &in,float dt){
  auto &m=s.members.back();m.input=in;m.inputAge=0;s.step(dt);
}
}
int main(){try{
  setOriginalAssetRoot(MECHA_ASSETS);
  for(const auto &[dt,coop]:{std::pair{.02f,false},std::pair{1.f/36,true}}){
    auto s=arena(coop);s.enemies[0].fireClock=1.4f;enterWindup(s,dt);
    const auto health=s.members.back().sheet.hp;
    auto &enemy=s.enemies[0];
    bool sawActiveCut=false;
    std::vector<MechaTechnique> cuts;
    for(int n=0;n<400&&enemy.duel.phase!=DuelPhase::Recover;++n){
      if(enemy.duel.phase==DuelPhase::Windup&&enemy.duel.beat==int(cuts.size()))cuts.push_back(enemy.duel.cut);
      if(cuts.size()==1&&enemy.duel.phase==DuelPhase::Windup)check(s.members.back().sheet.hp==health,"Early damage in first windup");
      s.step(dt);
      if(enemy.duel.phase==DuelPhase::FollowThrough&&
          std::any_of(s.ordnance.begin(),s.ordnance.end(),[](const Shot &shot){return shot.meleeStrike&&shot.body.life>0;})){
        check(s.members.back().meleeGuide.kind==MeleeGuide::Guard,"Guard guide disappeared during a live cut");
        sawActiveCut=true;
      }
    }
    check(sawActiveCut,"Fixture failed to exercise a parryable travelling cut");
    check(cuts.size()==3&&cuts[0]!=cuts[1]&&cuts[2]==MechaTechnique::Overhead,"Ace failed alternating three-strike exchange");
    check(s.members.back().sheet.hp<health&&enemy.duel.until-s.world.elapsed>1,"No physical contact or recovery opening");
  }
  auto hybrid=arena();hybrid.enemies[0].spec=indexOf("mecha_sniper");
  hybrid.mission.recipe.enemies[0]=hybrid.enemies[0].spec;
  hybrid.enemies[0].fireClock=1.4f;hybrid.world.targets[0].origin.z=-15;
  hybrid.world.updateEnemyBounds(hybrid.world.targets[0]);enterWindup(hybrid,.025f);
  auto s=arena();enterWindup(s,.025f);
  auto overlap=s;
  overlap.members.back().riposteTarget=0;overlap.members.back().riposteRemaining=.3f;
  overlap.step(.025f);
  Session peer;peer.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(overlap.snapshot())));
  const auto both=peer.view(overlap.members.back().id).meleeGuide;
  check(both.kind==MeleeGuide::Guard&&both.simultaneousCounter,"Defensive warning hid an active counterattack opening");
  const int hp=s.members.back().sheet.hp;
  auto dodge=s;dodge.members.back().body.position.x+=7.f;
  for(int n=0;n<44;++n)dodge.step(.025f);
  check(dodge.members.back().sheet.hp==hp,"Warned lane tracked a sidestepping pilot");
  auto severed=s;severed.enemies[0].missingArms=3;severed.step(.025f);
  check(severed.enemies[0].duel.phase==DuelPhase::Idle,"Limb loss failed to cancel committed attack");
  check(severed.members.back().meleeGuide.kind==MeleeGuide::None,"Disarming attacker left a stale cockpit guide");
  check(std::none_of(severed.world.effects.begin(),severed.world.effects.end(),[](const auto &fx){return fx.model==rpgDuelGuardTell;}),"Interrupted guard marker persisted");
  // Reach the legal guard pose with the tracked controller for each cut.
  for(int beat=0;beat<3;++beat){
    const auto &duel=s.enemies[0].duel;
    for(int n=0;n<140&&(duel.phase!=DuelPhase::Windup||duel.beat<beat);++n)s.step(.025f);
    check(duel.phase==DuelPhase::Windup&&duel.beat==beat,"Expected next guard pose");
    check(duel.guardPoint.y>s.members.back().body.bodyPoint(12).y,"Guard line lies below the forward cockpit glass");
    const int health=s.members.back().sheet.hp;
    const int hand=duel.guardPoint.x<0?LeftHand:RightHand;
    const auto axis=duel.guardAxis;
    const auto orient=angles(std::atan2(-axis.x,-axis.z),std::asin(axis.y));
    Input in;in.handValid[hand]=true;
    const auto eye=s.members.back().body.bodyPoint(12);
    in.handPoses[hand]={(duel.guardPoint-eye)*(1.f/6),orient,orient,true};
    for(int mode=0;mode<4;++mode){
      auto test=s;auto pose=in;auto &m=test.members.back();
      m.sheet.equipment[hand]=mode==2?Gear::Empty:Gear::Blade;
      m.handStashed[hand]=mode==3;
      if(mode==1)pose.handPoses[hand].aim=pose.handPoses[hand].grip={};
      for(int n=0;n<44&&m.riposteRemaining<=0&&m.sheet.hp==health;++n)guardInput(test,pose,.025f);
      if(mode==0){
        check(m.sheet.hp==health&&m.riposteTarget==0&&m.riposteRemaining>0,"Correct tracked blade failed to parry actual ace");
        check(test.view(m.id).meleeGuide.kind==MeleeGuide::Counter,"Missing counterattack guide after a parry");
        check(test.enemies[0].disabled>0&&test.world.targets[0].ai.combatMotion.action==EnemyAction::Stagger,"Parry failed to expose enemy");
      }else check(m.sheet.hp<health&&m.riposteRemaining<=0,"Invalid guard stopped attack");
    }
  }
  std::cout<<"Melee rush, directional chain, guard, stagger, dodge, disarm and co-op target passed\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
