#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/kaiju_audio.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool pass,const std::string &message){if(!pass)throw std::runtime_error(message);}
int enemyIndex(std::string_view id){for(size_t n=0;n<enemyCatalog().size();++n)if(enemySpec(n).id==id)return int(n);throw std::runtime_error("Unknown combat fixture enemy");}
// A flat deterministic arena retains real model bounds, Session::step,
// cooldowns, physical projectiles and multiplayer pilot collision. No renderer.
Session arena(uint32_t seed=713,int actors=3,int pilots=4,bool networked=false){
 Session game(seed,networked);for(int n=1;n<=pilots;++n)game.add(uint32_t(n),"Evaluation "+std::to_string(n));
 game.phase=Phase::Combat;game.mission.openerTime=100;game.mission.recipe.opener=5;
 game.mission.recipe.objective=0;game.mission.recipe.environment=3;game.biome=3;
 game.settings.extent=500;game.world.movementBounds={-500,-500,500,500};
 game.world.targets.clear();game.world.proceduralTerrain=false;game.world.editGeometry().terrain.clear();game.world.indexTerrain();
 game.world.interiorRegions.clear();game.world.waterRegions.clear();game.world.interiorNavNodes.clear();
 const int sniper=enemyIndex("mecha_sniper");
 for(int n=0;n<actors;++n){Enemy enemy;enemy.spec=sniper;enemy.hp=enemy.maxHp=800;enemy.tactic=8;enemy.fireClock=0;
  game.enemies.push_back(enemy);game.mission.recipe.enemies.push_back(sniper);
  Target target;target.origin={float(n-(actors-1)*.5f)*18,0,0};target.spawnType=3;target.active=true;
  applyOriginalModel(target,originalVariant(enemyModelId(sniper),uint32_t(n)*0x9e3779b9u));target.health=800;target.ai.maxHealth=800;
  game.world.updateEnemyBounds(target);game.world.targets.push_back(std::move(target));
 }
 for(auto &pilot:game.members){pilot.sheet.hp=pilot.sheet.maxHp=10000;pilot.sheet.ap=0;pilot.sheet.reactor=pilot.sheet.maxReactor=12;
  pilot.sheet.attributes.fill(8);pilot.body.position={float(pilot.id-1)*12,0,55};pilot.body.health=100;pilot.input.focused=false;}
 return game;
}
void readyShots(Session &game){game.ordnance.clear();for(auto &enemy:game.enemies)enemy.fireClock=0;}
void onlyPilot(Session &game,uint32_t chosen){for(auto &pilot:game.members){pilot.sheet.hidden=pilot.id!=chosen;pilot.hiddenRemaining=pilot.sheet.hidden?100.f:0.f;}}
size_t aimedShots(const Session &game,uint32_t member){size_t result=0;const auto pilot=std::find_if(game.members.begin(),game.members.end(),[&](const Member &candidate){return candidate.id==member;});require(pilot!=game.members.end(),"Missing evaluation pilot");const auto aim=pilot->body.position+Vec3{0,6,0};
 for(const auto &shot:game.ordnance)if(shot.body.owner>=0){
  const auto ray=aim-shot.body.position;require(dot(unit(shot.body.velocity),unit(ray))>.98f,"Emitted projectile ignored the eligible selected pilot");++result;
 }return result;
}
Target wall(Vec3 center,Vec3 half){Target target;target.center=center;target.origin=center;target.half=half;target.health=1000000;target.active=true;target.invulnerable=true;target.spawnType=22;return target;}
void placeActor(Session &game,size_t i,const char *id,Vec3 position,int hp=800){
  auto &enemy=game.enemies[i];enemy.spec=enemyIndex(id);enemy.hp=enemy.maxHp=hp;
  game.mission.recipe.enemies[i]=enemy.spec;
  auto &body=game.world.targets[i];body.origin=position;body.health=float(hp);
  game.world.updateEnemyBounds(body);
}
void detachment(Session &s,FactionRole role,std::initializer_list<int> actors){
  EncounterGroup g;g.factionRole=role;g.actors=actors;
  g.faction=actorFaction(s,size_t(g.actors.front()));
  for(int index:actors)s.enemies[size_t(index)].group=int(s.mission.groups.size());
  const auto p=s.world.targets[size_t(g.actors.front())].origin;
  g.route.push_back({p.x,p.y,p.z});s.mission.groups.push_back(std::move(g));
}
void hitFromPilot(Session &s,size_t actor,int damage){
  auto p=s.world.targets[actor].center;
  Shot shot;shot.body.position=p+Vec3{0,0,-9};shot.body.velocity={0,0,460};
  shot.body.shooter=1;shot.body.mount=-1;shot.body.damage=float(damage);shot.quality=Quality::Plasma;
  s.ordnance.push_back(shot);s.step(.045f);
}

}
int main(){try{
 setOriginalAssetRoot(MECHA_ASSETS);
 size_t checkedShots=0;
 // Contact produces the speaker's faction code. Another actor on the
 // same tick cannot overlap it, but a Voidmaw vocal is an independent cue.
 for(int faction=0;faction<npcRadioFactions;++faction){
  auto game=arena(713,3,1);
  size_t id=0;
  while(id<enemyCatalog().size()&&enemyFaction(id)!=faction)++id;
  require(id<enemyCatalog().size(),"Missing radio test faction");
  placeActor(game,0,enemySpec(id).id,{-18,0,0});
  placeActor(game,1,enemySpec(id).id,{0,0,0});
  placeActor(game,2,"kaiju_spawnling",{18,0,0});
  game.step(.025f);
  int chatter=0,creature=0;
  for(const auto &event:game.world.sounds)if(event.kind==SoundEvent::Special){
    if(isNpcRadio(event.weapon)){
      ++chatter;
      require((event.weapon-npcRadioBase)%npcRadioBankSize/(int(RadioLine::Count)*npcRadioBankVariants)==faction,"Intercept used the wrong faction language");
    }
    if(event.weapon==int(CombatCue::KaijuShriek))++creature;
  }
  require(chatter==1&&creature==1,"Contact failed to emit one intercept and one kaiju call");
 }

 // Keep each actor's radio performer consistent and rotate both takes.
 {
  RadioRotation rotation;
  for(unsigned crew:{0u,1u}){
    int first=rotation.next(crew,931),second=rotation.next(crew,931);
    require(first!=second && first%2==int(crew) && second%2==int(crew),"Radio switched performer or immediately repeated a take");
    require(rotation.next(crew,53)==first,"Radio lost a take during rotation");
  }
  for(int f=0;f<npcRadioFactions;++f)for(int l=0;l<int(RadioLine::Count);++l)for(int take=0;take<2;++take){
    int legacy=npcRadioBase+(f*int(RadioLine::Count)+l)*2+take;
    require(radioCue(f,RadioLine(l),take)==legacy && radioCue(f,RadioLine(l),take+2)==legacy+npcRadioBankSize,
            "Expanded radio bank changed a legacy language or intent ID");
  }
 }
 // Force the same physical firing opportunity every tick. All shooters of a
 // faction share a reporting interval; individual weapon cooldowns still tick.
 {
  auto game=arena(713,2,1,true);
  game.step(.025f);
  float previous=-1000;
  int reports=0,lastCue=-1,lastActor=-1;
  for(int tick=0;tick<2000;++tick){
    readyShots(game);game.member(1).sheet.hp=10000;
    for(size_t i=0;i<game.enemies.size();++i){
      game.world.targets[i].origin={float(i*14)-7,0,0};
      game.world.updateEnemyBounds(game.world.targets[i]);
      game.enemies[i].heat=0;game.enemies[i].overheated=false;
    }
    game.step(.08f);
    for(const auto &event:game.world.sounds){
      if(event.kind!=SoundEvent::Special||!isNpcRadio(event.weapon))continue;
      const int line=(event.weapon-npcRadioBase)%npcRadioBankSize/npcRadioBankVariants%int(RadioLine::Count);
      if(line!=int(RadioLine::EnemyAttack))continue;
      require(event.transmitterActor>=0 && game.world.elapsed-previous>=65.f,"Repeated firing orders crowded the radio");
      require(lastActor!=event.transmitterActor||lastCue!=event.weapon,"Repeated the same actor's last attack take");
      lastActor=event.transmitterActor;lastCue=event.weapon;
      ++reports;previous=game.world.elapsed;
    }
  }
  require(reports>=2&&reports<=3,"Continuous engagement did not produce sparse, recurring attack reports");
 }

 // Detachments use real combat projectiles without classifying player
 // bodies as targets for a faction that has not been provoked.
 {
  auto game=arena(797,3,1);
  placeActor(game,0,"mecha_recruit",{0,0,0});
  placeActor(game,1,"labor_bot",{0,0,24});
  placeActor(game,2,"mecha_recruit",{-45,0,0});
  game.member(1).body.position={30,0,65};
  detachment(game,FactionRole::Neutral,{0});
  detachment(game,FactionRole::Neutral,{2});
  auto voidmaw=game;
  placeActor(voidmaw,1,"kaiju_spawnling",{0,0,24});
  require(factionsAtWar(voidmaw,0,1)&&factionsAtWar(voidmaw,1,2)&&factionAttacksPlayers(voidmaw,1),
          "Voidmaw treated a neutral faction or pilot as friendly");
  require(!factionAttacksPlayers(game,0)&&!factionsAtWar(game,0,2),
          "Unprovoked neutral group regards pilots or its own faction as hostile");
  for(int tick=0;tick<150&&game.enemies[1].hp==800;++tick)game.step(.04f);
  require(game.enemies[1].hp<800,"Neutral faction did not damage the opposing NPC in combat");
  hitFromPilot(game,0,4);
  require(factionAttacksPlayers(game,0)&&factionAttacksPlayers(game,2),
          "Attack on one neutral group did not change its whole faction");
  require(game.campaign.standings[2]==0,"A neutral incident modified standings during combat");
  auto copy=game;copy.readSnapshot(game.snapshot());
  require(factionAttacksPlayers(copy,2),"Snapshot lost mission-long neutral hostility");
  game.mission.mapCount=2;game.mission.exitOpen=true;
  const auto at=game.member(1).body.position;
  game.mission.exit={at.x,at.y,at.z};
  for(int tick=0;tick<25&&game.mission.mapIndex==0;++tick)game.step(.1f);
  require(game.mission.mapIndex==1&&(game.mission.offendedNeutral&(1u<<2))&&game.campaign.standings[2]==0,
          "Sector transit cleared hostility or applied standings early");
 }
 {
  auto game=arena(799,3,1);
  placeActor(game,0,"mecha_recruit",{0,0,0},15);
  placeActor(game,1,"mecha_recruit",{14,0,0});
  placeActor(game,2,"labor_bot",{14,0,25});
  game.member(1).body.position={0,0,42};
  game.mission.recipe.adversary=1;game.mission.recipe.employer=2;
  game.campaign.standings[2]=7;
  detachment(game,FactionRole::Allied,{0,1});
  game.member(1).injuries|=uint32_t(MechaInjury::Sensors);
  auto radar=game.view(1).rpgRadar;
  require(radar.allies.size()==2&&radar.contacts.empty(),"Allied radar depended on tactical sensors");
  auto distant=game;
  distant.member(1).body.position={400,0,400};
  require(factionCombatContacts(distant)[1]<0,"Allied escort engaged while out of formation range");
  require(factionCombatContacts(game)[1]==2,"Nearby allied unit did not acquire an opponent");
  for(int tick=0;tick<110&&game.enemies[2].hp==800;++tick)game.step(.04f);
  require(game.enemies[2].hp<800,"Allied fire did not damage the enemy threatening the party");
  hitFromPilot(game,0,2);
  require(game.mission.groups[0].withdrawing&&!game.mission.groups[0].retaliating&&
          !factionAttacksPlayers(game,1),"Nonlethal friendly fire triggered retaliation instead of withdrawal");
  hitFromPilot(game,0,100);
  require(!game.enemies[0].hp&&game.mission.groups[0].retaliating&&factionAttacksPlayers(game,1),
          "Killing an allied unit failed to trigger return fire on withdrawal");
  require(game.view(1).rpgRadar.allies.empty(),"Retaliating actors remained marked as allied");
  require(game.campaign.standings[2]==7,"Friendly-fire penalty was charged before mission completion");
  auto retaliation=game;
  retaliation.ordnance.clear();
  retaliation.member(1).body.position=retaliation.world.targets[1].origin+Vec3{0,0,-19};
  retaliation.enemies[1].fireClock=0;
  retaliation.step(.04f);
  require(std::any_of(retaliation.ordnance.begin(),retaliation.ordnance.end(),[](const Shot &shot){
    return shot.body.owner==1&&shot.npcCanHitPilots;
  }),"Withdrawing survivor did not return fire at the attacking party");
  auto evacuated=game;
  auto &escape=evacuated.mission.groups[0];
  const auto p=evacuated.world.targets[1].origin;
  escape.withdrawal={p.x,p.y,p.z};
  evacuated.step(.04f);
  require(evacuated.world.targets[1].escaped&&!evacuated.world.targets[1].active,
          "Withdrawing survivor remained on the mission map");
  // A remaining allied or neutral detachment is outside the eradication quota.
  game.enemies[2].hp=0;game.world.targets[2].health=0;
  game.step(.04f);
  require(game.phase==Phase::Complete&&game.campaign.standings[2]<7,
          "Debriefing failed to record standing or waited on an auxiliary detachment");
  const auto standings=game.campaign.standings;
  game.step(.04f);
  require(game.campaign.standings==standings,"Debrief applied the same faction incident twice");
 }
 // Each human affiliation and encounter role fights independently of player proximity.
 for(auto role:{FactionRole::Contract,FactionRole::Neutral,FactionRole::Allied}){
  for(auto human:{"aeon_primarch","labor_bot","mecha_recruit"}){
   auto game=arena(919,2,1);
   placeActor(game,0,human,{0,0,0});
   placeActor(game,1,"kaiju_spawnling",{0,0,21});
   game.member(1).body.position={430,0,430};
   if(role!=FactionRole::Contract)detachment(game,role,{0});
   const auto contacts=factionCombatContacts(game);
   require(contacts[0]==1&&contacts[1]==0,"Human and kaiju failed to acquire each other without a pilot");
   for(int tick=0;tick<350&&(game.enemies[0].hp==800||game.enemies[1].hp==800);++tick)game.step(.04f);
   require(game.enemies[0].hp<800&&game.enemies[1].hp<800,std::string(human)+" and kaiju failed to exchange damage");
  }
 }
 {
  auto game=arena(923,2,1);
  placeActor(game,0,"mecha_recruit",{0,0,0});
  placeActor(game,1,"kaiju_spawnling",{0,0,20});
  detachment(game,FactionRole::Allied,{0});
  game.mission.groups[0].withdrawing=true;
  game.mission.groups[0].withdrawal={410,0,0};
  game.member(1).body.position={430,0,430};
  for(int tick=0;tick<100&&game.enemies[1].hp==800;++tick)game.step(.04f);
  require(game.enemies[1].hp<800&&!factionAttacksPlayers(game,0)&&game.world.targets[0].origin.x>0,
          "Withdrawing ally failed to fire on a kaiju while moving to extraction");
 }
 // A boss commits to a visible pilot and gives them time to break the lane.
 {
  auto game=arena(713,1,2);
  auto &boss=game.enemies[0];
  boss.boss=0;
  boss.spec=bossBaseEnemy(0);
  boss.tactic=2;
  game.member(2).body.position={20,0,60};
  game.step(.05f);
  require(boss.chargedAttack>=0&&boss.bossTell>0&&game.ordnance.empty(),"Boss fired before its charge warning");
  require(game.world.targets[0].ai.combatMotion.action==EnemyAction::RangedWindup,"Charge has no ranged windup pose");
  require(std::any_of(game.world.effects.begin(),game.world.effects.end(),[](const auto &fx){return fx.model==rpgEnemyBossCharge;}),"Boss charge has no visible world effect");
  auto interrupted=game;
  interrupted.member(boss.chargeTarget).hiddenRemaining=100;
  interrupted.step(.05f);
  require(interrupted.enemies[0].chargedAttack<0&&interrupted.ordnance.empty(),"Boss redirected a charge after losing its target");
  for(int i=0;i<45&&game.ordnance.empty();++i)game.step(.05f);
  require(!game.ordnance.empty()&&boss.chargedAttack<0&&boss.bossTell==0,"Telegraphed boss attack never released");
  require(game.world.targets[0].ai.combatMotion.action==EnemyAction::Fire,"Released shot has no firing pose");
 }
 // A heavy mecha commits to staggered projectile lanes and cannot pivot
 // its volley to follow a pilot who dodges after the warning.
 {
  auto game=arena(745,1,1);
  game.enemies[0].spec=enemyIndex("jump_pod_field_commander");
  game.member(1).body.position={0,0,54};
  game.step(.02f);
  auto &pattern=game.enemies[0].heavyAttack;
  require(pattern.pattern==HeavyPattern::Weave&&game.ordnance.empty(),"Heavy mecha skipped its pre-fire warning");
  const Vec3 lockedTarget=pattern.origin+pattern.aim*pattern.lockDistance;
  game.member(1).body.position={30,0,54};
  for(int tick=0;tick<140&&game.ordnance.size()<18;++tick)game.step(.02f);
  require(game.ordnance.size()==18&&game.member(1).sheet.hp==10000,
          "Commander failed to deploy three staggered waves");
  for(const auto &shot:game.ordnance){
    const auto &p=shot.body;
    const float untilPlane=dot(lockedTarget-p.position,pattern.aim)/dot(p.velocity,pattern.aim);
    const Vec3 endpoint=p.position+p.velocity*untilPlane;
    require(length(endpoint-lockedTarget)<15.f,"Commander redirected its committed pattern toward a dodging pilot");
  }
 }
 // The same committed breath damages a pilot in the sweep and stops at
 // solid cover or leaves a pilot alone after an off-axis dodge.
 {
  auto open=arena(747,1,1);
  open.enemies[0].spec=enemyIndex("kaiju_obliterator");
  applyOriginalEnemy(open.world.targets[0],open.enemies[0].spec);
  open.world.updateEnemyBounds(open.world.targets[0]);
  open.member(1).body.position={0,0,48};
  open.step(.02f);
  require(open.enemies[0].heavyAttack.pattern==HeavyPattern::Beam&&open.member(1).sheet.hp==10000,
          "Ranged Voidmaw did not charge its breath");
  auto covered=open;
  auto dodged=open;
  auto shielded=open,partial=open,critical=open;
  shielded.member(1).shield=100;shielded.member(1).shieldRemaining=10;
  partial.member(1).shield=1;partial.member(1).shieldRemaining=10;
  partial.member(1).sheet.ap=2;
  critical.member(1).sheet.hp=1;
  covered.world.targets.push_back(wall({0,20,25},{20,20,1}));
  dodged.member(1).body.position.x=20;
  float visibleReach=0,blockedReach=0;
  for(int tick=0;tick<125;++tick){
    open.step(.02f);
    covered.step(.02f);
    dodged.step(.02f);
    shielded.step(.02f);partial.step(.02f);critical.step(.02f);
    for(const auto &fx:open.world.effects)if(fx.model==rpgAtomicBreath)visibleReach=std::max(visibleReach,fx.size);
    for(const auto &fx:covered.world.effects)if(fx.model==rpgAtomicBreath)blockedReach=std::max(blockedReach,fx.size);
  }
  require(open.member(1).sheet.hp<10000&&covered.member(1).sheet.hp==10000&&dodged.member(1).sheet.hp==10000,
          "Atomic breath ignored a dodge or cover, or inflicted no damage in its lane");
  const int unprotectedDamage=10000-open.member(1).sheet.hp;
  require(shielded.member(1).shield==100-unprotectedDamage && shielded.member(1).sheet.hp==10000 &&
          shielded.member(1).sheet.reactorStress==0,"A fully shielded Maw Beam leaked hull damage or heat");
  require(partial.member(1).shield==0 && partial.member(1).sheet.ap==0 &&
          partial.member(1).sheet.hp==10000-unprotectedDamage+3 && partial.member(1).sheet.reactorStress>0,
          "Maw Beam overflow did not pass through shields and plating into hull");
  require(critical.member(1).sheet.hp==0 && critical.member(1).emergency==HullEmergency::LastWind,
          "Lethal Maw Beam bypassed solo hull recovery");
  require(blockedReach>0&&visibleReach>blockedReach*1.5f,"Beam graphic did not stop at the shielding wall");
 }
 // Every named biological attack has a deliberate sound, including the finale.
 const std::map<std::string_view,CombatCue> biologicalAttacks{
   {"Thundering Roar",CombatCue::KaijuRoar},{"Gravesong",CombatCue::KaijuRoar},
   {"Screeching Sonar",CombatCue::KaijuSonar},{"Piercing Screech",CombatCue::KaijuShriek},
   {"Rending Claws",CombatCue::KaijuSwipe},{"Ripping Talons",CombatCue::KaijuSwipe},
   {"Barbed Tail",CombatCue::KaijuSwipe},{"Bone Axe",CombatCue::KaijuImpact},
   {"Coiling Tentacles",CombatCue::KaijuImpact},{"Grapple",CombatCue::KaijuImpact},
   {"Chittering Mandibles",CombatCue::KaijuMandibles},{"Rending Mandibles",CombatCue::KaijuMandibles},
   {"Grave Mandibles",CombatCue::KaijuMandibles},{"Crushing Bite",CombatCue::KaijuBite},
   {"Rending Maw",CombatCue::KaijuBite},{"Consume",CombatCue::KaijuBite},{"Consume Core",CombatCue::KaijuBite},
   {"Dorsal Spines",CombatCue::KaijuSpines},{"Caustic Spines",CombatCue::KaijuSpines},{"Rocket Elbow",CombatCue::KaijuSpines},
   {"Maw Beam",CombatCue::KaijuDischarge},{"Miniature Maw Beam",CombatCue::KaijuDischarge},
   {"Caustic Spit",CombatCue::KaijuSpray},{"Caustic Web",CombatCue::KaijuSpray}};
 auto checkCalls=[&](const EnemySpec &spec){
   if(spec.kind!=EnemyKind::SmallKaiju&&spec.kind!=EnemyKind::LargeKaiju)return;
   for(const auto &attack:spec.attacks)
     require(biologicalAttacks.at(attack.name)==kaijuAttackCue(attack),std::string(spec.id)+" / "+attack.name);
 };
 for(const auto &spec:enemyCatalog())checkCalls(spec);
 for(const auto &boss:campaignBosses())checkCalls(boss.spec);
 // Exercise the authoritative role dispatch for each siege creature on the
 // same network session entry point used by the dedicated server.
 struct SiegeCase { const char *id,*attack; float range; int pulses; };
 for(const SiegeCase &scenario : {
       SiegeCase{"kaiju_obliterator","Maw Beam",48,7}, {"kaiju_tyrant","Maw Beam",48,5},
       {"abyssal_kaiju","Maw Beam",48,5}, {"kaiju_broodqueen","Caustic Spit",44,2},
       {"kaiju_stalker","Caustic Spit",44,2}, {"kaiju_dismantler","Caustic Web",44,1},
       {"abomination","Rocket Elbow",24,2}, {"gravesong_sovereign","Caustic Spines",44,2}}){
   auto game=arena(747,1,1,true);
   auto &enemy=game.enemies.front();
   if(std::string_view(scenario.id)=="gravesong_sovereign"){
     auto bosses=campaignBosses();
     auto boss=std::find_if(bosses.begin(),bosses.end(),[&](const auto &b){
       return std::string_view(b.spec.id)==scenario.id;
     });
     require(boss!=bosses.end(),"Missing Gravesong Sovereign");
     enemy.boss=int(boss-bosses.begin());
     enemy.spec=bossBaseEnemy(enemy.boss);
   }else enemy.spec=enemyIndex(scenario.id);
   auto &actor=game.world.targets.front();
   applyOriginalModel(actor,originalVariant(enemyModelId(enemy.spec),1));
   game.world.updateEnemyBounds(actor);
   game.member(1).body.position={0,0,scenario.range};
   bool braced=false,discharged=false,recovered=false;
   const bool beam=std::string_view(scenario.attack)=="Maw Beam";
   const auto release=biologicalAttacks.at(scenario.attack);
   const auto windup=std::string_view(scenario.id)=="abyssal_kaiju"?CombatCue::KaijuSonar:
       release==CombatCue::KaijuSpines?CombatCue::KaijuMandibles:CombatCue::KaijuHiss;
   int pulses=0,releaseSounds=0;
   bool windupHeard=false;
   for(int frame=0;frame<360&&!recovered;++frame){
     game.step(1.f/60);
     const auto phase=game.world.targets.front().ai.voidmawPose.phase;
     const auto &pattern=game.enemies.front().heavyAttack;
     for(const auto &sound:game.world.sounds){
       if(sound.kind!=SoundEvent::Special)continue;
       if(phase==VoidmawPose::Brace&&sound.weapon==int(windup))windupHeard=true;
       if(sound.weapon==int(release))++releaseSounds;
     }
     if(phase==VoidmawPose::Brace){
       braced=true;
       require(pattern.attack.name&&std::string_view(pattern.attack.name)==scenario.attack,
               std::string("Unexpected siege attack for ")+scenario.id);
     }
     discharged|=phase==VoidmawPose::Discharge;
     recovered=phase==VoidmawPose::SiegeRecover;
     pulses=std::max(pulses,pattern.pulse);
   }
   require(windupHeard&&releaseSounds==(beam?1:scenario.pulses),
           std::string("Incorrect windup or salvo sound count for ")+scenario.id);
   require(braced&&discharged&&recovered&&pulses==scenario.pulses,
           std::string("Missing siege attack phase or pulses for ")+scenario.id);
 }
 // A melee strike has a readable windup and releases only at its end.
 {
  auto game=arena(713,1,1);
  game.enemies[0].spec=enemyIndex("labor_bot");
  game.enemies[0].tactic=6;
  game.member(1).body.position={0,0,7};
  game.step(.02f);
  auto &motion=game.world.targets[0].ai.combatMotion;
  require(motion.action==EnemyAction::MeleeWindup&&game.ordnance.empty(),"Melee attack skipped the windup");
  require(std::any_of(game.world.effects.begin(),game.world.effects.end(),[](const auto &fx){
    return fx.model==rpgEnemyMeleeTell;
  }),"Melee windup used no dedicated tell");
  auto dodge=game;
  dodge.member(1).body.position={0,0,22};
  for(int i=0;i<30&&motion.action==EnemyAction::MeleeWindup;++i){
    game.step(.02f);
    dodge.step(.02f);
  }
  require(motion.action==EnemyAction::Strike,"Melee windup did not release into a strike pose");
  require(dodge.world.targets[0].ai.combatMotion.action==EnemyAction::Strike&&dodge.ordnance.empty()&&
          dodge.member(1).sheet.hp==10000,"An evaded swing skipped follow-through or still inflicted damage");
 }
 {
  auto game=arena(719,2,1);
  placeActor(game,0,"labor_bot",{0,0,0});
  placeActor(game,1,"kaiju_spawnling",{0,0,7});
  game.enemies[0].tactic=6;
  game.enemies[1].disabled=100;
  onlyPilot(game,0);
  game.step(.02f);
  auto &motion=game.world.targets[0].ai.combatMotion;
  require(motion.action==EnemyAction::MeleeWindup&&game.enemies[0].chargeWorldTarget==1&&game.ordnance.empty(),
          "An NPC rushed through the windup against another NPC");
  for(int tick=0;tick<35&&game.enemies[1].hp==800;++tick)game.step(.02f);
  require(game.enemies[1].hp<800&&motion.action==EnemyAction::Strike,
          "The committed melee animation failed to land on an opposing NPC");
 }
 // Forced movement stops before scenery and squadmates.
 {
  auto game=arena(713,1,2);
  game.enemies[0].disabled=100;
  game.member(1).body.position={0,0,30};
  game.member(2).body.position={0,0,35};
  Shot shot;
  shot.body.position={0,6,26};
  shot.body.velocity={0,0,100};
  shot.body.owner=0;
  shot.body.mount=-1;
  shot.body.damage=1;
  shot.effect=AttackEffect::Push;
  auto blocked=game;
  blocked.world.targets.push_back(wall({0,6,33},{5,6,.5f}));
  game.ordnance.push_back(shot);
  blocked.ordnance.push_back(shot);
  game.step(.05f);
  blocked.step(.05f);
  require(game.member(1).body.position.z==31,"Enemy knockback did not stop before an ally");
  require(blocked.member(1).body.position.z==30.5f,"Enemy knockback did not stop before a wall");
 }
 // Dead, ejected and cloaked pilots are nearer than the only eligible player.
 // The selected target must survive the later attack-victim sorting stage.
 {
  auto game=arena();game.member(1).body.position={0,0,55};
  game.member(2).body.position={-8,0,25};game.member(2).sheet.hidden=true;game.member(2).hiddenRemaining=100;
  game.member(3).body.position={8,0,20};game.member(3).sheet.hp=0;game.member(3).body.health=0;
  game.member(4).body.position={16,0,15};game.member(4).ejected=true;game.member(4).body.lifeSpent=true;game.member(4).body.ejectionTime=7;
  game.step(.01f);const auto shots=aimedShots(game,1);require(shots==3,"Eligible pilot did not receive three unobstructed enemy attacks");checkedShots+=shots;
  // Invalidating the cached target must take effect inside the decision cadence.
  onlyPilot(game,2);readyShots(game);game.step(.01f);checkedShots+=aimedShots(game,2);
  require(game.ordnance.size()==3,"Hidden cached target delayed acquisition of the newly exposed pilot");
  onlyPilot(game,0);readyShots(game);game.step(.01f);
  require(game.ordnance.empty(),"Enemy shot at a hidden, dead or ejected pilot");
 }
 // Three compatible snipers have two observable contacts. A visibly damaged
 // contact is a better concentrated-fire opportunity than splitting by nearest.
 {
  auto game=arena(727,3,2);game.member(1).body.position={-10,0,55};game.member(2).body.position={10,0,55};
  game.member(2).sheet.hp=1000;
  size_t nearestWeak=0;
  for(const auto &actor:game.world.targets)if(length(actor.origin-game.member(2).body.position)<length(actor.origin-game.member(1).body.position))++nearestWeak;
  game.step(.01f);const auto focus=aimedShots(game,2);checkedShots+=focus;
  require(focus==3&&focus>nearestWeak,"Compatible attackers did not concentrate actual attacks on the exposed damaged contact");
  std::cout<<"Damaged-contact concentration: "<<focus<<"/3 versus nearest-only "<<nearestWeak<<"/3; ";
 }
 // A fresh wall is genuine occlusion even when weather/sensor range allows the
 // pilot to be seen. A blocked attack must not waste its firing cooldown.
 {
  auto game=arena(731,1,1);game.world.targets.push_back(wall({0,10,25},{35,20,2}));
  game.step(.01f);require(game.ordnance.empty(),"Enemy fired into an invulnerable wall");
  require(game.enemies[0].fireClock<=.15f,"Blocked firing lane consumed a full attack cooldown");
  game.world.targets.back().active=false;for(int n=0;n<40;++n)game.step(.01f);
  require(aimedShots(game,1)==1,"Enemy failed to fire after its wall obstruction was removed");++checkedShots;
 }
 // Disabled allies remain physical hulls. They must not be shot through while
 // the attacker has a clear sensor observation of the player beyond them.
 {
  auto game=arena(739,2,1);auto &back=game.world.targets[0];auto &front=game.world.targets[1];
  back.origin={0,0,0};front.origin={0,0,23};game.world.updateEnemyBounds(back);game.world.updateEnemyBounds(front);
  game.enemies[1].disabled=100;game.step(.01f);
  require(game.ordnance.empty(),"Rear attacker fired through a compatible allied hull");
  front.active=false;game.enemies[1].hp=0;for(int n=0;n<40;++n)game.step(.01f);
  require(aimedShots(game,1)==1,"Rear attacker did not resume after its ally left the lane");++checkedShots;
 }
 // A precision shooter without a functioning squad partner still moves
 // sideways to regain a firing lane around a disabled allied hull.
 {
  auto game=arena(741,2,1);auto &back=game.world.targets[0];auto &front=game.world.targets[1];
  back.origin={0,0,0};front.origin={0,0,23};game.world.updateEnemyBounds(back);game.world.updateEnemyBounds(front);
  game.enemies[1].disabled=100;bool fired=false;
  for(int tick=0;tick<40;++tick){game.step(.1f);fired=fired||game.world.targets[0].ai.muzzleFlash>0;}
  require(std::abs(back.origin.x)>3,"Isolated precision shooter never moved out of an obstructed firing lane");
  require(fired,"Isolated precision shooter did not regain a firing lane around a disabled ally");
 }
 // The first lateral opening is blocked by terrain, so try the other side.
 {
  auto game=arena(742,2,1);auto &back=game.world.targets[0];auto &front=game.world.targets[1];
  back.origin={0,0,0};front.origin={0,0,23};game.world.updateEnemyBounds(back);game.world.updateEnemyBounds(front);
  game.enemies[1].disabled=100;
  game.world.targets.push_back(wall({7,10,0},{1,20,6}));
  const auto decisions=planEnemyTactics(game);
  require(!decisions[0].fireLaneClear&&decisions[0].positioning&&decisions[0].destination.x<0,
          "Blocked shooter chose the obstructed flank over an open firing position");
 }
 // Mobile ranged pressure happens between shots and excludes stationary guns.
 {
  auto game=arena(744,1,1);
  game.enemies[0].spec=enemyIndex("mecha_ace");
  game.enemies[0].tactic=7;
  game.enemies[0].fireClock=.75f;
  const auto aim=game.member(1).body.position;
  const auto drive=enemyRecoveryStrafe(game,0,aim);
  require(std::abs(drive.x)>.9f&&drive.z>0,"Mobile ace did not strafe into range during recovery");
  game.enemies[0].fireClock=.2f;
  require(length(enemyRecoveryStrafe(game,0,aim))<.001f,"Mobile ace did not settle before firing");
  game.enemies[0].fireClock=.75f;
  game.enemies[0].spec=enemyIndex("ion_emplacement");
  require(length(enemyRecoveryStrafe(game,0,aim))<.001f,"Stationary installation requested a recovery strafe");
 }
 // Losing actual line of sight searches the last observed position. Moving
 // the pilot behind a corner must not reveal its current position to navigation.
 {
  auto game=arena(743,1,1);game.member(1).body.position={0,0,70};game.enemies[0].fireClock=1000;
  for(int n=0;n<5;++n)game.step(.1f);
  const auto observed=game.member(1).body.position;
  game.member(1).body.position={60,0,70};game.world.targets.push_back(wall({30,10,35},{10,20,20}));
  auto before=game.world.targets[0].origin;
  for(int n=0;n<5;++n)game.step(.1f);
  auto displacement=game.world.targets[0].origin-before;
  require(displacement.z>.2f&&std::abs(displacement.x)<.15f,"Lost-contact search followed an occluded pilot instead of its observed position");
  require(game.ordnance.empty(),"Enemy fired while searching an unobserved contact");
  require(observed.x==0,"Corner fixture moved its original observed point");
  auto expired=game;for(int n=0;n<45;++n)expired.step(.1f);
  const auto stopped=expired.world.targets[0].origin;expired.step(.1f);
  require(length(expired.world.targets[0].origin-stopped)<.001f,"Lost-contact pursuit exceeded its memory lifetime");
  game.member(1).sheet.hidden=true;game.member(1).hiddenRemaining=100;
  before=game.world.targets[0].origin;game.step(.1f);
  require(length(game.world.targets[0].origin-before)<.001f,"Cloaked contact kept driving a stale pursuit intent");
 }
 // A carrier reuses a dead slot without changing the actor count. The old
 // occupant had seen a pilot; a fresh airborne child must inherit neither that
 // contact memory nor the previous mechanical squad's cached intent.
 {
  auto game=arena(767,maxEncounterEnemies,1);game.world.targets[0].origin={0,0,0};game.world.updateEnemyBounds(game.world.targets[0]);
  const int carrier=enemyIndex("carrier");game.enemies[1].spec=carrier;game.mission.recipe.enemies[1]=carrier;
  game.world.targets[1].origin={-80,0,-80};game.enemies[1].disabled=100;game.enemies[1].fireClock=1000;
  for(size_t n=2;n<game.enemies.size();++n){game.enemies[n].hp=0;game.world.targets[n].health=0;game.world.targets[n].active=false;game.world.targets[n].protectedTarget=true;}
  game.enemies[0].fireClock=1000;game.member(1).body.position={0,0,20};game.step(.01f);
  auto remembered=game;remembered.member(1).body.position={0,0,70};remembered.world.targets.push_back(wall({0,100,35},{450,100,2}));
  const auto oldPosition=remembered.world.targets[0].origin;
  for(int n=0;n<10;++n)remembered.step(.01f);
  require(remembered.world.targets[0].origin.z-oldPosition.z>.2f,"Reuse fixture never established observable contact memory");
  game.enemies[0].hp=0;game.world.targets[0].health=0;game.world.targets[0].active=false;
  game.member(1).body.position={0,0,70};game.world.targets.push_back(wall({0,100,35},{450,100,2}));
  game.enemies[1].disabled=0;
  for(int attempt=0;attempt<24&&game.enemies[0].hp<=0;++attempt){game.enemies[1].manualClock=0;game.step(.01f);}
  require(game.enemies.size()==maxEncounterEnemies&&game.enemies[0].hp>0&&game.enemies[1].spawned==1,"Carrier fixture did not exercise same-count dead-slot reuse");
  require((enemySpec(game.enemies[0].spec).traits&Flying)!=0,"Reused slot did not contain a new airborne child");
  const auto spawned=game.world.targets[0].origin;
  for(int n=0;n<20;++n)game.step(.01f);
  require(length(game.world.targets[0].origin-spawned)<.001f,"Newly spawned actor inherited the dead occupant's remembered pursuit");
  require(game.ordnance.empty(),"Reused enemy slot fired at a structurally occluded contact");
 }
 // Objective attacks inspect every surviving asset, rather than becoming
 // stuck on the first asset when it is farther than the current weapon reach.
 {
  auto game=arena(747,1,1);onlyPilot(game,0);game.mission.recipe.objective=1;
  auto asset=[](Vec3 position){Target t;t.origin=position;t.center=position+Vec3{0,6,0};t.half={2,6,2};t.health=10000;t.active=true;t.spawnType=-1;return t;};
  game.world.targets.push_back(asset({0,0,200}));
  game.world.targets.push_back(asset({15,0,55}));game.mission.assets={1,2};
  game.step(.01f);require(game.ordnance.size()==1,"Enemy ignored an attackable secondary mission asset");
  const auto &shot=game.ordnance.front().body;
  require(dot(unit(shot.velocity),unit(game.world.targets[2].center-shot.position))>.99f,"Mission attack stayed locked to the first distant asset");
 }
 // Reproducible episodes compare physical decisions/trajectories at each tick,
 // with no renderer, network timing, random_device or local-player dependency.
 {
  auto first=arena(751,3,2),second=arena(751,3,2);size_t ticks=0;
  for(int n=0;n<240;++n){
   first.step(1.f/60);second.step(1.f/60);require(first.phase==Phase::Combat&&second.phase==Phase::Combat,"Combat evaluation ended prematurely");
   require(first.ordnance.size()==second.ordnance.size(),"Seeded AI episodes emitted different projectile counts");
   require(first.ordnance.size()<=maxLiveProjectiles,"AI exceeded bounded projectile storage");
   for(size_t k=0;k<first.enemies.size();++k){const auto &a=first.world.targets[k],&b=second.world.targets[k];
    require(std::isfinite(length(a.origin))&&std::isfinite(length(a.ai.velocity)),"AI emitted nonfinite movement");
    require(length(a.origin-b.origin)<.00001f&&std::abs(a.ai.weaponYaw-b.ai.weaponYaw)<.00001f,"Same seeded AI episode chose a different movement or aim");
   }
   for(size_t k=0;k<first.ordnance.size();++k){const auto &a=first.ordnance[k].body,&b=second.ordnance[k].body;
    require(length(a.position-b.position)<.00001f&&length(a.velocity-b.velocity)<.00001f&&a.owner==b.owner,"Same seeded AI episode generated different physical attacks");
   }
   ++ticks;
  }
  std::cout<<"Seeded combat evaluation: "<<ticks<<" paired ticks; ";
 }
 std::cout<<checkedShots<<" selected-target projectiles; invalidation, wall and ally firing lanes verified\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
