#include "coop.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
static volatile std::sig_atomic_t stopped=0;
int main(int argc,char** argv){try{
    iron::EnvironmentCycle worldClock;worldClock.seconds=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::filesystem::path retail=IRON_ASSETS,ready,report;std::string bind="0.0.0.0";unsigned port=iron::coop::defaultPort,voice=0,ticks=0,workers=std::clamp(std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1u,1u,8u);
    for(int i=1;i<argc;++i){std::string a=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing "+a);return std::string(argv[i]);};
        if(a=="--world-day-seconds")worldClock.daySeconds=std::stod(value());
        else if(a=="--world-weather-seconds")worldClock.weatherSeconds=std::stod(value());
        else if(a=="--world-start-hour")worldClock.startHour=std::stod(value());
        else if(a=="--world-clock-seconds")worldClock.seconds=std::stod(value());
        else if(a=="--world-seed"){auto seed=std::stoull(value());if(seed>UINT32_MAX)throw std::runtime_error("World seed must be uint32");worldClock.seed=uint32_t(seed);}
        else if(a=="--workers"){auto text=value();size_t used=0;auto n=std::stoul(text,&used);if(used!=text.size()||n<1||n>16)throw std::runtime_error("Workers must be between 1 and 16");workers=unsigned(n);}else if(a=="--bind")bind=value();else if(a=="--port")port=std::stoul(value());else if(a=="--voice-port")voice=std::stoul(value());else if(a=="--retail")retail=value();else if(a=="--ticks")ticks=std::stoul(value());else if(a=="--ready-file")ready=value();else if(a=="--report")report=value();else if(a=="--help"){std::cout<<"iron-lobby-server [--bind IP] [--port 29597] [--voice-port 29598] [--retail PATH] [--workers 1..16] [--ticks N] [--ready-file PATH] [--report PATH] [--world-day-seconds 1440] [--world-weather-seconds 120] [--world-start-hour 8] [--world-seed N] [--world-clock-seconds N]\n";return 0;}else throw std::runtime_error("Unknown option "+a);
    }
    if(port>65535||voice>65535)throw std::runtime_error("Invalid port");
    iron::coop::Server server(retail,bind,uint16_t(port),uint16_t(voice),workers);server.setWorldClock(iron::readEnvironmentCycle(nlohmann::json{{"seconds",worldClock.seconds},{"day_seconds",worldClock.daySeconds},{"weather_seconds",worldClock.weatherSeconds},{"start_hour",worldClock.startHour},{"seed",worldClock.seed}}));std::signal(SIGINT,[](int){stopped=1;});std::signal(SIGTERM,[](int){stopped=1;});
    if(!ready.empty()){std::ofstream out(ready);out<<nlohmann::json{{"port",server.port()},{"voice_port",server.voicePort()}};}
    std::cout<<"Iron Soldier lobby server: UDP "<<server.port()<<", voice UDP "<<server.voicePort()<<", workers "<<workers<<"\n"<<std::flush;
    auto deadline=std::chrono::steady_clock::now();for(unsigned frame=0;!stopped&&(!ticks||frame<ticks);++frame){server.step();deadline+=std::chrono::microseconds(16667);std::this_thread::sleep_until(deadline);if(std::chrono::steady_clock::now()-deadline>std::chrono::seconds(1))deadline=std::chrono::steady_clock::now();}
    if(!report.empty()){std::ofstream out(report);out<<server.report().dump(2);}return 0;
}catch(const std::exception& e){std::cerr<<"Lobby server: "<<e.what()<<"\n";return 1;}}
