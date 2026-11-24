#include "weather_audio.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
using namespace iron;
static std::vector<float> render(WeatherAudio& audio,unsigned frames,unsigned block=512,float gain=1){std::vector<float> out(frames*2);for(unsigned i=0;i<frames;i+=block)audio.mix(std::span(out).subspan(i*2,std::min(block,frames-i)*2),gain);return out;}
static double rms(const std::vector<float>& v,size_t begin=0,size_t end=0){if(!end)end=v.size();double sum=0;for(size_t i=begin;i<end;++i){assert(std::isfinite(v[i]));sum+=v[i]*v[i];}return std::sqrt(sum/(end-begin));}
static void wav(const std::filesystem::path& file,const std::vector<float>& pcm){std::ofstream out(file,std::ios::binary);auto le=[&](unsigned n,int bytes){for(int i=0;i<bytes;++i)out.put(char(n>>(i*8)));};out<<"RIFF";le(36+unsigned(pcm.size()*2),4);out<<"WAVEfmt ";le(16,4);le(1,2);le(2,2);le(48000,4);le(192000,4);le(4,2);le(16,2);out<<"data";le(unsigned(pcm.size()*2),4);for(float v:pcm)le(unsigned(uint16_t(int16_t(std::clamp(v,-1.f,1.f)*32767))),2);}
int main(int argc,char** argv){
 EnvironmentSettings settings;settings.weather=Weather::Rain;settings.seed=42;
 WeatherAudio a,b;a.update(settings,0,true);b.update(settings,0,true);
 auto whole=render(a,96000),blocks=render(b,96000,13);assert(whole==blocks);assert(rms(whole)>.005&&rms(whole)<.1);
 double dc=0,stereo=0;for(size_t i=0;i<whole.size();++i){assert(std::abs(whole[i])<.3f);dc+=whole[i];if(i%2==0)stereo+=std::abs(whole[i]-whole[i+1]);}assert(std::abs(dc/whole.size())<.001&&stereo>10);
 WeatherAudio half;half.update(settings,0,true);auto quiet=render(half,96000,512,.5f);for(size_t i=0;i<whole.size();++i)assert(std::abs(quiet[i]-whole[i]*.5f)<1e-7);
 std::array<float,256> muted{};a.mix(muted,0);for(float v:muted)assert(v==0);
 a.update(settings,1,false);a.mix(muted,1);for(float v:muted)assert(v==0);
 for(auto preset:{Weather::Clear,Weather::Fog}){settings.weather=preset;a.update(settings,0,true);auto silence=render(a,512);assert(rms(silence)==0);}
 settings.weather=Weather::Sandstorm;a.update(settings,0,true);auto wind=render(a,96000);assert(rms(wind)>.005&&wind!=whole);
 settings.weather=Weather::Storm;float base=34-weatherNoise(settings.seed,0)*10+.05f;
 WeatherAudio storm,control;storm.update(settings,base,true);control.update(settings,base,true);assert(render(storm,12000)==render(control,12000));
 storm.update(settings,base+1.3f,true);assert(storm.statistics().thunderStarted==1);storm.update(settings,base+1.4f,true);assert(storm.statistics().thunderStarted==1);
 auto thunder=render(storm,480000),bed=render(control,480000);for(size_t i=0;i<thunder.size();++i)thunder[i]-=bed[i];
 assert(rms(thunder,0,2400)<rms(thunder,24000,72000)*.5); // rounded onset, not an impulse/crack
 assert(rms(thunder,24000,72000)>.003&&rms(thunder,9*48000*2,thunder.size())<1e-7); // finite rolling tail
 storm.update(settings,base,true);assert(storm.statistics().thunderStarted==1);WeatherAudio reset;reset.update(settings,base,true);assert(render(storm,4096)==render(reset,4096));
 if(argc==2){std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  for(auto preset:{Weather::Rain,Weather::Storm,Weather::Sandstorm}){WeatherAudio audio;settings.weather=preset;std::vector<float> pcm(20*48000*2);
   for(unsigned i=0;i<20*48000;i+=256){audio.update(settings,float(i)/48000,true);audio.mix(std::span(pcm).subspan(i*2,std::min(256u,20*48000-i)*2),1);}
   auto name=preset==Weather::Rain?"rain":preset==Weather::Storm?"storm":"sandstorm";wav(out/(std::string(name)+".wav"),pcm);std::cout<<name<<" RMS="<<rms(pcm)<<'\n';
  }
 }
 std::cout<<"Authored weather: stereo, bounded levels, sample-clock/block invariance, mute/focus, independent timbres and soft delayed thunder passed\n";
}
