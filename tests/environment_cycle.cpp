#include "environment.hpp"
#include "simulation.hpp"
#include "skirmish.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
using namespace iron;
int main(){
    EnvironmentCycle cycle;cycle.seconds=0;cycle.startHour=17.99;
    assert(cycle.sample(Climate::Temperate).settings.lighting==Lighting::Day);
    cycle.advance(1);assert(cycle.sample(Climate::Desert).settings.lighting==Lighting::Night);
    auto wire=environmentCycleWire(cycle);auto other=readEnvironmentCycle(wire);
    assert(environmentCycleWire(other)==wire);
    unsigned seen=0;
    for(int front=0;front<400;++front){cycle.seconds=front*cycle.weatherSeconds+10;
        auto humid=cycle.sample(Climate::Temperate),dry=cycle.sample(Climate::Desert);
        assert(humid.hour==dry.hour&&humid.weatherSeconds==dry.weatherSeconds&&humid.settings.seed==dry.settings.seed);
        assert(weatherAllowed(Climate::Temperate,humid.settings.weather)&&weatherAllowed(Climate::Desert,dry.settings.weather));
        seen|=1u<<unsigned(humid.settings.weather);seen|=1u<<unsigned(dry.settings.weather);
        assert(environmentDescription(humid,true).starts_with("SERVER "));
    }
    assert(seen==31);
    for(const char* key:{"seconds","day_seconds","weather_seconds","start_hour","seed"}){auto bad=wire;bad[key]=-1;bool rejected=false;try{readEnvironmentCycle(bad);}catch(...){rejected=true;}assert(rejected);}
    World campaign;campaign.environment={Lighting::Night,Weather::Storm,42};campaign.environmentSeconds=7;
    assert(campaign.activeEnvironment().lighting==Lighting::Authored&&campaign.activeEnvironment().weather==Weather::Clear);
    campaign.assault=true;campaign.climate=Climate::Desert;assert(campaign.activeEnvironment().weather==Weather::Clear);
    campaign.environment.weather=Weather::Sandstorm;assert(campaign.activeEnvironment().weather==Weather::Sandstorm&&campaign.weatherTime()==7);
    campaign.climate=Climate::Temperate;assert(campaign.activeEnvironment().weather==Weather::Clear);
    SkirmishSettings settings;settings.terrain="desert";settings.environment.weather=Weather::Rain;
    assert(generateSkirmishRecipe(settings)["environment"]["weather"]=="clear");
    std::cout<<"Shared hours, regional fronts, climate/mode restrictions and wire validation passed\n";
}
