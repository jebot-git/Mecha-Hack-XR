#include "rpg/manual_enemy_ai.hpp"
#include "rpg/original_assets.hpp"
#include <iostream>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int index(std::string_view id){for(size_t n=0;n<enemyCatalog().size();++n)if(enemySpec(n).id==id)return int(n);throw std::runtime_error("Missing fixture enemy");}
Session deployed(int enemy,int objective=1,int count=1,int environment=3){
  Session game(901);game.add(1,"Manual AI");GameSettings settings;settings.maps=1;
  settings.environment=environment;settings.location=2;settings.objective=objective;settings.opener=5;settings.enemy=enemy;settings.enemyCount=count;
  game.command(1,{{"op","settings"},{"settings",settings}});game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
  return game;
}
}
int main(){try{
 setOriginalAssetRoot(MECHA_ASSETS);
 require(manualEnemySpeed(enemySpec(index("bomber")),3)==2,"Starship lost double movement");
 require(manualEnemySpeed(enemySpec(index("cyclone_drone")),2)==2&&manualEnemySpeed(enemySpec(index("cyclone_drone")),3)==1,"Submersible bonus escaped underwater restriction");
 require(manualEnemySpread(enemySpec(index("interceptor")),60)>manualEnemySpread(enemySpec(index("interceptor")),20),"Far starship accuracy no longer differs");
 require(manualEnemyArmorDamage(enemySpec(index("kaiju_dismantler")),7)==14,"Dismantler must drain double armor");
 {
  Session pair;pair.enemies.resize(2);pair.world.targets.resize(2);pair.enemies[0].spec=index("aegis_mecha");pair.enemies[1].spec=index("gladius_mecha");
  pair.world.targets[0].origin={0,0,0};pair.world.targets[1].origin={50,0,0};
  require(manualEnemyExtraDice(pair,0)==1&&manualEnemyExtraDice(pair,1)==1,"Paired mecha must both gain damage dice");
  pair.world.targets[1].origin.x=100;require(manualEnemyExtraDice(pair,0)==0,"Pair bonus exceeds far range");
  pair.enemies[0].spec=index("aeon_diode");pair.world.targets[1].origin.x=50;
  require(manualEnemyDamageBonus(pair,0)==2,"Hive mind lost allied proximity damage");
  pair.enemies[1].spec=index("kaiju_spawnling");
  require(manualEnemyDamageBonus(pair,0)==0,"Hostile kaiju granted an Aeon hive-mind bonus");
  pair.enemies[1].spec=index("gladius_mecha");pair.enemies[1].hp=0;
  require(manualEnemyDamageBonus(pair,0)==0,"Dead ally still supplies hive mind");
 }
 {
  auto game=deployed(index("combiner_mecha"),1,2);int maximum=game.enemies[0].maxHp;
  game.world.targets[1].origin=game.world.targets[0].origin+Vec3{5,0,0};game.world.updateEnemyBounds(game.world.targets[1]);game.step(.01f);
  require(game.enemies[0].combined&&game.enemies[0].maxHp==maximum*2&&game.enemies[1].hp==0&&!game.world.targets[1].active,"Close combiners did not merge into one double-HP actor");
  require(manualEnemyExtraDice(game,0)==1,"Combined actor lost additional damage die");
 }
 {
  auto game=deployed(index("veriform_mecha"));game.member(1).body.position=game.world.targets[0].origin+Vec3{0,0,100};game.step(.01f);
  require(game.enemies[0].transformed,"Veriform did not switch to interceptor at range");
  auto &airframe=game.world.targets[0];
  require(airframe.ai.airborne,"Flight model was classified as ground traffic");
  const auto floor=game.world.surfaceHeight(airframe.origin.x,airframe.origin.z);
  airframe.origin.y=floor+fighterCruiseHeight;
  game.world.updateEnemyBounds(airframe);
  require(airframe.origin.y>floor+30,"Flight bounds snapped the fighter to the ground");
  Session replica;replica.readSnapshot(game.snapshot());require(replica.world.targets[0].generatorAsset==game.world.targets[0].generatorAsset,"Transformed model did not replicate");
  game.member(1).body.position=game.world.targets[0].origin+Vec3{0,0,12};game.step(.01f);
  require(!game.enemies[0].transformed,"Veriform did not return to mecha for close engagement");
  replica.readSnapshot(game.snapshot());require(replica.world.targets[0].generatorAsset==game.world.targets[0].generatorAsset,"Same-count snapshot retained stale transformed model");
 }
 // Start on a clear outdoor pad and climb under the ordinary mission AI.
 for(const char *id:{"veriform_mecha","interceptor"}){
  auto game=deployed(index(id));
  game.world.proceduralTerrain=false;game.world.editGeometry().terrain.clear();game.world.indexTerrain();
  game.world.interiorRegions.clear();game.world.waterRegions.clear();
  for(size_t i=1;i<game.world.targets.size();++i)game.world.targets[i].active=false;
  auto &pilot=game.member(1);pilot.body.position={0,0,100};pilot.sheet.hp=pilot.sheet.maxHp=10000;
  auto &body=game.world.targets[0];body.origin={0,0,0};game.world.updateEnemyBounds(body);
  for(int n=0;n<240;++n)game.step(1.f/60);
  const float clearance=body.origin.y-game.world.walkableHeight(body.origin);
  if(clearance<30)throw std::runtime_error(std::string(id)+" outdoor cruise clearance "+std::to_string(clearance));
 }
 // Conventional submersibles must actually traverse the deep facility,
 // using the ordinary swept wall collision path rather than six-metre wading.
 for(const char *id:{"cyclone_drone","deepdiver_submarine"}){
  auto game=deployed(index(id),0,1,2);require(game.world.waterDepthAt(game.world.targets[0].origin)>12,"Submersible deployed outside the flooded mission section");auto &body=game.world.targets[0];auto &pilot=game.member(1);
  pilot.sheet.hp=pilot.sheet.maxHp=1000000;game.enemies[0].fireClock=1000;game.enemies[0].tactic=1;game.enemies[0].decisionClock=1000;
  std::vector<Vec3> sites{{-.48f*game.settings.extent,0,-.18f*game.settings.extent}};
  sites.insert(sites.end(),game.world.interiorNavNodes.begin(),game.world.interiorNavNodes.end());bool staged=false;
  for(auto p:sites){p.y=game.world.surfaceHeight(p.x,p.z);
   if(game.world.waterDepthAt(p,8)<12||!clearPosition(game.world,p,8,0))continue;
   for(int angle=0;angle<16&&!staged;++angle){float turn=2*pi*float(angle)/16;auto goal=p+Vec3{std::sin(turn)*20,0,std::cos(turn)*20};goal.y=game.world.surfaceHeight(goal.x,goal.z);
    if(game.world.waterDepthAt(goal,3.5f)<12||!clearPosition(game.world,goal,3.5f,0))continue;
    bool clear=true;for(int n=1;n<=10&&clear;++n){auto sample=p+(goal-p)*(float(n)/10);sample.y=game.world.surfaceHeight(sample.x,sample.z);clear=clearPosition(game.world,sample,8,0);}
    if(!clear)continue;
    body.origin=p+Vec3{0,2,0};game.world.updateEnemyBounds(body);pilot.body.position=goal;staged=true;
   }
   if(staged)break;
  }
  require(staged,"No clear generated deep-water movement fixture");auto start=body.origin;
  for(int tick=0;tick<120;++tick){auto previous=body.origin;game.step(1.f/60);
   require(length(body.origin-previous)<.4f,"Submersible teleported during two-second movement");
   float floor=game.world.surfaceHeight(body.origin.x,body.origin.z),ceiling=game.world.interiorCeilingAt(body.origin);
   require(body.origin.y>=floor-.001f&&body.origin.y-floor<2.1f&&body.center.y+body.half.y<ceiling,"Submersible escaped underwater floor/headroom envelope");
   require(game.world.waterDepthAt(body.origin)>12,"Submersible left designated deep-water fixture");
  }
  require(std::hypot(body.origin.x-start.x,body.origin.z-start.z)>1,"Conventional submersible remains blocked by shallow-water wading limit");
  bool wallChecked=false;
  for(const auto &wall:game.world.targets)if(wall.active&&wall.invulnerable&&wall.half.y>10&&(wall.half.x<3||wall.half.z<3)){
   World probe=game.world;auto &actor=probe.targets[0];bool x=wall.half.x<wall.half.z;Vec3 normal=x?Vec3{1,0,0}:Vec3{0,0,1};
   float padding=(x?wall.half.x+actor.half.x*.65f:wall.half.z+actor.half.z*.65f)+2;
   actor.origin=wall.center+normal*padding;actor.origin.y=probe.surfaceHeight(actor.origin.x,actor.origin.z)+2;probe.updateEnemyBounds(actor);auto origin=actor.origin;
   if(actor.center.y+actor.half.y<=wall.center.y-wall.half.y||actor.center.y-actor.half.y>=wall.center.y+wall.half.y)continue;
   require(!probe.moveEnemy(0,normal*(-2*padding),1,true)&&length(actor.origin-origin)<.001f,"Buoyant collision bypassed a real generated facility wall");wallChecked=true;break;
  }
  require(wallChecked,"Underwater collision fixture did not exercise a structural wall");
 }
 for(const char *id:{"carrier","kaiju_broodqueen","dreadnought"}){
  auto game=deployed(index(id));const auto before=game.world.targets.size();const auto asset=game.mission.assets.at(0);
  auto assetId=game.world.targets[size_t(asset)].generatorAsset;
  const auto base=coop::captureScenery(game.world,game.enemies.size());
  Session observer;
  observer.readSnapshot(game.snapshot(&base),&base.rows);
  const auto map=observer.world.renderIdentity;
  const auto staticMesh=observer.world.targets[asset].mesh;
  auto view=observer.view(1);
  require(&view.geometry()==&observer.world.geometry(),"View copied the static map");
  view.editGeometry().terrain.clear();
  require(!observer.world.geometry().terrain.empty(),"Presentation edit modified the simulation map");
  // Drop the snapshots between initial replication and the reinforced state.
  const float originalHealth=game.world.targets[asset].health;
  game.world.targets[asset].health-=1;
  std::array<std::string,8> tail;for(size_t n=0;n<8;++n)tail[n]=game.world.targets[before-8+n].generatorAsset;
  for(int attempt=0;attempt<24&&game.enemies[0].spawned==0;++attempt){game.enemies[0].manualClock=0;game.step(.01f);}
  require(game.enemies[0].spawned>0&&game.enemies.size()>1,"Manual carrier did not create actual combat actors");
  require(game.enemies.size()<=maxEncounterEnemies&&game.world.targets.size()<=maxEncounterTargets,"Reinforcements exceed actor/scene cap");
  require(game.mission.assets[0]>asset&&game.world.targets[size_t(game.mission.assets[0])].generatorAsset==assetId,"Reinforcement insertion corrupted protected mission asset index");
  for(size_t n=0;n<8;++n)require(game.world.targets[game.world.targets.size()-8+n].generatorAsset==tail[n],"Reinforcement insertion moved reserved deployable tail");
  for(size_t n=0;n<game.enemies.size();++n)require(game.world.targets[n].spawnType==3&&game.world.targets[n].mesh,"Enemy prefix contains scenery or missing mesh");
  auto sparse=game.snapshot(&base);
  observer.readSnapshot(sparse,&base.rows);
  require(observer.world.renderIdentity==map&&observer.world.targets[game.mission.assets[0]].mesh==staticMesh,
          "Growing the actor prefix rebuilt static scenery");
  require(observer.snapshot()==game.snapshot(),"Dropped snapshots lost replicated actor or scenery state");
  Session sparseJoin;sparseJoin.readSnapshot(sparse,&base.rows);
  require(sparseJoin.snapshot()==observer.snapshot(),"Late replica missed a scenery or actor update");
  game.world.targets[game.mission.assets[0]].health=originalHealth;
  observer.readSnapshot(game.snapshot(&base),&base.rows);
  require(observer.world.targets[game.mission.assets[0]].health==originalHealth,
          "A scenery row that returned to baseline stayed damaged");
  Session replica;replica.readSnapshot(game.snapshot());
  require(replica.world.targets.size()==game.world.targets.size(),"Spawn snapshot rebuilt a different scene count");
  for(size_t n=0;n<game.world.targets.size();++n)require(replica.world.targets[n].generatorAsset==game.world.targets[n].generatorAsset,"Spawn snapshot regenerated different model identities");
  require(replica.enemies[0].spawned==game.enemies[0].spawned,"Carrier spawn counter did not replicate");
  for(int pulse=0;pulse<12;++pulse){game.enemies[0].manualClock=0;game.step(.001f);}
  require(game.enemies.size()<=maxEncounterEnemies&&game.world.targets.size()<=maxEncounterTargets,"Repeated carrier waves escaped bounded storage");
 }
 std::cout<<"Manual enemy movement, coordination, source deployment waves, bounded indices and spawned scene replication verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
