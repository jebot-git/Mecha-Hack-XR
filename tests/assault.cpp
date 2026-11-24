#include "assault.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace iron;
World fixture(){World w;w.targets.clear();w.game=0;w.mission=2;w.position={-150,0,0};w.missionStatus=MissionStatus::Active;w.counters={1,0,0};
    Target goal;goal.origin=goal.center={150,5,0};goal.half={5,5,5};goal.tag=32;goal.action=0x80020000u;w.targets.push_back(goal);
    for(const auto& unit:assaultUnitRoster){int type=unit.type;Target t;t.assetGame=1;t.spawnType=type;t.origin=t.center={500.f+type*30,5,500};t.half={2,2,2};t.health=80;t.ai.maxHealth=80;t.active=false;w.targets.push_back(t);}return w;}
void ticks(AssaultMatch& m,float seconds){for(int i=0;i<int(seconds*60)+1;++i)m.step(1.f/60);}
void kill(AssaultMatch& m,size_t i){auto& p=m.pilots[i].pilot;p.health=0;p.lifeSpent=true;p.missionStatus=MissionStatus::Ejecting;}
int main(){
 AssaultMatch m;m.prepare(fixture());assert(m.world.assault&&m.world.cooperative);unsigned air=0,ground=0,turrets=0;
 for(auto& t:m.world.targets)if(t.active&&t.spawnType>=0){assert(t.generated&&t.ai.respawnRange==0);if(t.team==Team::Attackers){air+=t.spawnType==1||t.spawnType==7||t.spawnType==12||t.spawnType==18;ground+=t.spawnType==0||t.spawnType==3;}else turrets+=t.spawnType==2||t.spawnType==5||t.spawnType==10;}
 assert(air==6&&ground==2&&turrets==4&&m.world.generators.empty());
 for(size_t i=0;i<8;++i){assert(m.pilots[i].pilot.playerSlot==i);assert(m.pilots[i].pilot.team==(i<4?Team::Attackers:Team::Defenders));}
 // Every IS2 weapon is available without campaign unlocks, with native mount rules.
 for(int weapon=1;weapon<int(Weapon::Count);++weapon){bool equipped=false;for(int mount=0;mount<MountCount;++mount)if(compatible(Weapon(weapon),mount)){
     Loadout selected{};selected[mount]=Weapon(weapon);assert(validAssaultLoadout(selected));m.setLoadout(0,selected);assert(m.pilots[0].pilot.weapons[mount]==Weapon(weapon));equipped=true;break;
 }assert(equipped);}
 Loadout invalid{};invalid[LeftHand]=Weapon::Homing;assert(!validAssaultLoadout(invalid));invalid={};invalid[LeftHip]=Weapon::AutoGrenade;invalid[RightHip]=Weapon::Rocket;assert(!validAssaultLoadout(invalid));
 invalid={};invalid[LeftHand]=Weapon(999);assert(!validAssaultLoadout(invalid));
 // All independent IS2 actor types can be explicitly selected on an IS1 map.
 for(const auto& unit:assaultUnitRoster){auto w=fixture();w.waterRegions.push_back({{400,400,800,800},-1,6});AssaultRules selected;selected.attackerUnits={unit.type};selected.defenderUnits={2};
     AssaultMatch full;full.prepare(std::move(w),selected);auto actor=std::find_if(full.world.targets.begin(),full.world.targets.end(),[&](auto& t){return t.active&&t.team==Team::Attackers&&t.spawnType==unit.type;});
     assert(actor!=full.world.targets.end()&&actor->assetGame==1&&full.world.game==0&&full.world.combatGame()==1);
     if(unit.role==AssaultUnitRole::Naval)assert(full.world.hasWater(actor->origin.x,actor->origin.z));
     actor->health=0;full.step(1.f/60);assert(full.world.counters[0]==1); // support deaths cannot complete the bridge goal
 }
 // IS2-specific Satyr AI moves on an IS1 objective map.
 {World ai;ai.targets.clear();ai.game=0;ai.assault=true;ai.cooperative=true;
  Pilot defender;defender.team=Team::Defenders;defender.position={100,0,0};ai.teammates={&defender};
  Target satyr;satyr.spawnType=14;satyr.team=Team::Attackers;satyr.generated=true;satyr.speed=15;satyr.center=satyr.origin={0,6,0};satyr.half={2,2,2};satyr.yaw=-pi/2;satyr.ai.words[0]=60000;
  ai.targets.push_back(satyr);ai.stepEnemies(.1f);assert(ai.targets[0].origin.x>0);
 }
 // Targeting must never fall back to the current world pilot or an allied target slot.
 m.world.teammates.clear();for(auto& p:m.pilots)m.world.teammates.push_back(&p.pilot);
 for(auto& t:m.world.targets)if(t.active&&t.spawnType>=0){int victim; m.world.enemyDestination(t,victim);if(victim==-1){assert(t.team==Team::Defenders);continue;}if(victim>=0)assert(m.world.targets[victim].team!=t.team);else assert(m.pilots[-2-victim].pilot.team!=t.team);}
 // Disable support attacks so rule timing checks are deterministic.
 auto quiet=[](AssaultMatch& x){for(auto& t:x.world.targets)if(t.spawnType>=0)t.active=false;};quiet(m);
 m.pilots[0].pilot.searchlightOn=true;kill(m,0);kill(m,4);m.step(1.f/60);assert(m.respawnsRemaining==11&&m.pilots[0].waiting&&m.pilots[4].waiting);
 Loadout queued{Weapon::Shotgun,Weapon::HeavyMachinegun,Weapon::Homing,Weapon::Shield,Weapon::AutoGrenade,Weapon::Empty};m.setLoadout(0,queued);
 Projectile survivor;survivor.position={800,100,800};survivor.velocity={};survivor.life=100;survivor.damage=0;m.world.projectiles.push_back(survivor);
 ticks(m,8.1f);assert(m.pilots[0].pilot.weapons==queued);assert(!m.world.projectiles.empty());assert(!m.pilots[0].pilot.searchlightOn);assert(!m.pilots[0].waiting&&m.pilots[0].pilot.health==100&&m.pilots[4].waiting);
 ticks(m,12.1f);assert(!m.pilots[4].waiting&&m.pilots[4].pilot.team==Team::Defenders);
 kill(m,4);m.step(1.f/60);assert(m.pilots[4].waiting&&m.respawnsRemaining==11);
 AssaultRules r;r.attackerRespawns=1;AssaultMatch limited;limited.prepare(fixture(),r);quiet(limited);
 for(int i=0;i<4;++i)kill(limited,i);limited.step(1.f/60);assert(limited.respawnsRemaining==0&&limited.result==AssaultResult::Playing);
 ticks(limited,8.1f);kill(limited,0);limited.step(1.f/60);assert(limited.result==AssaultResult::DefendersWin);
 AssaultMatch victory;victory.prepare(fixture());quiet(victory);victory.world.targets[0].health=0;victory.step(1.f/60);assert(victory.world.counters[0]==0);victory.world.effects.clear();victory.step(1.f/60);assert(victory.result==AssaultResult::AttackersWin);
 AssaultMatch timer;r.roundSeconds=.03f;timer.prepare(fixture(),r);quiet(timer);ticks(timer,.1f);assert(timer.result==AssaultResult::DefendersWin);float frozen=timer.clock;timer.step(.01f);assert(timer.clock==frozen);
 // Direct PvP projectile damage reaches defenders in slots beyond the old co-op cap.
 AssaultMatch combat;combat.prepare(fixture());quiet(combat);auto defender=combat.pilots[7].pilot.position;
 Projectile bullet;bullet.position=defender+Vec3{0,6,8};bullet.velocity={0,0,-100};bullet.damage=10;bullet.mount=1;bullet.shooter=0;
 combat.world.projectiles.push_back(bullet);combat.step(.1f);assert(combat.pilots[7].pilot.health<100);
 // Finite support deaths cannot replenish a slot or reserve a player ticket.
 auto actor=std::find_if(combat.world.targets.begin(),combat.world.targets.end(),[](auto& t){return t.team==Team::Attackers&&t.spawnType>=0;});
 actor->active=true;actor->health=0;combat.step(1.f/60);assert(actor->processed&&combat.respawnsRemaining==12);ticks(combat,1);assert(actor->health<=0);
 // A blocked respawn remains queued and does not consume a second reservation.
 AssaultMatch blocked;blocked.prepare(fixture());quiet(blocked);kill(blocked,0);blocked.step(1.f/60);
 Target obstruction;obstruction.center=blocked.pilots[0].pilot.position;obstruction.half={100,100,100};blocked.world.targets.push_back(obstruction);
 ticks(blocked,8.1f);assert(blocked.pilots[0].waiting&&blocked.respawnsRemaining==11);
 blocked.world.targets.back().health=0;blocked.world.targets.back().processed=true;ticks(blocked,1);assert(!blocked.pilots[0].waiting&&blocked.respawnsRemaining==11);
 // Every preset must reach victory through its native campaign counters.
 for(unsigned scenario=1;scenario<assaultScenarios.size();++scenario){auto w=fixture();auto spec=assaultScenarios[scenario];w.game=spec.game;w.mission=spec.mission;
     if(scenario==1){w.counters={0,0,1};w.targets[0].origin=w.targets[0].center={150,5,0};w.targets[0].action=0;}
     if(scenario==2){w.counters={0,0,1};w.objectiveRegions={{140,-10,160,10}};w.targets[0].action=0;}
     if(scenario==3){w.counters={1,0,0};w.targets[0].action=0;}
     if(scenario==4){w.counters={1,0,0};w.targets[0].origin=w.targets[0].center={250,5,-250};w.targets[0].action=0;}
     if(scenario==5){w.counters={1,0,0};w.targets[0].action=0x80020000u;}
     if(scenario==6){w.counters={1,0,0};w.targets[0].action=0;}
     AssaultMatch campaign;campaign.prepare(std::move(w));quiet(campaign);campaign.world.targets[0].health=0;campaign.step(1.f/60);campaign.world.effects.clear();campaign.step(1.f/60);assert(campaign.result==AssaultResult::AttackersWin);
 }
 bool rejected=false;try{timer.step(std::numeric_limits<float>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 rejected=false;r.defenderDelay=1;try{timer.prepare(fixture(),r);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 std::cout<<"Assault teams, finite support, native objectives, respawns, wipe and timeout passed\n";
}
