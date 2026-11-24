#include "coop.hpp"
#include "net/snapshot_packet.hpp"
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <thread>
using namespace iron;using namespace iron::coop;
void check(bool pass,const char* why){if(!pass)throw std::runtime_error(why);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(...){rejected=true;}check(rejected,"invalid state accepted");}
void workers(){
    rejects([]{RoomWorkers invalid(0);});rejects([]{RoomWorkers invalid(17);});RoomWorkers pool(4);
    std::barrier gate(4);std::atomic<unsigned> completed=0;std::vector<std::function<void()>> jobs;
    for(unsigned i=0;i<32;++i)jobs.push_back([&,i]{if(i<4)gate.arrive_and_wait();++completed;});pool.run(jobs);check(completed==32,"room jobs did not complete in parallel");
    jobs.assign(32,[&]{++completed;});jobs[0]=[]{throw std::runtime_error("room failed");};completed=0;rejects([&]{pool.run(jobs);});check(completed==31,"failed job prevented other rooms from completing");
    jobs.assign(32,[&]{++completed;});pool.run(jobs);check(completed==63,"pool did not recover after exception");jobs.push_back([]{});rejects([&]{pool.run(jobs);});
}
void packets(){
    Json j={{"type","state"},{"rows",Json::array()}};for(int i=0;i<300;++i)j["rows"].push_back(Json::array({i,1,2,3,4,5,6,"repeated"}));
    auto packet=encodeSnapshotPacket(j);check(packet.size()<Json::to_cbor(j).size(),"snapshot not compressed");check(decodeStatePacket(packet)==j,"compressed snapshot differs");
    check(decodeStatePacket(Json::to_cbor(j))==j,"legacy CBOR no longer accepted");rejects([&]{decodeStatePacket(packet,maxStateBytes,false);});
    auto bad=packet;bad.resize(8);rejects([&]{decodeStatePacket(bad);});bad=packet;bad[4]=9;rejects([&]{decodeStatePacket(bad);});bad=packet;bad[5]=0xff;rejects([&]{decodeStatePacket(bad);});bad=packet;bad.resize(10);rejects([&]{decodeStatePacket(bad);});
    Json deep=0;for(int i=0;i<70;++i)deep=Json::array({deep});rejects([&]{decodeStatePacket(Json::to_cbor(Json{{"deep",deep}}));});
    std::vector<uint8_t> duplicate{0xa2,0x61,'x',0x01,0x61,'x',0x02};rejects([&]{decodeStatePacket(duplicate);});
    std::vector<uint8_t> huge{0xa1,0x61,'x',0x9a,0xff,0xff,0xff,0xff};rejects([&]{decodeStatePacket(huge);});
}
void snapshots(){
    World w;w.targets.resize(4);w.targets[0].center={3,4,5};w.targets[1].ai.velocity={2,0,3};auto base=captureBaseline(w);auto received=readBaseline(Json::from_cbor(Json::to_cbor(base.wire())));
    auto badBase=base.wire();badBase["hash"]=base.hash+1;rejects([&]{readBaseline(badBase);});
    World dense=w,sparse=w,expected=w;sparse.targets[0].origin={999,999,999};
    w.targets[1].health=20;w.targets[2].active=false;w.projectiles.push_back({{1,10,3},{2,0,4},2,25,RightHand});w.effects.push_back({WeaponEffect::Smoke,{1,2,3},{},Weapon::Rocket});w.pickups.push_back({{1,2,3},0,false,2});w.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),{1,2,3}});
    worldRead(worldWire(w),expected);compactRead(compactWorld(w),dense);compactRead(compactWorld(w,&base),sparse,&received);
    check(worldWire(dense)==worldWire(expected)&&worldWire(sparse)==worldWire(expected),"compact world changed values");
    // Drop arbitrary intervening updates and restore a field to baseline.
    w.sounds.clear();w.targets[1].health=100;w.targets[3].origin={4,5,6};w.elapsed=7;expected.sounds.clear();sparse.sounds.clear();worldRead(worldWire(w),expected);auto packet=compactWorld(w,&base);compactRead(packet,sparse,&received);check(worldWire(sparse)==worldWire(expected),"snapshot loss recovery failed");
    auto before=worldWire(sparse);auto malformed=packet;malformed[3].push_back(Json::array({9999,1,0}));rejects([&]{compactRead(malformed,sparse,&received);});check(worldWire(sparse)==before,"malformed packet changed world");
    malformed=packet;malformed[2]=base.hash+1;rejects([&]{compactRead(malformed,sparse,&received);});check(worldWire(sparse)==before,"wrong baseline changed world");
    malformed=packet;malformed[4][0][5]=1e100;rejects([&]{compactRead(malformed,sparse,&received);});check(worldWire(sparse)==before,"noninteger enum changed world");
    malformed=packet;malformed[4][0][2]=std::numeric_limits<double>::infinity();rejects([&]{compactRead(malformed,sparse,&received);});check(worldWire(sparse)==before,"nonfinite packet changed world");

    World combat;
    combat.rpgPrototype=true;
    combat.elapsed=1;
    combat.targets.resize(1);
    combat.targets[0].ai.combatMotion.play(EnemyAction::MeleeWindup,1,1,MechaTechnique::RightCut);
    combat.projectiles.resize(4);
    for(size_t i=0;i<combat.projectiles.size();++i)combat.projectiles[i].visual=ProjectileVisual(i);
    auto packetJson=worldWire(combat);
    check(!packetJson["projectiles"][0].contains("visual"),"standard projectile grew a visual field");
    World client;client.rpgPrototype=true;client.targets.resize(1);
    worldRead(Json::from_cbor(Json::to_cbor(packetJson)),client);
    check(client.targets[0].ai.combatMotion.technique==MechaTechnique::RightCut,"co-op blade pose lost");
    for(size_t i=0;i<combat.projectiles.size();++i)
        check(client.projectiles[i].visual==ProjectileVisual(i),"co-op barrage appearance lost");
    auto &cue=packetJson["targets"][0][14];
    cue.erase(cue.size()-1);
    packetJson["projectiles"][1].erase("visual");
    worldRead(packetJson,client);
    check(client.targets[0].ai.combatMotion.technique==MechaTechnique::None&&
          client.projectiles[1].visual==ProjectileVisual::Standard,"older combat snapshots retained stale effects");
    cue.push_back(int(MechaTechnique::Count));
    rejects([&]{worldRead(packetJson,client);});
    cue.erase(cue.size()-1);
    packetJson["projectiles"][1]["visual"]=-1;
    rejects([&]{worldRead(packetJson,client);});
    auto packed=compactWorld(combat);
    check(packed[4][0].size()==14&&packed[4][1].size()==15,"compact visual tag grew the standard record");
    compactRead(Json::from_cbor(Json::to_cbor(packed)),client);
    for(size_t i=0;i<combat.projectiles.size();++i)
        check(client.projectiles[i].visual==ProjectileVisual(i),"compact barrage appearance lost");
    packed[4][1][14]=int(ProjectileVisual::LoftMissile)+1;
    rejects([&]{compactRead(packed,client);});

}
void prediction(){
    World anchor;anchor.cooperative=true;anchor.missionStatus=MissionStatus::Active;anchor.position={0,0,0};anchor.health=87;anchor.targets.resize(1);auto& target=anchor.targets[0];target.spawnType=0;target.origin={10,0,0};target.center={10,4,0};target.half={1,4,1};target.ai.velocity={8,0,0};
    anchor.mapPilots.push_back(anchor);anchor.projectiles.push_back({{0,20,0},{0,0,-100},2,25,RightHand});Json players=Json::array({{{"motion",Json::array({Json::array({8,0,0}),std::array<float,4>{0,.5f,0,0}})}}});
    auto auth=worldWire(anchor);auto pilot=pilotWire(anchor);MotionPrediction motion;motion.capture(anchor,players,3);
    World view=anchor;motion.present(view,0);check(worldWire(view)==auth&&pilotWire(view)==pilot,"zero-age prediction changed snapshot");
    for(int refresh:{72,90,120}){view=anchor;for(int f=1;f<=refresh;++f)motion.present(view,float(f)/refresh);check(std::abs(view.position.x-1)<.0001f&&std::abs(view.targets[0].origin.x-11)<.0001f,"prediction is frame-rate dependent or unbounded");check(std::abs(view.projectiles[0].position.z+5)<.0001f,"projectile prediction exceeded horizon");check(view.health==87&&view.ammo==anchor.ammo&&view.destroyed==anchor.destroyed&&view.sounds.empty(),"prediction changed gameplay authority");}
    motion.present(view,.02f);check(std::abs(view.position.x-.16f)<.0001f,"prediction accumulated frame deltas");
    World correction=anchor;correction.position={0,0,-2};correction.mapPilots[0]=correction;correction.targets[0].health=0;correction.targets[0].ai.velocity={};correction.projectiles.clear();motion.capture(correction,players,6);motion.present(correction,.05f);check(correction.projectiles.empty()&&correction.targets[0].origin.x==10&&correction.position.z==-2,"authoritative correction did not replace predictions");
    World limited=anchor;limited.aimYaw=headYawLimit;limited.mapPilots[0]=limited;auto turning=players;turning[0]["motion"][1]=std::array<float,4>{0,0,10,0};motion.capture(limited,turning,9);motion.present(limited,.1f);check(std::abs(wrap(limited.aimYaw-limited.facing()))<=headYawLimit+.0001f,"prediction exceeded head attack limit");
    World impact=anchor;impact.targets[0].center={0,20,-2};impact.targets[0].origin={0,16,-2};impact.targets[0].ai.velocity={};motion.capture(impact,players,9);motion.present(impact,.05f);check(impact.projectiles.empty()&&impact.targets[0].health==100,"visual impact changed damage or passed through scenery");
}
void deterministic(const std::filesystem::path& root){
    RoomWorkers pool(4);std::vector<std::unique_ptr<Room>> serial,parallel;
    for(int i=0;i<8;++i){auto r=std::make_unique<Room>(root);int game=i%2;r->campaign.rules.load(root,game);r->campaign.completed=r->campaign.rules.fullMask();r->host=1;r->mission=game?17:6;for(int p=1;p<=4;++p)r->add(p);r->start();parallel.push_back(std::make_unique<Room>(*r));serial.push_back(std::move(r));}
    for(int f=0;f<120;++f){std::vector<std::function<void()>> jobs;
        for(size_t i=0;i<serial.size();++i){for(auto* r:{serial[i].get(),parallel[i].get()})for(auto& p:r->players){p.input=Input{};p.input.forward=f%60<20?.5f:0;p.input.fire.fill(f%60<40);p.inputAge=0;}serial[i]->step(tick);jobs.push_back([&,i]{parallel[i]->step(tick);});}pool.run(jobs);
        if(f%12==0)for(size_t i=0;i<serial.size();++i)check(serial[i]->state()==parallel[i]->state()&&worldWire(serial[i]->world)==worldWire(parallel[i]->world),"parallel room simulation differs");
    }
}
void lobbies(const std::filesystem::path& root){
    Server server(root,"127.0.0.1",0,0,4);Progression campaign;std::vector<std::unique_ptr<Client>> clients;
    std::string endpoint="127.0.0.1:"+std::to_string(server.port());for(int i=0;i<4;++i)clients.push_back(std::make_unique<Client>(endpoint,true,campaign));
    auto pump=[&](int frames){for(int i=0;i<frames;++i){for(auto& c:clients)c->update(tick);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};pump(40);
    for(int room=0;room<4;++room){check(!clients[room]->code.empty(),"parallel lobby creation");for(int p=0;p<3;++p)clients.push_back(std::make_unique<Client>(endpoint,false,campaign,clients[room]->code,"Client",p!=0));}pump(50);
    for(int i=0;i<4;++i)clients[i]->send({{"op","select"},{"mission",0}});pump(20);for(auto& c:clients)c->send({{"op","ready"},{"ready",true},{"loadout",campaign.loadout}});pump(80);
    for(int wait=0;wait<200;++wait){bool ready=true;for(size_t i=0;i<clients.size();++i){const auto& c=*clients[i];bool legacy=i>=4&&(i-4)%3==0;ready=ready&&c.state.contains("world")&&(legacy||c.state.at("world").at(0)==2);}if(ready)break;pump(1);}
    check(server.report().at("transport").at("datagram_compression"),"legacy compression not retained");
    for(size_t i=0;i<clients.size();++i){auto& c=*clients[i];check(c.error.empty()&&c.phase()==Phase::Playing,"concurrent room deployment");if(!c.state.contains("world"))throw std::runtime_error("snapshot missing client "+std::to_string(i)+" state "+c.state.dump());bool legacy=i>=4&&(i-4)%3==0;check(c.state.at("world").is_object()==legacy,"snapshot negotiation");if(!legacy)check(c.state.at("world").at(0)==2,"reliable baseline was not acknowledged");World view;check(c.apply(view,root,ArmRig{}),"concurrent snapshot apply");check(view.mapPilots.size()==4&&view.cooperative,"cross-room state leak");check(c.state.at("tick").get<unsigned>()%3==c.roomId%3,"snapshot tickrate changed");}
    clients[0]->send({{"op","snapshot_reset"},{"round",clients[0]->round}});pump(30);check(clients[0]->state.at("world").at(0)==2,"baseline resynchronization failed");
    clients[0]->send({{"op","restart"}});pump(40);check(clients[0]->round==2&&clients[1]->round==1,"restart crossed lobby boundary");World restarted;bool applied=false;
    // The new-round state can precede its reliable baseline. Exercise the same
    // retry path as the client instead of requiring delivery within 40 pumps.
    for(int wait=0;wait<200&&!applied;++wait){applied=clients[0]->apply(restarted,root,ArmRig{});if(!applied)pump(1);}
    check(applied&&restarted.cooperative&&restarted.mapPilots.size()==4,"new round baseline failed");
    clients.clear();pump(20);auto report=server.report();check(report.at("peers")==0&&report.at("rooms").empty(),"concurrent lobby shutdown leaked state");check(!report.at("transport").at("datagram_compression"),"legacy compression was not released");check(report.at("backend").at("workers")==4,"worker configuration ignored");
}
int main(int argc,char** argv){try{check(argc==2,"retail path required");workers();packets();snapshots();prediction();deterministic(argv[1]);lobbies(argv[1]);std::cout<<"Bounded workers, compact packets/baselines, prediction, deterministic rooms and mixed-client concurrent lobbies passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
