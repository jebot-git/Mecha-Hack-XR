#include "coop.hpp"
#include <chrono>
#include <cmath>
namespace iron::coop {
EnvironmentClient::EnvironmentClient(const std::string& address):endpoint(address){
    cycle.seconds=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
EnvironmentClient::~EnvironmentClient(){if(peer)enet_peer_disconnect_now(peer,0);}
void EnvironmentClient::update(float dt){
    if(!std::isfinite(dt)||dt<0)dt=0;
    cycle.advance(dt);age+=dt;requestAge+=dt;retryAge+=dt;
    if(!peer&&retryAge>=30){try{peer=transport.connect(endpoint);}catch(const std::exception&){}retryAge=0;requestAge=100;}
    for(auto& e:transport.poll()){
        if(e.peer!=peer)continue;
        if(e.type==Event::Disconnected){peer=nullptr;age=100;continue;}
        if(e.type!=Event::Message)continue;
        if(e.message.value("type","")=="environment_clock"){
            try{auto incoming=readEnvironmentCycle(e.message.at("clock"));
                if(age<10&&incoming.seed==cycle.seed&&incoming.daySeconds==cycle.daySeconds&&incoming.weatherSeconds==cycle.weatherSeconds&&incoming.startHour==cycle.startHour&&std::abs(incoming.seconds-cycle.seconds)<1){
                    double corrected=cycle.seconds+std::clamp(incoming.seconds-cycle.seconds,-double(dt)*.25,double(dt)*.25);cycle=incoming;cycle.seconds=corrected;
                }else cycle=incoming;
                age=0;}catch(const std::exception&){age=100;}
        }else if(e.message.value("type","")=="error"){
            // A service without the clock capability must not interrupt local play.
            enet_peer_disconnect_now(peer,0);peer=nullptr;age=100;retryAge=0;
        }
    }
    if(peer&&peer->state==ENET_PEER_STATE_CONNECTED&&requestAge>=3){
        transport.send(peer,{{"op","world_clock"},{"version",protocol},{"clock_version",1}});requestAge=0;
    }
    transport.flush();
}
}
