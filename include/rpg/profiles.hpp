#pragma once
#include "rpg/rules.hpp"
#include <optional>
namespace iron::rpg {
inline constexpr int pilotProfileLimit=4;
struct PilotProfile {Sheet sheet;std::string callsign;};
class ProfileStore {
    std::filesystem::path root;
    int selected=0;
    void writeIndex()const;
public:
    explicit ProfileStore(std::filesystem::path directory=defaultProfilePath().parent_path());
    int active()const{return selected;}
    std::filesystem::path path(int slot)const;
    std::optional<PilotProfile> load(int slot)const;
    void select(int slot);
    void save(int slot,const Sheet &,const std::string &callsign);
    void reset(int slot,Dice &);
    std::vector<std::string> lines(int cursor,std::optional<PilotProfile> *highlighted=nullptr)const;
};
}
