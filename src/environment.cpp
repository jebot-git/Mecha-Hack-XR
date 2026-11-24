#include "environment.hpp"
#include "jaguar_cry.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace iron {
EnvironmentSettings readEnvironment(const nlohmann::json& j){
    if(!j.is_object())throw std::invalid_argument("Environment must be an object");
    EnvironmentSettings settings;auto lighting=j.value("lighting",std::string("authored")),weather=j.value("weather",std::string("clear"));
    auto l=std::find(lightingNames.begin(),lightingNames.end(),lighting),w=std::find(weatherNames.begin(),weatherNames.end(),weather);
    if(l==lightingNames.end()||w==weatherNames.end())throw std::invalid_argument("Unknown lighting or weather preset");
    settings.lighting=Lighting(l-lightingNames.begin());settings.weather=Weather(w-weatherNames.begin());
    if(j.contains("seed")&&(!j["seed"].is_number_integer()||j["seed"].get<double>()<0||j["seed"].get<double>()>UINT32_MAX))throw std::invalid_argument("Environment seed must be uint32");
    settings.seed=j.value("seed",1u);
    settings.authoredSky=j.value("authored_sky",false);return settings;
}
nlohmann::json environmentWire(const EnvironmentSettings& s){
    nlohmann::json j={{"lighting",lightingNames.at(size_t(s.lighting))},{"weather",weatherNames.at(size_t(s.weather))},{"seed",s.seed}};
    if(s.authoredSky)j["authored_sky"]=true;
    return j;
}
const WeatherHazard *weatherHazard(Weather weather){
    static constexpr std::array hazards{
        WeatherHazard{"ION STORM",{.28f,.23f,.43f},{.77f,.78f,1.f},{.64f,.73f,1.f},.006f,13.f,2.5f,0},
        WeatherHazard{"CORRUPTION SPORE BURST",{.37f,.29f,.36f},{.90f,.78f,.83f},{.88f,.52f,.67f},.010f,15.f,2.5f,12.f},
        WeatherHazard{"FIRE STORM",{.54f,.22f,.10f},{1.f,.78f,.62f},{1.f,.49f,.12f},.008f,12.f,2.f,11.f},
        WeatherHazard{"METEOR SHOWER",{.17f,.18f,.27f},{.92f,.91f,.99f},{1.f,.72f,.42f},0,15.f,3.f,13.f},
        WeatherHazard{"MAGNETIC BURST",{.23f,.34f,.39f},{.82f,.94f,1.f},{.42f,.86f,1.f},.003f,16.f,2.f,0},
        WeatherHazard{"ASH CLOUD",{.36f,.35f,.34f},{.86f,.83f,.79f},{.70f,.67f,.62f},.015f,14.f,2.f,0},
        WeatherHazard{"RADIOACTIVE WIND",{.30f,.38f,.19f},{.88f,1.f,.71f},{.64f,.87f,.32f},.008f,15.f,2.5f,0},
        WeatherHazard{"SNOWSTORM / HAIL",{.69f,.77f,.83f},{.87f,.94f,1.f},{.90f,.96f,1.f},.010f,14.f,2.5f,12.f}
    };
    const int index=int(weather)-int(Weather::IonStorm);
    return index>=0&&size_t(index)<hazards.size()?&hazards[size_t(index)]:nullptr;
}
float weatherNoise(uint32_t seed,uint32_t index){uint32_t x=seed+index*0x9e3779b9u;x^=x>>16;x*=0x85ebca6bu;x^=x>>13;x*=0xc2b2ae35u;x^=x>>16;return float(x>>8)/16777216.f;}
EnvironmentVisuals environmentVisuals(const EnvironmentSettings& s,bool authoredNight,Vec3 sky,Vec3 ground,float seconds){
    EnvironmentVisuals v;v.night=s.lighting==Lighting::Authored?authoredNight:s.lighting==Lighting::Night;
    v.sky=sky;v.ground=ground;v.solidGain=v.night==authoredNight?1:v.night?.5f:2.f;v.modelGain=v.night?.5f:1;
    if(s.lighting!=Lighting::Authored){
        if(!s.authoredSky)v.sky=v.night?jaguarCRY(0x250c):jaguarCRY(0x4762);
        v.ground=ground*(v.night?.5f:1);
    }
    if(const auto *hazard=weatherHazard(s.weather)){
        const bool vacuumSky=s.authoredSky&&s.weather!=Weather::Snowstorm;
        v.fog=hazard->haze*(v.night?.32f:1.f);
        v.tint=hazard->tint;
        v.density=vacuumSky?0.f:hazard->density;
        v.precipitation=1.f;
        if(!vacuumSky&&v.density>0)v.sky=v.fog;
        return v;
    }
    if(s.weather!=Weather::Clear){
        uint16_t fog=s.weather==Weather::Sandstorm?0xd87d:0x6857;
        if(v.night)fog=uint16_t((fog&0xff00)|((fog&255)>>2));
        v.fog=jaguarCRY(fog);
        v.density=s.weather==Weather::Fog?.012f:s.weather==Weather::Rain?.005f:s.weather==Weather::Storm?.009f:.016f;
        v.sky=v.fog;v.tint=s.weather==Weather::Sandstorm?Vec3{1,.86f,.65f}:Vec3{.78f,.84f,.9f};
        v.precipitation=s.weather==Weather::Rain||s.weather==Weather::Storm?1:s.weather==Weather::Sandstorm?.5f:0;
        // A single modest pulse per 17-second interval; shared by both eyes.
        if(weatherLightningActive(s,seconds)){float phase=weatherStormPhase(s,seconds);float flash=phase<.1f?.125f:.0625f;v.sky=v.sky+Vec3{flash,flash,flash};v.tint=v.tint+Vec3{flash,flash,flash};v.density=0;}
    }
    if(s.weather==Weather::Clear)v.fog=v.sky;
    return v;
}
float weatherStormPhase(const EnvironmentSettings& s,float seconds){return std::fmod(std::max(0.f,std::isfinite(seconds)?seconds:0.f)+weatherNoise(s.seed,0)*10,17.f);}
bool weatherLightningActive(const EnvironmentSettings& s,float seconds){return s.weather==Weather::Storm&&std::isfinite(seconds)&&seconds>=0&&weatherStormPhase(s,seconds)<.2f;}
std::vector<WeatherSprite> weatherSprites(const EnvironmentSettings& s,Vec3 anchor,float seconds){
    if(const auto *hazard=weatherHazard(s.weather)){
        const bool meteor=s.weather==Weather::MeteorShower;
        const bool snow=s.weather==Weather::Snowstorm;
        const bool veil=s.weather==Weather::AshCloud||s.weather==Weather::CorruptionSporeBurst||
                        s.weather==Weather::RadioactiveWind;
        const bool fire=s.weather==Weather::FireStorm;
        const bool magnetic=s.weather==Weather::MagneticBurst;
        const unsigned count=snow?64u:meteor?24u:veil?58u:36u;
        const float time=std::isfinite(seconds)&&seconds>0?seconds:0.f;
        const float extent=snow?46.f:meteor?170.f:veil?72.f:104.f,height=snow?26.f:meteor?100.f:veil?31.f:56.f;
        const Vec3 drift=snow?Vec3{12,-4,-5}:meteor?Vec3{27,-42,12}:fire?Vec3{14,5.f,-4}:magnetic?Vec3{-10,-2,12}:
            s.weather==Weather::IonStorm?Vec3{9,-10,-5}:Vec3{18,-1.8f,-5};
        auto wrap=[](double t,float size){return float(t-std::floor((t+size*.5)/size)*size);};
        std::vector<WeatherSprite> out;out.reserve(count);
        for(unsigned i=0;i<count;++i){
            auto random=[&](unsigned field){return weatherNoise(s.seed,i*7+field);};
            const float speed=.8f+random(4)*.38f;
            const Vec3 offset{
                wrap(random(0)*extent+double(time)*drift.x*speed-anchor.x,extent),
                wrap(random(1)*height+double(time)*drift.y*speed-anchor.y,height),
                wrap(random(2)*extent+double(time)*drift.z*speed-anchor.z,extent)
            };
            WeatherSprite particle;
            particle.layer=veil?WeatherLayer::HazardVeil:WeatherLayer::HazardStreak;
            particle.position=anchor+offset;
            if(meteor)particle.position.y+=42.f;
            particle.drift=drift;
            particle.colour=hazard->particles*(.76f+.24f*random(6));
            particle.width=meteor?8.f+random(3)*15.f:veil?1.3f+random(3)*5.8f:1.1f+random(3)*3.8f;
            particle.height=meteor?.44f:veil?particle.width*(s.weather==Weather::CorruptionSporeBurst?.75f:.35f):fire?.14f:.09f;
            particle.frame=veil?3+i%3:4;
            if(snow){
                const float phase=time*1.3f+random(3)*2*pi;
                particle.position.x+=.85f*std::sin(phase);
                particle.position.y+=.4f*std::cos(phase);
                particle.width=.22f+.48f*random(5);
                particle.height=particle.width*.64f;
            }
            const float edge=std::clamp((extent*.5f-std::max(std::abs(offset.x),std::abs(offset.z)))/9,0.f,1.f);
            particle.opacity=edge*(meteor?.8f:veil?.21f:.58f)*(.70f+.30f*std::sin(time*1.9f+random(5)*2*pi));
            out.push_back(particle);
        }
        return out;
    }
    const bool dust=s.weather==Weather::Sandstorm;
    const unsigned count=dust?128:s.weather==Weather::Storm?128:s.weather==Weather::Rain?96:0;
    std::vector<WeatherSprite> sprites;sprites.reserve(count);seconds=std::isfinite(seconds)&&seconds>=0?seconds:0;
    if(dust){
        // A shared wind field moves world-fixed grains, low sheets and veils.
        // Wrapping relative to the body only selects nearby cells: walking or
        // turning the head must not drag the dust along with the observer.
        auto wrapCell=[](double value,float extent){return float(value-std::floor((value+extent*.5)/extent)*extent);};
        const float phase=weatherNoise(s.seed,0)*2*pi;
        const double travel=double(seconds)*23+4*std::sin(double(seconds)*.7+phase);
        const float gust=.5f+.3f*std::sin(seconds*.7f+phase)+.2f*std::sin(seconds*1.7f+phase);
        for(unsigned i=0;i<count;++i){
            auto noise=[&](unsigned field){return weatherNoise(s.seed,i*8+field+1);};
            WeatherSprite sprite;bool grain=i<96,sheet=i>=96&&i<112;
            sprite.layer=grain?WeatherLayer::SandGrain:sheet?WeatherLayer::SandSheet:WeatherLayer::SandVeil;
            float speed=grain?1+noise(3)*.4f:sheet?.55f:.38f;
            const float extent=grain?44:sheet?96:68;
            float x=wrapCell(noise(0)*extent+travel*speed-anchor.x,extent);
            float z=wrapCell(noise(2)*extent-travel*speed*.38-anchor.z,extent);
            const float height=grain?20:32;
            sprite.position={anchor.x+x,anchor.y+wrapCell(noise(1)*height+std::sin(seconds*.9f+noise(4)*2*pi)*.4f-anchor.y,height),anchor.z+z};
            sprite.drift={1,.018f,-.38f};
            sprite.width=grain?1+noise(4)*2.5f:sheet?9+noise(4)*15:18+noise(4)*20;
            sprite.height=grain?.065f+noise(5)*.1f:sheet?.8f+noise(5)*1.6f:7+noise(5)*7;
            // Sand uses fixed smoke masks as a soft density stencil, rather
            // than repeatedly growing an identifiable explosion-shaped puff.
            sprite.frame=grain?4:3+i%3;
            sprite.colour=jaguarCRY(grain?0xd9c8:0xd9ae);
            sprite.opacity=(grain?.6f:sheet?.16f:.13f)*(.65f+gust*.7f);
            sprite.opacity*=std::clamp((extent*.5f-std::max(std::abs(x),std::abs(z)))/5,0.f,1.f);
            sprites.push_back(sprite);
        }
        return sprites;
    }
    // Match the verified ten-frame, ten-Hz cartridge smoke descriptor. Motion
    // stays local; frame selection never consumes the gameplay random stream.
    for(unsigned i=0;i<count;++i){auto noise=[&](unsigned field){return weatherNoise(s.seed,i*5+field);};WeatherSprite sprite;
        sprite.position=anchor+Vec3{(noise(0)-.5f)*90,0,(noise(2)-.5f)*90};
        float phase=noise(1)*36-seconds*(dust?2:26);sprite.position.y=anchor.y+phase-std::floor(phase/36)*36-18;
        float wind=std::fmod(seconds*(dust?5:1)+noise(3)*6,6.f);sprite.position.x+=wind-3;
        sprite.frame=unsigned(std::floor(std::fmod(double(seconds)*weatherSpriteFps+i,weatherSpriteFrames)));
        sprite.width=dust?2+noise(4)*3:.12f;sprite.height=dust?sprite.width:1.8f;
        sprite.colour=jaguarCRY(dust?0xc8bf:0x5871);sprites.push_back(sprite);
    }return sprites;
}

}
