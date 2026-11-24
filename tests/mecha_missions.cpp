#include "rpg/network.hpp"
#include "rpg/snapshot.hpp"
#include "rpg/mission_board.hpp"
#include <chrono>
#include <thread>
#include "rpg/original_assets.hpp"
#include "rpg/enemy_effects.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
static Session deployed(int biome=3, int objective=0, float extent=1000) {
  Session game(723);
  game.add(1,"Pilot");
  GameSettings s;s.maps=1;
  s.environment=biome;s.objective=objective;s.location=0;
  s.enemy=3;s.enemyCount=1;s.opener=5;s.extent=extent;
  game.command(1,{{"op","settings"},{"settings",s}});
  game.command(1,{{"op","ready"},{"value",true}});
  game.command(1,{{"op","deploy"}});
  game.enemies[0].disabled=10000;
  return game;
}
static void testWeatherHazards(){
  struct Example {Weather weather;int biome,location;};
  constexpr Example cases[]={
    {Weather::IonStorm,3,0},{Weather::CorruptionSporeBurst,5,1},{Weather::FireStorm,4,2},
    {Weather::MeteorShower,0,3},{Weather::MagneticBurst,3,0},{Weather::AshCloud,4,2},
    {Weather::RadioactiveWind,3,2},{Weather::Snowstorm,6,0}
  };
  for(auto example:cases){
    auto game=deployed(example.biome,0,500);
    game.phase=Phase::Combat;game.mission.openerTime=100;
    auto &recipe=game.mission.recipe;
    recipe.manualMission=recipe.planetoidHazard=-1;recipe.location=example.location;
    game.mission.sites.clear();game.mission.assets.clear();game.mission.depots.clear();game.world.targets.resize(1);
    game.world.proceduralTerrain=true;game.world.editGeometry().terrain.clear();game.world.indexTerrain();
    game.world.interiorRegions.clear();game.world.interiorDecks.clear();game.world.waterRegions.clear();
    game.world.effects.clear();game.world.pickups.clear();
    auto &pilot=game.member(1);
    pilot.body.position={};pilot.sheet.hp=pilot.sheet.maxHp=100;
    pilot.sheet.ap=pilot.sheet.maxAp=0;pilot.sheet.reactorStress=0;
    auto &body=game.world.targets[0];
    body.origin={0,0,-90};game.world.updateEnemyBounds(body);
    game.environmentCycle.seconds=3.97;
    unsigned seed=0;
    while(seed<4096&&missionEnvironment(recipe,game.environmentCycle).settings.weather!=example.weather)
      game.environmentCycle.seed=++seed;
    assert(seed<4096&&missionWeatherAllowed(recipe,example.weather));
    const auto settings=missionEnvironment(recipe,game.environmentCycle).settings;
    assert(readEnvironment(environmentWire(settings)).weather==example.weather);
    assert(!weatherAllowed(Climate::Temperate,example.weather));
    const auto sprites=weatherSprites(settings,{0,12,0},5.f);
    assert(!sprites.empty()&&sprites.size()<=64);
    for(const auto &particle:sprites)
      assert(std::isfinite(length(particle.position))&&particle.opacity>=0&&particle.opacity<=1);
    const auto *hazard=weatherHazard(example.weather);
    if(example.weather==Weather::Snowstorm){
      pilot.body.position.y=38.f;
      game.world.interiorDecks.push_back({{-40,-40,40,40},{-40,38,0},{40,38,0}});
    }
    game.step(.05f);
    const auto fx=std::find_if(game.world.effects.begin(),game.world.effects.end(),[](const auto &f){
      return f.model==rpgWeatherWarning||f.model==rpgWeatherMeteor;
    });
    assert(fx!=game.world.effects.end()&&pilot.sheet.hp==100&&pilot.weatherJam==0);
    if(example.weather==Weather::Snowstorm)assert(std::abs(fx->position.y-38.f)<.05f);
    assert(game.view(1).rpgPilotStatus.find(hazard->label)!=std::string::npos);
    auto client=game;client.readSnapshot(game.snapshot());
    assert(client.world.environment.weather==example.weather);
    assert(client.world.effects.back().model==fx->model);
    if(hazard->radius>0)pilot.body.position=fx->position;
    auto sheltered=game;
    const auto p=pilot.body.position;
    sheltered.world.interiorRegions.push_back({{p.x-30,p.z-30,p.x+30,p.z+30},p.y-1.f,p.y+35.f});
    auto underBridge=game;
    if(example.weather==Weather::Snowstorm)
      underBridge.world.interiorDecks.push_back({{p.x-20,p.z-20,p.x+20,p.z+20},p+Vec3{-20,34,0},p+Vec3{20,34,0}});
    auto dodged=game;dodged.member(1).body.position={120,0,100};
    auto shielded=game;
    if(example.weather==Weather::RadioactiveWind){
      pilot.sheet.ap=pilot.sheet.maxAp=8;
      dodged.member(1).sheet.hp=1;
      // A shield-bearing NPC must absorb the radiation pulse and its heat together.
      const auto roster=enemyCatalog();
      const auto defended=std::find_if(roster.begin(),roster.end(),[](const auto &spec){return spec.shield>0;});
      assert(defended!=roster.end());
      shielded.enemies[0].spec=int(defended-roster.begin());
    }
    const int npcHp=game.enemies[0].hp;
    auto &protectedPilot=shielded.member(1);
    protectedPilot.shield=3;protectedPilot.shieldRemaining=20.f;
    int ticks=int(std::ceil((hazard->warning+.22f)/.05f));
    for(int tick=0;tick<ticks;++tick){
      game.step(.05f);sheltered.step(.05f);dodged.step(.05f);shielded.step(.05f);
      if(example.weather==Weather::Snowstorm)underBridge.step(.05f);
    }
    if(example.weather==Weather::MeteorShower)
      assert(protectedPilot.shield==3&&protectedPilot.sheet.hp==pilot.sheet.hp);
    else assert(protectedPilot.shield==2&&protectedPilot.sheet.hp==100&&protectedPilot.weatherJam==0);
    if(example.weather==Weather::Snowstorm){
      assert(underBridge.member(1).sheet.hp==100);
      assert(std::any_of(game.world.effects.begin(),game.world.effects.end(),[](const auto &f){return f.model==rpgWeatherHail;}));
    }
    const auto &safe=sheltered.member(1);
    assert(safe.sheet.hp==100&&safe.sheet.reactorStress==0&&safe.weatherJam==0&&safe.mobilityDisrupt==0);
    assert(sheltered.view(1).activeEnvironment().weather==Weather::Clear);
    if(hazard->radius>0){
      assert(pilot.sheet.hp<100&&dodged.member(1).sheet.hp==100);
      if(example.weather==Weather::FireStorm||example.weather==Weather::CorruptionSporeBurst){
        const int first=pilot.sheet.hp;
        for(int n=0;n<25;++n)game.step(.05f);
        assert(pilot.sheet.hp<first);
      }
    }else if(example.weather==Weather::IonStorm||example.weather==Weather::MagneticBurst){
      assert(pilot.weatherJam>0&&!game.view(1).sensorsWorking());
      if(example.weather==Weather::MagneticBurst)assert(pilot.mobilityDisrupt>0);
      else assert(pilot.sheet.reactorStress>.1f&&game.enemies[0].heat>.1f);
      auto state=game.snapshot();
      client.readSnapshot(state);
      assert(client.member(1).weatherJam==pilot.weatherJam&&!client.view(1).sensorsWorking());
    }else if(example.weather==Weather::AshCloud){
      assert(pilot.sheet.reactorStress>.03f);
      assert(environmentVisuals(settings,false,{},{},5.f).density>.01f);
    }else{
      assert(example.weather==Weather::RadioactiveWind);
      assert(pilot.sheet.hp==99&&pilot.sheet.ap==8&&pilot.sheet.reactorStress>0);
      assert(protectedPilot.sheet.reactorStress==0&&shielded.enemies[0].hp==npcHp&&shielded.enemies[0].heat==0);
      assert(game.enemies[0].hp==npcHp-1&&game.enemies[0].heat>0);
      assert(dodged.member(1).sheet.hp==0&&dodged.member(1).emergency==HullEmergency::LastWind);
    }
  }
  int ordinaryLowVisibility=0,missionLowVisibility=0,nightInsertions=0;
  for(uint32_t seed=0;seed<256;++seed){
    MissionRecipe recipe;recipe.seed=seed;recipe.environment=seed%2?0:3;recipe.location=2;
    EnvironmentCycle cycle;cycle.seed=seed;
    auto obscured=[](const EnvironmentSettings &settings){
      return environmentVisuals(settings,false,{},{},0).density>=.008f;
    };
    ordinaryLowVisibility+=obscured(cycle.sample(missionClimate(recipe.environment,recipe.location)).settings);
    missionLowVisibility+=obscured(missionEnvironment(recipe,cycle).settings);
    for(unsigned offer=0;offer<9;++offer)nightInsertions+=generateMission(seed,offer,GameSettings{},1).opener==1;
  }
  assert(missionLowVisibility>0&&missionLowVisibility<ordinaryLowVisibility*3/4);
  assert(nightInsertions>0&&nightInsertions<256*9/9);
  MissionRecipe vacuum;vacuum.environment=1;vacuum.location=9;
  assert(missionWeatherAllowed(vacuum,Weather::MeteorShower));
  assert(!missionWeatherAllowed(vacuum,Weather::CorruptionSporeBurst)&&!missionWeatherAllowed(vacuum,Weather::RadioactiveWind));
  MissionRecipe flooded;flooded.environment=2;flooded.location=8;
  assert(missionWeatherAllowed(flooded,Weather::MagneticBurst));
  assert(!missionWeatherAllowed(flooded,Weather::FireStorm));
}
static void testSalvageDepot(){
  Session game(723);
  for(unsigned i=1;i<=2;++i){
    game.add(i,"Pilot "+std::to_string(i));
    finishCreation(game.member(i).sheet);
  }
  GameSettings settings;
  settings.maps=1;settings.environment=3;settings.objective=0;settings.location=0;
  settings.enemy=3;settings.enemyCount=1;settings.opener=5;
  game.command(1,{{"op","settings"},{"settings",settings}});
  for(unsigned i=1;i<=2;++i)game.command(i,{{"op","ready"},{"value",true}});
  game.command(1,{{"op","deploy"}});
  game.mission.openerTime=100;
  game.enemies[0].disabled=10000;
  game.mission.recipe.salvageForm=2;
  game.mission.recipe.salvageFeature=5;
  const auto depot=game.mission.depots.at(0).target;
  assert(depot>=int(game.enemies.size())&&!game.view(1).rpgSalvageDepots.empty()&&game.supplies.empty());
  assert(std::find(game.mission.assets.begin(),game.mission.assets.end(),depot)==game.mission.assets.end());
  Session guest;guest.readSnapshot(game.snapshot());
  assert(guest.mission.depots.at(0).target==depot&&!guest.view(2).rpgSalvageDepots.empty());
  auto skipped=game;
  auto win=[](Session &s){
    s.enemies[0].hp=0;s.world.targets[0].health=0;s.world.targets[0].active=false;
    for(auto &m:s.members)m.body.position=s.world.missionEntry;
    for(int i=0;i<110&&s.phase==Phase::Combat;++i)s.step(.05f);
    assert(s.phase==Phase::Complete);
  };
  win(skipped);
  for(const auto &m:skipped.members)
    assert(m.sheet.inventory[size_t(Gear::Scrap)]==int(m.recoveredGear.size()));

  const auto &structure=game.world.targets[size_t(depot)];
  Shot strike;
  strike.body.position=structure.center-Vec3{0,0,structure.half.z+2};
  strike.body.velocity={0,0,1500};
  strike.body.damage=100;strike.body.shooter=1;
  game.ordnance.push_back(strike);
  game.step(.05f);
  assert(game.mission.depots[0].opened&&game.view(1).rpgSalvageDepots.empty());
  const auto crate=std::find_if(game.supplies.begin(),game.supplies.end(),[](const SupplyDrop &drop){return drop.kind==SafeBoxKind::Depot;});
  assert(crate!=game.supplies.end());
  const size_t pickup=crate->pickup;
  const auto cachePosition=game.world.pickups[pickup].position;
  auto &pilot=game.member(1);
  pilot.body.position=cachePosition;
  pilot.thrusters.altitude=cachePosition.y-game.world.surfaceHeight(cachePosition.x,cachePosition.z);
  game.step(.05f);
  assert(pilot.pendingDepotSalvage&&!game.member(2).pendingDepotSalvage&&
         pilot.sheet.inventory[size_t(Gear::Scrap)]==0&&game.world.pickups[pickup].collectedBy==1);
  guest.readSnapshot(game.snapshot());
  assert(guest.member(1).pendingDepotSalvage&&!guest.member(2).pendingDepotSalvage);
  auto defeated=game;
  for(auto &m:defeated.members)m.sheet.hp=0;
  defeated.step(.05f);
  assert(defeated.phase==Phase::Defeat&&!defeated.member(1).pendingDepotSalvage&&
         defeated.member(1).sheet.inventory[size_t(Gear::Scrap)]==0);
  win(game);
  assert(pilot.sheet.inventory[size_t(Gear::Scrap)]==8+int(pilot.recoveredGear.size())&&
         game.member(2).sheet.inventory[size_t(Gear::Scrap)]==int(game.member(2).recoveredGear.size())&&
         !pilot.pendingDepotSalvage);
}
// Both snapshot formats can occupy a lobby through a same-seed map replay.
static void testCompactSnapshotRecovery(){
  auto host=deployed();
  auto scenery=coop::captureScenery(host.world,host.enemies.size());
  auto initial=host.snapshot(&scenery);
  auto context=packRpgSnapshot(initial);
  // Lose a whole intervening frame. Dynamic state is always absolute.
  host.world.projectiles.resize(maxLiveProjectiles);
  for(auto& p:host.world.projectiles){p.position={1,2,3};p.velocity={4,5,6};p.visual=ProjectileVisual::LoftMissile;}
  --host.member(1).sheet.hp;
  auto next=host.snapshot(&scenery),packed=next;
  assert(packRpgSnapshot(packed)==context);
  assert(Json::to_cbor(packed).size()<Json::to_cbor(next).size());
  unpackRpgSnapshot(packed,context);
  assert(packed==next);
  ++host.member(1).sheet.credits;
  packed=host.snapshot(&scenery);
  auto updated=packRpgSnapshot(packed);
  assert(updated!=context);
  unpackRpgSnapshot(packed,updated);
  assert(packed==host.snapshot(&scenery));
}
static void testMixedSnapshotClients(){
  Server server("127.0.0.1",0,713);
  const auto endpoint="127.0.0.1:"+std::to_string(server.port());
  Client current(endpoint,"Current");
  coop::Transport legacy;
  auto *peer=legacy.connect(endpoint);
  Session oldReplica;
  auto pump=[&]{
    current.update(1.f/60);
    for(auto event:legacy.poll()){
      if(event.type==coop::Event::Connected)
        legacy.send(peer,{{"op","hello"},{"rpg",schema},{"name","Legacy"}});
      if(event.type==coop::Event::Message&&event.message.at("type")=="snapshot"){
        assert(!event.message.contains("scenery_revision"));
        oldReplica.readSnapshot(event.message.at("state"));
      }
    }
    server.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };
  for(int i=0;i<100&&server.session.members.size()!=2;++i)pump();
  assert(server.session.members.size()==2);
  for(int replay=0;replay<2;++replay){
    for(const auto &member:server.session.members)
      server.session.command(member.id,{{"op","ready"},{"value",true}});
    const auto epoch=server.session.epoch;
    auto previous=current.session.world.renderIdentity;
    for(int i=0;i<180&&(oldReplica.epoch!=epoch||current.session.epoch!=epoch);++i)pump();
    assert(oldReplica.epoch==epoch&&current.session.epoch==epoch);
    assert(previous!=current.session.world.renderIdentity);
    assert(oldReplica.environmentSeed==current.session.environmentSeed);
    assert(oldReplica.mission.recipe.seed==server.session.mission.recipe.seed);
    assert(coop::worldWire(oldReplica.world).at("targets")==
           coop::worldWire(current.session.world).at("targets"));
    server.session.command(server.session.host,{{"op","hangar"}});
  }
}
static void testExplorationSectors(){
  Session host(151);host.add(1,"Scout");host.add(2,"Wing");
  auto board=generateMissionBoard(host);
  auto offer=std::find_if(board.begin(),board.end(),[](const MissionRecipe &r){return r.depotCount>=2&&r.maps>1;});
  assert(offer!=board.end());
  host.command(1,missionSelectionCommand(int(offer-board.begin())));
  for(auto &m:host.members)host.command(m.id,{{"op","ready"},{"value",true}});
  host.command(1,{{"op","deploy"}});
  assert(host.mission.depots.size()>=2&&host.mission.recipe.siteCount>=2);
  assert(host.world.movementBounds[2]==offer->radius&&host.view(1).rpgSalvageDepots.size()==host.mission.depots.size());
  Session guest;guest.readSnapshot(host.snapshot());
  assert(guest.missionRadius()==host.missionRadius()&&guest.world.movementBounds==host.world.movementBounds);
  assert(guest.mission.depots.size()==host.mission.depots.size()&&guest.world.targets.size()==host.world.targets.size());
  host.mission.openerTime=100;
  for(const auto &depot:host.mission.depots){host.world.targets[depot.target].health=0;host.world.targets[depot.target].active=false;}
  host.step(.05f);
  std::vector<Vec3> caches;
  for(const auto &drop:host.supplies)if(drop.kind==SafeBoxKind::Depot)caches.push_back(host.world.pickups[drop.pickup].position);
  assert(caches.size()==host.mission.depots.size()&&host.view(1).rpgSalvageDepots.empty());
  for(auto p:caches){host.member(1).body.position=p;host.step(.05f);host.step(.05f);}
  assert(host.member(1).pendingDepotSalvage==int(caches.size())&&host.member(2).pendingDepotSalvage==0);
  guest.readSnapshot(host.snapshot());
  assert(guest.member(1).pendingDepotSalvage==int(caches.size()));
  host.mission.exitOpen=true;host.mission.transitTime=1.99f;
  for(auto &m:host.members)m.body.position={host.mission.exit[0],host.mission.exit[1],host.mission.exit[2]};
  host.step(.05f);
  assert(host.phase==Phase::Briefing&&host.mission.mapIndex==1);
  guest.readSnapshot(host.snapshot());
  assert(guest.member(1).pendingDepotSalvage==int(caches.size())&&guest.world.movementBounds==host.world.movementBounds);
}
int main() {
  testExplorationSectors();
  testCompactSnapshotRecovery();
  testMixedSnapshotClients();
  testSalvageDepot();
  testWeatherHazards();
  unsigned traversableWaterSamples=0;
  for(int biome=0;biome<8;++biome) {
    auto game=deployed(biome,0,biome%2?500:1000);
    auto &w=game.world;
    assert(w.rpgPrototype && w.environmentEnabled() && w.proceduralTerrain);
    assert(w.movementBounds[0]==-game.settings.extent &&
           w.movementBounds[2]==game.settings.extent);
    assert(!w.geometry().terrain.empty() && !w.targets.empty() && w.targets[0].mesh);
    assert(w.groundPassable(game.member(1).body.position,3.5f));
    for(unsigned front=0;front<40;++front) {
      EnvironmentCycle cycle;
      cycle.seed=game.mission.recipe.seed;
      cycle.seconds=double(front)*120;
      auto sample=missionEnvironment(game.mission.recipe,cycle);
      assert(missionWeatherAllowed(game.mission.recipe,sample.settings.weather));
      if(biome==1||biome==2||biome==7)
        assert(weatherHazard(sample.settings.weather)||sample.settings.weather==Weather::Clear);
      if(biome==6) assert(sample.settings.weather!=Weather::Rain && sample.settings.weather!=Weather::Storm);
    }
    game.environmentCycle.seconds=1000;
    game.step(.05f);
    auto original=game.snapshot();
    Session replica;
    replica.readSnapshot(original);
    assert(replica.snapshot()==original);
    auto a=game.view(1),b=replica.view(1);
    assert(environmentWire(a.activeEnvironment())==environmentWire(b.activeEnvironment()));
    assert(a.weatherTime()==b.weatherTime() && a.climate==b.climate && a.night==b.night);
    assert(a.environmentSeconds>=0 && a.mission==game.selectedMission);
    assert(a.rpgChassis==int(game.member(1).sheet.chassis));
    for (int slot=0;slot<MountCount;++slot) {
      auto gear=game.member(1).sheet.equipment[slot];
      assert(a.rpgModelNames[slot]==(gear==Gear::Empty||equipmentOwner(game.member(1).sheet,slot)!=slot?"":originalVariant(gearVisualId(gear),game.member(1).sheet.creationSeed^uint32_t(game.member(1).sheet.qualities[slot])^uint32_t(gear))));
    }
    if(!w.waterRegions.empty()) {
      for(const auto &water:w.waterRegions) {auto r=water.bounds;
      for(float x:{.2f,.35f,.5f,.65f,.8f})for(float z:{.2f,.35f,.5f,.65f,.8f}) {
        Vec3 p{r[0]+x*(r[2]-r[0]),0,r[1]+z*(r[3]-r[1])};
        p.y=w.surfaceHeight(p.x,p.z);
        if(w.waterDepthAt(p,3)>1&&w.waterDepthAt(p,3)<=w.waterTraversalLimitAt(p,3)&&w.groundPassable(p,3)) {
          assert(w.waterMovementScale(p,3)<1);++traversableWaterSamples;
        }
      }
      }
    }
  }
  assert(traversableWaterSamples>0);
  // Every native objective binds original models to navigable positions while
  // retaining the enemy prefix and the eight stable deployable slots.
  for(int objective=0;objective<10;++objective) {
    auto game=deployed(3,objective);
    assert(game.world.targets[0].ai.maxHealth==float(enemySpec(3).hd*8));
    for(int id:game.mission.assets) {
      assert(id>=int(game.enemies.size()) && id<int(game.world.targets.size())-8);
      auto &asset=game.world.targets[id];
      assert(asset.mesh && asset.wreck && game.world.groundPassable(asset.origin,3));
    }
  }
  // Native locomotion keeps RPG cooldowns; footsteps remain audible and do not
  // bypass RPG damage resolution through native crushing.
  {
    auto game=deployed();
    auto &pilot=game.member(1);
    pilot.body.cooldown[RightHand]=.8f;
    Input input;
    game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
    game.step(.05f);
    assert(pilot.body.cooldown[RightHand]>.7f);
    static_cast<Pilot&>(game.world)=pilot.body;
    float hp=game.world.targets[0].health;
    game.world.stomp(0);
    assert(game.world.targets[0].health==hp);
    assert(game.world.sounds.back().kind==SoundEvent::Footstep);
  }
  // Official d4 overheat vents normally; an exhausted/destroyed reactor uses
  // the engine's full forced ejection even after the fireteam has lost.
  {
    auto game=deployed();
    auto &pilot=game.member(1);
    pilot.sheet.reactor=4;
    pilot.sheet.overheated=true;pilot.sheet.ventRemaining=3;
    game.step(.1f);
    assert(!pilot.ejected && !pilot.body.lifeSpent);
    pilot.sheet.reactor=0;
    game.step(.1f);
    assert(!pilot.ejected);
    pilot.ejected=true;
    game.step(.1f);
    assert(pilot.ejected && pilot.body.lifeSpent && pilot.sheet.hp==0);
    assert(game.phase==Phase::Combat && game.view(1).missionStatus==MissionStatus::Ejecting);
    assert(pilot.body.ejectionTime>0);
    bool blocked=false;
    try {game.command(1,{{"op","hangar"}});} catch(...) {blocked=true;}
    assert(blocked);
    Session replica;
    replica.readSnapshot(game.snapshot());
    assert(replica.member(1).body.ejectionTime==pilot.body.ejectionTime);
    for(int i=0;i<80;++i)game.step(.1f);
    assert(game.phase==Phase::Defeat && pilot.body.ejectionTime==7 && game.view(1).missionStatus==MissionStatus::Failed);
    game.command(1,{{"op","hangar"}});
    assert(game.phase==Phase::Hangar && !pilot.body.lifeSpent && pilot.body.ejectionTime==0);
  }
  // Held rifle and Quick cadence remain stable across 10/30/60/120-Hz steps.
  for (bool quick : {false,true}) {
    unsigned expected = 0;
    for (int rate : {10,30,60,120}) {
      auto game=deployed();
      auto &pilot=game.member(1);
      pilot.sheet.equipment.fill(Gear::Empty);
      pilot.sheet.equipment[RightHand]=Gear::Rifle;
      pilot.sheet.qualities[RightHand]=quick?Quality::Quick:Quality::None;
      pilot.sheet.reactor=pilot.sheet.maxReactor=20;
      game.enemies[0].hp=game.enemies[0].maxHp=100000;
      game.world.targets[0].health=100000;
      Input input;
      auto step=[&] {
        game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
        game.step(1.f/rate);
      };
      step(); // Release the deployment fire interlock.
      input.fire[RightHand]=true;
      for(int i=0;i<4*rate;++i){step();pilot.sheet.coolant(1.f);}
      unsigned shots=pilot.body.shots[RightHand];
      if(!expected)expected=shots;
      assert(shots==expected && shots==(quick?18u:9u));
    }
  }
  // Victory keeps locomotion available but no input consumes combat gear or
  // activates chassis powers, including a held confirmation trigger.
  {
    auto game=deployed();
    auto &pilot=game.member(1);
    game.enemies[0].hp=0;game.world.targets[0].health=0;
    game.step(.05f);
    if(game.mission.recoveryOpen){
      pilot.body.position=game.world.missionEntry;
      for(int frame=0;frame<52&&game.phase==Phase::Combat;++frame)game.step(.1f);
    }
    assert(game.phase==Phase::Complete);
    pilot.sheet.chassis=Chassis::Scout;
    pilot.sheet.equipment[RightShoulder]=Gear::FragGrenade;
    pilot.sheet.inventory[size_t(Gear::FragGrenade)]=2;
    Input input;
    auto step=[&] {
      game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)},
                      {"systems",input.ping}});
      game.step(.05f);
    };
    step();
    auto position=pilot.body.position;
    input.forward=-1;input.fire[RightShoulder]=true;input.ping[1]=true;
    for(int i=0;i<20;++i)step();
    assert(pilot.sheet.inventory[size_t(Gear::FragGrenade)]==2 && !pilot.sheet.hidden);
    assert(game.ordnance.empty() && length(pilot.body.position-position)>4);
  }
  // An ejection cue emitted between 20-Hz snapshots is delivered once. The
  // next snapshot carries no stale event, even though the cinematic continues.
  {
    Server server("127.0.0.1",0,723);
    Client client("127.0.0.1:"+std::to_string(server.port()),"Pilot");
    for(int i=0;i<60;++i) {
      client.update(1.f/60);server.step();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(client.id==1);
    server.session=deployed();
    auto &pilot=server.session.member(1);
    pilot.ejected=true;pilot.sheet.hp=0;pilot.body.health=0;
    pilot.body.lifeSpent=true;pilot.body.ejectionTime=.99f;
    pilot.body.missionStatus=MissionStatus::Ejecting;
    server.session.phase=Phase::Defeat;
    for(int i=0;i<3;++i)server.step();
    for(int i=0;i<30 && client.session.epoch!=server.session.epoch;++i) {
      client.update(.001f);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(client.session.epoch==server.session.epoch);
    assert(client.session.world.sounds.size()==1 &&
           client.session.world.sounds[0].kind==SoundEvent::Explosion);
    client.session.world.sounds.clear(); // Audio consumer has played the cue.
    float previous=client.session.world.elapsed;
    for(int i=0;i<3;++i)server.step();
    // Later state packets use the unreliable channel. Keep both ends servicing
    // ENet until a fresh state arrives, as the normal host and client do.
    for(int i=0;i<60 && client.session.world.elapsed==previous;++i) {
      client.update(.001f);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(client.session.world.elapsed>previous && client.session.world.sounds.empty());
  }
  // The sky palette belongs to its setting: sunlight still changes model and
  // ground lighting, but space, water and interiors never gain a blue sky.
  for (int biome : {1,2,7}) {
    auto game=deployed(biome);
    for (auto lighting : {Lighting::Day,Lighting::Night}) {
      auto settings=game.world.activeEnvironment();
      settings.lighting=lighting;
      assert(settings.authoredSky&&missionWeatherAllowed(game.mission.recipe,settings.weather));
      assert(environmentWire(readEnvironment(environmentWire(settings)))==environmentWire(settings));
      auto visual=environmentVisuals(settings,false,game.world.skyColor,game.world.groundColor,0);
      assert(length(visual.sky-game.world.skyColor)==0);
      assert(visual.night==(lighting==Lighting::Night));
      assert(visual.modelGain==(lighting==Lighting::Night?.5f:1.f));
    }
  }
  {
    World interior;
    std::array<int,1> ids{3};
    buildMissionEnvironment(interior,723,3,500,ids,7,0);
    MissionRecipe recipe;recipe.environment=3;recipe.location=7;
    auto sample=missionEnvironment(recipe,EnvironmentCycle{});
    interior.environment=sample.settings;
    auto protectedSettings=interior.environmentAt({0,0,-100});
    auto visual=environmentVisuals(protectedSettings,false,interior.skyColor,interior.groundColor,0);
    assert(protectedSettings.authoredSky && protectedSettings.weather==Weather::Clear);
    assert(length(visual.sky-interior.skyColor)==0 && visual.sky.z<.03f);
  }
  // Fill the native effect budget with live damage volumes. Cosmetic pressure
  // and another destruction must neither overflow nor mutate retained volumes.
  {
    World world;
    for(size_t i=0;i<maxLiveEffects;++i) {
      assert(world.effect(WeaponEffect::Blast,{float(i),0,0},{},Weapon::Rifle));
      world.effects.back().damageRadius=5;
      world.effects.back().damageRate=float(i+1);
    }
    assert(!world.effect(WeaponEffect::Impact,{},{},Weapon::Rifle));
    world.destruction({},10,Weapon::Rocket,8,500);
    assert(world.effects.size()==maxLiveEffects);
    for(size_t i=0;i<world.effects.size();++i)
      assert(world.effects[i].damageRate==float(i+1) && world.effects[i].age==0);
    // A cosmetic slot may be reclaimed without evicting an active volume.
    world.effects[0].damageRate=0;
    assert(world.effect(WeaponEffect::Sparks,{},{},Weapon::Rifle));
    assert(world.effects.size()==maxLiveEffects && world.effects.front().damageRate==2);
  }
  // Player special weapons and NPC launch paths both obey the same projectile
  // cap. Rejected grenade/barrage/beam launches never retag the prior shot.
  for(auto gear:{Gear::FragGrenade,Gear::ChestBeam,Gear::MissileBarrage}) {
    auto game=deployed();
    auto &pilot=game.member(1);
    pilot.sheet.equipment[RightShoulder]=gear;
    pilot.sheet.inventory.fill(10);
    for(size_t i=0;i<maxLiveProjectiles;++i) {
      Shot shot;shot.body.position={700,100,700};shot.body.velocity={};
      shot.body.life=100;shot.body.damage=1;
      game.ordnance.push_back(shot);
    }
    Input input;
    auto step=[&] {
      game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
      game.step(.05f);
    };
    step();input.fire[RightShoulder]=true;step();
    assert(game.ordnance.size()==maxLiveProjectiles && game.world.projectiles.size()==maxLiveProjectiles);
    for(const auto &shot:game.ordnance)
      assert(shot.body.kind==ProjectileKind::Bullet && shot.body.radius==0 && shot.effect==AttackEffect::Damage);
    game.enemies[0].disabled=0;game.enemies[0].fireClock=0;
    step();
    assert(game.ordnance.size()==maxLiveProjectiles && game.world.projectiles.size()==maxLiveProjectiles);
  }
  // A full volley can produce 256 simultaneous contact effects; a second full
  // volley reclaims cosmetic slots. Full live damage volumes reject new FX.
  {
    auto game=deployed();
    game.enemies[0].hp=game.enemies[0].maxHp=100000;
    game.world.targets[0].health=100000;
    auto volley=[&] {
      for(size_t i=0;i<maxLiveProjectiles;++i) {
        Shot shot;shot.body.position=game.world.targets[0].center+Vec3{0,0,20};
        shot.body.velocity={0,0,-1000};shot.body.shooter=1;shot.body.damage=1;
        game.ordnance.push_back(shot);
      }
      game.step(.05f);
      assert(game.ordnance.empty() && game.world.effects.size()<=maxLiveEffects);
    };
    volley();assert(game.world.effects.size()==maxLiveEffects);
    volley();assert(game.world.effects.size()==maxLiveEffects);
    for(auto &fx:game.world.effects){fx.damageRate=100;fx.damageRadius=4;fx.age=0;}
    volley();assert(game.world.effects.size()==maxLiveEffects);
    for(const auto &fx:game.world.effects)assert(fx.damageRate==100);
    auto oversized=game.snapshot();
    oversized["world"]["effects"].push_back(oversized["world"]["effects"][0]);
    Session replica;bool rejected=false;
    try{replica.readSnapshot(oversized);}catch(...){rejected=true;}
    assert(rejected);
  }
  // Generated arrivals respect their physical setting. Explicit host choices
  // remain usable for deliberately unusual scenarios.
  for (int biome=0;biome<8;++biome) {
    for (int location : {0,6,7,8,9}) {
      GameSettings settings;settings.maps=1;settings.environment=biome;settings.location=location;
      for (unsigned seed=0;seed<64;++seed) {
        auto recipe=generateMission(seed,seed%9,settings,3);
        assert(Json(recipe)==Json(generateMission(seed,seed%9,settings,3)));
        if (location==7) assert(recipe.opener!=0 && recipe.opener!=4);
        if (biome==2) assert(recipe.opener==2 || recipe.opener==4);
        if (biome==1 || biome==7 || location==6 || location==9)
          assert(recipe.opener!=4);
      }
      settings.opener=4;
      assert(generateMission(42,0,settings,3).opener==4);
    }
  }
  std::cout<<"RPG physical maps, native climate cycles, objective assets, cooldowns and forced ejection passed\n";
}
