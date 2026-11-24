#include "coop.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
using namespace iron;
static void kill(World& w,Target& t){t.health=0;w.targetDestroyed(t);}
int main(int argc,char** argv){assert(argc==2);std::filesystem::path root=argv[1];
    CampaignRules rules;rules.load(root,1);assert(rules.count()==20&&rules.fullMask()==0xfffff&&rules.available(0)==15);
    Progression progress;progress.rules=rules;
    assert(progress.weaponAvailable(Weapon::Cutter)&&progress.weaponAvailable(Weapon::Grenade)&&!progress.weaponAvailable(Weapon::Gatling));
    for(int i=0;i<20;++i){assert(progress.complete(i));}assert(progress.finished()&&rules.weapons(progress.completed)==8191);
    auto save=std::filesystem::temp_directory_path()/"iron-is2-progression-test.json";progress.save(save,false);Progression restored;restored.rules=rules;restored.load(save);assert(restored.completed==0xfffff);bool wrong=false;try{Progression base;base.load(save);}catch(...){wrong=true;}assert(wrong);std::filesystem::remove(save);std::filesystem::remove(save.string()+".lock");
    Frontend menu;Input input;menu.game=1;menu.update(input,.02f,true,false);assert(menu.screen==Screen::Campaign&&menu.action==MenuAction::None);menu.update(input,.02f,false,false);menu.update(input,.02f,true,false);assert(menu.game==1&&menu.action==MenuAction::Singleplayer);
    for(int m=0;m<20;++m){World w;w.loadMission(root,m,1);assert(w.game==1&&w.mission==m&&!w.geometry().terrain.empty());w.stepMission(.02f);assert(w.missionStatus==MissionStatus::Active);
        for(auto& t:w.targets)if(t.mesh)for(auto& v:*t.mesh){assert(std::isfinite(length(v.position)));if(v.textured)assert(v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1);}
        World fail=w;
        // Feed the decoded objective events; unrelated enemies are left alive.
        if(m==0||m==1||m==7||m==11||m==14||m==17)for(auto& t:w.targets)if(t.spawnType<0&&!t.protectedTarget)kill(w,t);
        if(m==2||m==12){for(auto& t:w.targets)if(t.tag==22)kill(w,t);for(auto& p:w.pickups)if(p.kind==22)p.position=w.position;w.collectPickups();}
        if(m==3)for(auto& t:w.targets)if(t.spawnType==14&&!t.generated)kill(w,t);
        if(m==4||m==8||m==18){for(auto& t:w.targets)if(t.spawnType==8){w.arrivedEnemy(t);t.escaped=true;}w.evaluateMission();assert(w.missionFlags&(1u<<30));}
        if(m==5){int n=0;for(auto& t:w.targets)if(t.spawnType==17&&n++<9)kill(w,t);}
        if(m==6||m==15)for(auto& t:w.targets)if(t.spawnType==9)kill(w,t);
        if(m==7)for(auto& t:w.targets)if(t.spawnType==18)kill(w,t);
        if(m==9)for(auto& t:w.targets)if(t.spawnType==8)kill(w,t);
        if(m==10){w.elapsed=300;for(auto& t:w.targets)if(t.spawnType==0||t.spawnType==11||t.spawnType==14)kill(w,t);for(auto& g:w.generators)g.remaining=0;}
        if(m==13)for(auto& t:w.targets)if(t.spawnType==3)kill(w,t);
        if(m==16){for(int n=0;n<19;++n)w.pickups.push_back({w.position,22});w.collectPickups();}
        if(m==19)for(auto& t:w.targets)if(t.spawnType==19)kill(w,t);
        w.effects.clear();w.evaluateMission();if(w.missionStatus!=MissionStatus::Complete){std::cerr<<"IS2 objective "<<m+1<<" failed\n";return 1;}
        if(m==3||m==6||m==13){for(auto& t:fail.targets)if(t.protectedTarget&&t.spawnType<0)kill(fail,t);fail.evaluateMission();assert(fail.missionStatus==MissionStatus::Failed);}
        if(m==4||m==8||m==18){for(auto& t:fail.targets)if(t.spawnType==8)kill(fail,t);fail.missionFlags|=1u<<30;fail.evaluateMission();assert(fail.missionStatus==MissionStatus::Failed);}
        if(m==5){int n=0;for(auto& t:fail.targets)if(t.spawnType==17&&n++<4)fail.arrivedEnemy(t);fail.evaluateMission();assert(fail.missionStatus==MissionStatus::Failed);}
        if(m==9){auto it=std::find_if(fail.targets.begin(),fail.targets.end(),[](auto& t){return t.spawnType==8;});fail.arrivedEnemy(*it);fail.evaluateMission();assert(fail.missionStatus==MissionStatus::Failed);}
        if(m==15){fail.elapsed=720.1f;fail.evaluateMission();assert(fail.missionStatus==MissionStatus::Failed);}
    }
    World drops;drops.loadMission(root,10,1);assert(drops.cargoFlights.size()>=4);auto& flight=drops.cargoFlights.front();auto index=flight.carrier;drops.targets[index].active=true;drops.targets[index].origin.y=30;drops.dropCargo(index);auto slot=*std::find_if(flight.slots.begin(),flight.slots.end(),[&](size_t i){return drops.targets[i].active;});assert(drops.targets[slot].ai.airborne);auto mesh=drops.targets[slot].mesh;for(size_t i=0;i<drops.targets.size();++i)if(i!=slot){drops.targets[i].active=false;drops.targets[i].spawnFlags|=2;}drops.position=drops.targets[slot].origin;for(int i=0;i<400&&drops.targets[slot].ai.airborne;++i)drops.stepEnemies(.02f);assert(!drops.targets[slot].ai.airborne&&drops.targets[slot].mesh==mesh);
    for(int type:{11,14,18,19,22}){World actor;actor.game=1;actor.mission=-1;actor.missionStatus=MissionStatus::Active;actor.position={0,0,0};actor.targets.clear();Target t;t.spawnType=type;t.origin=t.center={0,6,-40};t.half={1,1,1};t.yaw=t.ai.weaponYaw=pi;t.ai.maxHealth=t.health=100;t.speed=10;actor.targets.push_back(t);actor.stepEnemies(.02f);assert(!actor.projectiles.empty());}
    World transport;transport.loadMission(root,15,1);auto pad=std::find_if(transport.targets.begin(),transport.targets.end(),[](auto& t){return t.spawnType==20;});assert(pad!=transport.targets.end()&&pad->route.size()==1&&pad->invulnerable);for(auto& t:transport.targets){t.active=false;t.spawnFlags|=2;}pad->active=true;transport.position=pad->origin;Vec3 destination=pad->route.front();for(int n=0;n<200;++n)transport.step(Input{},.05f,false);assert(length(transport.position-destination)<.01f);
    coop::Room room(root);room.campaign.rules.load(root,1);room.host=1;room.add(1);room.add(2);room.command(1,{{"op","select"},{"mission",0}});
    Loadout loadout{Weapon::Cutter,Weapon::Rifle,Weapon::Empty,Weapon::Empty,Weapon::Grenade,Weapon::Empty};
    assert(coop::validLoadout(loadout,room.campaign));auto locked=loadout;locked[LeftShoulder]=Weapon::Gatling;assert(!coop::validLoadout(locked,room.campaign));
    room.command(1,{{"op","ready"},{"selection",room.selection},{"ready",true},{"loadout",loadout}});assert(room.phase==coop::Phase::Briefing);room.command(2,{{"op","ready"},{"selection",room.selection},{"ready",true},{"loadout",loadout}});assert(room.world.game==1&&room.state().at("campaign")==1&&room.player(1).pilot.weapons==loadout);World client;client.loadMission(root,0,1);coop::worldRead(coop::worldWire(room.world),client);assert(client.targets.size()==room.world.targets.size());
    coop::Server server(root,"127.0.0.1",0);std::string endpoint="127.0.0.1:"+std::to_string(server.port());coop::Client host(endpoint,true,progress);std::vector<std::unique_ptr<coop::Client>> clients;
    auto pump=[&](int n){for(int i=0;i<n;++i){host.update(coop::tick);for(auto& client:clients)client->update(coop::tick);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    pump(30);assert(!host.code.empty());for(int i=0;i<3;++i)clients.push_back(std::make_unique<coop::Client>(endpoint,false,Progression{},host.code));pump(30);assert(host.state.at("campaign")==1&&host.state.at("completed")==0xfffff);for(auto& client:clients)assert(client->phase()==coop::Phase::Selecting&&client->state.at("campaign")==1);
    host.send({{"op","select"},{"mission",19}});pump(30);auto selection=host.state.at("selection");for(auto& client:clients)assert(client->phase()==coop::Phase::Briefing&&client->state.at("mission")==19);
    host.send({{"op","ready"},{"selection",selection},{"ready",true},{"loadout",loadout}});for(auto& client:clients)client->send({{"op","ready"},{"selection",selection},{"ready",true},{"loadout",loadout}});pump(40);assert(host.phase()==coop::Phase::Playing);World view;bool applied=false;for(int n=0;n<100&&!applied;++n){pump(1);applied=clients[0]->apply(view,root,ArmRig{});}assert(applied&&view.game==1&&view.mission==19);
    host.send({{"op","abort"}});pump(30);for(auto& client:clients)assert(client->phase()==coop::Phase::Selecting&&client->state.at("campaign")==1);
    std::cout<<"20 IS2 maps, objective events/failures, air drops, save isolation and host campaign readiness passed\n";
}
