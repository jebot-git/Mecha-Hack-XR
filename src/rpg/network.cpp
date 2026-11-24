#include "rpg/network.hpp"
#include "rpg/snapshot.hpp"
#include "net/snapshot_packet.hpp"
namespace iron::rpg {
Server::Server(const std::string &bind, uint16_t port, uint32_t seed)
    : transport(true, bind, port), voice(bind,port?uint16_t(port==65535?65534:port+1):0), session(seed,true) {}
void Server::step(float dt) {
  if (!std::isfinite(dt) || dt <= 0 || dt > .1f)
    return;
  for (auto &[peer, state] : peers) {
    state.age += dt;
    state.budget = std::min(120.f, state.budget + 90 * dt);
  }
  for (auto event : transport.poll()) {
    if (event.type == coop::Event::Connected) {
      peers.emplace(event.peer, Peer{});
      continue;
    }
    auto it = peers.find(event.peer);
    if (it == peers.end())
      continue;
    auto &state = it->second;
    if (event.type == coop::Event::Disconnected) {
      if (state.id){voice.remove(state.id);session.remove(state.id);}
      peers.erase(it);
      continue;
    }
    try {
      state.age = 0;
      if (state.budget < 1)
        throw std::runtime_error("RPG command rate exceeded");
      --state.budget;
      auto &j = event.message;
      if (!state.id) {
        if (j.at("op") != "hello" || j.at("rpg") != schema)
          throw std::runtime_error("RPG handshake required");
        // A persistent dedicated process starts a fresh lobby after the last
        // member leaves, including when a previous party disconnected mid-mission.
        if(session.members.empty()&&nextId!=1){
          session=Session(session.seed,true);
          pendingSounds.clear();
        }
        uint32_t id = nextId++;
        session.add(id, j.at("name"));
        state.id = id;
        state.sparseScenery=j.value("scenery_baseline",0)==1;
        state.compact=state.sparseScenery&&j.value("compact_rpg",0)==1;
        transport.send(event.peer, {{"type", "welcome"}, {"id", state.id},{"voice",voice.admit(state.id)}});
      } else
        session.playerCommand(state.id, j);
    } catch (const std::exception &e) {
      transport.send(event.peer, {{"type", "error"}, {"message", e.what()}});
      if (!state.id)
        enet_peer_disconnect_later(event.peer, 1);
    }
  }
  for (auto it = peers.begin(); it != peers.end();) {
    if (it->second.age > 10) {
      if (it->second.id){voice.remove(it->second.id);session.remove(it->second.id);}
      enet_peer_disconnect_now(it->first, 0);
      it = peers.erase(it);
    } else
      ++it;
  }
  session.step(dt);
  if (soundEpoch != session.epoch || session.phase == Phase::Hangar) {
    pendingSounds.clear();
    soundEpoch = session.epoch;
  }
  if (session.phase == Phase::Combat || session.phase == Phase::Complete ||
      session.phase == Phase::Defeat) {
    for (const auto &event : session.world.sounds) {
      if (pendingSounds.size() == 128) pendingSounds.erase(pendingSounds.begin());
      pendingSounds.push_back(event);
    }
  }
  ++tick;
  if (tick % 3 == 0) {
    std::vector<ENetPeer*> legacy,sparse,compact;
    for(auto &[peer,state]:peers)if(state.id){
      if(state.compact)compact.push_back(peer);
      else if(state.sparseScenery)sparse.push_back(peer);
      else legacy.push_back(peer);
    }
    const bool useScenery=!sparse.empty()||!compact.empty();
    if(useScenery&&(sceneryMap!=session.world.renderIdentity||
        scenery.states.size()!=session.world.targets.size()-session.enemies.size())){
      scenery=coop::captureScenery(session.world,session.enemies.size());
      sceneryMap=session.world.renderIdentity;
      ++sceneryRevision;
    }
    Json sounds=pendingSounds;
    pendingSounds.clear();
    auto packetFor=[&](const coop::SceneryBaseline *base){
      auto state=session.snapshot(base);
      state["world"]["sounds"]=sounds;
      Json packet={{"type","snapshot"},{"tick",tick},{"state",std::move(state)}};
      if(base)packet["scenery_revision"]=sceneryRevision;
      return packet;
    };
    if(!legacy.empty())transport.broadcastEncoded(legacy,Json::to_cbor(packetFor(nullptr)),1);
    if(useScenery){
      Json packet=packetFor(&scenery);
      // Reliable keyframes include everything that changed on the same tick.
      // A late join always receives both the map rows and its career/loadout.
      auto publish=[&](const auto &recipients,bool packed){
        std::vector<ENetPeer*> regular,mapNeeded,contextNeeded;
        for(auto *peer:recipients){
          auto &client=peers.at(peer);
          if(client.baselineSent!=sceneryRevision)mapNeeded.push_back(peer);
          else if(packed&&client.contextSent!=contextRevision)contextNeeded.push_back(peer);
          else regular.push_back(peer);
          client.baselineSent=sceneryRevision;
          if(packed)client.contextSent=contextRevision;
        }
        if(!regular.empty()){
          auto bytes=packed?coop::encodeSnapshotPacket(packet):Json::to_cbor(packet);
          transport.broadcastEncoded(regular,bytes,1);
        }
        if(packed&&(!contextNeeded.empty()||!mapNeeded.empty()))packet["context"]=snapshotContext;
        if(!contextNeeded.empty())transport.broadcastEncoded(contextNeeded,coop::encodeSnapshotPacket(packet),0);
        if(!mapNeeded.empty()){
          packet["scenery_rows"]=scenery.rows;
          transport.broadcastEncoded(mapNeeded,coop::encodeSnapshotPacket(packet),0);
          packet.erase("scenery_rows");
        }
      };
      if(!sparse.empty())publish(sparse,false);
      if(!compact.empty()){
        auto context=packRpgSnapshot(packet.at("state"));
        if(context!=snapshotContext){snapshotContext=std::move(context);++contextRevision;}
        packet["context_revision"]=contextRevision;
        publish(compact,true);
      }
    }
  }
  std::vector<avp::voice::Participant> participants;
  for(const auto &m:session.members)participants.push_back({m.id,m.body.position,true,1});
  voice.poll(participants,session.epoch);
  transport.flush();
}
Client::Client(const std::string &endpoint, const std::string &name)
    : playerName(name) {
  voiceHost=avp::net::parseEndpoint(endpoint,defaultPort).host;
  peer = transport.connect(endpoint);
}
Client::~Client() {
  if (peer)
    enet_peer_disconnect_now(peer, 0);
}
void Client::command(Json j) {
  if (!connected || !id)
    return;
  transport.send(peer, j);
  transport.flush();
}
void Client::update(float dt, const Input *in) {
  pendingThrust=in&&in->focused&&(pendingThrust||in->thrust);
  pendingAscent=in&&in->focused&&(pendingAscent||in->ascend);
  pendingHeadlight=in&&in->focused&&(pendingHeadlight||in->searchlight);
  pendingFireMode=in&&in->focused&&(pendingFireMode||in->fireMode);
  if (!std::isfinite(dt) || dt < 0)
    return;
  age += dt;
  inputClock += dt;
  snapshotAge += dt;
  if (in && in->focused) {
    for (int m = 0; m < MountCount; ++m)
      pending[m] = pending[m] || in->fire[m];
    for (int h = 0; h < 2; ++h)
      punches[h] = punches[h] || in->punch[h];
  } else {
    pending.fill(false);
    punches.fill(false);
  }
  for (auto event : transport.poll()) {
    if (event.type == coop::Event::Connected) {
      connected = true;
      transport.send(peer,
                     {{"op", "hello"}, {"rpg", schema}, {"name", playerName},{"scenery_baseline",1},{"compact_rpg",1}});
      error.clear();
      age = 0;
    } else if (event.type == coop::Event::Disconnected) {
      connected = false;
      peer = nullptr;
      error = "RPG server disconnected";
    } else {
      age = 0;
      auto &j = event.message;
      std::string type = j.at("type");
      if (type == "welcome") {
        id = j.at("id");voiceCredentials=j.at("voice");
      }
      else if (type == "error")
        error = j.at("message");
      else if (type == "snapshot") {
        uint64_t tick = j.at("tick");
        const auto revision=j.value("scenery_revision",uint64_t{0});
        if(j.contains("scenery_rows")&&revision>sceneryRevision){
          const auto &rows=j.at("scenery_rows");
          if(!rows.is_array()||rows.size()>maxEncounterTargets)throw std::runtime_error("Invalid scenery baseline");
          scenery=std::move(j.at("scenery_rows"));sceneryRevision=revision;
        }
        // A later unreliable update may precede the reliable map on its channel.
        if(revision&&revision!=sceneryRevision)continue;
        const auto context=j.value("context_revision",uint64_t{0});
        if(context>contextRevision&&j.contains("context")){
          snapshotContext=std::move(j.at("context"));contextRevision=context;
        }
        if(context&&context!=contextRevision)continue;
        if (tick > lastTick) {
          if(context)unpackRpgSnapshot(j.at("state"),snapshotContext);
          auto sounds = std::move(session.world.sounds);
          auto oldEpoch = session.epoch;
          session.readSnapshot(j.at("state"),revision?&scenery:nullptr);
          if (oldEpoch == session.epoch) {
            sounds.insert(sounds.end(), session.world.sounds.begin(), session.world.sounds.end());
            if (sounds.size() > 128) sounds.erase(sounds.begin(), sounds.end() - 128);
            session.world.sounds = std::move(sounds);
          }
          lastTick = tick;
          snapshotAge = 0;
        }
      }
    }
  }
  if (peer && age > 10) {
    enet_peer_disconnect_now(peer, 0);
    peer = nullptr;
    connected = false;
    error = "RPG server timed out";
  }
  if (connected && id && inputClock >= 1.f / 30) {
    inputClock = 0;
    Input sample = in ? *in : Input{};
    if (!in)
      sample.focused = false;
    sample.thrust=sample.focused&&(sample.thrust||pendingThrust);
    sample.ascend=sample.focused&&(sample.ascend||pendingAscent);
    sample.searchlight=sample.focused&&(sample.searchlight||pendingHeadlight);
    sample.fireMode=sample.focused&&(sample.fireMode||pendingFireMode);
    pendingThrust=pendingAscent=pendingHeadlight=pendingFireMode=false;
    sample.fire = pending;
    sample.punch = punches;
    pending.fill(false);
    punches.fill(false);
    transport.send(peer,
                   {{"op", "input"},
                    {"epoch", session.epoch},
                    {"input", coop::inputWire(sample)},
                    {"systems", sample.ping}},
                   2);
  }
  transport.flush();
}
std::vector<avp::voice::Participant> Client::participants() const {
  std::vector<avp::voice::Participant> result;
  for(const auto &m:session.members)result.push_back({m.id,m.body.position,true,1});
  return result;
}
} // namespace iron::rpg
