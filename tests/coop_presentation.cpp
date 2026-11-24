// Finite visual QA fixture: three bots plus one real desktop/XR client.
#include "coop.hpp"
#include <chrono>
#include <fstream>
#include <thread>
using namespace iron;using namespace iron::coop;
int main(int argc,char** argv){if(argc!=3)return 2;Server server(argv[1],"127.0.0.1",0);Progression campaign;Client host("127.0.0.1:"+std::to_string(server.port()),true,campaign);std::vector<std::unique_ptr<Client>> bots;bool selected=false,ready=false;float joined=0;uint32_t fourth=0;
    for(int frame=0;frame<4800;++frame){auto start=std::chrono::steady_clock::now();server.step();host.update(tick);for(auto& b:bots)b->update(tick);
        if(!host.code.empty()&&bots.empty()){for(int i=0;i<2;++i)bots.push_back(std::make_unique<Client>("127.0.0.1:"+std::to_string(server.port()),false,campaign,host.code));std::ofstream out(argv[2]);out<<Json{{"server","127.0.0.1:"+std::to_string(server.port())},{"code",host.code}};}
        if(!host.state.empty()&&host.state.at("players").size()==4){joined+=tick;if(joined>3&&!selected){host.send({{"op","select"},{"mission",0}});selected=true;}
            if(host.phase()==Phase::Briefing&&!ready){host.send({{"op","ready"},{"ready",true},{"loadout",campaign.loadout}});for(auto& b:bots)b->send({{"op","ready"},{"ready",true},{"loadout",campaign.loadout}});ready=true;}
        }
        if(frame%30==0&&!host.state.empty()){std::ofstream out(std::string(argv[2])+".state");out<<host.state.dump();}
        std::this_thread::sleep_until(start+std::chrono::microseconds(16667));
    }
}
