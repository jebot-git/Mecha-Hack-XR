#include "coop.hpp"
#include "config.hpp"
#include <algorithm>
#include <chrono>
#include "net/snapshot_packet.hpp"
#include <fstream>
#include <stdexcept>
namespace iron::coop {
std::vector<Vec3> spawnPositions(const World& world,std::span<const unsigned> slots){
    constexpr float radius=3.5f,height=12,spacing=10,walkOut=8,searchRadius=64;
    if(slots.size()>4)throw std::runtime_error("Lobby is full");
    std::array<bool,4> used{};std::vector<Vec3> result;
    // Side-by-side lanes avoid putting a teammate immediately in front of a hip
    // weapon. Keep the original host anchor when it has sufficient clearance.
    constexpr float lanes[]={0,10,-10,20};
    std::vector<std::array<int,2>> offsets;
    for(int z=-32;z<=32;++z)for(int x=-32;x<=32;++x)offsets.push_back({x,z});
    std::sort(offsets.begin(),offsets.end(),[](auto a,auto b){int da=a[0]*a[0]+a[1]*a[1],db=b[0]*b[0]+b[1]*b[1];return da!=db?da<db:a<b;});
    auto forward=direction(world.chassisYaw,0)*walkOut;
    auto clear=[&](Vec3 point){
        if(std::abs(point.x)>1000-radius||std::abs(point.z)>1000-radius)return false;
        if(std::abs(point.x+forward.x)>1000-radius||std::abs(point.z+forward.z)>1000-radius)return false;
        // Retail ground is flat; also reject uneven support if height fields are
        // introduced later. The same footprint must support the exit corridor.
        for(float along:{0.f,.5f,1.f})for(float x:{-radius,radius})for(float z:{-radius,radius})
            if(std::abs(world.surfaceHeight(point.x+forward.x*along+x,point.z+forward.z*along+z)-point.y)>.4f)return false;
        for(const auto& target:world.targets){
            // Reserve authored dormant actors too: they may activate on the
            // first simulation tick. Generated pool slots have no live body yet.
            if(target.health<=0||target.escaped||(!target.active&&(target.spawnType<0||target.generated)))continue;
            if(target.center.y+target.half.y<=point.y||target.center.y-target.half.y>=point.y+height)continue;
            // Sweep the whole spawn footprint forward, using the same bounds as
            // movement. Sampling the start alone can leave a pilot boxed in.
            float low=0,high=1;
            for(int axis=0;axis<2&&low<=high;++axis){
                float p=axis?point.z:point.x,d=axis?forward.z:forward.x;
                float center=axis?target.center.z:target.center.x,half=(axis?target.half.z:target.half.x)+radius;
                if(std::abs(d)<.00001f){if(std::abs(p-center)>half){low=2;break;}}
                else {float a=(center-half-p)/d,b=(center+half-p)/d;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);}
            }
            if(low<=high)return false;
        }
        for(auto other:result){
            auto delta=point-other;if(std::hypot(delta.x,delta.z)<spacing)return false;
            auto local=rotate(angles(-world.chassisYaw),delta);
            if(std::abs(local.x)<6&&std::abs(local.z)<40)return false;
        }
        return true;
    };
    for(auto slot:slots){
        if(slot>=used.size()||used[slot])throw std::runtime_error("Invalid player slot");used[slot]=true;
        bool found=false;
        for(auto offset:offsets){
            Vec3 local{lanes[slot]+2*offset[0],0,float(2*offset[1])};
            if(std::hypot(local.x,local.z)>searchRadius)continue;
            auto point=world.position+rotate(angles(world.chassisYaw),local);point.y=world.surfaceHeight(point.x,point.z);
            if(clear(point)){result.push_back(point);found=true;break;}
        }
        if(!found)throw std::runtime_error("No clear co-op spawn nearby - deployment cancelled");
    }
    return result;
}
Player& Room::player(uint32_t id){auto it=std::find_if(players.begin(),players.end(),[&](auto& p){return p.id==id;});if(it==players.end())throw std::runtime_error("Not a lobby member");return *it;}
void Room::add(uint32_t id){if(players.size()>=4)throw std::runtime_error("Lobby is full");if(phase!=Phase::Selecting&&phase!=Phase::Briefing)throw std::runtime_error("Join while host is selecting or briefing a mission");
    unsigned slot=0;while(std::any_of(players.begin(),players.end(),[&](auto& p){return p.slot==slot;}))++slot;
    Player p;p.id=id;p.slot=slot;p.pilot.playerSlot=slot;players.push_back(p);for(auto& member:players)member.ready=false;++revision;
}
void Room::remove(uint32_t id){
    std::erase_if(players,[&](auto& p){return p.id==id;});if(host==id)host=players.empty()?0:players.front().id;
    for(auto& p:players)p.ready=false;
    // Enemy targets index the roster, so removing a pilot invalidates those indices.
    for(auto& t:world.targets){t.ai.playerTarget=-1;t.ai.retarget=0;}
    world.teammates.clear();++revision;
}
void Room::links(size_t current){world.teammates.clear();for(size_t i=0;i<players.size();++i)world.teammates.push_back(i==current?static_cast<Pilot*>(&world):&players[i].pilot);}
void Room::returnToSelection(const std::string& reason){
    outcome=reason;phase=Phase::Selecting;++selection;++revision;
    world.teammates.clear();world.mapPilots.clear();world.projectiles.clear();world.effects.clear();world.sounds.clear();world.missile.active=false;
    for(auto& p:players){p.ready=false;p.input={};p.input.focused=false;p.inputAge=100;p.pilot.missile.active=false;p.pilot.arms={};}
}
void Room::start(){
    World next;next.cooperative=true;next.difficulty=difficulty;next.loadMission(retail,mission,campaign.rules.game);
    std::vector<unsigned> slots;for(auto& p:players)slots.push_back(p.slot);
    auto spawns=spawnPositions(next,slots);auto yaw=next.chassisYaw;auto nextBaseline=captureBaseline(next);world=std::move(next);baseline=std::move(nextBaseline);
    for(size_t i=0;i<players.size();++i){auto& p=players[i];static_cast<Pilot&>(world)=Pilot{};world.position=spawns[i];world.chassisYaw=yaw;world.playerSlot=p.slot;world.missionStatus=MissionStatus::Active;world.configure(p.loadout);p.pilot=world;p.velocity={};p.angularVelocity={};p.input=Input{};p.input.focused=false;p.inputAge=100;p.ready=false;}
    world.teammates.clear();phase=Phase::Playing;++round;++revision;
}
void Room::command(uint32_t id,const Json& j){auto& p=player(id);std::string op=j.at("op");
    if(op=="input") {if(phase!=Phase::Playing||j.at("round").get<uint64_t>()!=round)return;p.input=inputRead(j.at("input"));p.inputAge=0;return;}
    if(op=="select"){if(id!=host||phase==Phase::Playing)throw std::runtime_error("Only host selects missions between deployments");int selected=j.at("mission");if(!campaign.missionAvailable(selected))throw std::runtime_error("Mission locked by host campaign");mission=selected;outcome.clear();++selection;phase=Phase::Briefing;for(auto& member:players)member.ready=false;}
    else if(op=="ready") {if(phase!=Phase::Briefing||j.at("selection").get<uint64_t>()!=selection)throw std::runtime_error("Mission selection changed - confirm ready again");auto loadout=j.at("loadout").get<Loadout>();if(!validLoadout(loadout,campaign))throw std::runtime_error("Equipment locked or incompatible");p.loadout=loadout;p.ready=j.at("ready").get<bool>();if(std::all_of(players.begin(),players.end(),[](auto& a){return a.ready;}))start();}
    else if(op=="restart"){if(id!=host||phase==Phase::Selecting||phase==Phase::Briefing)throw std::runtime_error("Only host can restart a deployed mission");start();}
    else if(op=="abort"){if(id!=host)throw std::runtime_error("Only host can abort");returnToSelection("MISSION ABORTED");}
    else if(op=="ping"){
        if(phase!=Phase::Playing||j.at("round").get<uint64_t>()!=round||p.pilot.lifeSpent)return;
        unsigned kind=j.at("kind").get<unsigned>();if(kind>1)throw std::runtime_error("Invalid map ping");
        Input aim=inputRead(j.at("input"));if(!aim.focused||!aim.headValid)return;
        Pilot previous=world;static_cast<Pilot&>(world)=p.pilot;world.aimYaw=wrap(world.facing()+attackYaw(aim.headYaw));world.aimPitch=std::clamp(aim.headPitch,-1.45f,1.45f);world.pingMap(kind);p.pilot=world;static_cast<Pilot&>(world)=previous;
    }
    else if(op=="destroy"){if(phase!=Phase::Playing||j.at("round").get<uint64_t>()!=round||p.pilot.lifeSpent)return;world.damagePilot(p.pilot,100000);}
    else if(op=="difficulty"){if(id!=host||phase==Phase::Playing)throw std::runtime_error("Only host can change difficulty before deployment");std::string value=j.at("value");if(value!="normal"&&value!="hard")throw std::runtime_error("Difficulty must be normal or hard");difficulty=value=="hard"?World::Difficulty::Hard:World::Difficulty::Normal;for(auto& member:players)member.ready=false;}
    else throw std::runtime_error("Unknown lobby command");
    ++revision;
}
void Room::step(float dt){
    if(phase!=Phase::Playing){
        for(auto& p:players){p.velocity={};p.angularVelocity={};}
        // A victory/failure must not strand an already-ejecting player mid-animation.
        if(phase==Phase::Complete||phase==Phase::Failed)for(auto& p:players)if(p.pilot.lifeSpent&&p.pilot.ejectionTime<7){static_cast<Pilot&>(world)=p.pilot;world.stepEjection(dt);p.pilot=world;}
        if(phase==Phase::Failed&&std::none_of(players.begin(),players.end(),[](auto& p){return p.pilot.lifeSpent&&p.pilot.ejectionTime<7;}))returnToSelection("MISSION FAILED");
        return;
    }
    for(size_t i=0;i<players.size();++i){auto& p=players[i];
        if(p.pilot.pingTarget>=0){auto index=size_t(p.pilot.pingTarget);if(index<world.targets.size()&&world.targets[index].active&&!world.targets[index].escaped&&world.targets[index].health>0)p.pilot.pingPosition=world.targets[index].center;else p.pilot.pingTarget=-1;}
        p.inputAge+=dt;static_cast<Pilot&>(world)=p.pilot;links(i);
        Input input=p.input;if(p.inputAge>.3f||!input.focused||!input.headValid){input=Input{};input.fire.fill(false);input.handValid.fill(false);for(auto& h:input.handPoses)h.valid=false;}
        auto before=world.position;std::array<float,4> angles{world.chassisYaw,world.torsoYaw,world.aimYaw,world.aimPitch};
        world.step(input,dt,false);p.pilot=world;
        p.velocity=dt>0?(p.pilot.position-before)*(1/dt):Vec3{};
        if(length(p.velocity)>100||p.pilot.lifeSpent)p.velocity={};
        std::array<float,4> after{world.chassisYaw,world.torsoYaw,world.aimYaw,world.aimPitch};
        for(size_t k=0;k<4;++k)p.angularVelocity[k]=dt>0?std::clamp(wrap(after[k]-angles[k])/dt,-10.f,10.f):0;
    }
    auto living=std::find_if(players.begin(),players.end(),[](auto& p){return !p.pilot.lifeSpent&&p.pilot.health>0;});
    if(living==players.end()){if(std::all_of(players.begin(),players.end(),[](auto& p){return p.pilot.ejectionTime>=7;})){returnToSelection("MISSION FAILED - ALL PILOTS LOST");}world.teammates.clear();return;}
    size_t active=size_t(living-players.begin());static_cast<Pilot&>(world)=living->pilot;links(active);world.stepProjectiles(dt);world.stepMission(dt);living->pilot=world;
    if(world.missionStatus==MissionStatus::Complete){campaign.complete(mission);campaign.selectedMission=campaign.nextMission();phase=Phase::Complete;++revision;}
    else if(world.missionStatus==MissionStatus::Failed&&!world.lifeSpent){phase=Phase::Failed;++revision;}
    if(phase==Phase::Complete||phase==Phase::Failed)for(auto& p:players)if(!p.pilot.lifeSpent)p.pilot.missionStatus=phase==Phase::Complete?MissionStatus::Complete:MissionStatus::Failed;
    world.teammates.clear();
}
Json Room::state()const{Json list=Json::array();for(auto& p:players)list.push_back({{"id",p.id},{"name",p.name},{"slot",p.slot},{"ready",p.ready},{"loadout",p.loadout},{"pilot",pilotWire(p.pilot)},{"motion",Json::array({Json::array({p.velocity.x,p.velocity.y,p.velocity.z}),p.angularVelocity})}});
    return {{"type","state"},{"room",id},{"code",code},{"host",host},{"revision",revision},{"round",round},{"selection",selection},{"mission",mission},{"phase",int(phase)},{"outcome",outcome},{"campaign",campaign.rules.game},{"completed",campaign.completed},{"difficulty",difficulty==World::Difficulty::Hard?"hard":"normal"},{"players",list},{"searchlight_preview",false},{"environment",environmentWire(EnvironmentSettings{})}};
}
Server::Server(const std::filesystem::path& root,const std::string& bind,uint16_t port,uint16_t voicePort,unsigned count):retail(root),transport(true,bind,port),voice(bind,voicePort?voicePort:uint16_t(transport.port()==65535?65534:transport.port()+1)),workers(count){worldClock.seconds=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();}
void Server::publish(Room& r){auto bytes=Json::to_cbor(r.state());std::vector<ENetPeer*> recipients;for(auto& [peer,p]:peers)if(p.code==r.code)recipients.push_back(peer);transport.broadcastEncoded(recipients,bytes);}
void Server::leave(ENetPeer* peer){auto found=peers.find(peer);if(found==peers.end())return;auto p=found->second;voice.remove(p.id);peers.erase(found);auto room=rooms.find(p.code);if(room==rooms.end())return;
    room->second->remove(p.id);
    if(room->second->players.empty())rooms.erase(room);else publish(*room->second);
}
void Server::message(ENetPeer* peer,const Json& j,uint8_t channel){auto& p=peers.at(peer);p.age=0;if((p.budget-=1)<0)throw std::runtime_error("Message rate exceeded");std::string op=j.at("op");
    if(op=="world_clock"){
        if(channel!=0||!p.code.empty()||j.at("version")!=protocol||j.value("clock_version",0)!=1)throw std::runtime_error("Invalid world clock subscription");
        p.clockSubscriber=true;transport.send(peer,{{"type","environment_clock"},{"clock",environmentCycleWire(worldClock)}});return;
    }
    if(op=="create"||op=="join"){
        if(!p.code.empty()||p.clockSubscriber||channel!=0)throw std::runtime_error("Already in a lobby");
        if(j.at("version")!=protocol)throw std::runtime_error("Client version mismatch");
        bool compact=false;
        if(j.contains("snapshot")){if(!j.at("snapshot").is_number_unsigned())throw std::runtime_error("Invalid snapshot capability");compact=j.at("snapshot")==1;}
        auto name=validatePlayerName(j.value("name",std::string("Pilot")));
        if(op=="create"){
            if(j.value("searchlight_preview",false))throw std::runtime_error("Weather and searchlights require Skirmish or Assault");
            if(rooms.size()>=32)throw std::runtime_error("Lobby server full");
            auto r=std::make_unique<Room>(retail);r->id=nextRoom++;r->host=p.id;r->campaign.rules.load(retail,j.value("campaign",0));
            auto completed=j.at("completed").get<int>();if(completed<0||unsigned(completed)>r->campaign.rules.fullMask()||(unsigned(completed)&~r->campaign.rules.available(uint32_t(completed))))throw std::runtime_error("Invalid host campaign");r->campaign.completed=uint32_t(completed);r->mission=r->campaign.nextMission();
            std::ifstream random("/dev/urandom",std::ios::binary);constexpr char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
            do{r->code.clear();for(int i=0;i<6;++i){unsigned char byte;if(!random.read(reinterpret_cast<char*>(&byte),1))throw std::runtime_error("Cannot generate lobby code");r->code+=alphabet[byte&31];}}while(rooms.contains(r->code));
            p.code=r->code;r->add(p.id);rooms.emplace(p.code,std::move(r));
        }else {std::string code=j.at("code");auto room=rooms.find(code);if(room==rooms.end())throw std::runtime_error("Lobby code not found");room->second->add(p.id);p.code=code;}
        p.compact=compact;rooms.at(p.code)->player(p.id).name=name;
        transport.send(peer,{{"type","welcome"},{"id",p.id},{"voice",voice.admit(p.id)},{"snapshot",p.compact?1:0}});publish(*rooms.at(p.code));return;
    }
    if(op=="leave"){leave(peer);enet_peer_disconnect_later(peer,0);return;}
    if(p.code.empty())throw std::runtime_error("Join a lobby first");auto& room=*rooms.at(p.code);
    if(op=="snapshot_reset"){
        if(!p.compact||channel!=0||!j.at("round").is_number_unsigned())throw std::runtime_error("Unexpected snapshot reset");
        if(j.at("round").get<uint64_t>()!=room.round)return;
        p.baselineSent=0;p.baselineAck=0;return;
    }
    if(op=="baseline"){
        if(!p.compact||channel!=0||!j.at("round").is_number_unsigned()||!j.at("hash").is_number_unsigned())throw std::runtime_error("Unexpected baseline acknowledgement");
        if(j.at("round").get<uint64_t>()!=room.round)return;
        if(j.at("hash").get<uint64_t>()!=room.baseline.hash)throw std::runtime_error("Invalid baseline acknowledgement");
        p.baselineAck=room.round;return;
    }
    if((op=="input")!=(channel==2))throw std::runtime_error("Invalid command channel");
    room.command(p.id,j);if(op!="input")publish(room);
}
void Server::step(float dt){
    if(!std::isfinite(dt)||dt<=0||dt>.1f)throw std::invalid_argument("Invalid server tick interval");
    worldClock.advance(dt);
    using Clock=std::chrono::steady_clock;auto begin=Clock::now();auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    for(auto e:transport.poll()){
        if(e.type==Event::Connected){peers.emplace(e.peer,Peer{nextPlayer++,""});continue;}
        if(e.type==Event::Disconnected){leave(e.peer);continue;}
        if(!peers.contains(e.peer))continue;
        try{message(e.peer,e.message,e.channel);}catch(const std::exception& error){transport.send(e.peer,{{"type","error"},{"error",error.what()}});}
    }
    std::vector<ENetPeer*> expired;for(auto& [peer,p]:peers){p.age+=dt;p.budget=std::min(180.f,p.budget+90*dt);if((p.code.empty()&&p.age>15)||p.budget< -90)expired.push_back(peer);}
    for(auto peer:expired){leave(peer);enet_peer_disconnect_now(peer,4);}
    lobbyClock+=dt;bool heartbeat=lobbyClock>=2;if(heartbeat){lobbyClock=0;auto clock=Json::to_cbor(Json{{"type","environment_clock"},{"clock",environmentCycleWire(worldClock)}});for(auto& [peer,p]:peers)if(p.clockSubscriber)transport.sendEncoded(peer,clock);}
    // Preserve legacy bandwidth behavior during staged upgrades. Once every
    // joined peer uses compact packets, compression is entirely in room jobs.
    transport.setDatagramCompression(std::any_of(peers.begin(),peers.end(),[](const auto& item){return !item.second.code.empty()&&!item.second.compact;}));
    ++frames;auto networkDone=Clock::now();
    struct Frame {
        Room* room=nullptr;std::string code,error;
        std::vector<ENetPeer*> legacy,dense,sparse,baselinePeers;
        std::vector<uint8_t> control,legacyPacket,densePacket,sparsePacket,baselinePacket;
        std::vector<avp::voice::Participant> participants;
        double simulationMs=0,encodeMs=0;
    };
    std::vector<Frame> framesOut(rooms.size());std::vector<std::function<void()>> jobs;size_t index=0;
    for(auto& [code,room]:rooms){auto& frame=framesOut[index++];frame.room=room.get();frame.code=code;
        for(auto& [peer,p]:peers)if(p.code==code){
            if(!p.compact)frame.legacy.push_back(peer);
            else {if(p.baselineSent!=room->round)frame.baselinePeers.push_back(peer);(p.baselineAck==room->round?frame.sparse:frame.dense).push_back(peer);}
        }
        jobs.push_back([&,out=&frame]{auto& f=*out;auto& r=*f.room;try{
            auto start=Clock::now();auto revision=r.revision;r.step(dt);auto simulated=Clock::now();
            for(auto& p:r.players)f.participants.push_back({p.id,p.pilot.position,true,r.id});
            if(revision!=r.revision||heartbeat)f.control=Json::to_cbor(r.state());
            if(frames%3==r.id%3&&r.round){
                auto snapshot=r.state();snapshot["tick"]=frames;
                if(!f.legacy.empty()){snapshot["world"]=worldWire(r.world);f.legacyPacket=Json::to_cbor(snapshot);}
                if(!f.dense.empty()){snapshot["world"]=compactWorld(r.world);f.densePacket=encodeSnapshotPacket(snapshot);}
                if(!f.sparse.empty()){snapshot["world"]=compactWorld(r.world,&r.baseline);f.sparsePacket=encodeSnapshotPacket(snapshot);}
                if(!f.baselinePeers.empty())f.baselinePacket=encodeSnapshotPacket({{"type","baseline"},{"room",r.id},{"round",r.round},{"baseline",r.baseline.wire()}});
                r.world.sounds.clear();
            }
            f.simulationMs=ms(start,simulated);f.encodeMs=ms(simulated,Clock::now());
        }catch(const std::exception& e){f.error=e.what();}});
    }
    workers.run(jobs);auto roomsDone=Clock::now();std::vector<avp::voice::Participant> participants;
    metrics.simulationMs=0;metrics.encodeMs=0;
    for(auto& f:framesOut){
        if(!f.error.empty()){
            // Other lobbies completed their jobs. Close only the failed room.
            std::vector<ENetPeer*> broken;for(auto& [peer,p]:peers)if(p.code==f.code)broken.push_back(peer);
            for(auto* peer:broken){transport.send(peer,{{"type","closed"},{"error","Lobby simulation failed: "+f.error}});voice.remove(peers.at(peer).id);peers.erase(peer);enet_peer_disconnect_later(peer,5);}
            rooms.erase(f.code);continue;
        }
        metrics.simulationMs+=f.simulationMs;metrics.encodeMs+=f.encodeMs;
        participants.insert(participants.end(),f.participants.begin(),f.participants.end());
        if(!f.control.empty())for(auto& [peer,p]:peers)if(p.code==f.code)transport.sendEncoded(peer,f.control);
        if(!f.baselinePacket.empty()){transport.broadcastEncoded(f.baselinePeers,f.baselinePacket);for(auto* peer:f.baselinePeers)peers.at(peer).baselineSent=f.room->round;}
        for(auto [recipients,packet]:{std::pair{&f.legacy,&f.legacyPacket},std::pair{&f.dense,&f.densePacket},std::pair{&f.sparse,&f.sparsePacket}})
            if(!packet->empty()){transport.broadcastEncoded(*recipients,*packet,1);++metrics.snapshots;metrics.snapshotBytes+=packet->size()*recipients->size();}
    }
    voice.poll(participants,1);transport.flush();auto end=Clock::now();
    metrics.networkMs=ms(begin,networkDone);metrics.roomsMs=ms(networkDone,roomsDone);metrics.sendMs=ms(roomsDone,end);metrics.totalMs=ms(begin,end);metrics.maxMs=std::max(metrics.maxMs,metrics.totalMs);++metrics.ticks;if(metrics.totalMs>1000./60)++metrics.overBudget;std::array<double,5> stages{metrics.networkMs,metrics.roomsMs,metrics.sendMs,metrics.simulationMs,metrics.encodeMs};for(size_t i=0;i<stages.size();++i)metrics.stageTotals[i]+=stages[i];
}
Json Server::report()const{Json list=Json::array();size_t targets=0,actors=0,shots=0;for(auto& [code,r]:rooms){list.push_back(r->state());targets+=r->world.targets.size();shots+=r->world.projectiles.size();for(auto& t:r->world.targets)actors+=t.spawnType>=0&&t.active&&t.health>0;}return {{"port",port()},{"world_clock",environmentCycleWire(worldClock)},{"voice",voice.report()},{"transport",transport.stats()},{"rooms",list},{"peers",peers.size()},
    {"backend",{{"workers",workers.count()},{"active_actors",actors},{"targets",targets},{"projectiles",shots},{"stage_total_ms",metrics.stageTotals},{"ticks",metrics.ticks},{"over_budget_ticks",metrics.overBudget},{"snapshot_variants",metrics.snapshots},{"snapshot_bytes",metrics.snapshotBytes},{"last_network_ms",metrics.networkMs},{"last_rooms_ms",metrics.roomsMs},{"last_send_ms",metrics.sendMs},{"last_total_ms",metrics.totalMs},{"max_total_ms",metrics.maxMs},{"last_simulation_job_ms",metrics.simulationMs},{"last_encode_job_ms",metrics.encodeMs}}}};}
Client::Client(const std::string& address,bool host,const Progression& campaign,const std::string& lobby,const std::string& name,bool compact,bool searchlights,const EnvironmentSettings& environment):endpoint(address),hosting(host){peer=transport.connect(address);voiceHost=avp::net::parseEndpoint(address,defaultPort).host;hello={{"op",host?"create":"join"},{"name",validatePlayerName(name)},{"version",protocol},{"campaign",campaign.rules.game},{"completed",campaign.completed},{"code",lobby},{"snapshot",compact?1:0},{"searchlight_version",1},{"searchlight_preview",host&&searchlights},{"environment",environmentWire(environment)}};}
Client::~Client(){if(peer&&peer->state==ENET_PEER_STATE_CONNECTED){transport.send(peer,{{"op","leave"}});transport.flush();enet_peer_disconnect_now(peer,0);}}
void Client::send(Json j){if(!connected)return;if(j.value("op","")=="ready"&&!state.empty())j["selection"]=state.at("selection");if(j.value("op","")=="destroy")j["round"]=round;transport.send(peer,j);transport.flush();}
void Client::update(float dt,const Input* input){if(!std::isfinite(dt)||dt<0)dt=0;age+=dt;inputClock+=dt;snapshotAge+=dt;
    if(!input||!input->searchlight)lightReleaseRequired=false;
    if(input&&input->focused){if(!lightReleaseRequired)pendingSearchlight=pendingSearchlight||input->searchlight;for(int i=0;i<MountCount;++i)pendingFire[i]=pendingFire[i]||input->fire[i];for(int i=0;i<2;++i)pendingPunch[i]=pendingPunch[i]||input->punch[i];}else{pendingSearchlight=false;pendingFire.fill(false);pendingPunch.fill(false);}
    for(auto e:transport.poll()){
        if(e.type==Event::Connected){connected=true;age=0;transport.send(peer,hello);}
        else if(e.type==Event::Disconnected){connected=false;peer=nullptr;error="Disconnected from lobby server";}
        else {age=0;auto type=e.message.value("type","");
            if(type=="welcome"){id=e.message.at("id");voiceCredentials=e.message.at("voice");}
            else if(type=="baseline"){
                auto incomingRoom=e.message.at("room").get<uint64_t>(),incomingRound=e.message.at("round").get<uint64_t>();
                if((roomId&&incomingRoom!=roomId)||incomingRound<round)continue;
                baseline=readBaseline(e.message.at("baseline"));baselineRoom=incomingRoom;baselineRound=incomingRound;requestedBaselineRound=0;
                transport.send(peer,{{"op","baseline"},{"round",incomingRound},{"hash",baseline.hash}});
            }
            else if(type=="closed"){error=e.message.value("error","Lobby closed");connected=false;enet_peer_disconnect_now(peer,0);peer=nullptr;}
            else if(type=="error")error=e.message.value("error","Lobby request failed");
            else if(type=="state"){
                uint64_t rev=e.message.at("revision"),incoming=e.message.value("tick",uint64_t(0));
                if(rev<revision||(incoming&&incoming<=lastSnapshot))continue;
                if(!incoming&&rev==revision&&!state.empty())continue;
                if(incoming){lastSnapshot=incoming;snapshotAge=0;}
                state=std::move(e.message);revision=rev;roomId=state.at("room");if(state.at("round").get<uint64_t>()!=round){pendingSearchlight=false;lightReleaseRequired=true;}round=state.at("round");code=state.at("code");hosting=state.at("host").get<uint32_t>()==id;
            }
        }
    }
    if(peer&&age>10){error="Lobby server timed out";connected=false;enet_peer_disconnect_now(peer,0);peer=nullptr;}
    if(connected&&input&&inputClock>=1.f/30&&phase()==Phase::Playing){inputClock=0;Input sample=*input;sample.searchlight=!lightReleaseRequired&&(sample.searchlight||pendingSearchlight);pendingSearchlight=false;sample.fire=pendingFire;sample.punch=pendingPunch;pendingFire.fill(false);pendingPunch.fill(false);transport.send(peer,{{"op","input"},{"round",round},{"input",inputWire(sample)}},2);}
    for(unsigned kind=0;kind<2;++kind){bool held=input&&input->focused&&input->headValid&&input->ping[kind];
        if(held&&!pingHeld[kind]&&connected&&phase()==Phase::Playing)transport.send(peer,{{"op","ping"},{"round",round},{"kind",kind},{"input",inputWire(*input)}});
        pingHeld[kind]=held;
    }
    transport.flush();
}
const Json* Client::local()const{if(state.empty())return nullptr;for(auto& p:state.at("players"))if(p.at("id")==id)return &p;return nullptr;}
Phase Client::phase()const{return state.empty()?Phase::Selecting:Phase(state.at("phase").get<int>());}
bool Client::apply(World& world,const std::filesystem::path& retail,const ArmRig& rig){
    if(!round||!state.contains("world")||!local())return false;uint64_t tick=state.at("tick");
    bool fresh=appliedRound!=round||tick>appliedTick;
    if(fresh){
        bool changedRound=appliedRound!=round;
        const SnapshotBaseline* base=baselineRound==round&&baselineRoom==roomId?&baseline:nullptr;
        if(state.at("world").is_array()&&state.at("world").at(0)==2&&(!base||state.at("world").at(2)!=base->hash)){
            if(requestedBaselineRound!=round){send({{"op","snapshot_reset"},{"round",round}});requestedBaselineRound=round;}return false;
        }
        if(changedRound){world=World{};world.cooperative=true;world.difficulty=state.at("difficulty")=="hard"?World::Difficulty::Hard:World::Difficulty::Normal;world.loadMission(retail,state.at("mission"),state.value("campaign",0));world.armRig=rig;prediction.reset();}
        world.mapPilots.clear();for(auto& member:state.at("players")){Pilot pilot;pilotRead(member.at("pilot"),pilot);world.mapPilots.push_back(pilot);}
        pilotRead(local()->at("pilot"),world);
        if(state.at("world").is_array())compactRead(state.at("world"),world,base);else worldRead(state.at("world"),world);
        prediction.capture(world,state.at("players"),tick);appliedRound=round;appliedTick=tick;
    }
    prediction.present(world,phase()==Phase::Playing?float(snapshotAge):0.f);
    return fresh;
}
std::vector<avp::voice::Participant> Client::participants()const{std::vector<avp::voice::Participant> list;if(state.empty())return list;for(auto& p:state.at("players")){Pilot pilot;pilotRead(p.at("pilot"),pilot);list.push_back({p.at("id"),pilot.position,true,roomId});}return list;}
}
