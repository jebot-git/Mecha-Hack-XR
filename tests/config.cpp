#include "config.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace iron;
void check(bool x){if(!x)throw std::runtime_error("config check failed");}
int main(){auto path=std::filesystem::temp_directory_path()/("iron-config-"+std::to_string(getpid())+".json");try{
ClientConfig c;check(c.server=="87.106.27.237:29597");c.load(path);std::ofstream(path)<<R"({"server":"127.0.0.1:12345","player_name":"  Ada  "})";c.load(path,true);check(c.server=="127.0.0.1:12345"&&c.playerName=="Ada");
std::ofstream(path)<<R"({"server":"45.147.228.101:29597","player_name":"Ada","sound_volume":0.3,"music_volume":0.7})";ClientConfig migrated;migrated.load(path,true);check(migrated.server=="87.106.27.237:29597"&&migrated.playerName=="Ada"&&migrated.soundVolume==.3f&&migrated.musicVolume==.7f);
std::ofstream(path)<<R"({"server":"127.0.0.1:12345","player_name":"Ada"})";
c.soundVolume=.4f;c.musicVolume=.65f;c.saveAudio(path);ClientConfig restored;restored.load(path,true);check(restored.soundVolume==.4f&&restored.musicVolume==.65f&&restored.server==c.server&&restored.playerName=="Ada");
c.musicVolume=0;c.saveAudio(path);restored.load(path,true);check(restored.musicVolume==0&&restored.soundVolume==.4f);
for(auto bad:{R"({"sound_volume":1.1})",R"({"music_volume":-0.1})",R"({"sound_volume":true})",R"({"music_volume":"loud"})"}){std::ofstream(path)<<bad;bool rejected=false;try{restored.load(path,true);}catch(...){rejected=true;}check(rejected&&restored.soundVolume==.4f&&restored.musicVolume==0);}
std::ofstream(path)<<R"({"server":"127.0.0.1:12345","player_name":"Ada"})";
setenv("IRON_PLAYER_NAME","Grace",1);setenv("IRON_LOBBY_SERVER","example.test:29597",1);c.environment();check(c.playerName=="Grace"&&c.server=="example.test:29597");
for(auto name:{std::string(""),std::string("\nBad"),std::string(21,'x')}){bool rejected=false;try{validatePlayerName(name);}catch(...){rejected=true;}check(rejected);}
std::ofstream(path)<<R"({"player_name":2})";bool rejected=false;try{c.load(path,true);}catch(...){rejected=true;}check(rejected);std::filesystem::remove(path);std::cout<<"Config default/override and player names validated\n";
}catch(const std::exception& e){std::filesystem::remove(path);std::cerr<<e.what()<<'\n';return 1;}}
