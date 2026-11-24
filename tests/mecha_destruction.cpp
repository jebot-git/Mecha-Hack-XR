#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/destruction.hpp"
#include <cassert>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
static Session fixture(int opener=5,int enemy=3) {
  Session game(723);game.add(1,"Pilot");
  GameSettings settings;settings.maps=1;settings.location=0;settings.environment=3;
  settings.objective=0;settings.opener=opener;settings.enemy=enemy;
  settings.enemyCount=1;settings.tactic=1;
  game.command(1,{{"op","settings"},{"settings",settings}});
  game.command(1,{{"op","ready"},{"value",true}});
  game.command(1,{{"op","deploy"}});
  game.enemies[0].disabled=10000;
  game.world.proceduralTerrain=false;game.world.editGeometry().terrain.clear();
  game.world.indexTerrain();game.world.interiorRegions.clear();
  game.world.waterRegions.clear();
  for(auto &t:game.world.targets)t.active=false;
  auto &actor=game.world.targets[0];actor.active=true;actor.origin={50,0,-100};
  game.world.updateEnemyBounds(actor);
  return game;
}
static size_t building(Session &game,Vec3 position,float hp=7,bool invulnerable=false,const char *model="building") {
  // Reuse a stable scene slot so native network snapshots keep their topology.
  for(size_t i=game.enemies.size();i<game.world.targets.size()-8;++i) {
    auto &target=game.world.targets[i];
    if(target.active)continue;
    target=Target{};target.origin=position;target.yaw=0;applyOriginalModel(target,model);
    target.health=hp;target.invulnerable=invulnerable;target.active=true;
    target.spawnType=-1;target.processed=false;
    return i;
  }
  throw std::runtime_error("No stable scenery slot in test map");
}
static void shoot(Session &game,size_t index,float damage,bool splash=false) {
  const auto &target=game.world.targets[index];Shot shot;
  shot.body.owner=0;shot.body.shooter=-1;shot.body.damage=damage;
  if(splash) {
    shot.body.position=target.center+Vec3{target.half.x+1,0,0};
    shot.body.velocity={};shot.body.life=.01f;shot.body.radius=2;
  } else {
    shot.body.position=target.center+Vec3{0,0,target.half.z+10};
    shot.body.velocity={0,0,-180};shot.body.life=2;
  }
  game.ordnance.push_back(shot);
  for(int tick=0;tick<20&&!game.ordnance.empty();++tick)game.step(.05f);
}
int main() {
  // Real enemy projectiles chip scenery, collapse it exactly once, and expose
  // the native ten-frame explosion/grounded-debris path to both network peers.
  {
    auto game=fixture();auto index=building(game,{0,0,0});
    game.member(1).body.position={0,0,60};
    shoot(game,index,4);assert(game.world.targets[index].health==3);
    assert(!game.world.targets[index].processed);
    shoot(game,index,5);assert(game.world.targets[index].health==0);
    assert(game.world.targets[index].processed);
    assert(std::any_of(game.world.effects.begin(),game.world.effects.end(),
                      [](const auto &fx){return fx.kind==WeaponEffect::Debris;}));
    Session replica;replica.readSnapshot(game.snapshot());
    assert(replica.world.targets[index].health==0);
    assert(replica.world.effects.size()==game.world.effects.size());
    for(int i=0;i<35;++i) {
      game.step(.1f);
      for(const auto &fx:game.world.effects)if(fx.kind==WeaponEffect::Debris)
        assert(fx.position.y>=game.world.debrisFloor(fx)-.0001f);
      assert(std::none_of(game.world.sounds.begin(),game.world.sounds.end(),
          [](const auto &s){return s.kind==SoundEvent::Explosion||s.kind==SoundEvent::LargeExplosion;}));
    }
    for(int i=0;i<220;++i)game.step(.1f);
    assert(game.world.effects.empty());
  }
  // Enemy splash reaches the nearest surface of a large structure, while
  // structural invulnerability still protects interior walls and hulls.
  {
    auto game=fixture();auto index=building(game,{0,0,0});
    auto structural=building(game,{0,0,0},7,true);
    shoot(game,index,8,true);
    assert(game.world.targets[index].health==0&&game.world.targets[index].processed);
    assert(game.world.targets[structural].health==7&&!game.world.targets[structural].processed);
  }
  // Heavy grounded enemies crush obstructing buildings through timed contact,
  // even when their melee plan stops inside the attack radius. The collapsed
  // slot no longer blocks their native locomotion.
  {
    auto game=fixture(5,28);game.enemies[0].disabled=0;
    game.member(1).body.position={0,0,60};
    auto index=building(game,{0,0,0},12);
    auto &actor=game.world.targets[0];actor.origin={0,0,0};actor.yaw=-pi;
    game.world.updateEnemyBounds(actor);
    const auto &cover=game.world.targets[index];
    actor.origin.z=cover.center.z-cover.half.z-(actor.center.z-actor.origin.z)-actor.half.z+.25f;
    game.world.updateEnemyBounds(actor);
    assert(enemySpec(game.enemies[0].spec).kind==EnemyKind::LargeKaiju);
    game.enemies[0].tactic=2;game.enemies[0].decisionClock=10000;
    game.enemies[0].anchorX=actor.origin.x;game.enemies[0].anchorZ=actor.origin.z;
    Vec3 before=actor.origin;game.enemies[0].fireClock=10000;
    game.step(.05f);
    assert(length(actor.origin-before)<.001f&&game.world.targets[index].health<12);
    for(int i=0;i<80&&game.world.targets[index].health>0;++i)game.step(.05f);
    assert(game.world.targets[index].health==0&&game.world.targets[index].processed);
  }
  // A fixed-step drop applies one crushing landing regardless of tick rate.
  for(int rate:{30,60,144}) {
    auto game=fixture(0);auto &pilot=game.member(1);
    Vec3 landing=pilot.body.position;landing.y=0;
    auto index=building(game,landing,160);
    int hp=pilot.sheet.hp,ap=pilot.sheet.ap;
    for(int i=0;i<6*rate;++i)game.step(1.f/rate);
    assert(game.world.targets[index].health==80);
    assert(pilot.sheet.hp==hp&&pilot.sheet.ap==ap&&pilot.body.position.y==0);
    for(int i=0;i<rate;++i)game.step(1.f/rate);
    assert(game.world.targets[index].health==80);
    Session replica;replica.readSnapshot(game.snapshot());
    auto a=game.view(1),b=replica.view(1);
    assert(a.rpgInsertion==b.rpgInsertion&&a.rpgInsertionTime==b.rpgInsertionTime);
  }
  // A delayed volatile chain damages nearby equipment once, preserves protected
  // structures, and reconstructs its clocks/one-shot state for a late join.
  {
    auto game=fixture();game.member(1).body.position={0,0,60};
    auto first=building(game,{0,0,0},7,false,"oxygen_tank"),second=building(game,{8,0,0},7,false,"ship_reactor");
    auto wall=building(game,{-8,0,0},7,true);
    shoot(game,first,20);for(int n=0;n<18;++n)game.step(.1f);
    assert(game.world.targets[first].rpgSecondarySpent&&game.world.targets[second].rpgSecondarySpent);
    assert(game.world.targets[second].health==0&&game.world.targets[wall].health==7);
    Session replica;replica.readSnapshot(game.snapshot());assert(replica.snapshot()==game.snapshot());
    auto malformed=game.snapshot();assert(!malformed["world"]["rpg_damage"].empty());
    malformed["world"]["rpg_damage"].push_back(malformed["world"]["rpg_damage"][0]);bool rejected=false;
    try{Session bad;bad.readSnapshot(malformed);}catch(const std::exception &){rejected=true;}assert(rejected);
    game.phase=Phase::Defeat;for(int n=0;n<240;++n)game.step(.1f);assert(game.world.effects.empty());
  }
  // Underwater equipment produces splashes and extinguishes instead of causing
  // the same above-water secondary chain.
  {
    auto game=fixture();game.member(1).body.position={0,0,60};game.world.waterRegions={{{-25,-25,25,25},2,6,0}};
    game.world.submergedRoutes={{-100,-100,100,100}};
    auto first=building(game,{0,0,0},7,false,"oxygen_tank"),second=building(game,{8,0,0},7,false,"ship_reactor");
    shoot(game,first,20);assert(game.world.targets[first].health==0&&game.world.targets[first].rpgBurn==0);
    assert(std::any_of(game.world.effects.begin(),game.world.effects.end(),[](const auto &fx){return fx.model==rpgSplash&&fx.position.y>=2;}));
    for(int n=0;n<15;++n)game.step(.1f);assert(game.world.targets[second].health==7);
  }
  // Heat uses elapsed simulation time at a fixed damage cadence. Combustible
  // scenery burns down and exposed pilots take local heat damage.
  for(int rate:{30,60,144}){
    auto game=fixture();game.member(1).body.position={0,0,60};auto tree=building(game,{0,0,0},7,false,"tree");
    shoot(game,tree,4);assert(game.world.targets[tree].rpgBurn>0);auto &pilot=game.member(1);int hp=pilot.sheet.hp;
    pilot.body.position={0,0,4};for(int n=0;n<rate/2;++n)game.step(1.f/rate);assert(pilot.sheet.hp<hp);
    pilot.sheet.modules.push_back(Module::ExothermicPlating);hp=pilot.sheet.hp;for(int n=0;n<rate/2;++n)game.step(1.f/rate);assert(pilot.sheet.hp==hp);
    pilot.body.position={0,0,60};for(int n=0;n<3*rate;++n)game.step(1.f/rate);
    assert(game.world.targets[tree].health==0&&game.world.targets[tree].rpgDeathAt>=0);
    assert(game.world.effects.size()<=maxLiveEffects);
    for(const auto &fx:game.world.effects)if(fx.kind==WeaponEffect::Debris)assert(fx.position.y>=game.world.debrisFloor(fx)-.0001f);
  }
  // The full enemy roster has finite articulated death poses and a stable
  // settled corpse, without exceeding its intact triangle/atlas budget.
  {
    setOriginalAssetRoot(MECHA_ASSETS);
    for(const auto &spec:enemyCatalog())for(unsigned choice:{0u,1u}){Target target;applyOriginalModel(target,originalVariant(spec.id,choice));
      auto rig=enemyAnimationRig(std::filesystem::path(MECHA_ASSETS)/"models",target.generatorAsset,target.mesh->size());assert(rig);
      auto rest=poseEnemyDeath(*rig,0,0,0,1.6f,37),later=poseEnemyDeath(*rig,0,0,0,5,37);
      assert(rest.parts.size()==rig->parts.size()&&rest.parts.size()==later.parts.size());
      for(size_t p=0;p<rest.parts.size();++p){assert(valid(rest.parts[p].orientation)&&std::isfinite(length(rest.parts[p].offset)));
        assert(length(rest.parts[p].offset-later.parts[p].offset)<.00001f);}
      assert(target.wreck->size()<=target.mesh->size());
    }
  }
  std::cout<<"Destruction, volatile chains, underwater quenching, fixed-cadence heat, replicated timelines, grounded fragments and full-roster death poses passed\n";
}
