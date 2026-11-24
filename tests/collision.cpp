#include "simulation.hpp"
#include <iostream>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
using namespace iron;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static Target target(int type,Vec3 p={1.7f,.5f,0}){Target t;t.spawnType=type;t.center=t.origin=p;t.half={.4f,.5f,.4f};t.health=2000;return t;}
int main(int argc,char** argv){try{
for(int game=0;game<2;++game){
    World w;w.game=game;w.position={};w.targets={target(0),target(3),target(10),target(0),target(8),target(2),target(0,{1.7f,4,0}),target(0,{7,.5f,0})};w.targets[3].parameters=64;
    w.stomp(0);for(int i:{0,4,5})check(w.targets[i].health==1000,"eligible ground unit receives exactly 10000 raw / 1000 native damage");
    for(int i:{1,2,3,6,7})check(w.targets[i].health==2000,"mech/fortress/heavy/airborne/out-of-foot actors not stomped");check(w.health==100,"stomp does not damage pilot");
    w.targets={target(0,{-1.7f,.5f,0})};w.stomp(0);check(w.targets[0].health==2000,"opposite foot has separate contact volume");w.stomp(1);check(w.targets[0].health==1000,"left foot damages left-side unit");
    w.targets={target(-1)};w.targets[0].type=6;w.stomp(0);check(w.targets[0].health==1000,"small scenery uses cartridge crush lookup");w.targets[0].type=0;w.stomp(0);check(w.targets[0].health==1000,"building excluded by cartridge lookup");
    w.targets={target(0)};w.targets[0].health=100;w.targets[0].protectedTarget=true;w.stomp(0);w.stomp(0);check(w.destroyed==1,"protected targets may be crushed but a dead target only counts once");
    w.targets={target(0)};w.targets[0].invulnerable=true;w.stomp(0);check(w.targets[0].health==2000,"invulnerable transport cannot be crushed");
    World gait;gait.game=game;gait.targets.clear();gait.position={};
    for(int i=0;i<28;++i)gait.movePilot(gait.position+Vec3{0,0,-.1f});check(gait.sounds.empty(),"no damage event before original gait phase three");
    gait.movePilot(gait.position+Vec3{0,0,-.2f});check(gait.sounds.size()==1,"first step crossing generates one stomp");
    for(int i=0;i<100;++i)gait.movePilot(gait.position);check(gait.sounds.size()==1,"standing still never generates repeated contact damage");
    for(int i=0;i<86;++i)gait.movePilot(gait.position+Vec3{0,0,-.1f});check(gait.sounds.size()==2,"second footfall at phase twelve");
    World fast,slow;fast.targets.clear();slow.targets.clear();fast.position=slow.position={};for(int i=0;i<100;++i)fast.movePilot(fast.position+Vec3{0,0,-.1f});for(int i=0;i<400;++i)slow.movePilot(slow.position+Vec3{0,0,-.025f});check(fast.sounds.size()==slow.sounds.size(),"stomp cadence independent of frame rate");
    World blocked;blocked.position={};blocked.targets={target(-1,{0,5,-3})};blocked.targets[0].half={5,5,3};
    for(int i=0;i<200;++i)blocked.movePilot(blocked.position+Vec3{0,0,-.1f});check(blocked.sounds.empty(),"pushing against scenery does not produce phantom footsteps");
    World reverse;reverse.targets.clear();reverse.position={};for(int i=0;i<50;++i)reverse.movePilot(reverse.position+Vec3{0,0,.1f});check(reverse.sounds.size()==1,"reverse movement crosses frame twelve correctly");
}
check(argc==2,"assets argument");
for(int game=0;game<2;++game){World w;w.loadMission(argv[1],0,game);w.targets.clear();w.generators.clear();w.position={};w.chassisYaw=0;w.counters[1]=2;auto t=target(-1);t.type=6;t.health=50;t.action=0x80020001;t.ai.maxHealth=50;w.targets.push_back(t);w.stomp(0);w.stepMission(.01f);check(w.targets[0].processed&&!w.effects.empty(),"stomp death enters original objective/destruction dispatch");check(w.counters[1]==1,"stomp dispatches original objective counter action");w.stepMission(.01f);check(w.counters[1]==1,"stomp objective action dispatched only once");}
// Test eligibility against the actual cartridge bytes, then approach real
// authored scenery through movement, rather than teleporting a foot into it.
unsigned sceneryKinds=0;
for(int game=0;game<2;++game){
    auto retail=std::filesystem::path(argv[1]);auto module=game?retail.parent_path()/"is2-research/blocks/26.bin":retail/"blocks/07.bin";
    std::ifstream input(module,std::ios::binary);std::vector<unsigned char> raw{std::istreambuf_iterator<char>(input),{}};
    check(!raw.empty(),"verified local cartridge module available for source eligibility check");
    size_t table=game?0x874dc-0xb000:0x36464-0x6000;World eligibility;eligibility.game=game;
    for(int type=0;type<(game?49:34);++type){auto t=target(-1);t.type=type;check(eligibility.crushable(t)==(raw.at(table+type)!=0),"every static type matches source stomp lookup");}
    std::map<int,Target> samples;
    for(int mission=0;mission<(game?20:16);++mission){World authored;authored.loadMission(retail,mission,game);for(const auto& t:authored.targets)if(t.spawnType<0&&authored.crushable(t)&&!samples.contains(t.type))samples.emplace(t.type,t);}
    for(auto& [type,authored]:samples)for(bool cooperative:{false,true}){
        World walk;walk.game=game;walk.cooperative=cooperative;if(cooperative)walk.teammates={&walk};
        Target scenery=authored;auto delta=Vec3{1.7f-scenery.center.x,0,-scenery.center.z};scenery.center=scenery.center+delta;scenery.origin=scenery.origin+delta;walk.targets={scenery};
        walk.position={0,0,scenery.half.z+3.4f};float goal=-scenery.half.z-5;
        for(int frame=0;frame<2000&&walk.position.z>goal;++frame){walk.movePilot(walk.position+Vec3{0,0,-.1f});walk.stepProjectiles(.0125f);}
        if(walk.targets[0].health>0)std::cerr<<"unreachable scenery game="<<game<<" type="<<type<<" position="<<walk.position.z<<" health="<<walk.targets[0].health<<'\n';
        check(walk.position.z<=goal&&walk.targets[0].health<=0,"walking can reach and stomp real crushable scenery without torso lockout");
        check(walk.targets[0].processed,"crushed authored scenery enters normal death/wreck dispatch");
        if(!cooperative)++sceneryKinds;
    }
}
std::cout<<"IS1/IS2 stomp rules for all 83 static types and walking destruction of "<<sceneryKinds<<" placed scenery kinds pass in solo/co-op\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
