#pragma once
#include "frontend.hpp"
#include "net/voice_wire.hpp"
#include "net/room_workers.hpp"
#include "coop_snapshot.hpp"
#include "coop_prediction.hpp"
#include <enet/enet.h>
#include <map>
#include <span>
namespace iron { void to_json(nlohmann::json&,const SoundEvent&); }
namespace iron::coop {
using Json=nlohmann::json;
inline constexpr uint16_t defaultPort=29597;
inline constexpr uint32_t protocol=0x49535839;
inline constexpr float tick=1.f/60;
struct Event { enum Type { Connected, Disconnected, Message } type; ENetPeer* peer; Json message; uint8_t channel=0; };
class Transport {
    ENetHost* host=nullptr;
    uint64_t droppedSnapshots=0;bool compressDatagrams=false;
public:
    Transport(bool server=false,const std::string& bind="0.0.0.0",uint16_t port=0);
    ~Transport();
    Transport(const Transport&)=delete;
    ENetPeer* connect(const std::string&);
    std::vector<Event> poll();
    void send(ENetPeer*,const Json&,uint8_t channel=0);
    void sendEncoded(ENetPeer*,const std::vector<uint8_t>&,uint8_t channel=0);
    void broadcastEncoded(std::span<ENetPeer* const>,const std::vector<uint8_t>&,uint8_t channel=0);
    Json stats() const;
    void setDatagramCompression(bool);
    void flush();
    uint16_t port() const;
};
Json inputWire(const Input&);
Input inputRead(const Json&);
Json pilotWire(const Pilot&);
void pilotRead(const Json&,Pilot&);
struct SceneryBaseline {
    Json rows=Json::array();
    std::vector<std::array<float,28>> states;
};
SceneryBaseline captureScenery(const World&,size_t actors);
Json worldWire(const World&,const SceneryBaseline* baseline=nullptr);
void worldRead(const Json&,World&,const Json* scenery=nullptr);
bool validLoadout(const Loadout&,const Progression&);
struct Player {
    uint32_t id=0; unsigned slot=0; bool ready=false; Loadout loadout{Weapon::Empty,Weapon::Rifle,Weapon::Empty,Weapon::Empty,Weapon::Empty,Weapon::Empty};
    std::string name="Pilot";
    Pilot pilot; Input input; float inputAge=100;
    Vec3 velocity{};std::array<float,4> angularVelocity{};
};
enum class Phase { Selecting, Briefing, Playing, Complete, Failed };
// Deterministic nearby starts, checked against scenery, actor starts and teammates.
std::vector<Vec3> spawnPositions(const World&,std::span<const unsigned> slots);
struct Room {
    uint64_t id=0,revision=0,round=0,selection=0;uint32_t host=0;std::string code,outcome;
    Progression campaign;World world;World::Difficulty difficulty=World::Difficulty::Hard;
    SnapshotBaseline baseline;
    Phase phase=Phase::Selecting;int mission=0;std::vector<Player> players;
    explicit Room(const std::filesystem::path& retail):retail(retail){}
    void command(uint32_t,const Json&);
    void step(float dt);
    Json state() const;
    Player& player(uint32_t);
    void add(uint32_t);
    void remove(uint32_t);
    void start();
private:
    std::filesystem::path retail;
    void links(size_t current);
    void returnToSelection(const std::string& reason);
};
// One process/ENet socket for all code-addressed rooms; one authenticated UDP voice relay.
class Server {
    struct Peer {uint32_t id;std::string code,outcome;float age=0,budget=180;bool compact=false;uint64_t baselineSent=0,baselineAck=0;bool clockSubscriber=false;};
    std::filesystem::path retail;
    Transport transport;avp::voice::Relay voice;
    RoomWorkers workers;
    std::map<ENetPeer*,Peer> peers;std::map<std::string,std::unique_ptr<Room>> rooms;
    uint32_t nextPlayer=1;uint64_t nextRoom=1;unsigned frames=0;float lobbyClock=0;
    EnvironmentCycle worldClock;
    struct Metrics {uint64_t ticks=0,overBudget=0,snapshots=0,snapshotBytes=0;double networkMs=0,roomsMs=0,sendMs=0,totalMs=0,maxMs=0,simulationMs=0,encodeMs=0;std::array<double,5> stageTotals{};} metrics;
    void publish(Room&);void leave(ENetPeer*);void message(ENetPeer*,const Json&,uint8_t);
public:
    Server(const std::filesystem::path&,const std::string&,uint16_t,uint16_t=0,unsigned workers=1);
    void step(float dt=tick);
    uint16_t port() const{return transport.port();}
    uint16_t voicePort() const{return voice.port();}
    Json report() const;
    void setWorldClock(const EnvironmentCycle& c){worldClock=c;}
};
// Read-only clock subscription: no lobby, player admission, voice or campaign state.
class EnvironmentClient {
    Transport transport;ENetPeer* peer=nullptr;std::string endpoint;
    EnvironmentCycle cycle;double age=100,requestAge=100,retryAge=100;
public:
    explicit EnvironmentClient(const std::string&);
    ~EnvironmentClient();
    void update(float dt);
    bool synchronized()const{return peer&&age<10;}
    EnvironmentSample sample(Climate c)const{return cycle.sample(c);}
};
class Client {
    Transport transport;ENetPeer* peer=nullptr;std::string endpoint;Json hello;
    std::array<bool,MountCount> pendingFire{};std::array<bool,2> pendingPunch{},pingHeld{};
    bool pendingSearchlight=false,lightReleaseRequired=true;
    double age=0,inputClock=0;uint64_t lastSnapshot=0;
public:
    bool connected=false,hosting=false;uint32_t id=0;uint64_t roomId=0,round=0,revision=0;
    std::string code,error;Json state,voiceCredentials;std::string voiceHost;
    Client(const std::string& endpoint,bool host,const Progression&,const std::string& code="",const std::string& name="Pilot",bool compact=true,bool searchlights=false,const EnvironmentSettings& environment=EnvironmentSettings{});
    ~Client();
    void update(float dt,const Input* input=nullptr);
    void send(Json command);
    bool apply(World&,const std::filesystem::path&,const ArmRig&);
    std::vector<avp::voice::Participant> participants() const;
    const Json* local() const;
    Phase phase() const;
private:
    uint64_t appliedRound=0,appliedTick=0;
    SnapshotBaseline baseline;uint64_t baselineRound=0,baselineRoom=0,requestedBaselineRound=0;
    MotionPrediction prediction;double snapshotAge=0;
};
}
