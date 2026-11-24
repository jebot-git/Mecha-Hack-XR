#include "spatial_audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool track=false;size_t allocations=0;
std::vector<float> render(SpatialAudio& audio,unsigned frames,unsigned block=512){
    std::vector<float> result(frames*2);
    for(unsigned i=0;i<frames;i+=block)audio.mixAdd(std::span(result).subspan(i*2,std::min(block,frames-i)*2));return result;
}
double energy(const std::vector<float>& audio,unsigned ear,unsigned skip=0){double e=0;for(size_t i=skip*2+ear;i<audio.size();i+=2){check(std::isfinite(audio[i]),"nonfinite audio output");e+=audio[i]*audio[i];}return e;}
double difference(const std::vector<float>& a,const std::vector<float>& b){double d=0,e=0;for(size_t i=0;i<a.size();++i){d+=(a[i]-b[i])*(a[i]-b[i]);e+=a[i]*a[i];}return std::sqrt(d/std::max(e,1e-20));}
unsigned risingCrossings(const std::vector<float>& audio,unsigned begin=0){
    unsigned crossings=0;
    for(size_t i=begin*2+2;i<audio.size();i+=2)
        if(audio[i-2]<=0&&audio[i]>0)++crossings;
    return crossings;
}
}
#ifdef IRON_TEST_ALLOCATIONS
extern "C" void* __real_malloc(size_t);extern "C" void* __real_calloc(size_t,size_t);extern "C" void* __real_realloc(void*,size_t);extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t n){if(track)++allocations;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(track)++allocations;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(track)++allocations;return __real_realloc(p,n);}
extern "C" void __wrap_free(void* p){__real_free(p);}
#endif
int main(int argc,char** argv){try{
    SpatialAudio audio;std::vector<float> impulse(4096);impulse[128]=1;
    auto at=[&](Vec3 source,const AudioListener& listener=AudioListener{}){audio.clear();audio.listener(listener);audio.play(impulse,source);return render(audio,8192);};
    auto front=at({0,0,-10}),back=at({0,0,10}),left=at({-10,0,0}),right=at({10,0,0});
    check(energy(right,1)>energy(right,0)*3&&energy(left,0)>energy(left,1)*3,"left/right HRTF axes reversed or not spatialised");
    check(difference(front,back)>.05,"front and back lack distinct HRTFs");
    auto above=at({0,10,-10}),below=at({0,-10,-10}),nadir=at({0,-10,0});
    check(difference(above,below)>.05&&difference(below,nadir)>.05,"vertical/nadir coverage missing");
    check(difference(right,at({0,0,-10},AudioListener::fromPose(avp::rotation(pi/2))))<.001,"full head yaw does not turn world sound correctly");
    check(difference(nadir,at({0,0,-10},AudioListener::fromPose(avp::rotation(0,pi/2))))<.001,"head pitch does not track sound elevation");
    AudioListener rolled;rolled.right={0,1,0};rolled.up={-1,0,0};
    check(difference(nadir,at({10,0,0},rolled))<.001,"head roll does not rotate the sound field");
    check(difference(back,at({0,0,-10},AudioListener::fromPose(avp::rotation(pi))))<.001,"audio incorrectly uses limited attack yaw");
    size_t peak=0;for(size_t i=0;i<front.size();++i)if(std::abs(front[i])>std::abs(front[peak]))peak=i;
    check(peak/2>=128&&peak/2<128+512,"unexpected filterbank/block delay");
    for(unsigned block:{13,64,128,1024}){audio.clear();audio.listener({});audio.play(impulse,{0,0,-10});check(difference(front,render(audio,8192,block))<.00001,"sample clock/tails depend on callback block size");}
    check(audio.statistics().active==0,"finished impulse left an active source");
    std::vector<float> tone(48000);for(size_t i=0;i<tone.size();++i)tone[i]=.05f*std::sin(i*.137f);
    audio.clear();audio.listener({});audio.play(tone,{},1,.90f);auto lowered=render(audio,4096);
    audio.clear();audio.play(tone,{},1,1.12f);auto raised=render(audio,4096);
    check(risingCrossings(raised,128)>1.18*risingCrossings(lowered,128),"one-shot pitch does not follow playback rate");
    audio.clear();audio.play(tone,{},1,1.12f);
    check(difference(raised,render(audio,4096,13))<.00001,"fractional playback rate depends on callback block size");
    audio.clear();
    SpatialAudio::Loop engine{1,&tone,{},1,.91f};
    audio.loops({&engine,1});
    auto idling=render(audio,8192);
    auto start=audio.statistics().started;
    engine.rate=1.12f;
    audio.loops({&engine,1});
    auto driving=render(audio,8192);
    check(audio.statistics().started==start&&risingCrossings(driving,4096)>1.16*risingCrossings(idling,4096),
          "engine rate change failed or restarted its loop");
    audio.clear();audio.listener({});audio.play(tone,{0,0,-10});render(audio,4096);
    audio.listener(AudioListener::fromPose(avp::rotation(pi/2)));auto turned=render(audio,4096);
    check(energy(turned,1,2048)>energy(turned,0,2048)*3,"ongoing sound ignores head turn");
    audio.listener({});auto near=render(audio,4096);auto farHead=AudioListener{};farHead.position={0,0,90};audio.listener(farHead);auto far=render(audio,4096);
    check(energy(far,0,2048)+energy(far,1,2048)<(energy(near,0,2048)+energy(near,1,2048))*.4,"ongoing sound ignores listener translation/distance");
    audio.clear();audio.listener({});audio.play(tone,{0,0,-10});auto full=render(audio,4096);
    audio.clear();audio.play(tone,{0,0,-10},.5f);auto half=render(audio,4096);
    for(size_t i=0;i<full.size();++i)check(std::abs(half[i]-full[i]*.5f)<.000001,"footstep half-gain is not linear");
    audio.clear();audio.play(tone,{0,0,-10});auto muted=render(audio,512);std::array<float,1024> silence{};audio.mixAdd(silence,0);for(float v:silence)check(v==0,"world volume zero is not silent");
    audio.clear();auto before=audio.statistics();for(unsigned i=0;i<64;++i)audio.play(tone,{float(i%3),0,-10});auto busy=audio.statistics();
    check(busy.active<=32&&busy.stolen-before.stolen==8&&busy.dropped>before.dropped,"bounded source replacement/retirement failed");
    render(audio,512);check(audio.statistics().active==24,"retiring sources did not fade out");audio.clear();auto stopped=render(audio,512);for(float v:stopped)check(v==0,"clear left stale source/tail audio");
    using Priority=SpatialAudio::Priority;
    for(unsigned i=0;i<SpatialAudio::sourceLimit;++i)audio.play(tone,{0,0,-10},1,1,Priority::Foreground);
    before=audio.statistics();audio.play(tone,{0,0,-8},1,1,Priority::Background);
    check(audio.statistics().dropped==before.dropped+1&&audio.statistics().started==before.started,
          "background sound displaced a full foreground voice pool");
    audio.clear();
    for(unsigned i=0;i<SpatialAudio::sourceLimit;++i)audio.play(tone,{0,0,-10},1,1,Priority::Background);
    before=audio.statistics();audio.play(tone,{0,0,-8},1,1,Priority::Foreground);
    check(audio.statistics().stolen==before.stolen+1&&audio.statistics().started==before.started+1,
          "foreground sound could not displace a background voice");
    audio.clear();
    AudioListener invalid;invalid.position.x=std::numeric_limits<float>::quiet_NaN();audio.listener(invalid);audio.play(tone,{0,0,-10});audio.play(tone,{std::numeric_limits<float>::infinity(),0,0});render(audio,512);
    std::array<float,1024> buffer{};track=true;
    for(unsigned frame=0;frame<100;++frame){buffer.fill(0);audio.mixAdd(buffer);}
    track=false;check(allocations==0,"audio callback allocated memory");
    if(argc==2&&std::string(argv[1])=="--benchmark"){
        audio.clear();audio.listener({});std::vector<float> sustained(480000);
        for(size_t i=0;i<sustained.size();++i)sustained[i]=.005f*std::sin(i*.137f);
        for(unsigned i=0;i<24;++i){
            float a=i*2*pi/24;
            audio.play(sustained,{std::sin(a)*20,float(int(i%3)-1)*10,-std::cos(a)*20},
                       1,.96f+.02f*float(i%5));
        }
        std::array<SpatialAudio::Loop,SpatialAudio::loopLimit> motors;
        for(size_t i=0;i<motors.size();++i)
            motors[i]={i+1,&tone,{float(i*3),0,-16},.22f,.91f};
        std::vector<double> dsp,control;double checksum=0;
        for(unsigned frame=0;frame<320;++frame){buffer.fill(0);auto start=std::chrono::steady_clock::now();audio.listener(AudioListener::fromPose(avp::rotation(.6f*std::sin(frame*.025f),.2f*std::sin(frame*.013f))));
            for(size_t i=0;i<motors.size();++i)motors[i].rate=1.f+.09f*std::sin(frame*.037f+i);
            audio.loops(motors);
            auto prepared=std::chrono::steady_clock::now();audio.mixAdd(buffer);auto end=std::chrono::steady_clock::now();
            if(frame>=20){control.push_back(std::chrono::duration<double,std::micro>(prepared-start).count());dsp.push_back(std::chrono::duration<double,std::micro>(end-prepared).count());}for(float v:buffer)checksum+=v;
        }
        std::sort(control.begin(),control.end());std::sort(dsp.begin(),dsp.end());
        std::cout<<"24 one-shots and 6 variable-rate loops, 512-frame production DSP: median_us="<<dsp[dsp.size()/2]<<" p99_us="<<dsp[size_t(dsp.size()*.99)]<<"; control_p99_us="<<control[size_t(control.size()*.99)]<<"; checksum="<<checksum<<'\n';
    }
    std::cout<<"Full-sphere HRTFs, physical head yaw/pitch, moving listener, callback sizes 13–1024, fractional pitch, engine rate, source priorities, tails, half gain, mute, bounded retirement, reset and allocation-free callback passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
