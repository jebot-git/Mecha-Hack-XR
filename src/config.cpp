#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <cmath>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace iron {
std::string validatePlayerName(std::string name){
    auto first=name.find_first_not_of(' '),last=name.find_last_not_of(' ');
    if(first==std::string::npos)throw std::runtime_error("Player name cannot be blank");
    name=name.substr(first,last-first+1);
    if(name.size()>20)throw std::runtime_error("Player name must be at most 20 characters");
    for(unsigned char c:name)if(c<32||c>126)throw std::runtime_error("Player name must use printable ASCII characters");
    return name;
}
std::filesystem::path ClientConfig::defaultPath(){
#ifdef _WIN32
    if(auto p=_wgetenv(L"APPDATA");p&&*p)return std::filesystem::path(p)/"iron-soldier-xr/config.json";
#endif
    if(auto p=std::getenv("XDG_CONFIG_HOME");p&&*p)return std::filesystem::path(p)/"iron-soldier-xr/config.json";
    if(auto p=std::getenv("HOME");p&&*p)return std::filesystem::path(p)/".config/iron-soldier-xr/config.json";
    return "config.json";
}
void ClientConfig::load(const std::filesystem::path& path,bool required){
    if(!std::filesystem::exists(path)&&!required)return;
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot read client config: "+path.string());
    auto j=nlohmann::json::parse(f);
    if(!j.is_object())throw std::runtime_error("Client config must be a JSON object");
    for(auto it=j.begin();it!=j.end();++it)if(it.key()!="server"&&it.key()!="player_name"&&it.key()!="sound_volume"&&it.key()!="music_volume")throw std::runtime_error("Unknown client config key: "+it.key());
    ClientConfig next=*this;next.server=j.value("server",server);next.playerName=validatePlayerName(j.value("player_name",playerName));
    // Migrate the retired public endpoint saved by earlier test clients.
    if(next.server=="45.147.228.101:29597")next.server=defaultLobbyServer;
    if(next.server.empty())throw std::runtime_error("Lobby server cannot be empty");
    auto volume=[&](const char* key,float current){if(!j.contains(key))return current;if(!j[key].is_number())throw std::runtime_error(std::string(key)+" must be a number from 0 to 1");float v=j[key].get<float>();if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error(std::string(key)+" must be from 0 to 1");return v;};
    next.soundVolume=volume("sound_volume",soundVolume);next.musicVolume=volume("music_volume",musicVolume);*this=next;
}
void ClientConfig::saveAudio(const std::filesystem::path& path)const{
    if(!std::isfinite(soundVolume)||!std::isfinite(musicVolume)||soundVolume<0||soundVolume>1||musicVolume<0||musicVolume>1)throw std::runtime_error("Invalid audio volume");
    nlohmann::json j=nlohmann::json::object();
    if(std::filesystem::exists(path)){std::ifstream in(path);if(!in)throw std::runtime_error("Cannot read client config");in>>j;if(!j.is_object())throw std::runtime_error("Client config must be a JSON object");}
    j["sound_volume"]=soundVolume;j["music_volume"]=musicVolume;
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{
        {std::ofstream out(temporary);if(!out||!(out<<j.dump(2)<<'\n')||!out.flush())throw std::runtime_error("Cannot write client config");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace client config");
#else
        std::filesystem::rename(temporary,path);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
void ClientConfig::environment(){
    if(auto p=std::getenv("IRON_LOBBY_SERVER");p&&*p)server=p;
    if(auto p=std::getenv("IRON_PLAYER_NAME");p&&*p)playerName=validatePlayerName(p);
}
}
