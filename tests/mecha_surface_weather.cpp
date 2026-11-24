#include "surface_shelter.hpp"
#include "surface_weather.hpp"
#include <iostream>
#include <stdexcept>

using namespace iron;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){
    for(auto weather:{Weather::Sandstorm,Weather::Snowstorm,Weather::AshCloud,Weather::FireStorm,
                      Weather::CorruptionSporeBurst,Weather::Rain,Weather::Storm}){
        SurfaceWeather empty{};
        auto full=surfaceWeather(Weather::Clear,weather,30);
        require(full!=empty,"Depositing front has no surface material");
        require(surfaceWeather(Weather::Clear,weather,0)==empty,"Fresh weather coated the ground instantly");
        require(surfaceWeather(weather,Weather::Clear,0)==full,"New front discarded the old material");
        require(surfaceWeather(weather,Weather::Clear,30)==empty,"Old material did not clear");
        require(surfaceWeather(weather,weather,0)==surfaceWeather(weather,weather,120),"Repeated fronts restarted accumulation");
        for(float time=1;time<30;++time){
            auto now=surfaceWeather(Weather::Clear,weather,time);
            auto last=surfaceWeather(Weather::Clear,weather,time-1);
            for(size_t i=0;i<full.size();++i)
                if(full[i]>0)require(now[i]>last[i]&&now[i]<full[i],"Deposit failed to accumulate progressively");
        }
    }
    require(surfaceWeather(Weather::Clear,Weather::IonStorm,60)==SurfaceWeather{},
            "Electrical storm deposited material");
    const auto change=surfaceWeather(Weather::Snowstorm,Weather::Sandstorm,15);
    require(change[0]==.5f&&change[1]==.5f,"Transition between two materials broke");
    World world;world.movementBounds={0,0,100,100};
    world.interiorRegions.push_back({{8,8,30,30},0,50});
    world.interiorDecks.push_back({{40,40,80,80},{40,12,50},{80,32,50}});
    world.waterRegions.push_back({{0,40,36,80},6,8,0,{{0,40},{36,40},{0,80}}});
    SurfaceShelter cover(world);
    auto top=[&](int x,int z){return cover.ceiling[size_t(z/2)*cover.width+x/2];};
    require(top(5,5)<-100&&top(15,15)==50,"Roof footprint leaks onto open ground");
    require(std::abs(top(51,51)-16.3f)<.01f,"Bridge underside does not follow the ramp");
    require(top(5,51)==6&&top(33,75)<-100,"Water mask lost the shoreline");
    std::cout<<"Front growth, carryover, clearing, roofs, ramps and shoreline passed\n";
}
