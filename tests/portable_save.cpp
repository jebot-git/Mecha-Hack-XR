#include "progression.hpp"
#include "config.hpp"
#include <barrier>
#include <chrono>
#include <fstream>
#include <exception>
#include <iostream>
#include <thread>

int main() {
    namespace fs=std::filesystem;
    auto unique=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto folder=fs::temp_directory_path()/fs::path(u8"iron-save-ČR")/unique;
    fs::create_directories(folder);
    auto path=folder/fs::path(u8"Kampaň.json");
    try {
        auto configPath=folder/fs::path(u8"Zvuk-ČR.json");
        {std::ofstream config(configPath);config<<R"({"server":"example.test:29597","player_name":"Ada"})";}
        iron::ClientConfig audio;audio.load(configPath,true);audio.soundVolume=.5f;audio.musicVolume=.75f;audio.saveAudio(configPath);
        audio.musicVolume=.25f;audio.saveAudio(configPath);iron::ClientConfig loaded;loaded.load(configPath,true);
        if(loaded.soundVolume!=.5f||loaded.musicVolume!=.25f||loaded.playerName!="Ada"||loaded.server!="example.test:29597")
            throw std::runtime_error("Audio settings replacement lost values or lobby config");
#ifdef _WIN32
        if(_wputenv_s(L"LOCALAPPDATA",folder.c_str())||_wputenv_s(L"APPDATA",folder.c_str()))
            throw std::runtime_error("Cannot set Unicode profile path");
        if(iron::Progression::defaultPath()!=folder/"iron-soldier-xr/campaign.json"||
           iron::ClientConfig::defaultPath()!=folder/"iron-soldier-xr/config.json")
            throw std::runtime_error("Unicode Windows profile path was lost");
#endif
        iron::Progression a,b;
        a.complete(0);a.save(path);
        b.complete(2);b.save(path);
        iron::Progression restored;restored.load(path);
        if(restored.completed!=5)throw std::runtime_error("Stale-writer merge failed");
        fs::remove(path);
        std::barrier start(3);
        std::exception_ptr errors[2];
        auto write=[&](int i,iron::Progression value){
            start.arrive_and_wait();
            try {for(int n=0;n<10;++n)value.save(path);}
            catch(...){errors[i]=std::current_exception();}
        };
        std::thread first(write,0,a),second(write,1,b);
        start.arrive_and_wait();first.join();second.join();
        for(auto& error:errors)if(error)std::rethrow_exception(error);
        restored.load(path);
        if(restored.completed!=5)throw std::runtime_error("Concurrent merge lost progress");
        const std::string corrupt="{\"schema\":2}";
        {std::ofstream file(path);file<<corrupt;}
        bool rejected=false;try {a.save(path);}catch(...){rejected=true;}
        std::ifstream input(path);std::string contents{std::istreambuf_iterator<char>(input),{}};input.close();
        if(!rejected||contents!=corrupt)throw std::runtime_error("Corrupt save was overwritten");
        for(auto& entry:fs::directory_iterator(folder))
            if(entry.path().filename().string().find(".tmp.")!=std::string::npos)
                throw std::runtime_error("Temporary save leaked");
        fs::remove_all(folder);
        std::cout<<"Unicode paths, audio settings replacement, concurrent campaign merge and corrupt-save preservation passed\n";
        return 0;
    }catch(const std::exception& error){fs::remove_all(folder);std::cerr<<error.what()<<'\n';return 1;}
}
