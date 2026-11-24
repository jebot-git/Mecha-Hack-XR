#pragma once
#include "rpg/session.hpp"
namespace iron::rpg {
std::vector<std::string> campaignDebriefLines(const Session &,uint32_t);
inline constexpr const char *menuHint="STICK: MOVE / CHANGE   A / RT: SELECT   B / LT: BACK";
inline constexpr const char *mountHint="LT/RT: ARMS   X/Y: LEFT MOUNTS";
bool playerEquipment(Gear);
struct MenuStep {int row=0,change=0;bool select=false,back=false;};
class MenuNavigation {
  bool axisHeld=true,selectHeld=true,backHeld=true;
public:
  MenuStep update(float x,float y,bool select,bool back,bool focused);
  void blockButtons(){selectHeld=backHeld=true;}
};
enum class MenuPage {None,Hangar,Pause,Options};
enum class PlayerAction {None,Missions,Equipment,Inventory,Trade,Sortie,Lobby,Pilots,NewCampaign,Options,Resume,Recenter,Sound,Music,Microphone,Back,Quit};
class PlayerMenu {
  MenuPage returnPage=MenuPage::Hangar;
public:
  MenuPage page=MenuPage::None;
  int selected=0;
  bool profilesAvailable=true;
  void open(MenuPage);
  std::vector<PlayerAction> actions(bool host,bool online)const;
  PlayerAction update(const MenuStep &,bool host,bool online,float &sound,float &music,bool &microphone);
  std::vector<std::string> lines(const Session &,uint32_t,float sound,float music,bool microphone,bool online)const;
};
}
