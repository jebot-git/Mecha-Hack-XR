#include "rpg/session.hpp"
#include "net/snapshot_packet.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/mecha_ranged.hpp"
#include <iostream>
#include <stdexcept>

using namespace iron;
using namespace iron::rpg;

namespace {
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int species(std::string_view id){
  for(size_t i=0;i<enemyCatalog().size();++i)if(enemySpec(i).id==id)return int(i);
  throw std::runtime_error("Missing ranged test actor");
}
void actor(Session &s,const char *id,Vec3 position){
  Enemy e;e.spec=species(id);e.hp=e.maxHp=1000;e.fireClock=0;e.tactic=2;
  s.enemies.push_back(e);s.mission.recipe.enemies.push_back(e.spec);
  Target t;t.origin=position;t.active=true;t.spawnType=3;
  applyOriginalEnemy(t,e.spec);t.health=t.ai.maxHealth=1000;
  s.world.updateEnemyBounds(t);s.world.targets.push_back(std::move(t));
}
Session arena(const char *id,float distance){
  Session s(713,true);
  s.add(1,"Host");s.add(2,"Wing");
  s.phase=Phase::Combat;s.biome=s.mission.recipe.environment=3;
  s.mission.openerTime=100;s.mission.recipe.opener=5;s.settings.extent=500;
  s.world.targets.clear();s.world.movementBounds={-500,-500,500,500};
  s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();
  s.world.interiorRegions.clear();s.world.waterRegions.clear();s.world.rpgPrototype=true;
  for(auto &m:s.members){
    m.sheet.hp=m.sheet.maxHp=10000;m.sheet.ap=0;m.sheet.attributes.fill(12);
    m.body.position={0,0,distance};m.body.health=100;m.input.focused=true;
  }
  // The host observes at the side. A joining client owns the selected target.
  s.member(1).body.position={150,0,100};
  s.member(1).sheet.hidden=true;s.member(1).hiddenRemaining=100;
  actor(s,id,{});
  return s;
}
int count(const Session &s,ProjectileVisual visual){
  return int(std::count_if(s.ordnance.begin(),s.ordnance.end(),[&](const Shot &shot){return shot.body.visual==visual;}));
}
void checkSnapshot(const Session &s){
  Session guest;
  guest.readSnapshot(coop::decodeStatePacket(coop::encodeSnapshotPacket(s.snapshot())));
  const auto view=guest.view(2);
  check(view.projectiles.size()==s.world.projectiles.size(),"Co-op omitted ranged projectiles");
  for(size_t i=0;i<view.projectiles.size();++i){
    const auto &remote=view.projectiles[i],&host=s.world.projectiles[i];
    check(remote.visual==host.visual&&remote.kind==host.kind&&remote.owner==host.owner&&
          length(remote.position-host.position)<.02f&&length(remote.velocity-host.velocity)<.02f,
          "Co-op snapshot lost a moving projectile or its visual");
  }
}
Target wall(){
  Target w;w.center=w.origin={0,20,25};w.half={55,20,1.2f};
  w.active=w.invulnerable=true;w.health=1e6f;w.spawnType=22;return w;
}
void weave(float dt){
  auto s=arena("jump_pod_field_commander",54);
  s.step(dt);
  const auto lock=s.enemies.front().heavyAttack;
  check(lock.pattern==HeavyPattern::Weave&&s.ordnance.empty()&&s.enemies.front().chargeTarget==2,
        "Agile commander failed to warn the selected pilot");
  const auto mark=lock.origin+lock.aim*lock.lockDistance;
  auto walk=s,cover=s;
  cover.world.targets.push_back(wall());
  auto &walker=walk.member(2);
  const auto start=walker.body.position;
  walker.sheet.armor=Armor::Light;
  walker.body.chassisYaw=std::atan2(-lock.lateral.x,-lock.lateral.z);
  float crossing=0,firstRelease=0,furthest=0;
  bool verifiedRows=false;
  for(int n=0;n<int(5.25f/dt);++n){
    const float now=walk.world.elapsed;
    // Walk to the first opening during the tell, then traverse the two
    // successive openings using only the normal forward/reverse VR input.
    const float desired=mechaWaveSpacing*(crossing<=0?1.f:
        std::clamp(1.f-(now-crossing)/mechaWaveCadence,-1.f,1.f));
    const float progress=dot(walker.body.position-start,lock.lateral);
    walker.input.forward=std::clamp((desired-progress)*3.f,-1.f,1.f);
    walk.command(walker.id,{{"op","input"},{"epoch",walk.epoch},{"input",coop::inputWire(walker.input)}});
    furthest=std::max(furthest,progress);
    s.step(dt);walk.step(dt);cover.step(dt);
    if(!s.ordnance.empty()&&firstRelease==0){
      firstRelease=s.world.elapsed;
      const auto &p=s.ordnance.front().body;
      crossing=firstRelease+dot(mark-p.position,lock.aim)/dot(p.velocity,lock.aim);
    }
    if(!verifiedRows&&s.ordnance.size()==18){
      check(count(s,ProjectileVisual::WaveA)==12&&count(s,ProjectileVisual::WaveB)==6,
            "Volley did not produce three alternating six-projectile rows");
      for(int row=0;row<3;++row){
        std::array<bool,7> occupied{};
        for(int n=0;n<6;++n){
          const auto &p=s.ordnance[size_t(6*row+n)].body;
          const float t=dot(mark-p.position,lock.aim)/dot(p.velocity,lock.aim);
          const float lane=dot(p.position+p.velocity*t-mark,lock.lateral)/mechaWaveSpacing;
          const int col=int(std::lround(lane))+3;
          check(col>=0&&col<7&&std::abs((col-3)-lane)<.03f&&!occupied[col],
                "Row bullets failed to cross their distinct locked lanes");
          occupied[col]=true;
        }
        check(!occupied[4-row],"Adjacent wave rows closed the promised opening");
      }
      check(std::abs(s.world.targets.front().origin.x)>3,"Firing skirmisher stood still");
      checkSnapshot(s);verifiedRows=true;
    }
  }
  check(verifiedRows&&std::abs(firstRelease-.76f)<.09f,"Skirmisher cadence diverged");
  check(s.member(2).sheet.hp<10000,"Stationary pilot took no wave damage");
  check(furthest>3.f&&dot(walker.body.position-start,lock.lateral)<-3.f&&walker.sheet.hp==10000,
        "Normal walking cannot traverse successive wave gaps");
  check(cover.member(2).sheet.hp==10000,"A wave penetrated solid cover");
}
void rack(float dt){
  auto s=arena("siege_mecha",58);s.step(dt);
  check(s.enemies[0].heavyAttack.pattern==HeavyPattern::Loft&&s.ordnance.empty(),"Missing rack warning");
  auto cover=s,broken=s;
  cover.world.targets.push_back(wall());
  bool lifted=false,dived=false,replicated=false,intercepted=false,lockLost=false;
  float firstRelease=0,lastRelease=0;int peak=0;
  for(int n=0;n<int(6.f/dt);++n){
    s.step(dt);cover.step(dt);broken.step(dt);
    const int active=count(s,ProjectileVisual::LoftMissile);
    if(active>peak){peak=active;lastRelease=s.world.elapsed;}
    if(active>0&&!firstRelease){
      firstRelease=s.world.elapsed;
      auto &p=s.ordnance.front().body;
      lifted=p.kind==ProjectileKind::Rocket&&p.hull>0&&p.velocity.y>10;
      broken.member(2).hiddenRemaining=.15f;
    }
    if(firstRelease&&broken.world.elapsed>firstRelease+.25f&&!broken.ordnance.empty()){
      check(std::all_of(broken.ordnance.begin(),broken.ordnance.end(),[](const auto &q){
        return q.body.visual!=ProjectileVisual::LoftMissile||q.loftPilot==0;
      }),"Missile reacquired a lost pilot");lockLost=true;
    }
    if(active==12&&!replicated){checkSnapshot(s);replicated=true;}
    for(const auto &q:s.ordnance)if(q.body.visual==ProjectileVisual::LoftMissile&&q.body.velocity.y<-1.f)dived=true;
    if(!intercepted&&active==12){
      auto intercept=s;
      auto &wing=intercept.member(2);
      wing.sheet.equipment.fill(Gear::Empty);wing.sheet.equipment[RightHand]=Gear::Rifle;
      const int before=count(intercept,ProjectileVisual::LoftMissile);
      const auto shotCount=wing.body.shots[RightHand];
      // Feed a joining pilot's tracked aim and trigger through the normal
      // input receiver, IK, rifle dispersion and authoritative shot collision.
      bool blast=false;
      for(int frame=0;frame<int(2.2f/dt)&&!blast;++frame){
        auto it=std::find_if(intercept.ordnance.begin(),intercept.ordnance.end(),[](const auto &q){
          return q.body.visual==ProjectileVisual::LoftMissile;
        });
        if(it==intercept.ordnance.end())break;
        const auto &rocket=it->body;
        const auto &arm=wing.body.arms[RightHand];
        const Vec3 muzzle=arm.tracked?arm.muzzle:wing.body.bodyPoint(10);
        const float range=length(rocket.position-muzzle);
        const Vec3 aim=unit(rocket.position+rocket.velocity*(range/weaponTuning(Gear::Rifle)->velocity)-muzzle);
        Input input;
        input.handPoses[RightHand].aim=angles(std::atan2(-aim.x,-aim.z),std::asin(aim.y));
        input.handPoses[RightHand].grip=input.handPoses[RightHand].aim;
        input.fire[RightHand]=range<35.f;
        intercept.command(wing.id,{{"op","input"},{"epoch",intercept.epoch},{"input",coop::inputWire(input)}});
        intercept.step(dt);
        blast=std::any_of(intercept.world.effects.begin(),intercept.world.effects.end(),[](const auto &fx){
          return fx.model==rpgWeaponBlast;
        });
      }
      check(blast&&wing.body.shots[RightHand]>shotCount&&count(intercept,ProjectileVisual::LoftMissile)<before,
            "Wing pilot's tracked rifle failed to intercept a missile");
      checkSnapshot(intercept);intercepted=true;
    }
  }
  check(peak==12&&lifted&&dived&&intercepted&&replicated&&lockLost,"Incomplete loft trajectory or rack salvo");
  check(std::abs(firstRelease-1.05f)<.09f&&std::abs(lastRelease-firstRelease-3*mechaRackCadence)<.07f,
        "Rack timing changed with tick interval");
  check(s.member(2).sheet.hp<10000&&cover.member(2).sheet.hp==10000,"Loft missile missed its stationary pilot or penetrated cover");
}
void saturatedPool(){
  auto s=arena("jump_pod_field_commander",54);
  s.step(.05f);
  check(s.enemies[0].heavyAttack.pattern==HeavyPattern::Weave,"Saturation fixture failed to lock");
  Shot background;background.body.position={100,80,-90};background.body.velocity={0,0,-1};
  background.body.life=10;background.body.shooter=1;
  s.ordnance.assign(maxLiveProjectiles-27,background);
  unsigned pulses=0;
  for(int i=0;i<51;++i){
    s.step(.05f);
    pulses=std::max(pulses,unsigned(s.enemies[0].heavyAttack.pulse));
    check(s.ordnance.size()==maxLiveProjectiles-27,"Saturated gunner emitted a partial row or consumed pilot reserve");
  }
  check(pulses==3,"Skipped rows stalled the next attack cycle");
}
void factionTarget(){
  auto s=arena("siege_mecha",80);
  actor(s,"labor_bot",{0,0,55});
  auto &neutral=s.mission.groups.emplace_back();
  neutral.factionRole=FactionRole::Neutral;neutral.actors={0};neutral.faction=actorFaction(s,0);
  s.enemies[0].group=0;s.enemies[1].disabled=10;
  bool salvo=false;
  for(int n=0;n<360;++n){
    s.step(1.f/60);
    for(const auto &shot:s.ordnance)if(shot.body.visual==ProjectileVisual::LoftMissile){
      check(!shot.npcCanHitPilots,"Neutral faction missile can hurt a pilot");
      salvo|=shot.loftNpc==1;
    }
  }
  check(salvo&&s.enemies[1].hp<1000,"Ranged faction NPC failed to engage its hostile NPC contact");
}
}
int main(){try{
  setOriginalAssetRoot(MECHA_ASSETS);
  check(mechaRangedRole(enemySpec(species("jump_pod_heavy")))==MechaRangedRole::Skirmisher&&
      mechaRangedRole(enemySpec(species("terran_mammoth")))==MechaRangedRole::Skirmisher&&
      mechaRangedRole(enemySpec(species("thunder_cannon_mecha")))==MechaRangedRole::MissileCarrier&&
      mechaRangedRole(enemySpec(species("mecha_sniper")))==MechaRangedRole::None&&
      mechaRangedRole(enemySpec(species("heavy_hover_tank")))==MechaRangedRole::None,
      "Mechanical role dispatch crossed a mobility, vehicle or sniper boundary");
  for(float hz:{30.f,60.f,90.f}){weave(1.f/hz);rack(1.f/hz);}
  factionTarget();saturatedPool();
  std::cout<<"Ranged mecha: walkable three-row volleys, twelve guided and interceptable missiles, cover, factions and joining snapshots passed at 30/60/90 Hz.\n";
}catch(const std::exception &error){std::cerr<<error.what()<<"\n";return 1;}}
