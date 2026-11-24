#pragma once

#include "rpg/score.hpp"
#include <opus.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace iron::rpg {
// Load compressed song data up front. The mixer decodes one 20 ms packet at
// a time and never has to open a file or allocate a PCM buffer in the callback.
class ScorePlayer {
    struct Track {
        std::vector<unsigned char> bytes;
        uint32_t frames=0, skip=0;
    };
    std::array<Track,scoreFiles.size()> tracks;
    std::unique_ptr<OpusDecoder,decltype(&opus_decoder_destroy)> decoder{nullptr,opus_decoder_destroy};
    std::array<float,960*2> packet{};
    int current=-1, requested=-1;
    size_t offset=12;
    unsigned position=0, count=0;
    uint32_t played=0, skip=0;
    float gain=0;

    static uint32_t get(std::span<const unsigned char> bytes,size_t index,unsigned size){
        uint32_t value=0;
        for(unsigned n=0;n<size;++n)value|=uint32_t(bytes[index+n])<<(8*n);
        return value;
    }
    void resetPacketState(){
        opus_decoder_ctl(decoder.get(),OPUS_RESET_STATE);
        offset=12;
        position=count=0;
        played=0;
        skip=current>=0?tracks[size_t(current)].skip:0;
    }
    bool decodeNext(const Track &track){
        if(offset+2>track.bytes.size())return false;
        const unsigned bytes=get(track.bytes,offset,2);
        offset+=2;
        if(!bytes||bytes>1275||offset+bytes>track.bytes.size())return false;
        const int decoded=opus_decode_float(decoder.get(),track.bytes.data()+offset,int(bytes),packet.data(),960,0);
        offset+=bytes;
        if(decoded!=960)return false;
        count=unsigned(decoded);
        position=std::min(skip,count);
        skip-=position;
        return true;
    }

public:
    explicit ScorePlayer(const std::filesystem::path &directory){
        size_t total=0;
        for(size_t i=0;i<tracks.size();++i){
            auto &track=tracks[i];
            const auto path=directory/(std::string(scoreFiles[i])+".mop");
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            const auto length=file?file.tellg():std::streampos(0);
            if(length<12||length>2*1024*1024)throw std::runtime_error("Invalid score file: "+path.string());
            total+=size_t(length);
            if(total>12*1024*1024)throw std::runtime_error("Campaign score exceeds compressed budget");
            track.bytes.resize(size_t(length));
            file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(track.bytes.data()),std::streamsize(length))||
               std::string_view(reinterpret_cast<const char*>(track.bytes.data()),4)!="MOP1")
                throw std::runtime_error("Invalid score header: "+path.string());
            track.frames=get(track.bytes,4,4);
            track.skip=get(track.bytes,8,4);
            unsigned packets=0;
            for(size_t at=12;at<track.bytes.size();++packets){
                if(at+2>track.bytes.size())throw std::runtime_error("Truncated score packet");
                const unsigned bytes=get(track.bytes,at,2);
                at+=2;
                if(!bytes||bytes>1275||at+bytes>track.bytes.size()||
                   opus_packet_get_nb_samples(track.bytes.data()+at,int(bytes),48000)!=960)
                    throw std::runtime_error("Invalid score packet");
                at+=bytes;
            }
            if(track.frames==0||track.frames>48000*180||track.skip>=960||
               track.frames+track.skip>packets*960||track.frames+track.skip<=(packets-1)*960)
                throw std::runtime_error("Invalid score duration");
        }
        int error=0;
        decoder.reset(opus_decoder_create(48000,2,&error));
        if(error!=OPUS_OK||!decoder)throw std::runtime_error("Cannot initialize score decoder");
    }
    void select(int track){
        requested=track>=0&&size_t(track)<tracks.size()?track:-1;
    }
    size_t mix(std::span<float> stereo,float volume){
        size_t audible=0;
        constexpr float step=1.f/2880.f;
        for(size_t i=0;i+1<stereo.size();i+=2){
            if(current!=requested){
                gain=std::max(0.f,gain-step);
                if(gain==0){
                    current=requested;
                    resetPacketState();
                }
            }else gain=std::min(1.f,gain+step);
            if(current<0){if(requested<0)break;continue;}
            const auto &track=tracks[size_t(current)];
            if(played>=track.frames){
                if(!loopingScore(ScoreCue(current)))continue;
                resetPacketState();
            }
            if(position==count&&!decodeNext(track)){
                played=track.frames;
                continue;
            }
            stereo[i]+=packet[position*2]*gain*volume;
            stereo[i+1]+=packet[position*2+1]*gain*volume;
            ++position;
            ++played;
            if(volume>0)++audible;
        }
        return audible;
    }
};
}
