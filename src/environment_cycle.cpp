#include "environment.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace iron {
Climate climateForBiome(std::string_view biome){
    if(biome=="desert")return Climate::Desert;
    if(biome=="coastal")return Climate::Coastal;
    if(biome=="urban"||biome=="grassland"||biome=="hills")return Climate::Temperate;
    throw std::invalid_argument("Unknown map climate");
}
bool weatherAllowed(Climate climate,Weather weather){
    if(size_t(climate)>=climateNames.size()||size_t(weather)>size_t(Weather::Sandstorm))return false;
    return climate==Climate::Desert?(weather==Weather::Clear||weather==Weather::Sandstorm):weather!=Weather::Sandstorm;
}
EnvironmentSettings restrictEnvironment(EnvironmentSettings s,Climate climate){if(!weatherAllowed(climate,s.weather))s.weather=Weather::Clear;return s;}
void EnvironmentCycle::advance(double dt){if(std::isfinite(dt)&&dt>=0)seconds+=dt;}
EnvironmentSample EnvironmentCycle::sample(Climate climate)const{
    if(!std::isfinite(seconds)||seconds<0||!std::isfinite(daySeconds)||daySeconds<=0||!std::isfinite(weatherSeconds)||weatherSeconds<=0||!std::isfinite(startHour))throw std::invalid_argument("Invalid world clock");
    double hour=std::fmod(startHour+std::fmod(seconds,daySeconds)/daySeconds*24,24);
    auto front=uint32_t(std::fmod(std::floor(seconds/weatherSeconds),4294967296.));
    float choice=weatherNoise(seed,front);
    Weather weather=climate==Climate::Desert?(choice>=.75f?Weather::Sandstorm:Weather::Clear):
        choice<.45f?Weather::Clear:choice<.60f?Weather::Fog:choice<.85f?Weather::Rain:Weather::Storm;
    return {{hour>=6&&hour<18?Lighting::Day:Lighting::Night,weather,seed^(front*0x9e3779b9u)},float(std::fmod(seconds,weatherSeconds)),hour};
}
EnvironmentCycle readEnvironmentCycle(const nlohmann::json& j){
    EnvironmentCycle c;c.seconds=j.at("seconds");c.daySeconds=j.at("day_seconds");c.weatherSeconds=j.at("weather_seconds");c.startHour=j.at("start_hour");
    double seed=j.at("seed");
    if(!std::isfinite(c.seconds)||c.seconds<0||c.seconds>1e12||!std::isfinite(c.daySeconds)||c.daySeconds<60||c.daySeconds>86400||
       !std::isfinite(c.weatherSeconds)||c.weatherSeconds<30||c.weatherSeconds>3600||!std::isfinite(c.startHour)||c.startHour<0||c.startHour>=24||
       !j.at("seed").is_number_integer()||seed<0||seed>UINT32_MAX)throw std::invalid_argument("Invalid world clock");
    c.seed=uint32_t(seed);return c;
}
nlohmann::json environmentCycleWire(const EnvironmentCycle& c){
    nlohmann::json regions=nlohmann::json::object();
    for(auto climate:{Climate::Temperate,Climate::Desert,Climate::Coastal}){auto s=c.sample(climate);regions[climateNames[size_t(climate)]]={{"environment",environmentWire(s.settings)},{"weather_age",s.weatherSeconds},{"hour",s.hour},{"description",environmentDescription(s,true)}};}
    return {{"seconds",c.seconds},{"day_seconds",c.daySeconds},{"weather_seconds",c.weatherSeconds},{"start_hour",c.startHour},{"seed",c.seed},{"regions",regions}};
}
std::string environmentDescription(const EnvironmentSample& s,bool shared){
    char time[16];unsigned minute=unsigned(s.hour*60)%1440;std::snprintf(time,sizeof time,"%02u:%02u",minute/60,minute%60);
    const auto *hazard=weatherHazard(s.settings.weather);
    std::string text=std::string(shared?"SERVER ":"LOCAL / OFFLINE ")+time+" / "+std::string(lightingNames[size_t(s.settings.lighting)])+" / "+std::string(hazard?hazard->label:weatherNames[size_t(s.settings.weather)]);
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(c>='a'&&c<='z'?c-'a'+'A':c);});return text;
}
}
