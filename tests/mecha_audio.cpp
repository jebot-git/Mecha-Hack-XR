#include "audio.hpp"
#include "coop.hpp"
#include "net/snapshot_packet.hpp"
#include "weather_audio.hpp"
#include "rpg/score.hpp"
#include <SDL.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace iron;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Validate the distributed WAVs independently of the mixer's legacy fallback.
static std::vector<Sint16> readBankSample(const std::string &name, int maximumPeak=29491){
    const auto path=std::filesystem::path(MECHA_ASSETS)/"fx/audio"/(name+".wav");
    SDL_AudioSpec spec{};
    Uint8 *raw=nullptr;
    Uint32 bytes=0;
    if(!SDL_LoadWAV(path.string().c_str(),&spec,&raw,&bytes))
        throw std::runtime_error("Cannot decode audio sample "+name+": "+SDL_GetError());
    const std::unique_ptr<Uint8,decltype(&SDL_FreeWAV)> buffer(raw,SDL_FreeWAV);
    if(spec.freq!=48000||spec.channels!=1||spec.format!=AUDIO_S16LSB||
       bytes<2*sizeof(Sint16)||bytes%sizeof(Sint16)!=0)
        throw std::runtime_error("Unexpected audio sample format: "+name);
    std::vector<Sint16> pcm(bytes/sizeof(Sint16));
    std::memcpy(pcm.data(),raw,bytes);
    int peak=0;
    for(auto &sample:pcm){
        sample=static_cast<Sint16>(SDL_SwapLE16(static_cast<Uint16>(sample)));
        peak=std::max(peak,std::abs(int(sample)));
    }
    if(peak<328||peak>maximumPeak)
        throw std::runtime_error("Audio sample is silent or has insufficient headroom: "+name);
    return pcm;
}

static std::vector<Sint16> readWeaponSample(const std::string& name){
    return readBankSample("weapons/"+name);
}

static void checkNpcRadioSpatial(){
    SpatialAudio spatial;
    std::array<float,512> mono;
    for(unsigned i=0;i<mono.size();++i)mono[i]=.2f*std::sin(i*.36f);
    auto render=[&](Vec3 source,const AudioListener &head,bool fresh){
        spatial.listener(head);spatial.radioEmitter(source,fresh);
        std::array<float,1024> stereo{};
        spatial.mixRadioAdd(mono,stereo); // let an orientation crossfade finish
        stereo.fill(0);spatial.mixRadioAdd(mono,stereo);
        std::array<double,2> power{};
        for(size_t i=0;i<stereo.size();++i)power[i%2]+=stereo[i]*stereo[i];
        return power;
    };
    auto left=render({-20,0,0},{},true);
    auto turned=render({-20,0,0},AudioListener::fromPose(avp::rotation(pi)),false);
    auto far=render({-160,0,0},{},true);
    check(left[0]>left[1]*1.5 && turned[1]>turned[0]*1.5,"Radio did not track transmitter bearing and physical head turn");
    check(std::abs(far[0]/left[0]-1)<.001,"Radio distance applied ordinary world-SFX attenuation on top of static");
    spatial.clear();
    std::array<float,1024> cleared{};
    spatial.mixRadioAdd(mono,cleared);
    check(std::all_of(cleared.begin(),cleared.end(),[](float sample){return sample==0;}),"Receiver leaked a previous transmission after reset");
}

static void checkKaijuAssets(){
    for(const std::string name:{"shriek","roar","sonar","mandibles","hiss","bite","swipe","impact",
            "discharge","spray","spine","skitter","tread","burrow","breath","wing","swim","crawl"}){
        std::array<std::vector<Sint16>,3> variations;
        for(size_t i=0;i<variations.size();++i){
            variations[i]=readBankSample("kaiju/"+name+"-"+std::to_string(i+1),32760);
            const auto& pcm=variations[i];
            if(name=="breath"||name=="wing"||name=="swim"||name=="crawl")
                check(pcm.size()>=24000&&std::abs(int(pcm.back())-pcm.front())<512,
                      "Creature bed is too short or discontinuous at its loop boundary");
        }
        check(variations[0]!=variations[1]&&variations[1]!=variations[2]&&variations[2]!=variations[0],
              "Duplicate creature performances");
    }
}

static void checkWeaponAssets(){
    for(const std::string family:{"auto","cannon","energy","melee","missile"}){
        for(const std::string role:{"report","cycle"}){
            std::array<std::vector<Sint16>,3> takes;
            for(size_t take=0;take<takes.size();++take)
                takes[take]=readWeaponSample(family+"-"+role+"-"+std::to_string(take+1));
            if(takes[0]==takes[1]||takes[0]==takes[2]||takes[1]==takes[2])
                throw std::runtime_error("Duplicate weapon takes: "+family+"-"+role);
        }
        std::array<std::vector<Sint16>,3> yards,hangars;
        for(size_t i=0;i<yards.size();++i){
            const auto suffix=std::to_string(i+1);
            yards[i]=readWeaponSample("returns/"+family+"-yard-"+suffix);
            hangars[i]=readWeaponSample("returns/"+family+"-hangar-"+suffix);
            const auto wetOnly=[](const auto &pcm){
                return pcm.size()>480&&std::all_of(pcm.begin(),pcm.begin()+480,[](Sint16 v){return std::abs(int(v))<=3;});
            };
            if(!wetOnly(yards[i])||!wetOnly(hangars[i])||hangars[i].size()<=yards[i].size())
                throw std::runtime_error("Missing wet predelay or enclosed decay: "+family+"-"+suffix);
            for(size_t previous=0;previous<i;++previous)
                if(yards[i]==yards[previous]||hangars[i]==hangars[previous])
                    throw std::runtime_error("Duplicate weapon room returns: "+family+"-"+suffix);
        }
        readWeaponSample(family+"-ready");
        readWeaponSample(family+"-charge");
        const auto idle=readWeaponSample(family+"-idle");
        const auto [low,high]=std::minmax_element(idle.begin(),idle.end());
        if(std::max(-int(*low),int(*high))>8192||
           std::abs(int(idle.front())-int(idle.back()))>328)
            throw std::runtime_error("Weapon idle is too loud or clicks at the loop boundary: "+family);
    }
}

static void checkWeaponRouting(Audio &audio){
    using Family=rpg::WeaponSoundFamily;
    const std::pair<Weapon,Family> expected[]={
        {Weapon::Rifle,Family::Auto},{Weapon::Gatling,Family::Auto},
        {Weapon::HeavyMachinegun,Family::Cannon},{Weapon::Shotgun,Family::Cannon},
        {Weapon::Grenade,Family::Cannon},{Weapon::AutoGrenade,Family::Cannon},
        {Weapon::Rocket,Family::Missile},{Weapon::Homing,Family::Missile},{Weapon::Cruise,Family::Missile},
        {Weapon::Rail,Family::Energy},{Weapon::Cutter,Family::Melee}
    };
    World world;
    world.rpgPrototype=true;
    world.missionStatus=MissionStatus::Active;
    const Vec3 near=world.eye()+Vec3{2,0,-6};
    check(rpg::weaponSoundFamily(Weapon::Empty)==Family::Count&&
          rpg::weaponSoundFamily(Weapon::Shield)==Family::Count,"Non-firing item has a report family");
    for(const auto &[weapon,family]:expected){
        check(rpg::weaponSoundFamily(weapon)==family,"Incorrect weapon sound family");
        audio.update(world,false);
        const auto before=audio.statistics();
        for(int shot=0;shot<3;++shot)
            world.sounds.push_back({SoundEvent::Shot,int(weapon),near});
        audio.update(world,true);
        const auto after=audio.statistics();
        check(after.weaponReports==before.weaponReports+3&&after.spatialStarted==before.spatialStarted+3,
              "Weapon did not play its three layered reports through single positional voices");
    }
    audio.update(world,false);
    auto before=audio.statistics();
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Cannon),near});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Pulse),near,Weapon::Rail});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Melee),near,Weapon::Cutter});
    world.sounds.push_back({SoundEvent::Punch,0,near,Weapon::Cutter});
    world.sounds.push_back({SoundEvent::CutterMotor,0,near,Weapon::Cutter});
    world.sounds.push_back({SoundEvent::Punch,0,near}); // An unequipped fist uses the original hit.
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Charge),near,Weapon::Rail});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Charge),near});
    audio.update(world,true);
    const auto shots=audio.statistics();
    check(shots.weaponReports==before.weaponReports+5&&shots.weaponCharges==before.weaponCharges+1&&
          shots.spatialStarted==before.spatialStarted+8,"Special shot, charge or melee event took the wrong path");

    const auto checkRoom=[&](SkyKind sky,Vec3 muzzle,int yard,int hangar){
        audio.update(world,false);
        const auto previous=audio.statistics();
        world.skyKind=sky;
        world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),muzzle});
        audio.update(world,true);
        const auto next=audio.statistics();
        check(next.weaponReports==previous.weaponReports+1&&next.spatialStarted==previous.spatialStarted+1&&
              next.weaponYardReturns==previous.weaponYardReturns+yard&&
              next.weaponHangarReturns==previous.weaponHangarReturns+hangar,
              "Weapon reflections did not match the firing position");
    };
    world.interiorRegions.push_back({{near.x-1,near.z-1,near.x+1,near.z+1},near.y-1,near.y+1});
    const Vec3 outside=near+Vec3{3,0,0};
    checkRoom(SkyKind::Urban,outside,1,0);
    checkRoom(SkyKind::Urban,near,0,1);
    checkRoom(SkyKind::Space,near,0,1);
    checkRoom(SkyKind::Interior,outside,0,1);
    for(auto sky:{SkyKind::Space,SkyKind::Lunar,SkyKind::Underwater})
        checkRoom(sky,outside,0,0);

    world.rpgAudioWeapons[0]=Weapon::Rail;
    world.arms[0].tracked=true;
    world.arms[0].muzzle=near;
    audio.update(world,true);
    const auto armed=audio.statistics();
    check(armed.weaponReadies==shots.weaponReadies+1&&armed.weaponIdleHands==1,
          "Drawing a tracked weapon failed to start its ready sound and idle");
    audio.update(world,true);
    check(audio.statistics().weaponReadies==armed.weaponReadies,"Held weapon restarted its ready sound");
    world.arms[0].tracked=false;
    audio.update(world,true);
    check(audio.statistics().weaponIdleHands==0,"Untracked weapon kept its idle sound");
    audio.update(world,false);
}

static void checkCreatureBeds(Audio& audio){
    World world;
    world.rpgPrototype=true;
    Target actor{};
    actor.active=true;
    actor.health=1;
    actor.center=world.eye()+Vec3{0,0,-5};
    world.targets.push_back(actor);
    for(auto motor:{EnemyMotor::CreatureCrawl,EnemyMotor::CreatureSwim,EnemyMotor::CreatureWing}){
        auto& ai=world.targets.front().ai;
        ai.rpgMotor=motor;
        ai.velocity={};
        audio.update(world,false);
        const auto before=audio.statistics().spatialStarted;
        audio.update(world,true);
        check(audio.statistics().spatialStarted==before+1,"Resting creature bed failed to start");
        ai.velocity={0,0,4};
        audio.update(world,true);
        const auto moving=audio.statistics().spatialStarted;
        check(moving==before+(motor==EnemyMotor::CreatureWing?1:2),"Wrong moving creature bed");
        audio.update(world,true);
        check(audio.statistics().spatialStarted==moving,"Continuous creature bed restarted");
        ai.rpgGrapplePilot=1;
        audio.update(world,true);
        check(audio.statistics().spatialStarted==moving+1,"Canopy grapple did not switch to close breathing");
        ai.rpgGrapplePilot=0;
    }
    audio.update(world,false);
}

static void checkNetworkWeaponAudio(){
    const Vec3 muzzle{7,3,-13};
    World server,client;
    server.sounds={{SoundEvent::Special,int(rpg::CombatCue::Charge),muzzle,Weapon::Rail},
                   {SoundEvent::Special,int(rpg::CombatCue::Charge),muzzle},
                   {SoundEvent::Special,rpg::radioCue(2,rpg::RadioLine::FriendlySupport,3),muzzle,Weapon::Empty,13}};
    const auto snapshot=coop::worldWire(server);
    const auto &sounds=snapshot.at("sounds");
    check(sounds.at(0).at("audio_weapon")==int(Weapon::Rail)&&!sounds.at(1).contains("audio_weapon"),
          "Sound snapshot lost the weapon family or altered a legacy event");
    coop::worldRead(coop::decodeStatePacket(coop::encodeSnapshotPacket(snapshot)),client);
    check(client.sounds.size()==3,"Compressed sound snapshot lost an event");
    check(client.sounds[2].weapon==server.sounds[2].weapon && client.sounds[2].transmitterActor==13 &&
          client.sounds[0].transmitterActor==-1 && !sounds.at(0).contains("transmitter_actor"),
          "Radio wire roundtrip lost the take or moving transmitter");
    for(size_t i=0;i<client.sounds.size();++i){
        const auto &event=client.sounds[i];
        check(event.kind==SoundEvent::Special&&event.weapon==server.sounds[i].weapon&&length(event.position-muzzle)<.001f,
              "Remote audio event changed cue or position");
    }
    check(client.sounds[0].audioWeapon==Weapon::Rail&&client.sounds[1].audioWeapon==Weapon::Empty,
          "Client failed to distinguish a typed charge from a legacy charge");
}

static void checkRecordingFallback(){
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("mecha-legacy-audio-"+std::to_string(SDL_GetPerformanceCounter()));
    if(!fs::create_directory(root))throw std::runtime_error("Could not create an isolated audio fixture");
    struct Cleanup {
        fs::path path;
        ~Cleanup(){std::error_code error;fs::remove_all(path,error);}
    } cleanup{root};
    const auto legacy=root/"fx/audio";
    fs::create_directories(legacy);
    // Keep the flat legacy bank and just one standalone rifle take from the new bank.
    for(const auto &file:fs::directory_iterator(fs::path(MECHA_ASSETS)/"fx/audio"))
        if(file.is_regular_file()&&file.path().extension()==".wav")
            fs::copy_file(file.path(),legacy/file.path().filename());
    fs::create_directory(legacy/"weapons");
    const auto partial=fs::path("weapons/auto-report-2.wav");
    fs::copy_file(fs::path(MECHA_ASSETS)/"fx/audio"/partial,legacy/partial);
    Audio fallback(root,true);
    check(fallback.available()&&fallback.spatialAvailable(),"Fallback audio failed to initialise");
    World world;
    world.rpgPrototype=true;
    world.missionStatus=MissionStatus::Active;
    const auto near=world.eye()+Vec3{0,0,-5};
    world.skyKind=SkyKind::Urban;
    for(Weapon weapon:{Weapon::Grenade,Weapon::Rocket,Weapon::Rail,Weapon::Cutter})
        world.sounds.push_back({SoundEvent::Shot,int(weapon),near});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Charge),near,Weapon::Rail});
    fallback.update(world,true);
    const auto old=fallback.statistics();
    check(old.spatialStarted==5&&old.weaponReports==0&&old.weaponCharges==0,
          "Missing weapon family failed to fall back to the legacy reports and charge");
    for(int shot=0;shot<3;++shot)
        world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),near});
    fallback.update(world,true);
    const auto partialBank=fallback.statistics();
    check(partialBank.weaponReports==3&&partialBank.spatialStarted==old.spatialStarted+3&&
          partialBank.weaponYardReturns==0&&partialBank.weaponHangarReturns==0,
          "One dry take failed to cover missing takes, cycles and room returns");
    fallback.update(world,false);
    const auto beforeCreature=fallback.statistics();
    for(auto cue:{rpg::CombatCue::KaijuRoar,rpg::CombatCue::KaijuBite,rpg::CombatCue::KaijuSkitter})
        world.sounds.push_back({SoundEvent::Special,int(cue),near});
    fallback.update(world,true);
    const auto afterCreature=fallback.statistics();
    check(afterCreature.kaijuRecordedEvents==0&&afterCreature.spatialStarted==beforeCreature.spatialStarted+3,
          "An older bank lost creature calls or contact sounds");
}

static void checkListenerExposure(Audio& audio){
    World world;
    world.rpgPrototype=true;
    world.skyKind=SkyKind::Wilderness;
    world.climate=Climate::Temperate;
    world.missionStatus=MissionStatus::Active;
    world.environment={Lighting::Day,Weather::Rain,19};
    world.waterRegions.push_back({{8,-5,24,12},2,4});
    world.interiorRegions.push_back({{32,-5,48,12},0,12});
    AudioListener head;
    auto move=[&](Vec3 position){
        head.position=position;
        audio.update(world,true,false,&head);
        SDL_Delay(60);
        return audio.statistics();
    };
    move({0,5,0});
    move({16,1,0});
    SDL_Delay(350); // Let the room-to-water crossfade complete.
    const auto submerged=audio.statistics();
    const auto lake=move({16,1,0});
    check(lake.weatherFrames==submerged.weatherFrames&&lake.biomeFrames==submerged.biomeFrames&&
          lake.recordedWeatherFrames>submerged.recordedWeatherFrames,"Underwater listener still heard the biome or lost the water bed");
    const auto outside=move({16,5,0});
    check(outside.weatherFrames>lake.weatherFrames&&outside.biomeFrames>lake.biomeFrames,
          "Surfacing failed to restore rain and wilderness");
    const auto room=move({40,5,0});
    const auto stillInside=move({40,5,0});
    check(stillInside.weatherFrames==room.weatherFrames&&stillInside.biomeFrames>room.biomeFrames,
          "Interior listener still heard direct rain or lost the room bed");
    audio.update(world,false);
}

static void checkEnvironmentalMix(){
    WeatherAudio mix;
    auto steady=[](float level){return std::vector<float>(32000,level);};
    mix.setAmbienceBed(Ambience::Urban,steady(.02f));
    mix.setAmbienceBed(Ambience::UrbanNight,steady(.03f));
    mix.setAmbienceBed(Ambience::Interior,steady(.01f));
    mix.setAmbienceBed(Ambience::Wilderness,steady(.025f));
    mix.setBed(Weather::Fog,steady(.04f));
    mix.setUnderwaterBed(steady(.05f));
    std::vector<float> pcm(48000);
    auto tail=[&](Weather weather,Lighting light,Ambience scene,bool submerged=false,bool sheltered=false){
        mix.update({light,weather,7},2.f,true,submerged,scene,sheltered);
        std::fill(pcm.begin(),pcm.end(),0.f);
        mix.mix(pcm,1.f);
        return pcm.back();
    };
    check(std::abs(tail(Weather::Clear,Lighting::Day,Ambience::Urban)-.02f)<.0001f,
          "Clear city lost its ambience");
    check(std::abs(tail(Weather::Clear,Lighting::Night,Ambience::Urban)-.03f)<.0001f,
          "Night city did not select its separate recording");
    check(std::abs(tail(Weather::Clear,Lighting::Night,Ambience::Wilderness)-.025f)<.0001f,
          "Missing night recording did not retain the day bed");
    check(tail(Weather::Fog,Lighting::Day,Ambience::Urban)>.045f,
          "Environmental and weather beds did not play together");
    check(std::abs(tail(Weather::Rain,Lighting::Day,Ambience::Interior,false,true)-.01f)<.0001f,
          "Direct rain continued inside the room");
    check(std::abs(tail(Weather::Storm,Lighting::Day,Ambience::Urban,true)-.05f)<.0001f,
          "Submersion did not replace the outdoor mix");
    mix.update({},2.f,false);
    std::fill(pcm.begin(),pcm.end(),0.f);
    mix.mix(pcm,1.f);
    check(std::abs(pcm.back())<.00001f,"Environmental beds remained active after leaving combat");

    const EnvironmentSettings storm{Lighting::Day,Weather::Storm,31};
    const float flash=34.f-weatherNoise(storm.seed,0)*10.f;
    for(bool recorded:{false,true}){
        WeatherAudio thunder;
        // A partial bank still needs to give every strike a return.
        if(recorded)thunder.setThunderTake(2,steady(.22f));
        thunder.update(storm,flash-.1f,true);
        thunder.update(storm,flash+.1f,true);
        check(thunder.statistics().thunderStarted==0,"Thunder preceded the flash");
        thunder.update(storm,flash+1.22f,true);
        thunder.update(storm,flash+1.24f,true);
        std::fill(pcm.begin(),pcm.end(),0.f);
        thunder.mix(pcm,1.f);
        const auto stats=thunder.statistics();
        check(stats.thunderStarted==1&&stats.thunderFrames>0&&
              ((stats.recordedThunderFrames>0)==recorded),"Thunder selection or strike timing failed");
    }
}

int main(){try{
    checkNetworkWeaponAudio();
    checkEnvironmentalMix();
    checkWeaponAssets();
    checkKaijuAssets();
    {
        WeatherAudio beds;
        beds.setBed(Weather::Rain,std::vector<float>(16000,.04f));
        beds.setUnderwaterBed(std::vector<float>(16000,.02f));
        std::vector<float> mixed(40000);
        beds.update({Lighting::Day,Weather::Rain,7},2.f,true);
        beds.mix(mixed,1.f);
        const auto played=beds.statistics();
        check(played.recordedFrames==20000,"Recorded rain did not wrap on the audio clock");
        std::fill(mixed.begin(),mixed.end(),0.f);
        beds.update({Lighting::Day,Weather::Clear,7},3.f,true);
        beds.mix(mixed,1.f);
        check(mixed.front()>.035f&&std::abs(mixed.back())<.0001f,"Recorded rain did not fade when the weather cleared");
        std::fill(mixed.begin(),mixed.end(),0.f);
        beds.update({Lighting::Day,Weather::Clear,7},4.f,true,true);
        beds.mix(mixed,1.f);
        check(std::abs(mixed.back()-.02f)<.0001f,"Underwater bed failed in clear weather");
    }
    for(int kind=int(Weather::IonStorm);kind<int(weatherNames.size());++kind){
        WeatherAudio ambience;
        ambience.update({Lighting::Day,Weather(kind),42},5.25f,true);
        std::array<float,4096> audio{};
        ambience.mix(audio,.65f);
        const auto peak=*std::max_element(audio.begin(),audio.end());
        check(std::isfinite(peak)&&peak>.0001f&&peak<.3f,"Weather hazard ambience is silent or clipped");
    }
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    checkRecordingFallback();
    Audio audio(MECHA_ASSETS,true);check(audio.available(),"Native audio output could not initialize");check(audio.spatialAvailable(),"Native spatial decoder could not initialize");
    checkListenerExposure(audio);
    checkWeaponRouting(audio);
    checkCreatureBeds(audio);
    World menu;
    audio.update(menu,false,false,nullptr,int(rpg::ScoreCue::Main));
    SDL_Delay(170);
    auto title=audio.statistics();
    check(title.scoreFrames>3000&&title.nonzeroFrames>0,"Tracker score did not play in the menu");
    World world;world.rpgPrototype=true;world.climate=Climate::Temperate;world.environment={Lighting::Night,Weather::Rain,42};world.missionStatus=MissionStatus::Active;
    world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rifle),world.position+Vec3{0,3,-12}});world.sounds.push_back({SoundEvent::Footstep,0,world.position});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::Charge),world.position+Vec3{0,4,-10}});
    audio.update(world,true,false,nullptr,int(rpg::factionScore(1,false)));check(world.sounds.empty(),"Native event submission was not consumed");SDL_Delay(100);
    auto rain=audio.statistics();check(rain.frames>0&&rain.nonzeroFrames>0&&rain.weatherFrames>0&&rain.recordedWeatherFrames>0&&rain.spatialStarted>=3,"Authored samples, recorded rain and spatial paths did not mix");check(std::isfinite(rain.peak)&&rain.peak<=.901f,"Original mixed output clipped");
    for(int i=0;i<9;++i){
        Target actor;actor.active=true;actor.health=20;actor.center={float(2*i),4,-22};
        actor.ai.rpgMotor=i%2?EnemyMotor::Walker:EnemyMotor::Turbine;
        actor.ai.velocity={5,0,0};world.targets.push_back(actor);
    }
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::EnemyAlert),{0,4,-22}});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::EnemyStep),{2,0,-22}});
    audio.update(world,true);
    const auto movement=audio.statistics();
    check(movement.spatialStarted==rain.spatialStarted+SpatialAudio::loopLimit+2,"Enemy beds or movement transients missing");
    for(int i=0;i<4;++i)audio.update(world,true);
    check(audio.statistics().spatialStarted==movement.spatialStarted,"Sustained enemy motors restarted on every update");
    for(auto &target:world.targets)target.active=false;
    audio.update(world,true);
    auto impacts=audio.statistics();
    world.sounds.push_back({SoundEvent::Damage,0,world.position});
    audio.update(world,true);
    check(audio.statistics().spatialStarted==impacts.spatialStarted,"Local armour hit entered the positional mixer");
    world.sounds.push_back({SoundEvent::Damage,0,world.eye()+Vec3{10,0,0}});
    audio.update(world,true);
    impacts=audio.statistics();
    check(impacts.spatialStarted==movement.spatialStarted+1,"Squadmate armour hit did not enter the positional mixer");
    world.rpgRadar.distantBearings=1;world.rpgRadar.sweep=1;
    audio.update(world,true);
    const auto ping=audio.statistics();
    check(ping.sonarPings==1&&ping.spatialStarted==impacts.spatialStarted,"Sonar ping is missing or spatialized outside the cockpit");
    audio.update(world,true);
    check(audio.statistics().sonarPings==1,"Sonar repeated during one activation");
    world.elapsed=5;world.rpgRadar.sweep=2;
    world.rpgInjuries=uint32_t(MechaInjury::Sensors);
    audio.update(world,true);
    check(audio.statistics().sonarPings==1,"Offline sensors emitted a sonar ping");
    world.rpgInjuries=0;
    audio.update(world,true);
    check(audio.statistics().sonarPings==2,"Sonar did not ping on the next activation");
    world.rpgRadar.distantBearings=0;world.rpgHeat=.94f;
    audio.update(world,true);SDL_Delay(45);
    const auto hot=audio.statistics();
    check(hot.thermalFrames>0,"Hot reactor did not emit a cockpit warning");
    world.rpgDroneView=true;
    audio.update(world,true);
    const auto droneFrames=audio.statistics().thermalFrames;
    SDL_Delay(25);
    check(audio.statistics().thermalFrames==droneFrames,"Reactor warning leaked into drone view");
    world.rpgDroneView=false;world.rpgOverheated=true;world.rpgHeat=.5f;
    audio.update(world,true);SDL_Delay(30);
    check(audio.statistics().thermalFrames>hot.thermalFrames,"Reactor vent was silent during cooldown");
    // The receiver has its own directional feed alongside world SFX.
    checkNpcRadioSpatial();
    check(rpg::npcRadioInterference(0)==0 && rpg::npcRadioInterference(35)==0 &&
          rpg::npcRadioInterference(107.5f)==.5f && rpg::npcRadioInterference(180)==1,
          "NPC radio interference does not increase with distance");
    const Vec3 near=world.eye()+Vec3{0,0,-20};
    auto started=audio.statistics();
    for(int clip=0;clip<rpg::npcRadioClips;++clip){
        audio.update(world,false);
        world.sounds.push_back({SoundEvent::Special,rpg::npcRadioBase+clip,near});
        audio.update(world,true);
    }
    const auto radio=audio.statistics();
    check(radio.npcRadioStarted==started.npcRadioStarted+rpg::npcRadioClips,"A faction radio clip is missing");
    check(radio.spatialStarted==started.spatialStarted && radio.spatialQueries>=started.spatialQueries+rpg::npcRadioClips,
          "NPC reception failed to prepare a dedicated HRTF for each spoken clip");
    audio.update(world,false);
    world.sounds.push_back({SoundEvent::Special,rpg::npcRadioBase,near+Vec3{0,0,-240}});
    for(const auto cue:{rpg::CombatCue::KaijuShriek,rpg::CombatCue::KaijuRoar,rpg::CombatCue::KaijuSonar})
        world.sounds.push_back({SoundEvent::Special,int(cue),near});
    audio.update(world,true);
    const auto creature=audio.statistics();
    check(creature.npcRadioStarted==radio.npcRadioStarted,"Out-of-range radio was audible");
    check(creature.kaijuVocalsStarted==radio.kaijuVocalsStarted+3 &&
          creature.spatialStarted==radio.spatialStarted+3,"Kaiju vocals failed positional playback");
    audio.update(world,false);
    // All attack/movement cues use the new mono recordings, through the same
    // positional path as battle cries. A distant canopy bite should stay local.
    using Cue=rpg::CombatCue;
    constexpr Cue creatureFoley[]{Cue::KaijuMandibles,Cue::KaijuHiss,Cue::KaijuBite,Cue::KaijuSwipe,
        Cue::KaijuImpact,Cue::KaijuDischarge,Cue::KaijuSpray,Cue::KaijuSpines,Cue::KaijuBurrow,
        Cue::KaijuSkitter,Cue::KaijuTread};
    const auto beforeFoley=audio.statistics();
    for(auto cue:creatureFoley)world.sounds.push_back({SoundEvent::Special,int(cue),near});
    world.sounds.push_back({SoundEvent::Special,int(Cue::KaijuBite),near+Vec3{0,0,-100}});
    audio.update(world,true);
    const auto afterFoley=audio.statistics();
    check(creature.kaijuRecordedEvents==radio.kaijuRecordedEvents+3&&
          afterFoley.kaijuRecordedEvents==beforeFoley.kaijuRecordedEvents+std::size(creatureFoley)&&
          afterFoley.weaponReports==beforeFoley.weaponReports,
          "Creature attack/locomotion selection or distance gating failed");
    audio.update(world,false);
    const auto beforeArrivals=audio.statistics();
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::ArrivalWarp),world.eye()+Vec3{105,0,-160}});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::ArrivalDropship),world.eye()+Vec3{-150,12,-170}});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::ArrivalBurrow),world.eye()+Vec3{0,-4,-180}});
    world.sounds.push_back({SoundEvent::Special,int(rpg::CombatCue::ArrivalWarp),world.eye()+Vec3{0,0,-320}});
    audio.update(world,true);
    check(audio.statistics().spatialStarted==beforeArrivals.spatialStarted+3,
          "Distinct reinforcement cues failed positional playback and range gating");
    world.missionStatus=MissionStatus::Ejecting;world.lifeSpent=true;world.ejectionTime=0;world.elapsed=.1f;audio.update(world,true);SDL_Delay(40);auto eject=audio.statistics();check(eject.frames>rain.frames,"Ejection audio clock stopped");
    audio.setVolumes(0,0);world.elapsed=.2f;audio.update(world,false,false,nullptr,int(rpg::ScoreCue::Victory));SDL_Delay(30);
    const auto muted=audio.statistics();
    check(muted.frames>eject.frames&&muted.scoreFrames==eject.scoreFrames,"Music volume did not mute the tracker score");
    std::cout<<"Authored PCM effects, native rain, spatial playback and ejection alarm passed; frames="<<eject.frames<<" peak="<<eject.peak<<"\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
