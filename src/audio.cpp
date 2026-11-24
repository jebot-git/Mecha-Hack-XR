#include "audio.hpp"
#include "cockpit_effects.hpp"
#include "rpg/score_player.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstring>
#include <numeric>
#include <nlohmann/json.hpp>
namespace iron {
namespace {
std::vector<float> loadMonoSample(const std::filesystem::path &file,bool loop=false){
    SDL_AudioSpec spec{};
    Uint8 *raw=nullptr;
    Uint32 length=0;
    const auto filename=file.u8string();
    if(!SDL_LoadWAV(reinterpret_cast<const char*>(filename.c_str()),&spec,&raw,&length)){
        std::cerr<<"Cannot load effect: "<<file<<"\n";
        return {};
    }
    std::unique_ptr<Uint8,decltype(&SDL_FreeWAV)> buffer(raw,SDL_FreeWAV);
    if(!length)return {};
    SDL_AudioCVT cvt{};
    const int status=SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_F32SYS,1,48000);
    if(status<0)return {};
    // Give SDL's resampler real neighbours on both sides of a cyclic cut.
    // Align the pads to whole output frames so trimming preserves loop phase.
    const size_t bytesPerFrame=SDL_AUDIO_BITSIZE(spec.format)/8*spec.channels;
    size_t padFrames=0;
    if(loop&&bytesPerFrame&&spec.freq>0&&spec.freq!=48000){
        const size_t phase=size_t(spec.freq/std::gcd(spec.freq,48000));
        const size_t frames=(64+phase-1)/phase*phase;
        if(frames<length/bytesPerFrame&&length%bytesPerFrame==0)
            padFrames=frames;
    }
    const size_t padBytes=padFrames*bytesPerFrame;
    std::vector<Uint8> converted((size_t(length)+2*padBytes)*std::max(1,cvt.len_mult));
    std::copy_n(raw,length,converted.begin()+padBytes);
    if(padBytes){
        std::copy_n(raw+length-padBytes,padBytes,converted.begin());
        std::copy_n(raw,padBytes,converted.begin()+padBytes+length);
    }
    cvt.buf=converted.data();
    cvt.len=int(length+2*padBytes);
    if(status!=0&&SDL_ConvertAudio(&cvt)!=0)return {};
    const size_t frames=(status==0?cvt.len:cvt.len_cvt)/sizeof(float);
    const size_t trim=padFrames?padFrames*48000/size_t(spec.freq):0;
    if(frames<=2*trim)return {};
    std::vector<float> sample(frames-2*trim);
    std::memcpy(sample.data(),converted.data()+trim*sizeof(float),sample.size()*sizeof(float));
    return sample;
}
std::vector<float> loadAmbienceBed(const std::filesystem::path &file,float targetRms){
    if(!std::filesystem::exists(file))return {};
    auto sample=loadMonoSample(file,true);
    if(sample.empty())return {};
    double energy=0;
    for(float value:sample)energy+=double(value)*value;
    const double rms=std::sqrt(energy/sample.size());
    if(!std::isfinite(rms)||rms<.0001)return {};
    const float gain=std::min(2.f,float(targetRms/rms));
    for(auto &value:sample)value*=gain;
    return sample;
}
constexpr std::array combatCueFiles{
    "rifle", "cannon", "beam", "rocket", "charge", "arc", "pulse", "shield", "boost",
    "metal-hit", "melee", "explosion", "heavy-explosion", "step", "motor", "repair", "ready", "failed",
    "enemy-step", "enemy-alert", "enemy-growl", "enemy-thrust",
    "kaiju-shriek", "kaiju-roar", "kaiju-sonar",
    "arrival-warp", "arrival-dropship", "arrival-burrow"
};
static_assert(combatCueFiles.size()==size_t(rpg::CombatCue::KaijuMandibles));
// World units. Mouth and leg Foley is local; attack warnings carry across the encounter.
constexpr float enemyCueRange(rpg::CombatCue cue){
    using Cue=rpg::CombatCue;
    switch(cue){
    case Cue::ArrivalWarp: case Cue::ArrivalDropship: case Cue::ArrivalBurrow: return 300.f;
    case Cue::KaijuRoar: case Cue::KaijuSonar: case Cue::KaijuShriek: case Cue::KaijuDischarge: return 240.f;
    case Cue::KaijuMandibles: case Cue::KaijuBite: case Cue::KaijuSkitter: return 60.f;
    case Cue::KaijuHiss: case Cue::KaijuSwipe: return 90.f;
    case Cue::KaijuSpray: case Cue::KaijuSpines: return 145.f;
    default:return 190.f;
    }
}
constexpr std::array enemyBedFiles{"enemy-idle","enemy-servo","enemy-tracks","enemy-turbine","enemy-submersible","enemy-creature"};
}
void loadEnvironmentRecordings(WeatherAudio& mixer,const std::filesystem::path& audioRoot){
    const auto weatherRoot=audioRoot/"weather";
    for(size_t i=1;i<weatherNames.size();++i){
        const float rms=Weather(i)==Weather::Fog?.012f:
                        Weather(i)==Weather::Storm||Weather(i)==Weather::FireStorm?.027f:.023f;
        mixer.setBed(Weather(i),loadAmbienceBed(weatherRoot/(std::string(weatherNames[i])+".wav"),rms));
    }
    mixer.setUnderwaterBed(loadAmbienceBed(weatherRoot/"underwater.wav",.020f));
    for(size_t i=0;i<ambienceNames.size();++i){
        const auto path=audioRoot/"ambience"/(std::string(ambienceNames[i])+".wav");
        mixer.setAmbienceBed(Ambience(i),loadAmbienceBed(path,.018f));
    }
    for(size_t i=0;i<3;++i){
        const auto path=weatherRoot/("thunder-"+std::to_string(i+1)+".wav");
        if(std::filesystem::is_regular_file(path))mixer.setThunderTake(i,loadMonoSample(path));
    }
}
void Audio::loadKaijuRecordings(const std::filesystem::path& root){
    using Cue=rpg::CombatCue;
    struct Entry {Cue cue; const char* name; Cue fallback;};
    constexpr Entry calls[]{
        {Cue::KaijuShriek,"shriek",Cue::KaijuShriek}, {Cue::KaijuRoar,"roar",Cue::KaijuRoar},
        {Cue::KaijuSonar,"sonar",Cue::KaijuSonar}, {Cue::KaijuMandibles,"mandibles",Cue::Melee},
        {Cue::KaijuHiss,"hiss",Cue::KaijuShriek}, {Cue::KaijuBite,"bite",Cue::Melee},
        {Cue::KaijuSwipe,"swipe",Cue::Melee}, {Cue::KaijuImpact,"impact",Cue::MetalHit},
        {Cue::KaijuDischarge,"discharge",Cue::KaijuRoar}, {Cue::KaijuSpray,"spray",Cue::KaijuShriek},
        {Cue::KaijuBurrow,"burrow",Cue::ArrivalBurrow}, {Cue::KaijuSkitter,"skitter",Cue::EnemyStep},
        {Cue::KaijuTread,"tread",Cue::EnemyStep}, {Cue::KaijuSpines,"spine",Cue::KaijuShriek}
    };
    static_assert(std::size(calls)==3+size_t(Cue::Count)-size_t(Cue::KaijuMandibles));
    auto loadTakes=[&](const char* name,bool loop){
        RecordingTakes takes;
        for(int take=1;take<=3;++take){
            const auto path=root/(std::string(name)+"-"+std::to_string(take)+".wav");
            if(!std::filesystem::is_regular_file(path))continue;
            auto sample=loadMonoSample(path,loop);
            if(!sample.empty())takes.push_back(std::move(sample));
        }
        return takes;
    };
    for(const auto& entry:calls){
        auto& bank=kaijuRecordings[size_t(entry.cue)];
        bank.fallback=entry.fallback;
        bank.takes=loadTakes(entry.name,false);
    }
    constexpr std::array beds{"breath","wing","swim","crawl"};
    static_assert(beds.size()==size_t(CreatureBed::Count));
    for(size_t i=0;i<beds.size();++i)creatureBeds[i]=loadTakes(beds[i],true);
}
void Audio::loadWeaponRecordings(const std::filesystem::path& root){
    // Report, moving parts and early reflections travel together through one HRTF voice.
    constexpr std::array names{"auto","cannon","missile","energy","melee"};
    constexpr std::array<size_t,5> cycleOffset{2208,8160,8880,9120,5520}; // 46, 170, 185, 190, 115 ms at 48 kHz
    static_assert(names.size()==size_t(rpg::WeaponSoundFamily::Count));
    auto sample=[&](const std::filesystem::path& path,bool loop=false){
        return std::filesystem::is_regular_file(path)?loadMonoSample(path,loop):std::vector<float>{};
    };
    auto add=[](std::vector<float>& mix,const std::vector<float>& layer,size_t offset,float gain){
        if(layer.empty())return;
        mix.resize(std::max(mix.size(),layer.size()+offset),0.f);
        for(size_t i=0;i<layer.size();++i)mix[i+offset]+=layer[i]*gain;
    };
    for(size_t i=0;i<names.size();++i){
        auto& bank=weaponRecordings[i];
        const std::string prefix=std::string(names[i])+"-";
        bank.charge=sample(root/(prefix+"charge.wav"));
        bank.ready=sample(root/(prefix+"ready.wav"));
        bank.idle=sample(root/(prefix+"idle.wav"),true);
        for(size_t v=0;v<bank.shots.size();++v){
            const auto suffix="-"+std::to_string(v+1)+".wav";
            auto& shot=bank.shots[v];
            shot.dry=sample(root/(prefix+"report"+suffix));
            if(shot.dry.empty())continue;
            for(auto& x:shot.dry)x*=.64f;
            add(shot.dry,sample(root/(prefix+"cycle"+suffix)),cycleOffset[i],.56f);
            auto mixRoom=[&](const char* room,std::vector<float>& mix){
                const auto wet=sample(root/"returns"/(prefix+room+suffix));
                if(wet.empty())return;
                mix=shot.dry;
                add(mix,wet,0,.64f);
            };
            mixRoom("yard",shot.yard);
            mixRoom("hangar",shot.hangar);
        }
    }
}
Audio::Audio(const std::filesystem::path& root,bool nativeCombat):nativeCombatAudio(nativeCombat){
    try{
        if(nativeCombat&&std::filesystem::exists(root/"ost/main.mop"))
            campaignScore=std::make_unique<rpg::ScorePlayer>(root/"ost");
    }catch(const std::exception& e){std::cerr<<"Campaign score unavailable: "<<e.what()<<"\n";}
    try{if(!nativeCombat&&std::filesystem::exists(root/"audio/music.json"))music=std::make_unique<Music>(root);}catch(const std::exception& e){std::cerr<<"Music unavailable: "<<e.what()<<"\n";}
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){std::cerr<<"Audio unavailable: "<<SDL_GetError()<<"\n";return;}
    SDL_AudioSpec want{};want.freq=48000;want.format=AUDIO_F32SYS;want.channels=2;want.samples=512;want.callback=&Audio::callback;want.userdata=this;
    device=SDL_OpenAudioDevice(nullptr,0,&want,nullptr,0);
    if(!device){std::cerr<<"Audio unavailable: "<<SDL_GetError()<<"\n";return;}
    if(nativeCombatAudio){
        loadWeaponRecordings(root/"fx/audio/weapons");
        loadKaijuRecordings(root/"fx/audio/kaiju");
        for(size_t cue=0;cue<combatCueFiles.size();++cue){
            const auto file=root/"fx/audio"/(std::string(combatCueFiles[cue])+".wav");
            combatAudio[cue]=loadMonoSample(file);
        }
        for(size_t index=0;index<npcRadioSamples.size();++index){
            const auto file=root/"fx/audio/radio"/(std::to_string(index)+".wav");
            if(std::filesystem::exists(file))npcRadioSamples[index]=loadMonoSample(file);
        }
        for(size_t index=0;index<enemyBeds.size();++index)
            enemyBeds[index]=loadMonoSample(root/"fx/audio"/(std::string(enemyBedFiles[index])+".wav"),true);
        radarSonar=loadMonoSample(root/"fx/audio/radar-sonar.wav");
        rifleAlternate=loadMonoSample(root/"fx/audio/rifle-alt.wav");
        stepAlternate=loadMonoSample(root/"fx/audio/step-alt.wav");
        const auto enemyFootfall=root/"fx/audio/enemy-step-alt.wav";
        if(std::filesystem::exists(enemyFootfall))enemyStepAlternate=loadMonoSample(enemyFootfall);
        loadEnvironmentRecordings(weather,root/"fx/audio");
    }else for(int game=0;game<2;++game)for(size_t event=0;event<32;++event){
        auto file=(game?root/"is2":root)/"audio"/("effect-"+std::string(event<10?"0":"")+std::to_string(event)+".wav");
        if(!std::filesystem::exists(file)||(game&&event==7))continue; // IS2 event 7 stops the cutter
        auto& sample=effects[game][event];
        sample=loadMonoSample(file);
        if(event==6&&!sample.empty()){size_t loop=sample.size();sample.resize(8640);for(size_t n=loop;n<sample.size();++n)sample[n]=sample[n%loop];}
    }
    voices.reserve(24);
    try{spatial=std::make_unique<SpatialAudio>();}catch(const std::exception& e){std::cerr<<"Spatial audio unavailable: "<<e.what()<<"\n";}
    SDL_PauseAudioDevice(device,0);
}
Audio::~Audio(){if(device)SDL_CloseAudioDevice(device);}
void Audio::setVolumes(float sound,float music){
    if(!std::isfinite(sound)||!std::isfinite(music))return;
    if(device)SDL_LockAudioDevice(device);
    soundVolume=std::clamp(sound,0.f,1.f);musicVolume=std::clamp(music,0.f,1.f);
    if(device)SDL_UnlockAudioDevice(device);
}
// All state shared with the callback is protected by SDL's device lock.
size_t Audio::voiceBytes()const {if(!device)return 0;SDL_LockAudioDevice(device);size_t n=radioCount*sizeof(float);SDL_UnlockAudioDevice(device);return n;}
void Audio::queueVoice(std::span<const float> pcm){if(!device)return;SDL_LockAudioDevice(device);for(float v:pcm){if(radioCount==radio.size()){radioRead=(radioRead+1)%radio.size();--radioCount;}radio[(radioRead+radioCount++)%radio.size()]=v;}SDL_UnlockAudioDevice(device);}
void Audio::clearVoice(){if(!device)return;SDL_LockAudioDevice(device);radioRead=radioCount=0;SDL_UnlockAudioDevice(device);}
Audio::Stats Audio::statistics()const {if(!device)return {};SDL_LockAudioDevice(device);auto copy=stats;auto weatherStats=weather.statistics();copy.weatherFrames=weatherStats.ambientFrames;copy.recordedWeatherFrames=weatherStats.recordedFrames;copy.thunderStarted=weatherStats.thunderStarted;copy.biomeFrames=weatherStats.biomeFrames;copy.recordedThunderFrames=weatherStats.recordedThunderFrames;if(spatial){auto s=spatial->statistics();copy.spatialStarted=s.started;copy.spatialQueries=s.queries;copy.spatialDropped=s.dropped;}SDL_UnlockAudioDevice(device);return copy;}
void Audio::callback(void* self,Uint8* bytes,int length){static_cast<Audio*>(self)->mix({reinterpret_cast<float*>(bytes),size_t(length)/sizeof(float)});}
void Audio::mixNpcRadio(std::span<float> stereo){
    auto& receiver=npcRadioVoice;
    if(!receiver.playing())return;
    std::array<float,512> mono;
    for(size_t offset=0;offset<stereo.size()/2 && receiver.playing();offset+=mono.size()){
        const size_t frames=std::min(mono.size(),stereo.size()/2-offset);
        for(size_t i=0;i<frames;++i){
            if(receiver.cursor==receiver.samples->size()){
                mono[i]=0;
                if(receiver.tail)--receiver.tail;
                continue;
            }
            const float dry=(*receiver.samples)[receiver.cursor++];
            receiver.interference+=(receiver.targetInterference-receiver.interference)*(1.f/2400.f);
            float broken=dry;
            receiver.staticLayer.process({&broken,1},1,receiver.interference>.001f);
            const float gain=radioCount>2*(offset+i)?.13f:.47f;
            mono[i]=receiver.receiving?std::lerp(dry,2.f*broken,receiver.interference)*gain*soundVolume:0.f;
        }
        auto output=stereo.subspan(2*offset,2*frames);
        if(spatial)spatial->mixRadioAdd(std::span(mono).first(frames),output);
        else for(size_t i=0;i<frames;++i){output[i*2]+=mono[i];output[i*2+1]+=mono[i];}
    }
}
void Audio::mix(std::span<float> out){
    std::fill(out.begin(),out.end(),0);
    if(musicPlaying&&music){music->mix(out);for(float& s:out)s*=.60f*musicVolume;}
    if(campaignScore)stats.scoreFrames+=campaignScore->mix(out,.63f*musicVolume);
    if(spatial)spatial->mixAdd(out,soundVolume);
    weather.mix(out,soundVolume);
    for(auto& voice:voices)for(size_t i=0;i+1<out.size()&&voice.cursor<voice.samples->size();i+=2){
        const auto &samples=*voice.samples;
        const size_t index=size_t(voice.cursor);
        const float next=index+1<samples.size()?samples[index+1]:0.f;
        float s=std::lerp(samples[index],next,float(voice.cursor-index))*soundVolume;
        voice.cursor+=voice.rate;
        out[i]+=s*voice.left;out[i+1]+=s*voice.right;
    }
    std::erase_if(voices,[](const auto& v){return v.cursor>=v.samples->size();});
    mixNpcRadio(out);
    const float pulseStep=(1.4f+thermalWarning*2.7f)/48000.f;
    const float toneStep=2*pi*(440.f+thermalWarning*360.f)/48000.f;
    for(size_t i=0;i+1<out.size();i+=2){
        if(thermalHiss>.001f||thermalWarning>.001f){
            thermalNoise^=thermalNoise<<13;thermalNoise^=thermalNoise>>17;thermalNoise^=thermalNoise<<5;
            const float noise=float(thermalNoise>>8)*(2.f/16777216.f)-1.f;
            thermalFilter+=(noise-thermalFilter)*.13f;
            thermalPulse+=pulseStep;if(thermalPulse>=1.f)thermalPulse-=1.f;
            thermalTonePhase+=toneStep;if(thermalTonePhase>=2*pi)thermalTonePhase-=2*pi;
            const float envelope=std::max(0.f,1.f-thermalPulse*4.f);
            const float vent=(noise-thermalFilter)*(.70f+.30f*std::sin(thermalPulse*2*pi));
            const float cue=(.115f*thermalHiss*vent+.105f*thermalWarning*envelope*std::sin(thermalTonePhase))*soundVolume;
            out[i]+=cue;out[i+1]+=cue;
            ++stats.thermalFrames;
        }
        if(alarmPlaying&&alarmTime<1.5){float hz=int(alarmTime*5)%2?880:660;alarmPhase+=2*pi*hz/48000;float s=.09f*std::sin(alarmPhase)*soundVolume;out[i]+=s;out[i+1]+=s;alarmTime+=1./48000;}
        for(size_t c=0;c<2;++c)if(radioCount){out[i+c]+=radio[radioRead];radioRead=(radioRead+1)%radio.size();--radioCount;}
        float peak=std::max(std::abs(out[i]),std::abs(out[i+1]));
        float target=peak>.9f?.9f/peak:1.f;
        limiterGain=std::min(target,limiterGain+(1-limiterGain)*.0002f);
        if(limiterGain<.999f)++stats.limitedFrames;
        out[i]*=limiterGain;out[i+1]*=limiterGain;
        stats.peak=std::max(stats.peak,peak*limiterGain);++stats.frames;if(peak>1e-7f)++stats.nonzeroFrames;
    }
}
void Audio::update(World& w,bool active,bool menuMusic,const AudioListener* listener,int campaignCue){
    if(!device){w.sounds.clear();return;}
    SDL_LockAudioDevice(device);
    if(!active||w.elapsed<lastElapsed){voices.clear();npcRadioVoice={};if(spatial)spatial->clear();if(!active)w.sounds.clear();}
    const bool inCockpit=active&&w.rpgPrototype&&!w.lifeSpent&&!w.rpgDroneView;
    thermalHiss=inCockpit?reactorSteam(w.rpgHeat,w.rpgOverheated):0.f;
    thermalWarning=inCockpit?(w.rpgOverheated?1.f:cockpitFade(.74f,1.f,w.rpgHeat)):0.f;
    AudioListener fallback;fallback.position=w.eye();fallback.right={std::cos(w.facing()),0,-std::sin(w.facing())};fallback.back={std::sin(w.facing()),0,std::cos(w.facing())};
    const auto &head=listener?*listener:fallback;
    const Vec3 listenerPosition=head.position;
    auto settings=w.outdoorEnvironment();
    if(settings.lighting==Lighting::Authored&&w.night)settings.lighting=Lighting::Night;
    Ambience scene=Ambience::Count;
    bool submerged=false,sheltered=false;
    if(active&&nativeCombatAudio&&w.rpgPrototype){
        const bool room=w.skyKind==SkyKind::Interior||w.indoors(listenerPosition);
        const float water=w.waterLevelAt(listenerPosition);
        submerged=listenerPosition.y<water||(!room&&w.skyKind==SkyKind::Underwater&&!std::isfinite(water));
        sheltered=room||std::isfinite(w.interiorCeilingAt(listenerPosition));
        switch(room?SkyKind::Interior:w.skyKind){
        case SkyKind::Urban:scene=Ambience::Urban;break;
        case SkyKind::Desert:scene=Ambience::Desert;break;
        case SkyKind::Polar:scene=Ambience::Polar;break;
        case SkyKind::Volcanic:scene=Ambience::Volcanic;break;
        case SkyKind::Wilderness:scene=Ambience::Wilderness;break;
        case SkyKind::Interior:scene=Ambience::Interior;break;
        case SkyKind::Space:case SkyKind::Lunar:case SkyKind::Underwater:scene=Ambience::Hull;break;
        default:break;
        }
        if(room&&settings.weather!=Weather::Storm)settings.weather=Weather::Clear;
    }else if(active&&w.indoors(listenerPosition)){
        settings.weather=Weather::Clear;
    }
    weather.update(settings,w.weatherTime(),active,submerged,scene,sheltered);
    using Family=rpg::WeaponSoundFamily;
    using Priority=SpatialAudio::Priority;
    auto playWorld=[&](const std::vector<float>& sample,Vec3 position,float multiplier,float rate,Priority priority){
        if(spatial){spatial->play(sample,position,multiplier,rate,priority);return;}
        const Vec3 d=position-listenerPosition;
        const float distance=length(d);
        const float gain=.45f*multiplier/(1+distance*.012f);
        const float pan=distance>1?std::clamp(dot(d,head.right)/distance,-1.f,1.f):0;
        if(voices.size()>=24)voices.erase(voices.begin());
        voices.push_back({&sample,0,gain*(1-.6f*pan),gain*(1+.6f*pan),rate});
    };
    const bool weaponsOut=nativeCombatAudio&&w.rpgPrototype&&w.missionStatus==MissionStatus::Active&&
        !w.lifeSpent&&w.health>0&&!w.rpgDroneView&&w.rpgVehicleBlend<.01f;
    if(!weaponsOut||w.elapsed<lastElapsed)readiedWeapons.fill(Weapon::Empty);
    stats.weaponIdleHands=0;
    if(npcRadioVoice.playing()){
        const auto actor=size_t(npcRadioVoice.actor);
        if(actor<w.targets.size()&&w.targets[actor].active&&w.targets[actor].health>0&&!w.targets[actor].escaped)
            npcRadioVoice.transmitter=w.targets[actor].center;
        else npcRadioVoice.actor=-1;
        const float distance=length(npcRadioVoice.transmitter-listenerPosition);
        npcRadioVoice.targetInterference=rpg::npcRadioInterference(distance);
        npcRadioVoice.receiving=!w.lifeSpent&&distance<rpg::npcRadioMaxRange;
    }
    if(active){
        if(spatial){
            spatial->listener(head);
            if(npcRadioVoice.playing())spatial->radioEmitter(npcRadioVoice.transmitter,false);
        }
        if(nativeCombatAudio){
            std::array<SpatialAudio::Loop,SpatialAudio::loopLimit> loops{};
            std::array<float,SpatialAudio::loopLimit> ranks;
            ranks.fill(1e9f);
            size_t count=0;
            for(size_t hand=0;hand<w.rpgAudioWeapons.size();++hand){
                const auto weapon=weaponsOut?w.rpgAudioWeapons[hand]:Weapon::Empty;
                const auto family=rpg::weaponSoundFamily(weapon);
                if(family==Family::Count){readiedWeapons[hand]=Weapon::Empty;continue;}
                const auto& arm=w.arms[hand];
                if(!arm.tracked)continue;
                const auto& bank=weaponRecordings[size_t(family)];
                if(readiedWeapons[hand]!=weapon&&!bank.ready.empty()){
                    playWorld(bank.ready,arm.muzzle,.85f,1.f,Priority::Normal);
                    ++stats.weaponReadies;
                }
                readiedWeapons[hand]=weapon;
                if(spatial&&!bank.idle.empty()){
                    loops[count++]={size_t(-1)-hand,&bank.idle,arm.muzzle,.30f};
                    ++stats.weaponIdleHands;
                }
            }
            // Keep close hand machinery and the nearest remaining scene motors within the existing loop budget.
            const auto firstEnemy=ranks.begin()+count;
            for(size_t i=0;spatial&&i<w.targets.size();++i){
                const auto &t=w.targets[i];
                if(t.ai.rpgMotor==EnemyMotor::Silent||!t.active||t.escaped||t.health<=0)continue;
                const float distance=length(t.center-head.position);
                if(distance>=145.f)continue;
                const float speed=length(t.ai.velocity);
                size_t clip=0;
                auto bed=CreatureBed::Count;
                switch(t.ai.rpgMotor){
                case EnemyMotor::Walker:clip=speed>.6f?1:0;break;
                case EnemyMotor::Tracked:clip=speed>.6f?2:0;break;
                case EnemyMotor::Turbine:clip=3;break;
                case EnemyMotor::Submersible:clip=4;break;
                case EnemyMotor::Creature: bed=CreatureBed::Breath;break;
                case EnemyMotor::CreatureWing: bed=CreatureBed::Wing;break;
                case EnemyMotor::CreatureSwim: bed=speed>.6f?CreatureBed::Swim:CreatureBed::Breath;break;
                case EnemyMotor::CreatureCrawl: bed=speed>.6f?CreatureBed::Crawl:CreatureBed::Breath;break;
                default:break;
                }
                const auto* sample=&enemyBeds[clip];
                if(bed!=CreatureBed::Count){
                    if(t.ai.rpgGrapplePilot)bed=CreatureBed::Breath;
                    const auto& takes=creatureBeds[size_t(bed)];
                    sample=takes.empty()?&enemyBeds[5]:&takes[i%takes.size()];
                }
                if(sample->empty())continue;
                const float rank=distance/(1.f+std::min(speed,12.f)*.025f);
                auto iter=std::lower_bound(firstEnemy,ranks.end(),rank);
                if(iter==ranks.end())continue;
                const auto index=size_t(iter-ranks.begin());
                std::move_backward(iter,ranks.end()-1,ranks.end());
                std::move_backward(loops.begin()+index,loops.end()-1,loops.end());
                ranks[index]=rank;count=std::min(count+1,loops.size());
                const float falloff=std::clamp((145.f-distance)/65.f,0.f,1.f);
                loops[index]={i+1,sample,t.center,(.16f+.016f*std::min(speed,13.f))*falloff};
                if(bed==CreatureBed::Count)
                    loops[index].rate=.91f+.21f*std::clamp(speed/13.f,0.f,1.f);
            }
            if(spatial)spatial->loops({loops.data(),count});
        }
    }
    if(w.elapsed<lastElapsed)lastSonarSweep=0;
    if(active&&w.rpgPrototype&&w.sensorsWorking()&&!w.lifeSpent&&!w.rpgDroneView&&
       w.rpgRadar.distantBearings&&!radarSonar.empty()){
        if(w.rpgRadar.sweep!=lastSonarSweep){
            lastSonarSweep=w.rpgRadar.sweep;
            if(voices.size()>=24)voices.erase(voices.begin());
            voices.push_back({&radarSonar,0,.32f,.32f});
            ++stats.sonarPings;
        }
    }
    if(music){int desired=menuMusic?-3:active?(w.game&&w.mission>=0?w.mission%16:w.mission):-2;if(desired!=musicMission||(active&&w.elapsed<lastElapsed)){if(desired==-3)music->intro();else music->start(desired);musicMission=desired;}}lastElapsed=w.elapsed;
    musicPlaying=active||menuMusic;
    if(campaignScore)campaignScore->select(campaignCue);
    bool alarm=active&&w.missionStatus==MissionStatus::Ejecting;
    if(alarm&&!alarmPlaying){alarmTime=w.ejectionTime;alarmPhase=0;}alarmPlaying=alarm;
    for(auto& e:w.sounds){
        using rpg::CombatCue;
        if(nativeCombatAudio && e.kind==SoundEvent::Special && rpg::isNpcRadio(e.weapon)){
            const auto index=size_t(e.weapon-rpg::npcRadioBase);
            const auto &sample=npcRadioSamples[npcRadioSamples[index].empty()?index%rpg::npcRadioBankSize:index];
            const float distance=length(e.position-listenerPosition);
            if(active && !w.lifeSpent && !npcRadioVoice.playing() && !sample.empty() && distance<rpg::npcRadioMaxRange){
                const float interference=rpg::npcRadioInterference(distance);
                npcRadioVoice={&sample,0,e.position,interference,interference,true,{},e.transmitterActor};
                if(spatial)spatial->radioEmitter(e.position,true);
                ++stats.npcRadioStarted;
            }
            continue;
        }
        const bool enemyCue=e.kind==SoundEvent::Special&&e.weapon>=int(CombatCue::EnemyStep);
        const bool kaijuVocal=nativeCombatAudio && e.kind==SoundEvent::Special &&
                              e.weapon>=int(CombatCue::KaijuShriek) && e.weapon<=int(CombatCue::KaijuSonar);
        const bool arrival=nativeCombatAudio&&e.kind==SoundEvent::Special&&
            e.weapon>=int(CombatCue::ArrivalWarp)&&e.weapon<=int(CombatCue::ArrivalBurrow);
        const float enemyDistance=enemyCue?length(e.position-listenerPosition):0.f;
        const float enemyRange=enemyCue?enemyCueRange(CombatCue(e.weapon)):190.f;
        if(enemyCue&&enemyDistance>=enemyRange)continue;
        const std::vector<float>* chosen=nullptr;
        auto priority=Priority::Normal;
        float rate=1.f;
        if(nativeCombatAudio){
            CombatCue cue;
            switch(e.kind){
            case SoundEvent::Special:
                if(e.weapon<0||size_t(e.weapon)>=combatAudio.size())continue;
                cue=CombatCue(e.weapon);break;
            case SoundEvent::Shot:
                switch(Weapon(e.weapon)){
                case Weapon::Rocket:case Weapon::Homing:case Weapon::Cruise:cue=CombatCue::Rocket;break;
                case Weapon::Rail:cue=CombatCue::Beam;break;
                case Weapon::HeavyMachinegun:case Weapon::Shotgun:case Weapon::Grenade:
                case Weapon::AutoGrenade:cue=CombatCue::Cannon;break;
                case Weapon::Cutter:cue=CombatCue::Melee;break;
                default:cue=CombatCue::Rifle;break;
                }
                break;
            case SoundEvent::Punch:cue=CombatCue::Melee;break;
            case SoundEvent::Footstep:cue=CombatCue::Step;break;
            case SoundEvent::CutterMotor:cue=CombatCue::Motor;break;
            case SoundEvent::LargeExplosion:cue=CombatCue::HeavyExplosion;break;
            case SoundEvent::Explosion:cue=CombatCue::Explosion;break;
            case SoundEvent::Damage:cue=CombatCue::MetalHit;break;
            case SoundEvent::PickupSound:cue=CombatCue::Repair;break;
            case SoundEvent::Complete:cue=CombatCue::Ready;break;
            case SoundEvent::Failed:cue=CombatCue::Failed;break;
            default:continue;
            }
            chosen=&combatAudio[size_t(cue)];
            auto& creature=kaijuRecordings[size_t(cue)];
            if(!creature.takes.empty()){
                chosen=&creature.takes[creature.nextTake];
                creature.nextTake=(creature.nextTake+1)%creature.takes.size();
                ++stats.kaijuRecordedEvents;
            }else if(creature.fallback!=CombatCue::Count){
                chosen=&combatAudio[size_t(creature.fallback)];
            }
            if(cue==CombatCue::Rifle&&!rifleAlternate.empty()){
                if(nextRifleAlternate)chosen=&rifleAlternate;
                nextRifleAlternate=!nextRifleAlternate;
            }else if(cue==CombatCue::Step&&!stepAlternate.empty()){
                if(nextStepAlternate)chosen=&stepAlternate;
                nextStepAlternate=!nextStepAlternate;
            }else if(cue==CombatCue::EnemyStep&&!enemyStepAlternate.empty()){
                if(nextEnemyStepAlternate)chosen=&enemyStepAlternate;
                nextEnemyStepAlternate=!nextEnemyStepAlternate;
            }
            Family family=Family::Count;
            if(e.kind==SoundEvent::Punch||(e.kind==SoundEvent::Special&&
               (cue==CombatCue::Charge||cue==CombatCue::Melee||cue==CombatCue::Pulse)))
                family=rpg::weaponSoundFamily(e.audioWeapon);
            if(e.kind==SoundEvent::Shot)family=rpg::weaponSoundFamily(Weapon(e.weapon));
            if(e.kind==SoundEvent::CutterMotor)family=Family::Melee;
            if(cue==CombatCue::Cannon&&e.kind==SoundEvent::Special)family=Family::Cannon;
            if(family!=Family::Count){
                auto& bank=weaponRecordings[size_t(family)];
                if(cue==CombatCue::Charge){
                    if(!bank.charge.empty()){chosen=&bank.charge;++stats.weaponCharges;}
                }else{
                    for(size_t offset=0;offset<bank.shots.size();++offset){
                        const unsigned take=(bank.nextTake+offset)%bank.shots.size();
                        const auto &shot=bank.shots[take];
                        if(shot.dry.empty())continue;
                        chosen=&shot.dry;
                        if(w.skyKind==SkyKind::Interior||w.indoors(e.position)){
                            if(!shot.hangar.empty()){chosen=&shot.hangar;++stats.weaponHangarReturns;}
                        }else if(w.skyKind!=SkyKind::Space&&w.skyKind!=SkyKind::Lunar&&
                                 w.skyKind!=SkyKind::Underwater&&!shot.yard.empty()){
                            chosen=&shot.yard;++stats.weaponYardReturns;
                        }
                        bank.nextTake=(take+1)%bank.shots.size();
                        ++stats.weaponReports;
                        break;
                    }
                }
            }
            float variation=0.f;
            switch(cue){
            case CombatCue::Rifle:case CombatCue::Cannon:case CombatCue::Pulse: variation=.04f;break;
            case CombatCue::MetalHit:case CombatCue::Melee: variation=.045f;break;
            case CombatCue::Step:case CombatCue::EnemyStep: variation=.035f;break;
            default:break;
            }
            if(variation>0){
                variationSeed^=variationSeed<<13;variationSeed^=variationSeed>>17;variationSeed^=variationSeed<<5;
                rate+=variation*(float(variationSeed>>8)*(2.f/16777216.f)-1.f);
            }
            if((arrival&&enemyDistance<180)||((cue==CombatCue::KaijuBite||cue==CombatCue::KaijuImpact)&&enemyDistance<20))
                priority=Priority::Foreground;
            else if(cue==CombatCue::EnemyStep||cue==CombatCue::EnemyAlert||cue==CombatCue::EnemyGrowl||
                    cue==CombatCue::KaijuTread||cue==CombatCue::KaijuSkitter)
                priority=Priority::Background;
            else if((cue==CombatCue::HeavyExplosion||e.kind==SoundEvent::Shot||cue==CombatCue::Cannon||
                     cue==CombatCue::Pulse)&&length(e.position-listenerPosition)<32.f)
                priority=Priority::Foreground;
        }else{
        constexpr int weaponEvents[]={12,0,1,6,3,4,5,-1,7,13,11,12,9};
        int event=-1;
        switch(e.kind){
        case SoundEvent::Shot:
            event=weaponEvents[std::clamp(e.weapon,0,int(Weapon::Count)-1)];
            // IS2 event 7 only stops the cutter. Reuse its rocket launch cue
            // for the native cruise missile rather than requesting silent audio.
            if(w.game&&e.weapon==8)event=1;
            break;
        case SoundEvent::Punch:case SoundEvent::Footstep:event=10;break;
        case SoundEvent::CutterMotor:event=6;break;
        case SoundEvent::LargeExplosion:event=22;break;
        case SoundEvent::Explosion:event=20;break;
        case SoundEvent::Damage:event=14;break;
        default:break;
        }
        bool is2Weapon=e.kind==SoundEvent::Shot&&e.weapon>=int(Weapon::Shotgun);
        if(w.game&&event>=10&&!is2Weapon)event+=5; // IS2 inserted five additional events before footsteps/hits
        if(event<0)continue;
        chosen=&effects[w.game||is2Weapon?1:0][event];
        }
        const auto &sample=*chosen;
        if(sample.empty())continue;
        if(kaijuVocal)++stats.kaijuVocalsStarted;
        const float multiplier=arrival?1.36f*std::clamp((enemyRange-enemyDistance)/110.f,0.f,1.f):
            enemyCue?(kaijuVocal?1.1f:.60f)*std::clamp((enemyRange-enemyDistance)/(kaijuVocal?135.f:std::min(80.f,enemyRange*.55f)),0.f,1.f):
            e.kind==SoundEvent::Footstep?.5f:1.f;
        // The native party damage event uses the struck pilot's body origin.
        // Keep the local hit as a cockpit cue, but place squadmate hits in the world.
        const bool remoteDamage=nativeCombatAudio&&e.kind==SoundEvent::Damage&&
            length(e.position-w.position)>.25f&&length(e.position-w.eye())>.25f;
        const bool worldCue=spatialEvent(e.kind)||remoteDamage;
        if(worldCue){playWorld(sample,e.position,multiplier,rate,priority);continue;}
        // Interface/status cues stay centred and bypass HRTF/propagation.
        // Voice chat and music use their existing separate mixing paths.
        if(voices.size()>=24)voices.erase(voices.begin());
        voices.push_back({&sample,0,.45f*multiplier,.45f*multiplier});
    }
    w.sounds.clear();SDL_UnlockAudioDevice(device);
}
}
