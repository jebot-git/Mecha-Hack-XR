#include "rpg/mission_intro.hpp"
#include "rpg/mission.hpp"
#include "rpg/bosses.hpp"
#include <algorithm>
#include <zlib.h>
#include <bit>
#include <cmath>
#include <iostream>

namespace iron::rpg {
std::array<std::string,3> briefingHeaderShots(const MissionRecipe &recipe,int sector) {
    const auto objectives=missionObjectives(),locations=missionLocations();
    const auto valid=[](int index,auto catalogue){return index>=0&&size_t(index)<catalogue.size();};
    std::string objective=valid(recipe.objective,objectives)?objectives[recipe.objective].id:"eradicate";
    if(recipe.finale){
        if(!recipe.finaleBattle&&valid(recipe.boss,campaignBosses()))
            objective=std::string("boss-")+campaignBoss(recipe.boss).spec.id;
        else
            objective="finale-battle";
    }
    std::string location=valid(recipe.location,locations)?locations[recipe.location].id:"battlefield";
    static constexpr const char *pilots[]={"maverick","commander","engineer","quipster"};
    const unsigned pilot=(recipe.seed+unsigned(sector))%4;
    return {objective,"site-"+location,std::string("pilot-")+pilots[pilot]};
}

void MissionBriefingHeader::stop() {
    shots.clear();
    totalFrames=0;
    lastFrame=-1;
}

void MissionBriefingHeader::open(const std::filesystem::path &directory,const MissionRecipe &recipe,int sector) {
    stop();
    for(const auto &name:briefingHeaderShots(recipe,sector)){
        const auto path=directory/(name+".mhr");
        Shot shot{std::ifstream(path,std::ios::binary|std::ios::ate),{}};
        if(!shot.file)continue;
        const auto end=shot.file.tellg();
        shot.file.seekg(0);
        auto read32=[&]{
            std::array<unsigned char,4> b{};
            shot.file.read(reinterpret_cast<char*>(b.data()),4);
            return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
        };
        std::array<char,4> magic{};
        shot.file.read(magic.data(),4);
        const auto count=read32();
        bool valid=shot.file.good()&&magic==std::array<char,4>{'M','H','B','1'}&&count>0&&count<=maxFrames;
        uint32_t offset=8+4*count;
        for(uint32_t i=0;valid&&i<count;++i){
            shot.offsets.push_back(offset);
            const auto bytes=read32();
            valid=shot.file.good()&&bytes>0&&bytes<=compressBound(width*height*2);
            if(valid)offset+=bytes;
        }
        if(!valid||std::streamoff(offset)!=end){
            std::cerr<<"Invalid briefing header "<<path<<"\n";
            continue;
        }
        shot.offsets.push_back(offset);
        totalFrames+=shot.frames();
        shots.push_back(std::move(shot));
    }
    if(totalFrames)decoded.resize(width*height);
}

bool MissionBriefingHeader::update(double seconds) {
    if(!std::isfinite(seconds)||seconds<0)return false;
    while(totalFrames){
        const auto frame=std::min(totalFrames-1,int(std::fmod(seconds,double(totalFrames)/fps)*fps));
        if(frame==lastFrame)return false;
        int local=frame;
        auto shot=shots.begin();
        while(local>=shot->frames()){local-=shot->frames();++shot;}
        const auto begin=shot->offsets[local];
        compressed.resize(shot->offsets[local+1]-begin);
        shot->file.seekg(begin);
        shot->file.read(reinterpret_cast<char*>(compressed.data()),std::streamsize(compressed.size()));
        uLongf bytes=width*height*2;
        if(!shot->file||uncompress(reinterpret_cast<Bytef*>(decoded.data()),&bytes,compressed.data(),compressed.size())!=Z_OK||bytes!=width*height*2){
            std::cerr<<"Could not decode briefing header frame "<<local<<"\n";
            totalFrames-=shot->frames();
            shots.erase(shot);
            lastFrame=-1;
            continue;
        }
        if constexpr(std::endian::native==std::endian::big){
            for(auto &p:decoded)p=uint16_t((p<<8)|(p>>8));
        }
        lastFrame=frame;
        return true;
    }
    return false;
}
}
