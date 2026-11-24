#include "spatial_audio.hpp"
#include "arms.hpp"
#include <climits> // JPL's pinned panner/math headers omit this on some toolchains.
#include <JPLSpatial/DistanceAttenuation.h>
#include <JPLSpatial/Math/SIMD.h>
#include <mysofa.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace iron {
extern const unsigned char embeddedHrtf[];
extern const std::size_t embeddedHrtfSize;
namespace {
constexpr unsigned tapsMax=SpatialAudio::filterTailFrames,chunkMax=512,fadeFrames=256,retireFrames=128,loopRetireFrames=2048,reserveSources=8,loopStart=SpatialAudio::sourceLimit+reserveSources;
// Roughly 35 ms to take up a motor speed change at 48 kHz.
constexpr float rateSlew=.0006f;
float boundedRate(float rate){return std::clamp(rate,.85f,1.16f);}
constexpr unsigned datasetDirections=836,datasetTaps=256;
constexpr unsigned rawBytes=20+datasetDirections*(3+2*datasetTaps)*4;
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
template<class T> T* allocate(size_t count){auto p=static_cast<T*>(std::calloc(count,sizeof(T)));if(!p)throw std::bad_alloc();return p;}
char* duplicate(const char* value){auto p=allocate<char>(std::strlen(value)+1);std::memcpy(p,value,std::strlen(value)+1);return p;}
struct Filter {
    // Reversed, right-aligned coefficients permit bounded SIMD convolution of
    // any support length without changing the history origin during a fade.
    std::array<float,tapsMax> left{},right{};
    unsigned taps=4;
};
struct Bank {
    MYSOFA_EASY* easy=nullptr;
    Bank(){
        try{
            std::vector<unsigned char> data(rawBytes);uLongf size=data.size();
            if(uncompress(data.data(),&size,embeddedHrtf,embeddedHrtfSize)!=Z_OK||size!=rawBytes)throw std::runtime_error("Invalid embedded HRTF data");
            size_t cursor=4;auto number=[&](){if(cursor+4>data.size())throw std::runtime_error("Truncated HRTF data");uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(data[cursor++])<<(i*8);return v;};
            if(std::memcmp(data.data(),"ISHR",4)||number()!=1||number()!=datasetDirections||number()!=datasetTaps||number()!=48000)throw std::runtime_error("Unsupported HRTF data");
            easy=allocate<MYSOFA_EASY>(1);easy->hrtf=allocate<MYSOFA_HRTF>(1);auto& h=*easy->hrtf;
            h.I=h.E=1;h.C=3;h.R=2;h.N=datasetTaps;h.M=datasetDirections;
            auto array=[&](MYSOFA_ARRAY& a,unsigned n,bool read){a.elements=n;a.values=allocate<float>(n);if(read)for(unsigned i=0;i<n;++i){a.values[i]=std::bit_cast<float>(number());if(!std::isfinite(a.values[i]))throw std::runtime_error("Nonfinite HRTF data");}};
            array(h.SourcePosition,h.M*h.C,true);array(h.DataIR,h.M*h.R*h.N,true);array(h.DataDelay,2,false);
            h.SourcePosition.attributes=allocate<MYSOFA_ATTRIBUTE>(1);h.SourcePosition.attributes->name=duplicate("Type");h.SourcePosition.attributes->value=duplicate("cartesian");
            // One global frontal-energy calibration, retaining directional and
            // spectral differences rather than normalising each ear/direction.
            unsigned front=0;for(unsigned i=1;i<h.M;++i)if(h.SourcePosition.values[i*3]>h.SourcePosition.values[front*3])front=i;
            double energy=0;for(unsigned i=0;i<h.R*h.N;++i){double v=h.DataIR.values[front*h.R*h.N+i];energy+=v*v;}
            if(!(energy>0))throw std::runtime_error("Empty HRTF reference");
            float scale=float(std::sqrt(2/energy));for(unsigned i=0;i<h.DataIR.elements;++i)h.DataIR.values[i]*=scale;
            easy->lookup=mysofa_lookup_init(&h);if(!easy->lookup)throw std::runtime_error("HRTF lookup initialisation failed");
            easy->neighborhood=mysofa_neighborhood_init(&h,easy->lookup);if(!easy->neighborhood)throw std::runtime_error("HRTF neighbourhood initialisation failed");
            easy->fir=allocate<float>(h.N*h.R);
        }catch(...){if(easy)mysofa_close(easy);throw;}
    }
    ~Bank(){if(easy)mysofa_close(easy);}
    Filter get(Vec3 direction,bool near){
        Filter result;
        if(near){result.left.back()=result.right.back()=1;return result;}
        std::array<float,datasetTaps> left{},right{};float dl=0,dr=0;
        // Client X-right/Y-up/-Z-forward -> SOFA X-forward/Y-left/Z-up.
        mysofa_getfilter_float(easy,-direction.z,-direction.x,direction.y,left.data(),right.data(),&dl,&dr);
        auto ear=[&](const auto& input,auto& output,float delay){
            float frames=std::clamp(std::isfinite(delay)?delay*48000:0.f,0.f,float(tapsMax-datasetTaps-1));
            unsigned whole=unsigned(frames);float fraction=frames-whole;
            result.taps=std::max(result.taps,(datasetTaps+whole+(fraction>0?1:0)+3)&~3u);
            for(unsigned i=0;i<datasetTaps;++i){output[tapsMax-1-i-whole]+=input[i]*(1-fraction);if(fraction>0)output[tapsMax-2-i-whole]+=input[i]*fraction;}
        };
        ear(left,result.left,dl);ear(right,result.right,dr);return result;
    }
};
std::array<float,2> convolve(const float* history,const Filter& filter,unsigned count){
    unsigned offset=tapsMax-count;JPL::simd left(0.f),right(0.f);
    for(unsigned k=offset;k<tapsMax;k+=4){JPL::simd value(history+k);left+=value*JPL::simd(filter.left.data()+k);right+=value*JPL::simd(filter.right.data()+k);}
    return {left.reduce(),right.reduce()};
}
}
struct SpatialAudio::Impl {
    Bank bank;
    AudioListener head;
    JPL::AttenuationBaseModel attenuation;
    struct Source {
        const std::vector<float>* samples=nullptr;double cursor=0;
        Vec3 position{},lastDirection{};float lastDistance=-1,scale=1,lastScale=0,gain=0,targetGain=0;
        float rate=1,targetRate=1;
        Priority priority=Priority::Normal;
        Filter current,target;
        std::array<float,tapsMax-1+chunkMax> history{};
        unsigned fade=0,tail=0,age=0,attack=64,retire=0;
        uint64_t order=0;size_t loopId=0;bool near=false;
        std::array<float,2> filterFrame(unsigned i,unsigned count){
            auto ears=convolve(history.data()+i,current,count);
            float amplitude=gain;
            if(fade){
                const auto next=convolve(history.data()+i,target,count);
                const float mix=1.f-float(fade)/fadeFrames;
                for(unsigned c=0;c<2;++c)ears[c]+=mix*(next[c]-ears[c]);
                amplitude+=mix*(targetGain-amplitude);
                if(--fade==0){current=target;gain=targetGain;}
            }
            return {ears[0]*amplitude,ears[1]*amplitude};
        }
    };
    Source receiver;
    std::array<Source,loopStart+2*loopLimit> sources;
    Stats stats;
    uint64_t sequence=0;
    Impl(){attenuation.Model=JPL::AttenuationModel::Inverse;attenuation.Parameters={1,1000000,.012f/1.012f};}
    void prepare(Source& source,bool initial,bool radio=false){
        Vec3 delta=source.position-head.position;float distance=length(delta);if(!std::isfinite(distance))return;
        Vec3 direction{dot(delta,head.right),dot(delta,head.up),dot(delta,head.back)};bool near=distance<.25f;
        direction=distance>.00001f?direction*(1/distance):Vec3{0,0,-1};
        // Match the existing 0.45/(1+0.012*d) curve, using JPL's inverse
        // attenuation model outside its 1-metre reference radius.
        float gain=radio?1.f:source.scale*(distance<1?.45f/(1+distance*.012f):(.45f/1.012f)*attenuation.Evaluate(distance));
        const bool directionChanged=initial||near!=source.near||length(direction-source.lastDirection)>.005f;
        if(!directionChanged&&std::abs(distance-source.lastDistance)<.05f&&std::abs(source.scale-source.lastScale)<.005f)return;
        if(!initial&&source.fade){float alpha=1-float(source.fade)/fadeFrames;
            for(unsigned i=0;i<tapsMax;++i){source.current.left[i]+=(source.target.left[i]-source.current.left[i])*alpha;source.current.right[i]+=(source.target.right[i]-source.current.right[i])*alpha;}
            source.current.taps=std::max(source.current.taps,source.target.taps);source.gain+=(source.targetGain-source.gain)*alpha;
        }
        if(directionChanged){source.target=bank.get(direction,near);++stats.queries;}
        source.targetGain=gain;source.lastScale=source.scale;
        if(initial){source.current=source.target;source.gain=gain;source.fade=0;}else source.fade=fadeFrames;
        if(directionChanged)source.lastDirection=direction;
        source.lastDistance=distance;source.near=near;
    }
};
SpatialAudio::SpatialAudio():impl(std::make_unique<Impl>()){}
SpatialAudio::~SpatialAudio()=default;
void SpatialAudio::listener(const AudioListener& value){
    if(!finite(value.position)||!finite(value.right)||!finite(value.up)||!finite(value.back)||length(value.right)<.5f||length(value.up)<.5f||length(value.back)<.5f)return;
    auto right=unit(value.right),up=unit(value.up-right*dot(value.up,right));auto back=unit(cross(right,up));
    if(dot(back,value.back)<.5f)return;
    impl->head={value.position,right,up,back};
    for(unsigned i=0;i<sourceLimit;++i)if(impl->sources[i].samples)impl->prepare(impl->sources[i],false);
}
void SpatialAudio::play(const std::vector<float>& mono,Vec3 position,float gain,float rate,Priority priority){
    if(mono.empty()||!finite(position)||!std::isfinite(gain)||!std::isfinite(rate)||gain<=0)return;
    auto& state=*impl;unsigned index=sourceLimit;
    for(unsigned i=0;i<sourceLimit;++i)if(!state.sources[i].samples){index=i;break;}
    bool replacing=index==sourceLimit;
    if(replacing){
        index=0;for(unsigned i=1;i<sourceLimit;++i){
            const auto &candidate=state.sources[i], &current=state.sources[index];
            if(candidate.priority<current.priority||
               (candidate.priority==current.priority&&candidate.order<current.order))index=i;
        }
        if(state.sources[index].priority>priority){++state.stats.dropped;return;}
        unsigned retirement=loopStart;for(unsigned i=sourceLimit;i<loopStart;++i)if(!state.sources[i].samples){retirement=i;break;}
        if(retirement==loopStart){++state.stats.dropped;return;}
        state.sources[retirement]=state.sources[index];state.sources[retirement].retire=retireFrames;++state.stats.stolen;
    }
    auto& source=state.sources[index];source=Impl::Source{};source.samples=&mono;source.position=position;source.scale=gain;source.order=++state.sequence;source.attack=replacing?retireFrames:64;
    source.rate=source.targetRate=boundedRate(rate);source.priority=priority;
    state.prepare(source,true);++state.stats.started;
}
void SpatialAudio::loops(std::span<const Loop> requested){
    auto &state=*impl;
    requested=requested.first(std::min<size_t>(loopLimit,requested.size()));
    for(size_t i=loopStart;i<state.sources.size();++i){
        auto &source=state.sources[i];
        if(!source.samples||source.retire)continue;
        const auto found=std::find_if(requested.begin(),requested.end(),[&](const Loop &loop){
            return loop.id==source.loopId&&loop.samples==source.samples;});
        if(found==requested.end())source.retire=loopRetireFrames;
    }
    for(const auto &loop:requested){
        if(!loop.id||!loop.samples||loop.samples->empty()||!finite(loop.position)||
           !std::isfinite(loop.gain)||!std::isfinite(loop.rate))continue;
        auto begin=state.sources.begin()+loopStart,end=state.sources.end();
        auto source=std::find_if(begin,end,[&](const Impl::Source &s){
            return s.loopId==loop.id&&s.samples==loop.samples&&!s.retire;});
        bool initial=source==end;
        if(initial){
            source=std::find_if(begin,end,[](const Impl::Source &s){return !s.samples;});
            if(source==end)continue;
            *source=Impl::Source{};
            source->loopId=loop.id;source->samples=loop.samples;
            source->attack=2048;
            source->cursor=(loop.id*15511)%loop.samples->size();
            source->rate=boundedRate(loop.rate);
            ++state.stats.started;
        }
        source->position=loop.position;source->scale=std::max(0.f,loop.gain);
        source->targetRate=boundedRate(loop.rate);
        state.prepare(*source,initial);
    }
}
void SpatialAudio::radioEmitter(Vec3 position,bool newTransmission){
    if(!finite(position))return;
    auto& receiver=impl->receiver;
    if(newTransmission)receiver=Impl::Source{};
    receiver.position=position;
    impl->prepare(receiver,newTransmission,true);
}
void SpatialAudio::mixRadioAdd(std::span<const float> mono,std::span<float> stereo){
    auto& receiver=impl->receiver;
    size_t size=std::min(mono.size(),stereo.size()/2);
    for(size_t offset=0;offset<size;offset+=chunkMax){
        const unsigned frames=unsigned(std::min<size_t>(size-offset,chunkMax));
        std::copy_n(mono.data()+offset,frames,receiver.history.data()+tapsMax-1);
        const unsigned count=std::max(receiver.current.taps,receiver.target.taps);
        for(unsigned i=0;i<frames;++i){
            const auto ears=receiver.filterFrame(i,count);
            // Headphone crossfeed keeps a nearby side transmitter intelligible
            // in both ears without losing its world bearing.
            stereo[2*(offset+i)]+=.84f*ears[0]+.16f*ears[1];
            stereo[2*(offset+i)+1]+=.84f*ears[1]+.16f*ears[0];
        }
        std::memmove(receiver.history.data(),receiver.history.data()+frames,(tapsMax-1)*sizeof(float));
    }
}
void SpatialAudio::clear(){
    for(auto& s:impl->sources)s.samples=nullptr;
    if(impl->receiver.gain!=0)impl->receiver=Impl::Source{};
}
void SpatialAudio::mixAdd(std::span<float> stereo,float volume){
    if(!std::isfinite(volume))volume=0;
    volume=std::clamp(volume,0.f,1.f);auto& state=*impl;
    for(size_t offset=0;offset+1<stereo.size();offset+=chunkMax*2){
        unsigned frames=unsigned(std::min<size_t>(chunkMax,(stereo.size()-offset)/2));
        for(auto& source:state.sources){if(!source.samples)continue;
            unsigned count=std::max(source.current.taps,source.target.taps);
            const auto &samples=*source.samples;
            const size_t length=samples.size();
            for(unsigned i=0;i<frames;++i){
                float value=0;
                if(source.cursor<length){
                    const size_t index=size_t(source.cursor);
                    value=samples[index];
                    if(source.rate!=1.f||source.cursor!=index){
                        const float next=index+1<length?samples[index+1]:source.loopId?samples.front():0.f;
                        value=std::lerp(value,next,float(source.cursor-index));
                    }
                    source.rate+=(source.targetRate-source.rate)*rateSlew;
                    source.cursor+=source.rate;
                    if(source.loopId&&source.cursor>=length){
                        source.cursor-=length;
                        if(source.cursor>=length)source.cursor-=length; // A one-sample loop can cross twice.
                    }
                }else ++source.tail;
                source.history[tapsMax-1+i]=std::isfinite(value)?value:0;
            }
            for(unsigned i=0;i<frames;++i){
                const auto value=source.filterFrame(i,count);
                float envelope=std::min(1.f,float(source.age)/source.attack);if(source.age<source.attack)++source.age;
                if(source.retire){envelope*=float(source.retire)/(source.loopId?loopRetireFrames:retireFrames);if(--source.retire==0)source.samples=nullptr;}
                stereo[offset+2*i]+=value[0]*envelope*volume;stereo[offset+2*i+1]+=value[1]*envelope*volume;
                if(!source.samples)break;
            }
            std::memmove(source.history.data(),source.history.data()+frames,(tapsMax-1)*sizeof(float));
            if(source.tail>=count&&source.fade==0)source.samples=nullptr;
        }
        state.stats.frames+=frames;
    }
}
SpatialAudio::Stats SpatialAudio::statistics()const{auto result=impl->stats;result.active=0;for(const auto& s:impl->sources)if(s.samples)++result.active;return result;}
}
