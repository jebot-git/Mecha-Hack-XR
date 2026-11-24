// Offline audition of the shipping environmental bus, driven by a 60 Hz game clock.
#include "weather_audio.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iron;

struct Scene {
    const char* title;
    float duration;
    Ambience ambience;
    Weather weather;
    Lighting light=Lighting::Day;
    bool sheltered=false;
    bool underwater=false;
};

int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"Usage: mecha-environment-audition <fx/audio> <output.f32le>\n";return 1;}
    try{
        WeatherAudio audio;
        loadEnvironmentRecordings(audio,argv[1]);
        std::ofstream file(argv[2],std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::failbit|std::ios::badbit);
        constexpr Scene scenes[]{
            {"City yard, day",4,Ambience::Urban,Weather::Clear},
            {"City yard, night",4,Ambience::Urban,Weather::Clear,Lighting::Night},
            {"Canopy birds",4,Ambience::Wilderness,Weather::Clear},
            {"Insects after dark",4,Ambience::Wilderness,Weather::Clear,Lighting::Night},
            {"Rain over open ground",4,Ambience::Wilderness,Weather::Rain},
            {"Lightning and thunder",6,Ambience::Urban,Weather::Storm},
            {"Inside the hangar",4,Ambience::Interior,Weather::Storm,Lighting::Day,true},
            {"Sand against the cockpit",4,Ambience::Desert,Weather::Sandstorm},
            {"Polar wind and hail",4,Ambience::Polar,Weather::Snowstorm},
            {"Below the surface",4,Ambience::Count,Weather::Clear,Lighting::Day,false,true},
            {"Volcanic fire",4,Ambience::Volcanic,Weather::FireStorm},
            {"Hull under a meteor shower",4,Ambience::Hull,Weather::MeteorShower}
        };
        constexpr int rate=48000,block=800;
        constexpr uint32_t seed=2097;
        std::array<float,block*2> bus;
        int elapsed=0;
        float clock=0;
        for(const auto& scene:scenes){
            std::cout<<float(elapsed)/rate<<"  "<<scene.title<<'\n';
            if(scene.weather==Weather::Storm&&!scene.sheltered)
                clock=34.f-weatherNoise(seed,0)*10.f;
            for(int tick=0;tick<int(scene.duration*60);++tick){
                audio.update({scene.light,scene.weather,seed},clock,true,scene.underwater,scene.ambience,scene.sheltered);
                bus.fill(0);
                audio.mix(bus,1.f);
                for(float v:bus)
                    if(!std::isfinite(v)||std::abs(v)>=1.f)throw std::runtime_error("Invalid audio bus sample");
                if constexpr(std::endian::native!=std::endian::little)
                    throw std::runtime_error("Render the f32le review on a little-endian host");
                file.write(reinterpret_cast<const char*>(bus.data()),sizeof(bus));
                elapsed+=block;
                clock+=block/float(rate);
            }
        }
        const auto stats=audio.statistics();
        if(!stats.recordedThunderFrames||!stats.biomeFrames||!stats.recordedFrames)
            throw std::runtime_error("Review is missing a recorded layer");
        return 0;
    }catch(const std::exception& err){std::cerr<<err.what()<<'\n';return 1;}
}
