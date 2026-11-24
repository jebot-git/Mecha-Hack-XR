#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace iron::rpg {
struct MissionRecipe;
std::array<std::string,3> briefingHeaderShots(const MissionRecipe &,int sector);

class MissionBriefingHeader {
public:
    static constexpr int width=384,height=104,fps=12,maxFrames=360;
    void open(const std::filesystem::path &directory,const MissionRecipe &,int sector);
    void stop();
    bool active() const {return lastFrame>=0;}
    // True only when the GPU texture needs a new frame.
    bool update(double seconds);
    const uint16_t *pixels() const {return decoded.data();}
private:
    struct Shot {
        std::ifstream file;
        std::vector<uint32_t> offsets;
        int frames() const {return int(offsets.size())-1;}
    };
    std::vector<Shot> shots;
    std::vector<unsigned char> compressed;
    std::vector<uint16_t> decoded;
    int totalFrames=0,lastFrame=-1;
};
}
