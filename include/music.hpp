#pragma once
#include <array>
#include <vector>
#include <filesystem>
#include <span>
#include <cstdint>
namespace iron {
// Native translation of the retail eight-channel Jerry driver, not a CPU emulator.
class Music {
    struct Instrument {unsigned kind=0;std::array<uint32_t,20> words{};std::vector<unsigned char> sample;std::array<int32_t,128> carrier{},modulator{};std::array<int32_t,10> envelope{};};
    struct Song {std::vector<Instrument> instruments;std::vector<uint32_t> score;};
    struct Voice {bool active=false;const Instrument* instrument=nullptr;std::array<uint32_t,20> words{};unsigned pitch=0,velocity=0;};
    std::array<Song,7> songs;std::array<unsigned,16> tracks{};std::array<Voice,8> voices{};
    int selected=-1;size_t cursor=0;double tick=0,sourcePhase=0;unsigned loopCount=0;
    double sourceRate=26593900.0/1280,tempoRate=26593900.0/(277*96);
    std::array<float,2> previous{},next{};
    void events();std::array<float,2> sample();void select(int);
public:
    explicit Music(const std::filesystem::path&);
    void start(int mission);void intro(){select(6);}
    void mix(std::span<float> stereo,unsigned rate=48000);
    unsigned loops() const{return loopCount;}
};
}
