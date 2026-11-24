#pragma once
#include "simulation.hpp"
namespace iron {
struct CampaignRules {
    uint32_t initialMissions=15;
    unsigned initialWeapons=1u<<int(Weapon::Rifle);
    std::vector<int> rewards{5,-1,3,4,2,-1,6,-1,7,-1,-1,8,-1,-1,-1,-1};
    int game=0;
    int count()const{return int(rewards.size());}
    uint32_t fullMask()const{return (1u<<count())-1;}
    void load(const std::filesystem::path& retail,int campaign=0);
    uint32_t available(uint32_t completed) const;
    unsigned weapons(uint32_t completed) const;
};
struct Progression {
    CampaignRules rules;
    uint32_t completed=0;
    World::Difficulty difficulty=World::Difficulty::Normal;
    int selectedMission=0;
    Loadout loadout{Weapon::Empty,Weapon::Rifle,Weapon::Empty,Weapon::Empty,Weapon::Empty,Weapon::Empty};
    bool missionAvailable(int) const;
    bool weaponAvailable(Weapon) const;
    bool complete(int mission);
    bool finished() const{return completed==rules.fullMask();}
    int nextMission() const;
    void sanitizeLoadout();
    void load(const std::filesystem::path&);
    void save(const std::filesystem::path&,bool merge=true); // locked, merged, atomic replacement
    static std::filesystem::path defaultPath();
};
}
