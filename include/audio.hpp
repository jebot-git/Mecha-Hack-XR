#pragma once
#include "simulation.hpp"
#include <SDL.h>
#include "music.hpp"
#include "weather_audio.hpp"
#include "spatial_audio.hpp"
#include "rpg/weapon_audio.hpp"
#include "rpg/npc_audio.hpp"
#include "net/radio_damage.hpp"
#include <span>
namespace iron {
namespace rpg {class ScorePlayer;}
// SDL's audio clock owns playback. update() submits events under the device lock;
// the callback never reads the world, performs I/O, or allocates sample buffers.
class Audio {
    SDL_AudioDeviceID device=0;
    std::unique_ptr<rpg::ScorePlayer> campaignScore;
    std::unique_ptr<Music> music;int musicMission=-2;float lastElapsed=-1;
    struct Voice {const std::vector<float>* samples=nullptr;double cursor=0;float left=1,right=1,rate=1;};
    std::vector<Voice> voices;
    std::array<std::vector<float>,rpg::npcRadioClips> npcRadioSamples;
    struct NpcRadio {
        const std::vector<float>* samples=nullptr;
        size_t cursor=0;
        Vec3 transmitter{};
        float interference=0,targetInterference=0;
        bool receiving=false;
        avp::voice::RadioDamage staticLayer;
        int actor=-1;
        unsigned tail=SpatialAudio::filterTailFrames;
        bool playing() const {return samples && (cursor<samples->size()||tail);}
    };
    NpcRadio npcRadioVoice;
    void mixNpcRadio(std::span<float> stereo);
    std::unique_ptr<SpatialAudio> spatial;
    WeatherAudio weather;
    std::array<float,24000> radio{};size_t radioRead=0,radioCount=0;
    std::array<std::array<std::vector<float>,32>,2> effects;
    std::array<std::vector<float>,static_cast<size_t>(rpg::CombatCue::Count)> combatAudio;
    struct WeaponRecording {
        struct Shot { std::vector<float> dry,yard,hangar; };
        std::array<Shot,3> shots;
        std::vector<float> charge,ready,idle;
        unsigned nextTake=0;
    };
    std::array<WeaponRecording,size_t(rpg::WeaponSoundFamily::Count)> weaponRecordings;
    std::array<Weapon,2> readiedWeapons{};
    void loadWeaponRecordings(const std::filesystem::path&);
    using RecordingTakes=std::vector<std::vector<float>>;
    struct KaijuRecording {
        RecordingTakes takes;
        rpg::CombatCue fallback=rpg::CombatCue::Count;
        size_t nextTake=0;
    };
    enum class CreatureBed { Breath, Wing, Swim, Crawl, Count };
    std::array<KaijuRecording,size_t(rpg::CombatCue::Count)> kaijuRecordings;
    std::array<RecordingTakes,size_t(CreatureBed::Count)> creatureBeds;
    void loadKaijuRecordings(const std::filesystem::path&);
    std::array<std::vector<float>,6> enemyBeds;
    std::vector<float> radarSonar;
    uint32_t lastSonarSweep=0;
    std::vector<float> rifleAlternate,stepAlternate,enemyStepAlternate;
    bool nextRifleAlternate=false,nextStepAlternate=false,nextEnemyStepAlternate=false;
    uint32_t variationSeed=0xa976fa43u;
    bool nativeCombatAudio=false;
    bool musicPlaying=false,alarmPlaying=false;double alarmTime=0,alarmPhase=0;
    float thermalHiss=0,thermalWarning=0,thermalTonePhase=0,thermalPulse=0,thermalFilter=0;
    uint32_t thermalNoise=0x48a1a4edu;
    float limiterGain=1;
    float soundVolume=1,musicVolume=1;
    static void callback(void*,Uint8*,int);
    void mix(std::span<float>);
public:
    struct Stats {uint64_t frames=0,nonzeroFrames=0,limitedFrames=0,spatialStarted=0,spatialQueries=0,spatialDropped=0,weatherFrames=0,recordedWeatherFrames=0,biomeFrames=0,recordedThunderFrames=0,thunderStarted=0,scoreFrames=0,sonarPings=0,thermalFrames=0,npcRadioStarted=0,kaijuVocalsStarted=0;
        uint64_t kaijuRecordedEvents=0;
        uint64_t weaponReports=0,weaponCharges=0,weaponReadies=0,weaponYardReturns=0,weaponHangarReturns=0;
        unsigned weaponIdleHands=0;float peak=0;};
private:
    Stats stats;
public:
    explicit Audio(const std::filesystem::path&,bool nativeCombat=false);~Audio();
    void update(World&,bool active,bool menuMusic=false,const AudioListener* listener=nullptr,int campaignCue=-1);
    static bool spatialEvent(SoundEvent::Kind kind){return kind==SoundEvent::Special||kind==SoundEvent::Shot||kind==SoundEvent::Punch||kind==SoundEvent::Footstep||kind==SoundEvent::CutterMotor||kind==SoundEvent::Explosion||kind==SoundEvent::LargeExplosion;}
    void setVolumes(float sound,float music);
    bool available()const{return device!=0;}
    bool spatialAvailable()const{return spatial!=nullptr;}
    size_t voiceBytes()const;
    void queueVoice(std::span<const float>);
    void clearVoice();
    Stats statistics()const;
};
}
