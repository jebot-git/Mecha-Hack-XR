#include "coop.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace iron;
using namespace iron::coop;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F> void rejects(F f,const char* m){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,m);}
int main(int argc,char** argv){try{
    check(argc==2,"retail path");std::filesystem::path root=argv[1];
    Pilot animated;animated.gaitPhase=4.5f;animated.walkingSpeed=8;Pilot remote;auto packet=pilotWire(animated);pilotRead(packet,remote);check(remote.gaitPhase==4.5f&&remote.walkingSpeed==8,"player locomotion survives snapshots");packet.erase("gait");packet.erase("walk_speed");pilotRead(packet,remote);check(remote.gaitPhase==0&&remote.walkingSpeed==0,"legacy pilot packets retain static fallback");
    World animatedWorld,remoteWorld;animatedWorld.targets.resize(1);remoteWorld.targets.resize(1);animatedWorld.targets[0].ai.gaitPhase=7;animatedWorld.targets[0].ai.velocity={2,0,3};auto snapshot=worldWire(animatedWorld);worldRead(snapshot,remoteWorld);check(remoteWorld.targets[0].ai.gaitPhase==7&&remoteWorld.targets[0].ai.velocity.z==3,"enemy locomotion survives snapshots");snapshot["targets"][0].erase(snapshot["targets"][0].end()-2,snapshot["targets"][0].end());worldRead(snapshot,remoteWorld);check(remoteWorld.targets[0].ai.gaitPhase==0&&length(remoteWorld.targets[0].ai.velocity)==0,"legacy enemy snapshots retain static fallback");
    Room r(root);r.host=1;r.campaign.rules.load(root);for(int i=1;i<=4;++i)r.add(i);
    rejects([&]{r.add(5);},"four player limit");rejects([&]{r.command(2,{{"op","select"},{"mission",0}});},"host selection only");
    rejects([&]{r.command(1,{{"op","select"},{"mission",12}});},"locked mission");
    rejects([&]{r.command(1,{{"op","difficulty"},{"value","insane"}});},"difficulty whitelist");
    r.command(1,{{"op","select"},{"mission",0}});check(r.phase==Phase::Briefing,"briefing broadcast phase");
    auto loadout=r.players[0].loadout;rejects([&]{r.command(2,{{"op","ready"},{"selection",0},{"ready",true},{"loadout",loadout}});},"stale readiness rejected");auto illegal=loadout;illegal[LeftShoulder]=Weapon::Cruise;
    rejects([&]{r.command(2,{{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",illegal}});},"shared locked loadouts");
    for(int i=1;i<=3;++i)r.command(i,{{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",loadout}});
    check(r.phase==Phase::Briefing,"all players must ready");r.command(4,{{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",loadout}});
    check(r.phase==Phase::Playing&&r.round==1&&r.world.difficulty==World::Difficulty::Hard,"hard cooperative deployment");
    Input moving;moving.forward=1;r.command(2,{{"op","input"},{"round",0},{"input",inputWire(moving)}});check(r.player(2).inputAge==100,"stale round input discarded");
    rejects([&]{r.command(2,{{"op","restart"}});},"client cannot restart");rejects([&]{r.command(2,{{"op","abort"}});},"client cannot abort");
    r.command(2,{{"op","destroy"},{"round",r.round}});check(r.player(2).pilot.lifeSpent,"self destruction");
    for(int i=0;i<425;++i)r.step(tick);check(r.player(2).pilot.ejectionTime>=7&&r.phase==Phase::Playing,"ejection leaves others active");
    r.world.counters[0]=0;r.world.missionFlags|=0x80000000u;r.step(tick);
    check(r.phase==Phase::Complete&&r.campaign.completed==1&&r.campaign.weaponAvailable(Weapon::Grenade),"team victory includes ejected member and shared reward");
    r.command(1,{{"op","restart"}});check(r.round==2&&!r.player(2).pilot.lifeSpent,"host restart fresh lives");r.command(2,{{"op","destroy"},{"round",1}});check(!r.player(2).pilot.lifeSpent,"stale self destruction cannot affect restarted mission");
    r.command(2,{{"op","destroy"},{"round",r.round}});r.world.counters[0]=0;r.world.missionFlags|=0x80000000u;r.step(tick);check(r.phase==Phase::Complete&&r.player(2).pilot.ejectionTime<7,"victory during ejection");
    for(int i=0;i<425;++i)r.step(tick);check(r.player(2).pilot.ejectionTime>=7,"ejection finishes after team result");
    r.command(1,{{"op","restart"}});for(int id=1;id<=4;++id)r.command(id,{{"op","destroy"},{"round",r.round}});for(int i=0;i<425;++i)r.step(tick);check(r.phase==Phase::Selecting&&r.outcome=="MISSION FAILED - ALL PILOTS LOST"&&r.campaign.completed==1,"team wipe returns to selection without losing prior wins");
    for(auto& p:r.players)check(!p.ready&&p.pilot.ejectionTime>=7,"wipe waits for ejections and clears readiness");
    uint64_t selectionBeforeAbort=r.selection;
    r.command(1,{{"op","abort"}});check(r.phase==Phase::Selecting&&r.campaign.completed==1&&r.selection>selectionBeforeAbort&&r.outcome=="MISSION ABORTED","abort retains progress and invalidates old ready packets");
    r.command(1,{{"op","select"},{"mission",1}});auto staleSelection=r.selection;
    r.command(1,{{"op","abort"}});r.command(1,{{"op","select"},{"mission",0}});
    rejects([&]{r.command(2,{{"op","ready"},{"selection",staleSelection},{"ready",true},{"loadout",loadout}});},"aborted mission readiness cannot launch new selection");
    r.command(1,{{"op","select"},{"mission",1}});for(int id=1;id<=4;++id)r.command(id,{{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",loadout}});
    r.world.elapsed=480;r.step(tick);r.step(tick);check(r.phase==Phase::Selecting&&r.outcome=="MISSION FAILED","objective timeout also returns team to selection");
    r.start();r.world.elapsed=12;auto previousRound=r.round;r.player(2).pilot.health=67;
    if(!r.world.targets.empty()){r.world.targets.front().ai.playerTarget=2;r.world.targets.front().ai.retarget=5;}
    r.remove(1);check(r.host==2&&r.phase==Phase::Playing&&r.round==previousRound&&r.world.elapsed==12&&r.player(2).pilot.health==67,"host migration preserves mission clock, round and pilot health");
    if(!r.world.targets.empty())check(r.world.targets.front().ai.playerTarget==-1&&r.world.targets.front().ai.retarget==0,"departures invalidate enemy roster indices");
    World w;w.targets.clear();w.cooperative=true;w.missionStatus=MissionStatus::Active;w.health=50;w.playerSlot=0;w.configure(loadout);w.ammo[RightHand]=0;w.pickups.push_back({w.position,0});
    w.collectPickups();check(w.health==57.5f&&w.ammo[RightHand]==50,"half pickup refill");w.collectPickups();check(w.health==57.5f,"pickup once per player");w.playerSlot=1;w.health=50;w.ammo[RightHand]=0;w.collectPickups();check(w.health==57.5f&&w.ammo[RightHand]==50&&w.pickups[0].collectedBy==3,"instanced pickup for second player");
    Pilot shooter,victim;shooter.playerSlot=0;shooter.position={0,0,10};victim.playerSlot=1;victim.position={0,0,0};victim.missionStatus=MissionStatus::Active;w.teammates={&shooter,&victim};w.projectiles.clear();Projectile bullet{{0,6,5},{0,0,-100},1,25,RightHand};bullet.shooter=0;w.projectiles.push_back(bullet);w.stepProjectiles(.1f);check(victim.health==75,"always on friendly projectile damage");
    static_cast<Pilot&>(w)=shooter;w.teammates={&w,&victim};w.explode(victim.position+Vec3{0,6,0},3,25,true);check(victim.health==50,"friendly melee/explosion damage");
    w.targets.clear();Target t;t.spawnType=0;t.center={0,0,0};w.targets.push_back(t);shooter.position={10,0,0};victim.position={100,0,0};w.teammates={&shooter,&victim};int near=0,far=0;
    for(int i=0;i<1000;++i){w.targets[0].ai.retarget=0;w.selectPlayerTargets(.1f);int target=w.targets[0].ai.playerTarget;near+=target==0;far+=target==1;}
    check(near>far*10&&far>0,"random target choice weighted by proximity");shooter.lifeSpent=true;w.targets[0].ai.retarget=0;w.selectPlayerTargets(.1f);check(w.targets[0].ai.playerTarget==1,"dead excluded from targeting");
    World fxCopy;fxCopy.targets=w.targets;w.effect(WeaponEffect::Blast,{1,2,3},{},Weapon::Rocket,6);w.effects.back().model=0x368d4;worldRead(worldWire(w),fxCopy);check(fxCopy.effects.size()==w.effects.size()&&fxCopy.effects.back().weapon==Weapon::Rocket&&fxCopy.effects.back().model==0x368d4,"weapon effects survive cooperative snapshot");
    Progression full;full.completed=65535;Loadout duplicated{};duplicated[2]=duplicated[3]=Weapon::Rail;check(!validLoadout(duplicated,full),"server rejects duplicate rail");duplicated[2]=duplicated[3]=Weapon::Cruise;check(validLoadout(duplicated,full),"server allows twin cruise launchers");
    for(int hand=LeftHand;hand<=RightHand;++hand){
        Loadout pair{};pair[hand]=Weapon::Rifle;pair[1-hand]=Weapon::Cutter;
        check(validLoadout(pair,full),"server accepts rifle and cutter in either hand arrangement");
        Room mixed(root);mixed.host=1;mixed.campaign.rules.load(root);mixed.campaign.completed=65535;mixed.add(1);mixed.add(2);
        mixed.command(1,{{"op","select"},{"mission",0}});
        for(int id:{1,2})mixed.command(id,{{"op","ready"},{"selection",mixed.selection},{"ready",true},{"loadout",pair}});
        check(mixed.phase==Phase::Playing&&mixed.player(1).pilot.weapons==pair&&mixed.player(2).pilot.weapons==pair,"co-op ready and deployment preserve both handheld weapons");
        pair[hand]=Weapon::Cutter;check(!validLoadout(pair,full),"server retains cutter inventory limit");
    }
    World normal,hard;normal.loadMission(root,0);hard.difficulty=World::Difficulty::Hard;hard.loadMission(root,0);check(hard.difficultyValues[4]==normal.difficultyValues[4]*2,"early hard cartridge multiplier");normal.loadMission(root,8);hard.loadMission(root,8);check(hard.difficultyValues[4]==normal.difficultyValues[4]*1.5f,"late hard cartridge multiplier");
    Input lost;lost.forward=1;lost.handPoses[0].valid=false;lost.handPoses[0].grip={0,0,0,0};auto tracked=inputRead(inputWire(lost));check(tracked.forward==1&&!tracked.handPoses[0].valid,"lost controller does not disable locomotion");
    auto bad=inputWire(Input{});bad["axes"][0]=1e30;rejects([&]{inputRead(bad);},"unbounded input rejected");
    Server server(root,"127.0.0.1",0);Progression campaign;Client host("127.0.0.1:"+std::to_string(server.port()),true,campaign,"","Test Host");
    std::vector<std::unique_ptr<Client>> clients;
    const Input* hostInput=nullptr;
    auto pump=[&](int count){for(int i=0;i<count;++i){host.update(tick,hostInput);for(auto& c:clients)c->update(tick);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    pump(20);check(host.code.size()==6&&host.id!=0,"ENet lobby creation");check(host.local()->at("name")=="Test Host","configured player name replicated by server");
    for(int i=0;i<3;++i)clients.push_back(std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),false,campaign,host.code));pump(30);
    check(host.state.at("players").size()==4,"four actual ENet clients");for(auto& c:clients)check(c->phase()==Phase::Selecting,"clients standby");
    auto idle=[&](const char* message){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(12);while(std::chrono::steady_clock::now()<end)pump(1);check(host.connected&&host.error.empty(),message);for(auto& c:clients)check(c->connected&&c->error.empty(),message);};
    idle("selection stays connected without any user input beyond old watchdog");
    host.send({{"op","select"},{"mission",0}});pump(20);for(auto& c:clients)check(c->phase()==Phase::Briefing,"clients receive briefing");
    idle("briefing stays connected without any user input beyond old watchdog");
    host.send({{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",loadout}});for(auto& c:clients)c->send({{"op","ready"},{"selection",r.selection},{"ready",true},{"loadout",loadout}});pump(30);
    check(host.phase()==Phase::Playing,"ENet ready starts mission");World view;bool applied=false;
    for(int wait=0;wait<200&&!applied;++wait){pump(1);applied=host.apply(view,root,ArmRig{});}
    check(applied&&view.cooperative&&view.mission==0,"world snapshot applied");
    check(view.mapPilots.size()==4,"all four minimap pilots replicated");
    Input ping;ping.ping[0]=ping.ping[1]=true;hostInput=&ping;pump(20);host.apply(view,root,ArmRig{});
    for(int wait=0;wait<200&&(view.selfPingUntil<=view.elapsed||view.aimPingUntil<=view.elapsed);++wait){pump(1);host.apply(view,root,ArmRig{});}
    check(view.selfPingUntil>view.elapsed&&view.aimPingUntil>view.elapsed,"both reliable grip pings reach snapshot");
    World teammateView;
    for(int wait=0;wait<200;++wait){pump(1);clients[0]->apply(teammateView,root,ArmRig{});if(teammateView.mapPilots.size()==4&&teammateView.mapPilots[0].selfPingUntil>teammateView.elapsed)break;}
    check(teammateView.mapPilots.size()==4&&teammateView.mapPilots[0].selfPingUntil>teammateView.elapsed,"other players receive sender highlight");
    float until=view.selfPingUntil;pump(60);host.apply(view,root,ArmRig{});
    check(view.selfPingUntil==until,"held grip does not repeatedly ping beyond rate limit");hostInput=nullptr;
    host.send({{"op","ping"},{"round",0},{"kind",0},{"input",inputWire(Input{})}});pump(10);host.apply(view,root,ArmRig{});
    check(view.selfPingUntil==until,"stale round ping rejected");
    clients[0]->send({{"op","destroy"}});pump(10);check(clients[0]->local()->at("pilot").at("dead"),"network self destruction authoritative");
    host.send({{"op","restart"}});pump(20);check(host.round==2&&!clients[0]->local()->at("pilot").at("dead").get<bool>(),"network restart resets all");
    clients.pop_back();pump(20);check(host.state.at("players").size()==3,"disconnect removes member");
    clients.push_back(std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),false,campaign,host.code));pump(20);
    check(clients.back()->error=="Join while host is selecting or briefing a mission"&&clients.back()->id==0,"joining active mission is explicitly disabled");check(host.phase()==Phase::Playing&&host.state.at("players").size()==3,"rejected late join leaves mission and roster intact");clients.pop_back();pump(10);
    host.send({{"op","destroy"}});for(auto& c:clients)c->send({{"op","destroy"}});pump(425);
    check(host.phase()==Phase::Selecting&&host.state.at("outcome")=="MISSION FAILED - ALL PILOTS LOST","network team wipe returns host to selection");for(auto& c:clients)check(c->phase()==Phase::Selecting,"network team wipe returns every client to standby");
    host.send({{"op","abort"}});pump(20);check(host.phase()==Phase::Selecting,"network abort returns to selection");for(auto& c:clients)check(c->phase()==Phase::Selecting&&c->state.at("outcome")=="MISSION ABORTED","network abort returns every client to standby");
    auto code=host.code;auto newHost=clients.front()->id;auto progress=host.state.at("completed");
    host.send({{"op","leave"}});pump(20);for(auto& c:clients)check(c->connected&&c->state.at("host")==newHost&&c->code==code&&c->state.at("completed")==progress,"host departure preserves lobby and campaign");
    check(clients.front()->hosting&&!clients.back()->hosting,"clients learn authoritative host role");
    clients.back()->send({{"op","select"},{"mission",0}});pump(10);check(clients.back()->error=="Only host selects missions between deployments","remaining guest cannot select mission");clients.back()->error.clear();
    clients.front()->send({{"op","select"},{"mission",0}});pump(20);check(clients.front()->phase()==Phase::Briefing,"promoted host can select mission");
    clients.push_back(std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),false,campaign,code));pump(20);check(clients.back()->id&&clients.front()->state.at("players").size()==3,"same code accepts new members after handover");
    for(auto& c:clients)c->send({{"op","ready"},{"ready",true},{"loadout",loadout}});pump(30);auto deployedRound=clients.front()->round;
    check(clients.front()->phase()==Phase::Playing,"migrated lobby deploys");
    clients.front()->send({{"op","leave"}});pump(20);check(clients[1]->connected&&clients[1]->hosting&&clients[1]->phase()==Phase::Playing&&clients[1]->round==deployedRound,"host handover preserves an active deployment");
    World migrated;check(clients[1]->apply(migrated,root,ArmRig{})&&migrated.mapPilots.size()==2,"remaining players receive active world after handover");
    clients.erase(clients.begin());clients.front()->send({{"op","abort"}});pump(20);check(clients.front()->phase()==Phase::Selecting,"new active host can abort");
    clients.front()->send({{"op","leave"}});pump(20);check(clients.back()->connected&&clients.back()->hosting&&server.report().at("rooms").size()==1,"last player retains room");
    clients.back()->send({{"op","leave"}});pump(20);check(server.report().at("rooms").empty(),"room retires only after last player leaves");
    // A process disappearing without Leave must hand over through ENet's real timeout.
    auto lostHost=std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),true,campaign);clients.clear();
    for(int i=0;i<30;++i){lostHost->update(tick);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    clients.push_back(std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),false,campaign,lostHost->code));
    for(int i=0;i<30;++i){lostHost->update(tick);pump(1);}
    check(clients.front()->id!=0,"timeout test survivor admitted");
    auto disconnectDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(16);
    while(!clients.front()->hosting&&std::chrono::steady_clock::now()<disconnectDeadline)pump(1);
    check(clients.front()->connected&&clients.front()->hosting&&clients.front()->state.at("players").size()==1,"unresponsive host hands over through actual ENet disconnect");
    clients.front()->send({{"op","leave"}});pump(20);check(server.report().at("rooms").empty(),"timed-out host leaves no orphan lobby");lostHost.reset();clients.clear();
    auto stoppedServer=std::make_unique<Server>(root,"127.0.0.1",0);Client watchdog("127.0.0.1:"+std::to_string(stoppedServer->port()),true,campaign);
    for(int i=0;i<30;++i){watchdog.update(tick);stoppedServer->step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(watchdog.id!=0,"watchdog test connected");stoppedServer.reset();
    for(int i=0;i<12;++i)watchdog.update(1);check(!watchdog.connected&&!watchdog.error.empty(),"a vanished server still times out");watchdog.error.clear();
    for(int i=0;i<20;++i)watchdog.update(1);check(watchdog.error.empty(),"terminal disconnect does not repeatedly overwrite recovery screen");
    std::cout<<"Cooperative simulation, authority, progression and four-client ENet checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
