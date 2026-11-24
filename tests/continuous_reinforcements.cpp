#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
using namespace iron;using Json=nlohmann::json;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::pair<unsigned,unsigned> counts(const World& w){unsigned ground=0,air=0;for(const auto& t:w.targets)if(t.spawnType>=0&&t.active&&t.health>0&&!t.escaped)(t.spawnType==1||t.spawnType==18?air:ground)++;return {ground,air};}
static Json policy(unsigned zones,unsigned ground,unsigned air,unsigned perZone=3){return {{"version",4},{"objectives",Json(std::vector<Json>(zones,Json::object()))},{"encounters",{{"ground_per_zone",perZone},{"air_units",air}}},{"reinforcements",{{"mode","continuous"},{"ground_limit",ground},{"air_limit",air},{"interval",5},{"activation_range",2000},{"kill_quota",512}}}};}
static ProceduralSpawnSite site(World& w,int type,int zone,Vec3 p){
    Target t;t.spawnType=type;t.proceduralZone=zone;t.prototypeObjective=type!=1&&type!=18;t.generated=true;t.origin=p;t.health=100;t.speed=6;t.assetGame=type==14||type==18?1:0;
    t.mesh=std::make_shared<WorldMesh>(WorldMesh{{{-1,0,-1},{1,1,1}},{{1,4,1},{1,1,1}}});w.updateEnemyBounds(t);t.ai.spawn=t.origin;t.ai.maxHealth=t.ai.lastHealth=t.health;
    return {t,{0,0,-1},20};
}
static World fixture(unsigned zones=1,unsigned ground=3,unsigned aircraft=2,unsigned perZone=3){
    World w;w.targets.clear();w.game=1;w.mission=0;w.missionStatus=MissionStatus::Active;w.missionPrototype=w.proceduralLayout=true;w.position={0,0,-700};w.prototypeSeed=42;
    std::vector<ProceduralSpawnSite> sites;
    for(unsigned z=0;z<zones;++z){float x=(int(z)-int(zones/2))*150.f;
        ProceduralObjective goal;goal.kind=ProceduralGoalKind::Recon;goal.position={x,0,0};goal.segment=int(z);w.procedural.objectives.push_back(goal);
        for(int family=0;family<3;++family)for(int n=0;n<3;++n)sites.push_back(site(w,std::array<int,3>{0,2,14}[family],int(z),{x,0,family*100.f+n*30.f}));
        for(int n=0;n<2;++n)sites.push_back(site(w,n?18:1,int(z),{x,100,-100.f-n*50.f}));
    }
    auto recipe=policy(zones,ground,aircraft,perZone);prepareContinuousProceduralReinforcements(w,recipe,sites);return w;
}
static void advance(World& w,float seconds,float dt=.1f){int steps=int(std::round(seconds/dt));for(int i=0;i<steps;++i)w.stepGenerators(dt);}
static void kill(World& w,Target& t){t.health=0;w.targetDestroyed(t);}
static void validation(){
    auto good=policy(2,6,2);validateProceduralReinforcements(good);
    for(int field=0;field<11;++field){auto bad=good;auto& r=bad["reinforcements"];
        if(field==0)r["ground_limit"]=65;if(field==1)r["air_limit"]=9;if(field==2)r["ground_limit"]=5;if(field==3)r["air_limit"]=1;if(field==4)r["kill_quota"]=0;
        if(field==5)r["activation_range"]=99;if(field==6)r["interval"]=4;if(field==7)r["per_zone"]=3;if(field==8)bad["version"]=3;if(field==9)r["mode"]="unknown";if(field==10)r["ground_limit"]=true;
        bool rejected=false;try{validateProceduralReinforcements(bad);}catch(const std::exception&){rejected=true;}check(rejected,"Invalid continuous reinforcement policy accepted");
    }
}
static void cappedSoak(){
    auto w=fixture(8,64,8,8);size_t size=w.targets.size();check(size==72,"Pool did not reserve exactly the map actor budget");std::set<int> kinds;unsigned peakGround=0,peakAir=0;
    for(int frame=0;frame<6000;++frame){w.stepGenerators(.1f);auto [ground,air]=counts(w);check(ground<=64&&air<=8&&w.targets.size()==size,"Continuous waves exceeded population/storage budget");peakGround=std::max(peakGround,ground);peakAir=std::max(peakAir,air);
        for(auto& t:w.targets)if(t.active&&t.health>0)kinds.insert(t.spawnType);
        if(frame%200==199){for(auto& t:w.targets)if(t.active&&t.health>0)kill(w,t);w.effects.clear();w.sounds.clear();}
    }
    check(peakGround==64&&peakAir==8&&w.proceduralPopulation.spawned>1000,"Soak did not exercise full caps or repeated recycling");check(kinds==std::set<int>({0,1,2,14,18}),"Spawner types did not rotate through all five enemy families");
    for(const auto& s:w.proceduralPopulation.spawners)check(s.spawned>0,"A spawner starved under shared pool pressure");
    std::cout<<"Ten simulated minutes: "<<w.proceduralPopulation.spawned<<" emissions, 64 ground / 8 air peaks, 72 fixed slots\n";
}
static void safetyAndTiming(){
    auto w=fixture();auto original=w.targets.size();Target obstruction;obstruction.spawnType=-1;obstruction.center={0,20,100};obstruction.half={30,30,200};w.targets.push_back(obstruction);
    advance(w,100);check(counts(w).first==0,"Spawner materialized inside occupied ground site");
    w.targets.back().active=false;w.stepGenerators(.1f);auto [ground,air]=counts(w);check(ground<=2&&ground+air<=4,"Blocked spawners accumulated a catch-up burst");
    advance(w,10);check(counts(w).first>0,"Blocked sites failed to recover after clearing");check(w.targets.size()==original+1,"Retry grew target storage");
    auto near=fixture(1,3,0);near.position={0,0,130};for(auto& s:near.proceduralPopulation.spawners)for(auto& p:s.sites)p.prototype=site(near,p.prototype.spawnType,0,{0,0,130}).prototype;
    advance(near,40);check(near.proceduralPopulation.spawned==0,"Spawner ignored pilot exclusion radius");near.position={0,0,-700};advance(near,6);check(near.proceduralPopulation.spawned>0,"Proximity blocking did not recover");
    auto invalid=fixture();float clock=invalid.proceduralPopulation.spawners[0].clock;
    for(float dt:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})invalid.stepGenerators(dt);
    check(invalid.proceduralPopulation.spawners[0].clock==clock&&invalid.proceduralPopulation.spawned==0,"Invalid time advanced continuous spawns");
    std::set<uint64_t> cadences;
    for(int hz:{60,72,90,120}){auto timed=fixture(1,1,0,1);timed.proceduralPopulation.spawners.resize(1);
        for(int frame=0;frame<60*hz;++frame){timed.stepGenerators(1.f/hz);for(auto& t:timed.targets)if(t.active&&t.health>0)kill(timed,t);timed.effects.clear();timed.sounds.clear();}
        cadences.insert(timed.proceduralPopulation.spawned);
    }
    check(*cadences.begin()>=10&&*cadences.rbegin()-*cadences.begin()<=1,"Reinforcement cadence depends on headset refresh rate");
}
static void progression(){
    auto w=fixture(2,6,2);w.procedural.segmentCount=2;w.procedural.orderedSegments=true;w.procedural.objectives[0].kind=ProceduralGoalKind::Eliminate;w.procedural.objectives[0].killQuota=6;
    w.procedural.requireExtraction=true;w.procedural.extraction={400,0,-700};
    advance(w,15);for(auto& s:w.proceduralPopulation.spawners)if(s.zone==1)check(s.spawned==0,"Future sector emitted before activation");
    for(int i=0;i<400&&!w.procedural.objectives[0].complete;++i){w.stepGenerators(.1f);
        for(auto& t:w.targets)if(t.active&&t.health>0&&t.proceduralZone==0&&t.prototypeObjective){auto before=w.procedural.objectives[0].kills;kill(w,t);unsigned once=w.procedural.objectives[0].kills;w.targetDestroyed(t);check(w.procedural.objectives[0].kills==once&&once<=before+1,"One pooled life counted more than once");}
        w.effects.clear();w.sounds.clear();updateProceduralMission(w,0);
    }
    check(w.procedural.objectives[0].complete&&w.procedural.objectives[0].kills==6,"Continuous elimination quota could not finish");
    uint64_t stopped=0;for(const auto& s:w.proceduralPopulation.spawners)if(s.zone==0)stopped+=s.spawned;
    advance(w,20);uint64_t after=0;bool next=false;for(const auto& s:w.proceduralPopulation.spawners){if(s.zone==0)after+=s.spawned;else next|=s.spawned>0;}
    check(after==stopped&&next,"Cleared sector continued spawning or next sector did not activate");
    auto& recon=w.procedural.objectives[1];w.position=recon.position;for(int i=0;i<42;++i)updateProceduralMission(w,.1f);check(recon.complete&&w.procedural.extracting,"Recon completion/extraction gated incorrectly");
    auto before=w.proceduralPopulation.spawned;advance(w,30);check(w.proceduralPopulation.spawned==before,"Completed objectives still emitted threats during extraction");
    w.effects.clear();w.position=w.procedural.extraction;updateProceduralMission(w,0);check(w.missionStatus==MissionStatus::Complete,"Continuous mission could not extract");advance(w,30);check(w.proceduralPopulation.spawned==before,"Completed mission still emitted enemies");
    auto far=fixture();advance(far,15);check(counts(far).first>0,"No roster for culling check");far.position={4000,0,0};far.stepGenerators(.1f);check(counts(far)==std::pair<unsigned,unsigned>{0,0},"Remote enemies retained shared population slots");auto spawned=far.proceduralPopulation.spawned;advance(far,20);check(far.proceduralPopulation.spawned==spawned,"Remote sector emitted outside activation range");
    far.position={0,0,-700};advance(far,10);check(far.proceduralPopulation.spawned>spawned,"Sector failed to replenish after returning");
    for(auto status:{MissionStatus::Ejecting,MissionStatus::Failed,MissionStatus::Complete}){auto stoppedWorld=fixture();stoppedWorld.missionStatus=status;advance(stoppedWorld,20);check(stoppedWorld.proceduralPopulation.spawned==0,"Inactive mission emitted reinforcements");}
}
int main(){try{validation();cappedSoak();safetyAndTiming();progression();std::cout<<"Continuous pools, variety, safety, time, activation and completion passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
