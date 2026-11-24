#pragma once
#include "environment.hpp"
#include <filesystem>
#include <span>
namespace iron {
enum class Ambience { Urban, Desert, Wilderness, Polar, Volcanic, Interior, Hull, UrbanNight, WildernessNight, Count };
inline constexpr std::array<std::string_view,size_t(Ambience::Count)> ambienceNames{
    "urban","desert","wilderness","polar","volcanic","interior","hull","urban-night","wilderness-night"
};
// The caller serializes updates with the device. Mixing does not allocate or access files.
class WeatherAudio {
    struct Channel {uint32_t rng=1;float bass=0,low=0,mid=0,high=0,drop=0,rumble=0;};
    Channel channels[2];
    Weather weather=Weather::Clear;uint32_t seed=0;
    float lastTime=-1,fade=0,gust=.5f,gustTarget=.5f,thunderGain=1;
    double warningTime=0;
    uint64_t frames=0,thunderFrame=0;unsigned gustFrames=0;
    int64_t lastThunderCycle=-1;bool thunderPlaying=false;
    std::array<std::vector<float>,3> thunderTakes;
    const std::vector<float>* thunderTake=nullptr;
    static constexpr size_t underwaterSlot=weatherNames.size();
    static constexpr size_t biomeStart=underwaterSlot+1;
    struct Bed {std::vector<float> pcm;size_t cursor=0;float gain=0,target=0;};
    std::array<Bed,biomeStart+size_t(Ambience::Count)> beds;
    void targetBed(size_t slot,float gain,uint32_t newSeed);
    void mixBeds(std::span<float> stereo,float volume);
    void clearSynthesis();
    static float noise(Channel&);
public:
    struct Stats {
        uint64_t ambientFrames=0,thunderFrames=0,thunderStarted=0,recordedFrames=0,biomeFrames=0,recordedThunderFrames=0;
    };
private:
    Stats stats;
public:
    void setBed(Weather,std::vector<float>);
    void setUnderwaterBed(std::vector<float>);
    void setAmbienceBed(Ambience,std::vector<float>);
    void setThunderTake(size_t,std::vector<float>);
    void update(const EnvironmentSettings&,float seconds,bool active,bool underwater=false,
                Ambience scene=Ambience::Count,bool sheltered=false);
    void mix(std::span<float> stereo,float volume);
    void clear();
    Stats statistics()const{return stats;}
};
void loadEnvironmentRecordings(WeatherAudio&,const std::filesystem::path& audioRoot);
}
