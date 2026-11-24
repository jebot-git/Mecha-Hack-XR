#include "weather_audio.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace iron {
namespace {
constexpr float bedCrossfadeStep=1.f/(.32f*48000.f);
}
float WeatherAudio::noise(Channel& c){uint32_t x=c.rng;x^=x<<13;x^=x>>17;x^=x<<5;c.rng=x;return float(x>>8)*(2.f/16777216.f)-1;}
void WeatherAudio::clearSynthesis(){
    channels[0]=channels[1]=Channel{};weather=Weather::Clear;lastTime=-1;
    thunderTake=nullptr;frames=thunderFrame=0;gustFrames=0;fade=0;gust=gustTarget=.5f;thunderPlaying=false;
}
void WeatherAudio::clear(){
    clearSynthesis();
    for(auto& bed:beds){bed.cursor=0;bed.gain=bed.target=0;}
}
void WeatherAudio::setBed(Weather kind,std::vector<float> samples){
    beds.at(size_t(kind)).pcm=std::move(samples);
}
void WeatherAudio::setUnderwaterBed(std::vector<float> samples){
    beds[underwaterSlot].pcm=std::move(samples);
}
void WeatherAudio::setAmbienceBed(Ambience scene,std::vector<float> samples){
    beds.at(biomeStart+size_t(scene)).pcm=std::move(samples);
}
void WeatherAudio::setThunderTake(size_t index,std::vector<float> samples){
    thunderTakes.at(index)=std::move(samples);
}
void WeatherAudio::targetBed(size_t slot,float gain,uint32_t newSeed){
    auto& bed=beds[slot];
    if(bed.pcm.empty())return;
    if(bed.gain==0)bed.cursor=(newSeed+uint32_t(slot)*2654435761u)%bed.pcm.size();
    bed.target=gain;
}
void WeatherAudio::update(const EnvironmentSettings& settings,float seconds,bool active,bool underwater,
                          Ambience scene,bool sheltered){
    for(auto& bed:beds)bed.target=0;
    if(!active||!std::isfinite(seconds)||seconds<0){clearSynthesis();return;}
    Weather kind=settings.weather;
    // Only the field hazards that operate underwater or in vacuum can pass the hull.
    const bool field=kind==Weather::MagneticBurst||kind==Weather::RadioactiveWind;
    if(underwater&&!field)kind=Weather::Clear;
    if(scene==Ambience::Hull&&!underwater&&!weatherHazard(kind))kind=Weather::Clear;
    if(sheltered&&(kind==Weather::Fog||kind==Weather::Rain||kind==Weather::Sandstorm||
                   kind==Weather::Snowstorm))kind=Weather::Clear;
    if(underwater)targetBed(underwaterSlot,1,settings.seed);
    else if(scene!=Ambience::Count){
        if(settings.lighting==Lighting::Night){
            if(scene==Ambience::Urban&&!beds[biomeStart+size_t(Ambience::UrbanNight)].pcm.empty())scene=Ambience::UrbanNight;
            if(scene==Ambience::Wilderness&&!beds[biomeStart+size_t(Ambience::WildernessNight)].pcm.empty())scene=Ambience::WildernessNight;
        }
        targetBed(biomeStart+size_t(scene),kind==Weather::Clear?1.f:.7f,settings.seed);
    }
    const bool coveredStorm=sheltered&&kind==Weather::Storm;
    if(!coveredStorm&&kind!=Weather::Clear)targetBed(size_t(kind),1,settings.seed);
    thunderGain=coveredStorm?.24f:1.f;
    if(kind!=Weather::Rain&&kind!=Weather::Storm&&kind!=Weather::Sandstorm&&!weatherHazard(kind)){
        clearSynthesis();return;
    }
    const bool reset=weather!=kind||seed!=settings.seed||seconds<lastTime||lastTime<0;
    if(reset){
        clearSynthesis();weather=kind;seed=settings.seed;
        channels[0].rng=(seed^0x9e3779b9u)|1;channels[1].rng=(seed^0x85ebca6bu)|1;
    }
    if(kind==Weather::Storm){
        // Each visual strike receives one return 1.2 s later. Initialising or
        // resuming midway through a cycle must not replay a missed storm.
        const double clock=double(seconds)+weatherNoise(seed,0)*10.-1.2;
        const auto cycle=int64_t(std::floor(clock/17.));
        if(reset)lastThunderCycle=cycle;
        else if(cycle>lastThunderCycle){
            lastThunderCycle=cycle;
            thunderTake=nullptr;
            for(size_t i=0;i<thunderTakes.size();++i){
                const auto &take=thunderTakes[(uint64_t(cycle)+seed+i)%thunderTakes.size()];
                if(!take.empty()){thunderTake=&take;break;}
            }
            thunderFrame=thunderTake?uint64_t(std::max(0.,clock-17.*cycle)*48000.):0;
            thunderPlaying=thunderFrame<(thunderTake?thunderTake->size():9u*48000);
            if(thunderPlaying)++stats.thunderStarted;
        }
    }
    warningTime=seconds;
    lastTime=seconds;
}
void WeatherAudio::mixBeds(std::span<float> out,float volume){
    size_t weatherFrames=0,biomeFrames=0;
    for(size_t slot=0;slot<beds.size();++slot){
        auto& bed=beds[slot];
        if(bed.pcm.empty()||(bed.gain==0&&bed.target==0))continue;
        const size_t delay=std::min<size_t>(1379,bed.pcm.size()-1);
        size_t count=0;
        for(;count<out.size()/2;++count){
            if(bed.gain==0&&bed.target==0)break;
            bed.gain+=std::clamp(bed.target-bed.gain,-bedCrossfadeStep,bedCrossfadeStep);
            size_t second=bed.cursor+delay;
            if(second>=bed.pcm.size())second-=bed.pcm.size();
            const float sample=bed.pcm[bed.cursor],gain=bed.gain*volume;
            out[2*count]+=sample*gain;
            out[2*count+1]+=(bed.pcm[second]*.65f+sample*.35f)*gain;
            if(++bed.cursor==bed.pcm.size())bed.cursor=0;
        }
        auto& played=slot>=biomeStart?biomeFrames:weatherFrames;
        played=std::max(played,count);
    }
    stats.biomeFrames+=biomeFrames;
    stats.recordedFrames+=std::max(biomeFrames,weatherFrames);
}
void WeatherAudio::mix(std::span<float> out,float volume){
    if(!std::isfinite(volume))return;
    volume=std::clamp(volume,0.f,1.f);
    if(weather==Weather::Clear){mixBeds(out,volume);return;}
    const auto *hazard=weatherHazard(weather);
    const auto& weatherBed=beds[size_t(weather)];
    const float authoredGain=weather==Weather::Storm&&thunderGain<1.f?0.f:
                             weatherBed.pcm.empty()?1.f:.14f;
    double pulsePhase=hazard?std::fmod(std::max(0.,warningTime-4.),hazard->cadence):0.;
    for(size_t i=0;i+1<out.size();i+=2){
        const float warning=hazard&&warningTime>=4.&&pulsePhase<hazard->warning?
                            float(pulsePhase)/hazard->warning:0.f;
        warningTime+=1./48000.;
        if(hazard&&warningTime>=4.){
            pulsePhase+=1./48000.;
            if(pulsePhase>=hazard->cadence)pulsePhase-=hazard->cadence;
        }
        // Slow coherent gusts, stereo filtered noise and soft granular patter.
        // All clocks advance in audio samples, including during render stalls.
        if(gustFrames==0){gustTarget=.25f+.75f*weatherNoise(seed,unsigned(frames/48000)+93);gustFrames=48000;}
        --gustFrames;gust+=(gustTarget-gust)*.000035f;fade+=(1-fade)*.0005f;
        float thunderEnvelope=0;
        if(thunderPlaying&&!thunderTake){
            float time=float(thunderFrame)/48000,tail=std::max(0.f,1-time/9);
            // Rounded rise and rolling swells: no sharp weapon-like onset.
            thunderEnvelope=std::min(1.f,time/.24f)*tail*tail*(.65f+.22f*std::cos(time*3.7f)+.13f*std::cos(time*1.6f));
        }
        for(unsigned ear=0;ear<2;++ear){
            auto& c=channels[ear];float white=noise(c);
            c.bass+=(white-c.bass)*.004f;c.low+=(white-c.low)*.025f;
            c.mid+=(white-c.mid)*.14f;c.high+=(white-c.high)*.46f;c.rumble+=(white-c.rumble)*.011f;
            float wind=(c.low*.8f+c.bass*1.7f)*(.13f+gust*.17f),ambience,warningCue=0;
            if(hazard){
                switch(weather){
                    case Weather::IonStorm:{
                        const float tone=std::sin(float(frames%48000)*(2*pi*87.f/48000.f));
                        ambience=(c.high-c.low)*.035f+tone*.022f+wind*.35f;
                        warningCue=(c.high-c.low)*.06f*warning;
                        break;}
                    case Weather::MagneticBurst:{
                        const float tone=std::sin(float(frames%48000)*(2*pi*54.f/48000.f));
                        ambience=(c.rumble+tone*.11f)*.15f;
                        warningCue=(c.rumble+tone*.11f)*.11f*warning;
                        break;}
                    case Weather::MeteorShower:
                        ambience=c.bass*.09f+(c.mid-c.low)*.015f;
                        warningCue=c.bass*.23f*warning;
                        break;
                    case Weather::Snowstorm:
                        if((c.rng&65535)<42)c.drop=1.f;
                        c.drop*=.985f;
                        ambience=wind*.97f+(c.mid-c.low)*.024f+c.drop*(c.high-c.mid)*.19f;
                        warningCue=c.drop*(c.high-c.mid)*.13f*warning;
                        break;
                    case Weather::FireStorm: ambience=wind*.85f+(c.high-c.mid)*(.028f+.045f*gust);break;
                    case Weather::CorruptionSporeBurst: ambience=wind*.38f+c.mid*.026f;break;
                    case Weather::RadioactiveWind: ambience=wind*.72f+(c.high-c.low)*.032f;break;
                    case Weather::AshCloud: ambience=wind*.47f+(c.mid-c.low)*.036f;break;
                    default: ambience=0;break;
                }
            }else if(weather==Weather::Sandstorm)ambience=wind+(c.mid-c.low)*(.035f+gust*.055f);
            else{
                if((c.rng&65535)<150)c.drop+=.18f*(1-c.drop);
                c.drop*=.997f;
                float rain=(c.high-c.low)*.095f+(c.mid-c.low)*c.drop*.19f;
                ambience=rain+wind*.45f;if(weather==Weather::Storm)ambience*=1.35f;
            }
            float thunder=thunderPlaying&&thunderTake?(*thunderTake)[thunderFrame]*.46f:(c.rumble*.9f+c.bass*1.8f+(c.mid-c.low)*.055f)*thunderEnvelope*.85f;
            out[i+ear]+=(ambience*authoredGain+warningCue+thunder*thunderGain)*fade*volume;
        }
        ++frames;++stats.ambientFrames;
        if(thunderPlaying){
            ++stats.thunderFrames;
            if(thunderTake)++stats.recordedThunderFrames;
            if(++thunderFrame>=(thunderTake?thunderTake->size():9u*48000))thunderPlaying=false;
        }
    }
    mixBeds(out,volume);
}
}
