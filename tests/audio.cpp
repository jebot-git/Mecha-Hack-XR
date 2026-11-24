#include "audio.hpp"
#include <SDL.h>
#include <iostream>
#include <stdexcept>
using namespace iron;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    check(argc==2,"retail assets argument");
    auto effectPeak=[&](SoundEvent::Kind kind,float volume=1){Audio sample(argv[1]);check(sample.available(),"isolated sample device");sample.setVolumes(volume,1);World w;w.sounds.push_back({kind,0,w.eye()});sample.update(w,true);SDL_Delay(volume==0?150:2100);return sample.statistics().peak;};
    float punchPeak=effectPeak(SoundEvent::Punch),stepPeak=effectPeak(SoundEvent::Footstep);
    check(punchPeak>.05f&&std::abs(stepPeak/punchPeak-.5f)<.001f,"footsteps play the same source at exactly half gain without reducing fist audio");
    check(effectPeak(SoundEvent::Punch,0)==0,"sound volume zero mutes effects");
    check(std::abs(effectPeak(SoundEvent::Punch,.25f)/punchPeak-.25f)<.001f,"sound volume scales effects linearly");
    Audio audio(argv[1]);check(audio.available(),"audio device");check(audio.spatialAvailable(),"embedded full-sphere HRTFs available");World world;
    for(auto kind:{SoundEvent::Shot,SoundEvent::Punch,SoundEvent::Footstep,SoundEvent::Explosion,SoundEvent::LargeExplosion,SoundEvent::CutterMotor})check(Audio::spatialEvent(kind),"world sound routing");
    for(auto kind:{SoundEvent::Damage,SoundEvent::PickupSound,SoundEvent::Complete,SoundEvent::Failed})check(!Audio::spatialEvent(kind),"interface/status cue routed through spatialisation");
    audio.update(world,false,true);auto before=audio.statistics();
    // Deliberately do no rendering or Audio::update for longer than the old 21-ms queue.
    SDL_Delay(350);auto after=audio.statistics();
    check(after.frames-before.frames>10000,"audio clock continues through a stalled render loop");
    check(after.nonzeroFrames-before.nonzeroFrames>8000,"intro remains audible throughout render stall");
    check(after.peak<=.901f,"bounded mixed output");
    audio.setVolumes(1,0);before=audio.statistics();SDL_Delay(150);after=audio.statistics();check(after.nonzeroFrames==before.nonzeroFrames,"music volume zero independently mutes the intro");audio.setVolumes(1,1);
    audio.update(world,false);before=audio.statistics();
    for(int i=0;i<24;++i)world.sounds.push_back({SoundEvent::Explosion,0,world.eye()});
    audio.update(world,true);SDL_Delay(250);after=audio.statistics();
    check(after.nonzeroFrames>before.nonzeroFrames,"cartridge effects play on callback");
    check(after.peak<=.901f,"overlapping explosions cannot hard clip");
    audio.update(world,false);for(int i=0;i<24;++i)world.sounds.push_back({SoundEvent::Explosion,0,world.eye()+Vec3{float(i-12),-8,-10}});
    audio.update(world,true);SDL_Delay(250);check(audio.statistics().peak<=.901f,"overlapping HRTF explosions cannot hard clip");
    audio.update(world,false);before=audio.statistics();world.sounds.push_back({SoundEvent::Punch,0,world.eye()});audio.update(world,true);SDL_Delay(150);after=audio.statistics();
    check(after.nonzeroFrames>before.nonzeroFrames,"original fist swing sample plays");
    for(int mission=0;mission<20;++mission){audio.update(world,false);world.game=1;world.mission=mission;world.elapsed=0;before=audio.statistics();audio.update(world,true);/* Original tracks 3/4 start after 1000/3000 timer ticks (~1/3 sec). */ for(int wait=0;wait<80;++wait){SDL_Delay(50);after=audio.statistics();if(after.nonzeroFrames>before.nonzeroFrames)break;}check(after.nonzeroFrames>before.nonzeroFrames,"every IS2 mission uses audible IS1 BGM after its original opening rest");}
    world.mission=-1;audio.update(world,false);world.sounds.push_back({SoundEvent::Footstep,0,world.eye()});before=audio.statistics();audio.update(world,true);SDL_Delay(150);after=audio.statistics();check(after.nonzeroFrames>before.nonzeroFrames,"IS2 cartridge footstep effect audible");
    audio.setVolumes(1,0);world.mission=-1;
    for(int game=0;game<2;++game)for(int weapon: {1,2,3,4,5,6,8,9,10,11,12}){
        world.game=game;audio.update(world,false);before=audio.statistics();
        world.sounds.push_back({SoundEvent::Shot,weapon,world.eye()});audio.update(world,true);
        SDL_Delay(150);after=audio.statistics();
        check(after.nonzeroFrames>before.nonzeroFrames,"every firing weapon in both campaigns has audible launch audio with music muted");
    }
    audio.update(world,false);world.game=0;world.mission=-1;
    before=audio.statistics();world.sounds.push_back({SoundEvent::Damage,0,{100,0,0}});audio.update(world,true);
    check(audio.statistics().spatialStarted==before.spatialStarted,"damage interface feedback entered HRTF path");
    audio.update(world,false);world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rocket),world.eye()+Vec3{20,0,-20}});audio.update(world,true);before=audio.statistics();
    check(before.spatialStarted>0,"world effect did not enter HRTF path");
    // Audio must continue for the full source, including through a stalled renderer.
    SDL_Delay(350);after=audio.statistics();check(after.frames-before.frames>10000&&after.nonzeroFrames-before.nonzeroFrames>5000,"world audio stalled with rendering");
    AudioListener head;head.position=world.eye();head.right={0,0,-1};head.back={1,0,0};audio.update(world,true,false,&head);
    check(audio.statistics().spatialQueries>after.spatialQueries,"ongoing world source ignores listener head turn");
    audio.update(world,false);before=audio.statistics();std::array<float,960> radio;radio.fill(.03f);audio.queueVoice(radio);SDL_Delay(30);after=audio.statistics();
    check(after.spatialStarted==before.spatialStarted&&after.spatialQueries==before.spatialQueries,"voice chat entered spatialisation");
    std::cout<<"Callback audio survives rendering stalls; overlapping effects bounded; all weapon launch cues audible\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
