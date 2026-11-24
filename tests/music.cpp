#include "music.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace iron;
int main(int argc,char** argv){
    assert(argc==2);Music music(argv[1]);std::ifstream f(std::filesystem::path(argv[1])/"audio/music.json");nlohmann::json catalog;f>>catalog;
    std::array<bool,6> tested{};
    for(int mission=0;mission<16;++mission){unsigned track=catalog["mission_tracks"][mission];if(tested[track])continue;tested[track]=true;music.start(mission);
        double energy=0;float peak=0;std::array<float,512> pcm;
        for(int block=0;block<7500;++block){pcm.fill(0);music.mix(pcm,8000);for(float s:pcm){assert(std::isfinite(s));energy+=s*s;peak=std::max(peak,std::abs(s));}}
        std::cerr<<track<<" energy "<<energy<<" peak "<<peak<<" loops "<<music.loops()<<"\n";assert(energy>1&&peak>.001f&&peak<4&&music.loops()>0);
        std::cout<<"Track "<<track<<": RMS "<<std::sqrt(energy/(7500*512))<<", peak "<<peak<<", loops "<<music.loops()<<"\n";
    }
    for(bool t:tested)assert(t);
    music.intro();double introEnergy=0;std::array<float,512> intro{};
    for(int block=0;block<7500;++block){intro.fill(0);music.mix(intro,8000);for(float v:intro){assert(std::isfinite(v)&&std::abs(v)<=1);introEnergy+=v*v;}}
    assert(introEnergy>1);std::cerr<<"Intro energy "<<introEnergy<<" loops "<<music.loops()<<"\n";
    Music split(argv[1]),whole(argv[1]);split.intro();whole.intro();std::vector<float> a(48000),b(48000);whole.mix(a);
    for(size_t p=0;p<b.size();p+=200)split.mix(std::span<float>(b).subspan(p,std::min(size_t(200),b.size()-p)));
    assert(a==b); // Audio queue block sizes cannot change pitch, events or synthesis.

    music.start(-1);std::array<float,512> silence{};music.mix(silence);for(float s:silence)assert(s==0);
}
