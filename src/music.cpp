#include "music.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace iron {
static std::vector<unsigned char> bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Missing music data: "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
static std::string number(unsigned n){return (n<10?"0":"")+std::to_string(n);}
Music::Music(const std::filesystem::path& root){
    std::ifstream f(root/"audio/music.json");nlohmann::json j;f>>j;
    if(j.at("schema")!=2)throw std::runtime_error("Run tools/decode_audio.py for Jerry instruments");
    sourceRate=j.at("system_clock").get<double>()/(64*(j.at("sclk").get<unsigned>()+1));
    tempoRate=j.at("system_clock").get<double>()/((j.at("timer_prescaler").get<unsigned>()+1)*(j.at("timer_divider").get<unsigned>()+1));
    tracks=j.at("mission_tracks").get<decltype(tracks)>();for(auto t:tracks)if(t>=6)throw std::runtime_error("Invalid music mapping");
    auto rom=bytes(root.parent_path()/"system/jerry-wavetable.bin");
    if(rom.size()!=4096)throw std::runtime_error("Invalid Jerry wave ROM");
    auto wave=[&](const nlohmann::json& j){std::array<int32_t,128> result{};
        if(j.contains("values")){auto v=j.at("values").get<std::array<uint32_t,128>>();for(unsigned i=0;i<128;++i)result[i]=int16_t(v[i]);}
        else{unsigned base=j.at("rom").get<unsigned>()-0xf1d000;if(base>3584||base%512)throw std::runtime_error("Invalid Jerry wave address");for(unsigned i=0;i<128;++i)result[i]=int16_t(unsigned(rom[base+i*4+2])*256+rom[base+i*4+3]);}
        return result;};
    for(unsigned n=0;n<songs.size();++n){auto& song=songs[n];auto& module=j.at("modules").at(n);
        for(auto& i:module.at("instruments")){Instrument ins;ins.kind=i.at("kind");ins.words=i.at("words").get<decltype(ins.words)>();
            if(ins.kind==4){ins.carrier=wave(i.at("carrier"));ins.modulator=wave(i.at("modulator"));auto env=i.at("envelope").get<std::array<uint32_t,10>>();for(unsigned k=0;k<10;++k)ins.envelope[k]=int16_t(env[k]);}
            else ins.sample=bytes(root/"audio"/i.value("sample_file",number(i.at("sample_id").is_null()?0:i.at("sample_id").get<unsigned>())+".bin"));
            song.instruments.push_back(std::move(ins));}
        auto raw=bytes(root/"audio"/("score-"+number(n+14)+".bin"));if(raw.size()%8)throw std::runtime_error("Invalid score length");
        for(size_t p=0;p<raw.size();p+=4)song.score.push_back(uint32_t(raw[p])<<24|uint32_t(raw[p+1])<<16|uint32_t(raw[p+2])<<8|raw[p+3]);
    }
}
void Music::select(int song){selected=song;cursor=0;tick=0;sourcePhase=0;loopCount=0;voices={};previous={};next={};}
void Music::start(int mission){select(mission>=0&&mission<16?int(tracks[mission]):-1);}
void Music::events(){
    if(selected<0)return;
    auto& song=songs[selected];
    for(unsigned budget=0;budget<128&&cursor+1<song.score.size();++budget){
        uint32_t stamp=song.score[cursor];if(stamp==0x7fffffffu||stamp>tick)return;
        uint32_t e=song.score[cursor+1];cursor+=2;auto& v=voices[(e>>26)&7];auto& w=v.words;
        if(e&0x80000000u){unsigned instrument=(e>>21)&31;if(instrument>=song.instruments.size())continue;
            v={};v.active=true;v.instrument=&song.instruments[instrument];w=v.instrument->words;v.pitch=(e>>7)&16383;v.velocity=(e&127)*64;
            unsigned pitchWord=w[0]==4?4:3,gainWord=w[0]==4?6:1;
            w[pitchWord]=((w[pitchWord]&65535)*v.pitch)>>(w[0]==4?4:12);
            if(w[0]==4&&(w[8]&0x80000000u))w[8]=(w[8]&65535)*v.pitch;
            w[gainWord]=(w[gainWord]&0x80000000u)|(((w[gainWord]&65535)*v.velocity)>>8);
        }else if((e>>29)==0){if(w[0]==4){w[6]&=0x7fffffffu;w[14]=w[15];}else if(w[0]==32)v.active=false;else{w[1]&=0x7fffffffu;w[6]=w[8];}}
        else if((e>>29)==3){
            if(!(e&255))continue;
            if(!(e&128))--song.score[cursor-1];
            int offset=int((e>>8)&0x1fffff);if(offset&0x100000)offset-=0x200000;
            int64_t target=int64_t(cursor)+int64_t(offset)*2;
            if(target<0||target+1>=int64_t(song.score.size())||(target&1)){selected=-1;return;}
            cursor=size_t(target);tick=song.score[cursor];++loopCount;
        }else if((e>>29)==1){
            unsigned control=(e>>15)&31,value=e&32767;bool channel=!(e&(1u<<20));
            if(!channel){unsigned index=(e>>21)&31;if(index>=song.instruments.size())continue;auto& templateWords=song.instruments[index].words;
                if(control==10)templateWords[19]=value;
                else if(control==9)templateWords[templateWords[0]==4?4:3]=value;
                else if(control==7){unsigned k=templateWords[0]==4?6:1;templateWords[k]=(templateWords[k]&0x80000000u)|value;}continue;}
            if(control==10)w[19]=(value<<8)-1;
            else if(control==9)w[w[0]==4?4:3]=(v.pitch*value)>>12;
            else if(control==7){unsigned k=w[0]==4?6:1;w[k]=(w[k]&0x80000000u)|((v.velocity*value)>>8);}
        }
    }
}
std::array<float,2> Music::sample(){
    events();int64_t left=0,right=0;
    for(auto& v:voices){if(!v.active)continue;auto& w=v.words;const auto& ins=*v.instrument;int32_t value=0;
        if(w[0]==32||w[0]==12){w[6]+=w[3];if(w[6]>w[4]){if((w[1]&0x80000000u)&&w[5])w[6]-=w[5];else{v.active=false;continue;}}
            size_t at=w[6]>>8;unsigned frac=w[6]&255;size_t width=w[0]==12?2:1;
            auto decode=[&](size_t p)->int32_t{if((p+1)*width>ins.sample.size())return 0;unsigned b=ins.sample[p*width];return width==2?int16_t((b<<8)|ins.sample[p*2+1]):((b&128)?-1:1)*int((b&127)*(b&127)*2);};
            value=int32_t((decode(at)*int(255-frac)+decode(at+1)*int(frac))>>8);value=(value*int32_t(w[1]&65535))>>15;
        }else if(w[0]==4){
            w[3]=(w[3]+w[4]+w[5])&0x7fffff;w[7]=(w[7]+w[8])&0x7fffff;w[14]+=w[11];
            if(w[14]>w[12]){if((w[6]&0x80000000u)&&w[13])w[14]-=w[13];else{v.active=false;continue;}}
            auto interpolate=[](auto& wave,uint32_t phase,bool wrap){unsigned at=phase>>16,f=(phase>>8)&255;unsigned a=wrap?at%wave.size():std::min<unsigned>(at,wave.size()-1),b=wrap?(at+1)%wave.size():std::min<unsigned>(at+1,wave.size()-1);return int32_t((int64_t(wave[a])*(255-f)+int64_t(wave[b])*f)>>8);};
            int32_t env=interpolate(ins.envelope,w[14],false),carrier=interpolate(ins.carrier,w[3],true);
            int32_t modulation=ins.modulator[(w[7]>>16)&127];
            int32_t depth=int32_t(w[9])+int32_t((int64_t(int16_t(w[16]))*env)>>15);
            w[5]=uint32_t((int64_t(int16_t(depth))*modulation)>>7);
            value=int32_t((int64_t(carrier)*int16_t(w[6]))>>15);value=int32_t((int64_t(value)*env)>>15);
        }else{v.active=false;continue;}
        int32_t pan=std::clamp<int32_t>(w[19],0,32767);left+=int64_t(value)*(32767-pan);right+=int64_t(value)*pan;
    }
    tick+=tempoRate/sourceRate;
    return {float(std::clamp<int64_t>(left>>15,-32768,32767))/32768,float(std::clamp<int64_t>(right>>15,-32768,32767))/32768};
}
void Music::mix(std::span<float> out,unsigned rate){
    if(selected<0||rate==0)return;
    for(size_t frame=0;frame+1<out.size();frame+=2){
        while(sourcePhase>=1){previous=next;next=sample();sourcePhase-=1;}
        out[frame]+=previous[0]+(next[0]-previous[0])*float(sourcePhase);out[frame+1]+=previous[1]+(next[1]-previous[1])*float(sourcePhase);
        sourcePhase+=sourceRate/rate;
    }
}
}
