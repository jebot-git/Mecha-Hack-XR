#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
namespace avp::voice {
// Sample-clocked communications failure. No device, allocation or game-audio
// dependency; identical PCM produces identical faults at every display rate.
class RadioDamage {
    uint64_t frames=0,staticCount=0;
    uint32_t random=0x72616469;
    float low=0,envelope=0,previousNoise=0;
    bool enabled=false;
public:
    void reset(){*this=RadioDamage{};}
    uint64_t processedFrames()const{return frames;}
    uint64_t staticFrames()const{return staticCount;}
    void process(std::span<float> pcm,unsigned channels,bool damaged){
        if(!damaged){if(enabled)reset();return;}
        if(channels<1||channels>2||pcm.size()%channels)return;
        enabled=true;
        for(size_t i=0;i<pcm.size();i+=channels){
            float input=0;
            for(unsigned c=0;c<channels;++c)input+=std::isfinite(pcm[i+c])?pcm[i+c]/channels:0;
            envelope=std::max(std::abs(input),envelope*.9994f);
            random^=random<<13;random^=random>>17;random^=random<<5;
            float noise=float(random&65535)/32767.5f-1;
            float crackle=noise-previousNoise*.75f;previousNoise=noise;
            uint64_t window=frames/12000; // 250ms faults at48kHz
            bool staticOnly=((window*7+3)%11)<4;
            float value=0;
            if(envelope>.0001f){
                low+=.11f*(input-low);
                float clipped=std::clamp(low*9.f,-1.f,1.f);
                float crushed=std::round(clipped*7.f)/7.f;
                value=staticOnly?.075f*crackle:.15f*crushed+.022f*crackle;
                if(!staticOnly&&(frames/1200)%9==0)value*=.08f;
                if(staticOnly)++staticCount;
            }
            for(unsigned c=0;c<channels;++c)pcm[i+c]=std::clamp(value,-.25f,.25f);
            ++frames;
        }
    }
};
}
