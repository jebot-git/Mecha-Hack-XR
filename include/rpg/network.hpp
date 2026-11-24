#pragma once
#include "rpg/session.hpp"
namespace iron::rpg {
inline constexpr uint16_t publicLobbyPort=29597;
inline std::string publicLobbyEndpoint(unsigned port=publicLobbyPort){
  return "87.106.27.237:"+std::to_string(port);
}
class Server {
  struct Peer {
    uint32_t id = 0;
    float age = 0, budget = 120;
    bool sparseScenery=false,compact=false;
    uint64_t baselineSent=0,contextSent=0;
  };
  coop::Transport transport;
  avp::voice::Relay voice;
  std::map<ENetPeer *, Peer> peers;
  uint32_t nextId = 1;
  uint64_t tick = 0, soundEpoch = 0;
  std::vector<SoundEvent> pendingSounds;
  coop::SceneryBaseline scenery;
  std::shared_ptr<const int> sceneryMap;
  uint64_t sceneryRevision=0,contextRevision=0;
  Json snapshotContext;

public:
  Session session;
  Server(const std::string &bind = "127.0.0.1", uint16_t port = defaultPort,
         uint32_t seed = 1);
  uint16_t port() const { return transport.port(); }
  uint16_t voicePort() const { return voice.port(); }
  void step(float dt = 1.f / 60);
};
class Client {
  coop::Transport transport;
  ENetPeer *peer = nullptr;
  std::string playerName;
  float age = 0, inputClock = 0, snapshotAge = 0;
  uint64_t lastTick = 0,sceneryRevision=0,contextRevision=0;
  Json snapshotContext;
  Json scenery=Json::array();
  std::array<bool, MountCount> pending{};
  std::array<bool, 2> punches{};
  bool pendingThrust=false,pendingAscent=false,pendingHeadlight=false,pendingFireMode=false;

public:
  uint32_t id = 0;
  bool connected = false;
  std::string error;
  std::string voiceHost;
  Json voiceCredentials;
  Session session;
  Client(const std::string &endpoint, const std::string &name);
  ~Client();
  void update(float dt, const Input * = nullptr);
  bool disconnected() const {return peer==nullptr;}
  void command(Json);
  float poseLead() const {return std::min(snapshotAge,1.f/20); }
  std::vector<avp::voice::Participant> participants() const;
};
} // namespace iron::rpg
