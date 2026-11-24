#include "coop.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace iron;
using namespace iron::coop;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    Server server("/no-retail-required","127.0.0.1",0);EnvironmentCycle clock;clock.seconds=42;server.setWorldClock(clock);
    std::string endpoint="127.0.0.1:"+std::to_string(server.port());
    EnvironmentClient a(endpoint),b(endpoint);
    // Long enough for multiple reliable heartbeats and the old unjoined-peer expiry.
    for(int i=0;i<1200;++i){server.step();a.update(tick);b.update(tick);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(a.synchronized()&&b.synchronized(),"clock subscriptions failed or expired");
    auto x=a.sample(Climate::Desert),y=b.sample(Climate::Temperate);
    check(std::abs(x.hour-y.hour)<.02&&std::abs(x.weatherSeconds-y.weatherSeconds)<.2,"subscribers do not share a clock");
    auto state=server.report();check(state["rooms"].empty()&&state["peers"]==2,"clock access admitted a campaign lobby");
    check(state["world_clock"]["seconds"].get<double>()>61,"world clock stopped while no lobbies existed");
    // The ordinary campaign transport must reject the retired environment override.
    Transport raw;auto* peer=raw.connect(endpoint);bool rejected=false,sent=false;
    for(int i=0;i<300;++i){server.step();for(auto e:raw.poll()){
        if(e.type==Event::Connected&&!sent){raw.send(peer,{{"op","create"},{"version",protocol},{"name","Probe"},{"campaign",0},{"completed",0},{"searchlight_preview",true}});sent=true;}
        if(e.type==Event::Message&&e.message.value("type","")=="error"&&e.message.value("error","").find("Skirmish or Assault")!=std::string::npos)rejected=true;
    }raw.flush();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(rejected&&server.report()["rooms"].empty(),"campaign environment override was accepted");
    std::cout<<"ENet clock subscribers share time, survive idle, create no lobby and reject campaign weather overrides\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
