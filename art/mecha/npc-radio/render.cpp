// Offline listening review. Run the shipping callback on a deterministic clock.
#include "audio.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace iron;
namespace fs = std::filesystem;
namespace { SDL_AudioSpec output{}; }
extern "C" SDL_AudioDeviceID __real_SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*,SDL_AudioSpec*,int);
extern "C" SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char* name,int recording,const SDL_AudioSpec* want,SDL_AudioSpec* obtained,int changes){
    if(!recording)output=*want;
    return __real_SDL_OpenAudioDevice(name,recording,want,obtained,changes);
}
// The dummy device opens paused. Only the review loop advances its callback.
extern "C" void __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID,int){}

int main(int argc,char** argv)try{
    if(argc!=6)throw std::runtime_error("Usage: mecha-radio-audition <asset pack> <clip number> <bearing degrees> <distance metres> <stereo.f32>");
    const fs::path root=argv[1];
    const int clip=std::stoi(argv[2]);
    const float bearing=std::stof(argv[3])*std::numbers::pi_v<float>/180.f,distance=std::stof(argv[4]);
    if(clip<0||clip>=rpg::npcRadioClips||distance<=0||distance>=rpg::npcRadioMaxRange)
        throw std::runtime_error("Invalid clip or transmission distance");
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    Audio audio(root,true);
    if(!audio.available()||output.freq!=48000||output.channels!=2||output.format!=AUDIO_F32SYS)
        throw std::runtime_error("48 kHz stereo mixer required");
    const auto file=root/"fx/audio/radio"/(std::to_string(clip)+".wav");
    SDL_AudioSpec source{}; Uint32 bytes=0;Uint8* samples=nullptr;
    if(!SDL_LoadWAV(file.string().c_str(),&source,&samples,&bytes))throw std::runtime_error(SDL_GetError());
    SDL_FreeWAV(samples);
    const int frames=int(double(bytes)*48000/(source.freq*source.channels*(SDL_AUDIO_BITSIZE(source.format)/8)))+12000;
    World world;world.targets.clear();world.sounds.clear();world.rpgPrototype=false;
    world.environment.weather=Weather::Clear;world.missionStatus=MissionStatus::Active;
    AudioListener head;head.position=world.eye();
    auto position=head.position+Vec3{std::sin(bearing)*distance,0,-std::cos(bearing)*distance};
    world.sounds.push_back({SoundEvent::Special,rpg::npcRadioBase+clip,position});
    audio.setVolumes(1,0);
    std::ofstream out(argv[5],std::ios::binary);
    out.exceptions(std::ios::failbit|std::ios::badbit);
    std::array<float,1024> buffer;
    for(int start=0;start<frames;start+=512){
        const int count=std::min(512,frames-start);
        world.elapsed=start/48000.f;audio.update(world,true,false,&head);
        auto bytesToMix=int(2*count*sizeof(float));
        output.callback(output.userdata,reinterpret_cast<Uint8*>(buffer.data()),bytesToMix);
        out.write(reinterpret_cast<const char*>(buffer.data()),bytesToMix);
    }
    if(audio.statistics().npcRadioStarted!=1)throw std::runtime_error("Radio failed to start");
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
