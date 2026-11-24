#include "searchlight.hpp"
#include "coop.hpp"
#include <cassert>
#include <iostream>
using namespace iron;
World fixture(){World w;w.targets.clear();w.position={0,0,0};w.chassisYaw=0;w.torsoYaw=0;w.assault=true;w.night=true;w.missionStatus=MissionStatus::Active;return w;}
int main(){
 auto w=fixture();auto ranges=visibilityRanges(w);assert(ranges.base==90&&ranges.lit==180);
 Vec3 ahead{0,6,-140},behind{0,6,140},outside{0,6,-181};assert(!w.canPerceive(w,ahead));
 Pilot observer;observer.position={0,0,-140};observer.missionStatus=MissionStatus::Active;
 assert(!w.canPerceive(observer,w.position+Vec3{0,6,0},false));
 Input click;click.searchlight=true;w.step(click,.02f,false);assert(w.searchlightOn);
 assert(w.canPerceive(w,ahead)&&!w.canPerceive(w,behind)&&!w.canPerceive(w,outside));
 assert(w.canPerceive(observer,w.position+Vec3{0,6,0},searchlightActive(w)));
 observer.position={0,0,-181};assert(!w.canPerceive(observer,w.position+Vec3{0,6,0},true)); // same 180-m boundary
 for(int i=0;i<10;++i)w.step(click,.02f,false);assert(w.searchlightOn); // one toggle per press
 w.step(Input{},.02f,false);w.step(click,.02f,false);assert(!w.searchlightOn);
 Input unfocused=click;unfocused.focused=false;w.step(unfocused,.02f,false);w.step(click,.02f,false);assert(!w.searchlightOn); // held through focus change
 w.step(Input{},.02f,false);w.step(click,.02f,false);assert(w.searchlightOn);
 auto ground=traceSearchlight(w,w);assert(ground.hit&&std::abs(ground.end.y)<.02f&&ground.normal.y>.99f);
 Target wall;wall.center={0,6,-70};wall.half={10,20,1};w.targets.push_back(wall);
 assert(!w.canPerceive(w,ahead));observer.position={0,0,-140};assert(!w.canPerceive(observer,w.position+Vec3{0,6,0},true));
 auto hit=traceSearchlight(w,w);assert(hit.hit&&hit.end.z>-70&&hit.normal.z>.99f&&hit.length<ground.length);
 w.targets.clear();w.hurt(10000);assert(!w.searchlightOn&&!searchlightActive(w));
 auto clear=fixture();clear.night=false;assert(visibilityRanges(clear).base==visibilityRanges(clear).lit); // no daytime penalty to reduce
 auto fog=fixture();fog.environment.weather=Weather::Fog;assert(visibilityRanges(fog).lit==visibilityRanges(fog).base*2);
 // Lightning restores daytime sight for both flashes, independently of lamps.
 auto storm=fixture();storm.environment.weather=Weather::Storm;storm.environment.seed=42;
 float strike=34-weatherNoise(42,0)*10;
 for(bool night:{false,true}){storm.night=night;storm.elapsed=strike+.3f;auto ordinary=visibilityRanges(storm);
  for(float phase:{.01f,.09f,.11f,.19f}){storm.elapsed=strike+phase;assert(weatherLightningActive(storm.environment,storm.elapsed));assert(visibilityRanges(storm).base==250&&visibilityRanges(storm).lit==250);assert(storm.canPerceive(storm,{0,6,-240}));
   auto visual=environmentVisuals(storm.environment,night,{},{},storm.elapsed);assert(visual.density==0);}
  storm.elapsed=strike+.21f;assert(!weatherLightningActive(storm.environment,storm.elapsed));assert(visibilityRanges(storm).base==ordinary.base);
 }
 World legacy;legacy.targets.clear();legacy.missionStatus=MissionStatus::Active;legacy.step(click,.02f,false);assert(!legacy.searchlightOn&&visibilityInRange(legacy,legacy,{0,0,-1000}));
 // Snapshot state lets remote presentation build the same beam from existing pose.
 auto on=fixture();on.searchlightOn=true;auto wire=coop::pilotWire(on);Pilot remote;coop::pilotRead(wire,remote);assert(remote.searchlightOn);
 assert(length(traceSearchlight(on,remote).end-traceSearchlight(on,on).end)<.001f);
 wire.erase("searchlight");coop::pilotRead(wire,remote);assert(!remote.searchlightOn);
 assert(coop::inputRead(coop::inputWire(click)).searchlight);auto old=coop::inputWire(click);old.erase("searchlight");assert(!coop::inputRead(old).searchlight);
 // An enemy cannot acquire the dark IS at 140 m, but can acquire its active lamp.
 auto ai=fixture();ai.cooperative=true;Pilot pilot;pilot.team=Team::Attackers;pilot.position={0,0,0};pilot.missionStatus=MissionStatus::Active;ai.teammates={&pilot};
 Target tank;tank.team=Team::Defenders;tank.spawnType=0;tank.origin={0,0,-140};tank.center={0,6,-140};tank.half={2,2,2};ai.targets={tank};int victim=0;
 ai.enemyDestination(ai.targets[0],victim);assert(victim==-1);pilot.searchlightOn=true;ai.enemyDestination(ai.targets[0],victim);assert(victim==-2);
 ai.stepEnemies(.02f);assert(ai.targets[0].ai.alert>0);pilot.searchlightOn=false;ai.enemyDestination(ai.targets[0],victim);assert(victim==-1);
 // Exercise the authoritative room input/state path without retail files.
 coop::Room room("/tmp");room.add(1);room.add(2);room.host=1;room.phase=coop::Phase::Playing;room.round=1;room.mission=2;

 room.world=fixture();room.world.assault=false;room.world.environment=readEnvironment({{"lighting","night"},{"weather","fog"}});room.world.cooperative=true;room.world.mission=2;room.world.counters={1,0,0};
 for(auto& member:room.players){member.pilot=Pilot{};member.pilot.playerSlot=member.slot;member.pilot.position={float(member.slot)*20,0,0};member.pilot.missionStatus=MissionStatus::Active;}
 room.command(1,{{"op","input"},{"round",1},{"input",coop::inputWire(click)}});room.step(.02f);auto state=room.state();
 assert(!state["searchlight_preview"].get<bool>()&&state["environment"]["weather"]=="clear"&&!state["players"][0]["pilot"]["searchlight"].get<bool>());
 assert(room.world.activeEnvironment().weather==Weather::Clear&&room.world.activeEnvironment().lighting==Lighting::Authored);
 auto offInput=coop::inputWire(Input{});offInput["searchlight_on"]=true;room.command(2,{{"op","input"},{"round",1},{"input",offInput}});room.step(.02f);assert(!room.player(2).pilot.searchlightOn); // no direct client state claim
 std::cout<<"Searchlight: matched sight/reveal ranges, cone, occlusion, toggle edges, death and wire/AI behavior passed\n";
}
