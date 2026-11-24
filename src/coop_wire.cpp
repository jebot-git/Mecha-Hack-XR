#include "coop.hpp"
#include "net/snapshot_packet.hpp"
#include <algorithm>
#include <stdexcept>
namespace iron {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3,x,y,z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Quaternion,x,y,z,w)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HandPose,position,grip,aim,valid)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Arm,shoulder,elbow,wrist,muzzle,forward,grip,aim,tracked)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Missile,active,position,yaw,pitch,life,launchYaw,headYaw,headPitch)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Pickup,position,kind,collected,collectedBy)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WeaponEffect,kind,position,direction,weapon,age,size,model,damageRadius,damageRate)
void to_json(nlohmann::json& j,const Projectile& p){
    j={{"position",p.position},{"velocity",p.velocity},{"life",p.life},{"damage",p.damage},
       {"mount",p.mount},{"kind",p.kind},{"owner",p.owner},{"victim",p.victim},{"shooter",p.shooter},
       {"radius",p.radius},{"weapon",p.weapon},{"homingTarget",p.homingTarget},{"hull",p.hull},{"intercepted",p.intercepted}};
    if(p.visual!=ProjectileVisual::Standard)j["visual"]=int(p.visual);
}
void from_json(const nlohmann::json& j,Projectile& p){
    j.at("position").get_to(p.position);j.at("velocity").get_to(p.velocity);
    j.at("life").get_to(p.life);j.at("damage").get_to(p.damage);j.at("mount").get_to(p.mount);
    j.at("kind").get_to(p.kind);j.at("owner").get_to(p.owner);j.at("victim").get_to(p.victim);
    j.at("shooter").get_to(p.shooter);j.at("radius").get_to(p.radius);j.at("weapon").get_to(p.weapon);
    j.at("homingTarget").get_to(p.homingTarget);j.at("hull").get_to(p.hull);j.at("intercepted").get_to(p.intercepted);
    p.visual=ProjectileVisual::Standard;
    if(auto v=j.find("visual");v!=j.end()){
        if(!v->is_number_integer()||*v<0||*v>int(ProjectileVisual::LoftMissile))
            throw std::runtime_error("Invalid projectile visual");
        p.visual=ProjectileVisual(v->get<int>());
    }
}
void to_json(nlohmann::json& j,const SoundEvent& event){
    j={{"kind",event.kind},{"weapon",event.weapon},{"position",event.position}};
    if(event.audioWeapon!=Weapon::Empty)j["audio_weapon"]=event.audioWeapon;
    if(event.transmitterActor>=0)j["transmitter_actor"]=event.transmitterActor;
}
void from_json(const nlohmann::json& j,SoundEvent& event){
    j.at("kind").get_to(event.kind);j.at("weapon").get_to(event.weapon);j.at("position").get_to(event.position);
    event.audioWeapon=j.value("audio_weapon",Weapon::Empty);
    event.transmitterActor=j.value("transmitter_actor",-1);
}
}
namespace iron::coop {
Transport::Transport(bool server,const std::string& bind,uint16_t port){
    static bool initialized=[](){if(enet_initialize())throw std::runtime_error("ENet initialization failed");return true;}();(void)initialized;
    ENetAddress address{};address.port=port;if(bind!="0.0.0.0"&&enet_address_set_host(&address,bind.c_str()))throw std::runtime_error("Invalid server address");
    host=enet_host_create(server?&address:nullptr,server?128:1,3,0,0);if(!host)throw std::runtime_error("Cannot open ENet socket");
    host->maximumPacketSize=1024*1024;host->maximumWaitingData=4*1024*1024;
    try{setDatagramCompression(!server);}catch(...){enet_host_destroy(host);host=nullptr;throw;}
}
void Transport::setDatagramCompression(bool enabled){
    if(host->compressor.context&&enabled==compressDatagrams)return;
    ENetCompressor codec{};codec.context=enet_range_coder_create();if(!codec.context)throw std::bad_alloc();
    codec.compress=enabled?enet_range_coder_compress:nullptr;codec.decompress=enet_range_coder_decompress;codec.destroy=enet_range_coder_destroy;
    enet_host_compress(host,&codec);compressDatagrams=enabled;
}
Transport::~Transport(){if(host)enet_host_destroy(host);}
ENetPeer* Transport::connect(const std::string& endpoint){auto parsed=avp::net::parseEndpoint(endpoint,defaultPort);ENetAddress address{};address.port=parsed.port;
    if(enet_address_set_host(&address,parsed.host.c_str()))throw std::runtime_error("Cannot resolve lobby server");
    auto peer=enet_host_connect(host,&address,3,protocol);if(!peer)throw std::runtime_error("Cannot connect to lobby server");enet_peer_timeout(peer,8,3000,8000);return peer;
}
std::vector<Event> Transport::poll(){std::vector<Event> result;ENetEvent e{};
    for(int i=0;i<256&&enet_host_service(host,&e,0)>0;++i){
        if(e.type==ENET_EVENT_TYPE_CONNECT){if(e.data!=protocol&&host->peerCount>1){enet_peer_disconnect_now(e.peer,1);continue;}enet_peer_timeout(e.peer,8,3000,8000);result.push_back({Event::Connected,e.peer,{}});}
        if(e.type==ENET_EVENT_TYPE_DISCONNECT)result.push_back({Event::Disconnected,e.peer,{}});
        if(e.type==ENET_EVENT_TYPE_RECEIVE){try{
            size_t limit=host->peerCount>1?8192:1024*1024;
            if(e.packet->dataLength>limit)throw std::runtime_error("Oversized network message");
            auto j=decodeStatePacket({e.packet->data,e.packet->dataLength},limit,host->peerCount==1);
            if(!j.is_object())throw std::runtime_error("Invalid network message");
            result.push_back({Event::Message,e.peer,std::move(j),e.channelID});
        }catch(...){enet_peer_disconnect_now(e.peer,2);result.push_back({Event::Disconnected,e.peer,{}});}enet_packet_destroy(e.packet);}
    }return result;
}
namespace {
template<class Peer> size_t queuedBytes(Peer* peer){
    auto bytes=[](ENetList* queue){size_t total=0;for(auto it=enet_list_begin(queue);it!=enet_list_end(queue);it=enet_list_next(it))total+=reinterpret_cast<ENetOutgoingCommand*>(it)->fragmentLength;return total;};
    size_t total=bytes(&peer->sentReliableCommands);
    if constexpr(requires{peer->outgoingSendReliableCommands;})return total+bytes(&peer->outgoingCommands)+bytes(&peer->outgoingSendReliableCommands);
    else return total+bytes(&peer->sentUnreliableCommands)+bytes(&peer->outgoingReliableCommands)+bytes(&peer->outgoingUnreliableCommands);
}
}
void Transport::send(ENetPeer* peer,const Json& j,uint8_t channel){sendEncoded(peer,Json::to_cbor(j),channel);}
void Transport::sendEncoded(ENetPeer* peer,const std::vector<uint8_t>& bytes,uint8_t channel){broadcastEncoded({&peer,1},bytes,channel);}
void Transport::broadcastEncoded(std::span<ENetPeer* const> peers,const std::vector<uint8_t>& bytes,uint8_t channel){
    if(channel>2||bytes.empty()||bytes.size()>host->maximumPacketSize)throw std::runtime_error("Network state exceeds limit");
    ENetPacket* packet=nullptr;
    for(auto* peer:peers){
        if(!peer||peer->state!=ENET_PEER_STATE_CONNECTED)continue;
        auto queued=queuedBytes(peer);
        if(queued+bytes.size()>4*1024*1024){enet_peer_disconnect_later(peer,3);continue;}
        if(channel==1&&queued>256*1024){++droppedSnapshots;continue;}
        if(!packet){packet=enet_packet_create(bytes.data(),bytes.size(),channel==0?ENET_PACKET_FLAG_RELIABLE:ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);if(!packet)throw std::bad_alloc();}
        enet_peer_send(peer,channel,packet);
    }
    if(packet&&!packet->referenceCount)enet_packet_destroy(packet);
}
Json Transport::stats()const{return {{"sent_bytes",host->totalSentData},{"received_bytes",host->totalReceivedData},{"sent_packets",host->totalSentPackets},{"received_packets",host->totalReceivedPackets},{"dropped_snapshots",droppedSnapshots},{"datagram_compression",compressDatagrams}};}
void Transport::flush(){enet_host_flush(host);}uint16_t Transport::port()const{return host->address.port;}
Json inputWire(const Input& in){return {{"searchlight",in.searchlight},{"thrust",in.thrust},{"ascend",in.ascend},{"fire_mode",in.fireMode},{"axes",{in.steer,in.forward,in.turn,in.headYaw,in.headPitch}},{"focused",in.focused},{"head",in.headValid},{"fire",in.fire},{"punch",in.punch},{"valid",in.handValid},{"hands",in.hands},{"poses",in.handPoses}};}
Input inputRead(const Json& j){Input in;in.searchlight=j.value("searchlight",false);in.thrust=j.value("thrust",false);in.ascend=j.value("ascend",false);in.fireMode=j.value("fire_mode",false);auto axes=j.at("axes").get<std::array<float,5>>();for(float f:axes)if(!std::isfinite(f)||std::abs(f)>100)throw std::runtime_error("Invalid controller axis");
    in.steer=std::clamp(axes[0],-1.f,1.f);in.forward=std::clamp(axes[1],-1.f,1.f);in.turn=std::clamp(axes[2],-1.f,1.f);in.headYaw=wrap(axes[3]);in.headPitch=std::clamp(axes[4],-1.5f,1.5f);
    in.focused=j.at("focused");in.headValid=j.at("head");in.fire=j.at("fire").get<decltype(in.fire)>();in.punch=j.at("punch").get<decltype(in.punch)>();in.handValid=j.at("valid").get<decltype(in.handValid)>();in.hands=j.at("hands").get<decltype(in.hands)>();in.handPoses=j.at("poses").get<decltype(in.handPoses)>();
    for(int h=0;h<2;++h){
        if(!in.handPoses[h].valid){in.handPoses[h]=HandPose{};in.handPoses[h].valid=false;in.handValid[h]=false;in.hands[h]={};continue;}
        if(!std::isfinite(length(in.hands[h]))||length(in.hands[h])>4||!std::isfinite(length(in.handPoses[h].position))||length(in.handPoses[h].position)>4||!valid(in.handPoses[h].grip)||!valid(in.handPoses[h].aim))throw std::runtime_error("Invalid hand tracking");
    }return in;
}
Json pilotWire(const Pilot& p){Json packet={{"searchlight",p.searchlightOn},{"team",int(p.team)},{"position",p.position},{"homing_target",p.homingTarget},{"gait",p.gaitPhase},{"walk_speed",p.walkingSpeed},{"thruster_pose",p.rpgThrust},{"landing_pose",p.rpgLanding},{"thruster_charge",{p.rpgThrustCharge,p.rpgThrustCapacity}},{"angles",{p.chassisYaw,p.torsoYaw,p.aimYaw,p.aimPitch}},{"health",p.health},{"ammo",p.ammo},{"weapons",p.weapons},{"arms",p.arms},{"missile",p.missile},{"dead",p.lifeSpent},{"ejection",p.ejectionTime},{"status",int(p.missionStatus)},{"slot",p.playerSlot},{"shots",p.shots},{"punches",p.punches},{"flash",p.fistFlash},{"map_ping",{p.selfPingUntil,p.aimPingUntil}},{"ping_target",p.pingTarget},{"ping_position",p.pingPosition}};
    if(std::any_of(p.damageUntil.begin(),p.damageUntil.end(),[](float t){return t>0;}))packet["damage_until"]=p.damageUntil;
    return packet;
}
void pilotRead(const Json& j,Pilot& p){int team=j.value("team",0);if(team<0||team>2)throw std::runtime_error("Invalid pilot team");p.team=Team(team);p.searchlightOn=j.value("searchlight",false);p.position=j.at("position");p.homingTarget=j.value("homing_target",-1);p.gaitPhase=j.value("gait",0.f);p.walkingSpeed=j.value("walk_speed",0.f);p.rpgThrust=j.value("thruster_pose",Vec3{});p.rpgLanding=j.value("landing_pose",0.f);if(j.contains("thruster_charge")){const auto &c=j.at("thruster_charge");p.rpgThrustCharge=std::clamp(c.at(0).get<float>(),0.f,4.f);p.rpgThrustCapacity=std::clamp(c.at(1).get<int>(),0,4);}auto angles=j.at("angles").get<std::array<float,4>>();p.chassisYaw=angles[0];p.torsoYaw=angles[1];p.aimYaw=angles[2];p.aimPitch=angles[3];p.health=j.at("health");p.ammo=j.at("ammo");p.weapons=j.at("weapons");p.arms=j.at("arms");p.missile=j.at("missile");p.lifeSpent=j.at("dead");p.ejectionTime=j.at("ejection");p.missionStatus=MissionStatus(j.at("status").get<int>());p.playerSlot=j.at("slot");p.shots=j.at("shots");p.punches=j.at("punches");p.fistFlash=j.at("flash");auto times=j.at("map_ping").get<std::array<float,2>>();p.selfPingUntil=times[0];p.aimPingUntil=times[1];p.pingTarget=j.at("ping_target");p.pingPosition=j.at("ping_position");
    p.damageUntil=j.value("damage_until",std::array<float,8>{});
    for(float until:p.damageUntil)if(!std::isfinite(until)||until<0)throw std::runtime_error("Invalid damage indicator timer");
}
namespace {
Json targetRow(const Target &t,const World &w){
    auto row=Json::array({t.center,t.half,t.origin,t.yaw,t.health,t.active,t.escaped,t.ai.bank,t.ai.pitch,t.ai.weaponYaw,t.ai.weaponPitch,t.ai.muzzleFlash,t.ai.gaitPhase,t.ai.velocity});
    const auto &motion=t.ai.combatMotion;
    if(w.rpgPrototype&&(motion.active(w.elapsed)||motion.aim>0||motion.retreat>0)){
        auto cue=Json::array({int(motion.action),motion.started,motion.duration,motion.aim,motion.retreat});
        if(motion.technique!=MechaTechnique::None)cue.push_back(int(motion.technique));
        row.push_back(std::move(cue));
    }
    return row;
}
std::array<float,28> targetState(const Target &t,const World &w){
    auto m=t.ai.combatMotion;
    if(!m.active(w.elapsed)&&m.aim<=0&&m.retreat<=0)m={};
    return {t.center.x,t.center.y,t.center.z,t.half.x,t.half.y,t.half.z,
        t.origin.x,t.origin.y,t.origin.z,t.yaw,t.health,float(t.active),float(t.escaped),
        t.ai.bank,t.ai.pitch,t.ai.weaponYaw,t.ai.weaponPitch,t.ai.muzzleFlash,t.ai.gaitPhase,
        t.ai.velocity.x,t.ai.velocity.y,t.ai.velocity.z,float(m.action),m.started,m.duration,m.aim,m.retreat,float(m.technique)};
}
}
SceneryBaseline captureScenery(const World &w,size_t actors){
    if(actors>w.targets.size())throw std::runtime_error("Invalid actor prefix");
    SceneryBaseline base;
    for(size_t i=actors;i<w.targets.size();++i){
        base.rows.push_back(targetRow(w.targets[i],w));
        base.states.push_back(targetState(w.targets[i],w));
    }
    return base;
}
Json worldWire(const World& w,const SceneryBaseline *base){
    if(base&&base->states.size()>w.targets.size())throw std::runtime_error("Invalid scenery baseline");
    const size_t actors=w.targets.size()-(base?base->states.size():0);
    Json targets=Json::array(),changed=Json::array();
    for(size_t i=0;i<actors;++i)targets.push_back(targetRow(w.targets[i],w));
    if(base)for(size_t i=0;i<base->states.size();++i){
        const auto &t=w.targets[actors+i];
        if(targetState(t,w)!=base->states[i])changed.push_back({i,targetRow(t,w)});
    }
    Json packet={{"projectiles",w.projectiles},{"effects",w.effects},{"pickups",w.pickups},{"sounds",w.sounds},{"elapsed",w.elapsed},{"flags",w.missionFlags},{"counters",w.counters},{"destroyed",w.destroyed}};
    packet["targets"]=std::move(targets);
    if(base)packet["scenery_changes"]=std::move(changed);
    if(w.rpgPrototype){Json damage=Json::array();
        for(size_t i=0;i<w.targets.size();++i){const auto &t=w.targets[i];
            if(t.rpgDeathAt>=0||t.rpgBurn>0||(t.rpgInitialHealth>0&&t.health<t.rpgInitialHealth))
                damage.push_back(Json::array({i,t.rpgDeathAt,t.rpgInitialHealth,t.rpgBurn,t.rpgRestY,t.rpgImpactSpent,t.rpgSecondarySpent,t.processed}));}
        packet["rpg_damage"]=std::move(damage);
    }
    return packet;
}
void worldRead(const Json& j,World& w,const Json *scenery){
    const auto &actors=j.at("targets");
    const size_t count=actors.size()+(scenery?scenery->size():0);
    if(count!=w.targets.size())throw std::runtime_error("Mission data does not match server");
    const Json *changes=scenery?&j.at("scenery_changes"):nullptr;
    if(changes&&(!changes->is_array()||changes->size()>scenery->size()))
        throw std::runtime_error("Invalid scenery change count");
    size_t next=0;
    for(size_t i=0;i<count;++i){
        const Json *row=nullptr;
        if(i<actors.size())row=&actors[i];
        else{
            const size_t offset=i-actors.size();
            row=&scenery->at(offset);
            if(next<changes->size()&&changes->at(next).at(0)==offset)
                row=&changes->at(next++).at(1);
        }
        const auto &a=*row;auto &t=w.targets[i];t.center=a.at(0);t.half=a.at(1);t.origin=a.at(2);t.yaw=a.at(3);t.health=a.at(4);t.active=a.at(5);t.escaped=a.at(6);t.ai.bank=a.at(7);t.ai.pitch=a.at(8);t.ai.weaponYaw=a.at(9);t.ai.weaponPitch=a.at(10);t.ai.muzzleFlash=a.at(11);t.ai.gaitPhase=a.size()>12?a.at(12).get<float>():0;t.ai.velocity=a.size()>13?a.at(13).get<Vec3>():Vec3{};
        t.ai.combatMotion={};
        if(w.rpgPrototype&&a.size()>14){
            const auto &v=a.at(14);
            const int action=v.at(0);
            const float started=v.at(1),duration=v.at(2),aim=v.at(3),retreat=v.at(4);
            if(action<0||action>=int(EnemyAction::Count)||!std::isfinite(started)||started<0||
               !(duration>=0&&duration<=4)||!(aim>=0&&aim<=1)||!(retreat>=0&&retreat<=1))
                throw std::runtime_error("Invalid enemy combat animation");
            MechaTechnique technique=MechaTechnique::None;
            if(v.size()>5){
                const auto &tag=v.at(5);
                if(!tag.is_number_integer()||tag<0||tag>=int(MechaTechnique::Count))
                    throw std::runtime_error("Invalid mecha technique");
                technique=MechaTechnique(tag.get<int>());
            }
            t.ai.combatMotion={EnemyAction(action),started,duration,aim,retreat,technique};
        }
    }
    if(changes&&next!=changes->size())throw std::runtime_error("Unordered scenery changes");
    w.projectiles=j.at("projectiles");w.effects=j.at("effects");w.pickups=j.at("pickups");auto sounds=j.at("sounds").get<std::vector<SoundEvent>>();w.sounds.insert(w.sounds.end(),sounds.begin(),sounds.end());w.elapsed=j.at("elapsed");w.missionFlags=j.at("flags");w.counters=j.at("counters");w.destroyed=j.at("destroyed");
    if(w.rpgPrototype){
        for(auto &t:w.targets){t.rpgDeathAt=-1;t.rpgInitialHealth=0;t.rpgBurn=0;t.rpgRestY=0;t.rpgImpactSpent=false;t.rpgSecondarySpent=false;t.processed=false;}
        const auto &rows=j.at("rpg_damage");if(!rows.is_array()||rows.size()>count)throw std::runtime_error("Invalid RPG destruction budget");
        std::vector<bool> seen(count);
        for(const auto &row:rows){if(!row.is_array()||row.size()!=8||!row[0].is_number_unsigned())throw std::runtime_error("Invalid RPG destruction record");
            size_t i=row[0];if(i>=count||seen[i])throw std::runtime_error("Invalid RPG destruction target");seen[i]=true;
            float at=row[1],health=row[2],burn=row[3],rest=row[4];
            if(!std::isfinite(at)||at< -1||at>w.elapsed+.001f||!std::isfinite(health)||health<0||health>1e9f||!std::isfinite(burn)||burn<0||burn>30||!std::isfinite(rest)||std::abs(rest)>10000)
                throw std::runtime_error("Invalid RPG destruction scalar");
            auto &t=w.targets[i];t.rpgDeathAt=at;t.rpgInitialHealth=health;t.rpgBurn=burn;t.rpgRestY=rest;t.rpgImpactSpent=row[5].get<bool>();t.rpgSecondarySpent=row[6].get<bool>();t.processed=row[7].get<bool>();
        }
    }
    for(auto &p:w.pickups)p.collected=(p.collectedBy&(1u<<w.playerSlot))!=0;
}
bool validLoadout(const Loadout& l,const Progression& p){for(int i=0;i<MountCount;++i)if(!p.weaponAvailable(l[i])||!compatible(l[i],i)||std::count(l.begin(),l.end(),l[i])>weaponLimit(l[i]))return false;return !(l[LeftHip]==Weapon::AutoGrenade&&l[RightHip]!=Weapon::Empty)&&!(l[RightHip]==Weapon::AutoGrenade&&l[LeftHip]!=Weapon::Empty);}
}
