#pragma once
#include "progression.hpp"
#include "skirmish.hpp"
#include <optional>
#include <string>
namespace iron {
enum class Screen { Title, Campaign, Cooperative, Join, Connecting, Standby, Ready, Pause, Missions, Briefing, Loadout, Playing, Debrief, Disconnected, Skirmish };
enum class MenuAction { None, Singleplayer, Load, Host, Join, Exit, Ready, Restart, Abort, Destroy, Leave, Skirmish, GenerateSkirmish, Recenter };
inline constexpr int skirmishStartRow=13,skirmishBackRow=14,skirmishRowCount=15;
struct MissionCard {
    std::string title,summary;
    std::vector<std::string> briefing;
    std::string art;
};
inline constexpr std::array<int,MountCount> equipmentPanelOrder{
    LeftShoulder,RightShoulder,LeftHand,RightHand,LeftHip,RightHip};
struct EquipmentPanel {
    struct Slot {std::string item,detail;bool empty=false,linked=false;};
    std::array<Slot,MountCount> slots;
    std::vector<std::string> options;
    std::string title,inspection,detail,action,status;
    int selectedSlot=-1,selectedOption=-1;
    bool actionAvailable=true;
};
struct Frontend {
    // Optional game-owned cards retain the campaign selector controller.
    std::vector<MissionCard> missionCards;
    std::vector<std::string> menuLines;
    std::string applicationTitle="IRON SOLDIER XR";
    std::string equipmentModel;
    int equipmentHandling=0;
    std::optional<EquipmentPanel> equipmentPanel;
    std::string interfaceArt;
    std::string chassisPortrait;
    int pilotPortrait=-1, serviceRank=0;
    unsigned pilotHonours=0;
    uint32_t interfaceArtSeed=0;
    int missionCount() const{return missionCards.empty()?campaign.rules.count():int(missionCards.size());}
    bool generatedMissions() const{return !missionCards.empty();}
    unsigned generatedCompleted=0;
    bool generatedFinale=false;
    Progression campaign;
    int game=0;MenuAction pendingAction=MenuAction::Singleplayer;
    bool sandbox=false;
    bool skirmish=false;
    SkirmishSettings skirmishSettings;
    std::array<std::string,skirmishRowCount> skirmishRows() const;
    std::string resultMessage,saveMessage;
    std::string prototypeBriefing;
    std::string environmentMessage;
    float environmentIntro=0;
    float soundVolume=1,musicVolume=1;
    std::string audioMessage;
    bool missionAvailable(int i) const{return i>=0&&i<missionCount()&&(generatedMissions()?(i<32&&!(generatedCompleted&(1u<<i))):(skirmish||sandbox||campaign.missionAvailable(i)));}
    bool weaponAvailable(Weapon w) const{return int(w)>=0&&int(w)<int(Weapon::Count)&&(skirmish||sandbox||campaign.weaponAvailable(w));}
    int weaponCount() const{return skirmish||game||sandbox?int(Weapon::Count):9;}
    bool recordResult(const World&);
    void showDebrief(int mission,const Input&);
    int resultMission=-1;
    bool handleFailure(const World&,const Input&);
    void acceptResult();
    void setLobbyHost(bool value);
    void connectionLost();
    void restoreCampaign();
    bool missionChosen=false,cooperative=false,host=false,ready=false,spectating=false,micEnabled=true,spectatorHeld=false;
    bool briefingHeader=false;
    bool renderWorldOnly=false; // Native asset captures omit spectator overlays.
    std::string lobbyCode,joinCode="AAAAAA",networkMessage,voiceMessage;
    int option=0,codeCursor=0,spectator=0,teamResult=0;unsigned localPlayer=0;
    std::vector<Pilot> remotePilots;
    std::vector<std::string> roster;
    World::Difficulty difficulty=World::Difficulty::Normal;
    MenuAction action=MenuAction::None;
    Screen screen=Screen::Title;
    int mission=0,weapon=1;
    float rotation=0,clock=0;
    Loadout loadout{Weapon::Empty,Weapon::Rifle,Weapon::Empty,Weapon::Empty,Weapon::Empty,Weapon::Empty};
    std::string message="SELECT WEAPON THEN PRESS ITS MOUNT BUTTON";
    std::array<bool,MountCount> held{};
    bool navHeld=false,confirmHeld=false,backHeld=false;
    bool equip(int mount,bool remove=false);
    // Returns true only on deployment. Menu transitions consume held controls.
    bool update(const Input&,float dt,bool confirm,bool back);
};
}
