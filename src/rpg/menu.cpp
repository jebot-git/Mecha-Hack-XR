#include "rpg/menu.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/mission_board.hpp"
#include <algorithm>
#include <bit>
namespace iron::rpg {
std::vector<std::string> campaignDebriefLines(const Session &s,uint32_t id){
  const auto found=std::find_if(s.members.begin(),s.members.end(),[&](const auto &m){return m.id==id;});
  if(found==s.members.end())throw std::runtime_error("Unknown debrief pilot");
  const auto &m=*found;
  if(s.campaign.victorious){
    auto ending=storyDebrief(s);
    std::vector<std::string> lines={"CAMPAIGN VICTORY / "+m.name,
      "NINE CONTRACTS AND THE FINAL OPERATION COMPLETE",
      m.campaignAwarded?"CAMPAIGN AWARD +1 CLAIMED":"ACCEPT TO CLAIM ONE CAMPAIGN AWARD POINT",
      "AWARDS AVAILABLE: "+std::to_string(availableCampaignAwards(m.sheet))+" / "+std::to_string(m.sheet.campaignAwards)+" EARNED",
      m.campaignAwarded?"STARTER KIT RESTORED":"STARTER KIT RESTORES ON ACCEPT",
      std::to_string(m.sheet.credits)+" CREDITS / "+std::to_string(m.sheet.inventory[size_t(Gear::Scrap)])+" SCRAP CARRY FORWARD"};
    lines.insert(lines.begin()+2,ending.begin(),ending.end());
    return lines;
  }
  std::vector<std::string> lines={"MISSION DEBRIEF / "+m.name,
      std::to_string(campaignCompleted(s.campaign))+" OF 9 CONTRACTS",
      "CREDITS "+std::to_string(m.sheet.credits)+" / CONTRACT +"+std::to_string(s.mission.contractReward)};
  const auto &r=s.mission.recipe;
  if(r.employer>=0&&r.adversary>=0){
    std::string relations=std::string(factions[r.employer])+" "+std::to_string(s.campaign.standings.at(r.employer));
    relations+=" / "+std::string(factions[r.adversary]);
    if(r.adversary!=voidmawFaction)relations+=" "+std::to_string(s.campaign.standings.at(r.adversary));
    lines.push_back(std::move(relations));
  }
  lines.push_back("INTEL "+std::to_string(s.campaign.intel)+" / SUPPLIES "+std::to_string(s.campaign.supply)+" / STRUCTURES LOST "+std::to_string(s.mission.structuresLost));
  if(!m.recoveredGear.empty()){
    lines.push_back("SAFE BOXES / "+std::to_string(m.recoveredGear.size())+" ITEMS BANKED");
    for(size_t i=0;i<std::min<size_t>(4,m.recoveredGear.size());++i)
      lines.push_back(std::string("RECOVERED / ")+gearName(m.recoveredGear[i]));
  }
  auto story=storyDebrief(s);lines.insert(lines.begin()+3,story.begin(),story.end());
  if(s.mission.encounterCredits)lines.insert(lines.begin()+3,"RANDOM ENCOUNTERS +"+std::to_string(s.mission.encounterCredits)+" CR");
  lines.push_back(s.campaign.awaitingAdvance?"REMAINING OFFERS UPDATE AFTER PARTY DEBRIEF":campaignFinale(s.campaign)?"FINALE UNLOCKED":"REMAINING OFFERS UPDATED");return lines;
}
bool playerEquipment(Gear gear){
  return gear>=Gear::Empty&&gear<Gear::Count&&gear!=Gear::Comlink&&!retiredGear(gear);
}
MenuStep MenuNavigation::update(float x,float y,bool select,bool back,bool focused){
  if(!std::isfinite(x)||!std::isfinite(y))x=y=0;
  bool axis=std::max(std::abs(x),std::abs(y))>.6f;MenuStep result;
  if(focused){
    if(axis&&!axisHeld){if(std::abs(x)>std::abs(y))result.change=x>0?1:-1;else result.row=y>0?-1:1;}
    result.back=back&&!backHeld;result.select=select&&!selectHeld&&!result.back;
  }
  axisHeld=axis;selectHeld=select;backHeld=back;return result;
}
void PlayerMenu::open(MenuPage next){if(next==MenuPage::Options)returnPage=page;page=next;selected=0;}
std::vector<PlayerAction> PlayerMenu::actions(bool host,bool online)const{
  if(page==MenuPage::Hangar){std::vector<PlayerAction> a={PlayerAction::Missions,PlayerAction::Sortie,PlayerAction::Equipment,PlayerAction::Inventory,PlayerAction::Trade,PlayerAction::Lobby,PlayerAction::Options};if(profilesAvailable)a.insert(a.end()-1,PlayerAction::Pilots);if(host)a.push_back(PlayerAction::NewCampaign);a.push_back(PlayerAction::Quit);return a;}
  if(page==MenuPage::Pause)return {PlayerAction::Resume,PlayerAction::Equipment,PlayerAction::Options,PlayerAction::Recenter,PlayerAction::Quit};
  if(page==MenuPage::Options){std::vector<PlayerAction> a={PlayerAction::Sound,PlayerAction::Music};if(online)a.push_back(PlayerAction::Microphone);a.push_back(PlayerAction::Back);return a;}
  return {};
}
PlayerAction PlayerMenu::update(const MenuStep &step,bool host,bool online,float &sound,float &music,bool &microphone){
  auto choices=actions(host,online);if(choices.empty())return PlayerAction::None;
  selected=std::clamp(selected,0,int(choices.size())-1);
  if(step.back){if(page==MenuPage::Options){page=returnPage;selected=0;return PlayerAction::None;}return page==MenuPage::Pause?PlayerAction::Resume:PlayerAction::None;}
  selected=(selected+step.row+int(choices.size()))%int(choices.size());auto action=choices[selected];
  if(page==MenuPage::Options){
    auto adjust=[&](float &volume){if(step.change)volume=std::clamp(std::round(volume*10+step.change)/10,0.f,1.f);};
    if(action==PlayerAction::Sound)adjust(sound);
    if(action==PlayerAction::Music)adjust(music);
    if(action==PlayerAction::Microphone&&(step.change||step.select))microphone=!microphone;
    if(action==PlayerAction::Back&&step.select){page=returnPage;selected=0;}
    return PlayerAction::None;
  }
  if(!step.select)return PlayerAction::None;
  if(action==PlayerAction::Options){open(MenuPage::Options);return PlayerAction::None;}
  return action;
}
std::vector<std::string> PlayerMenu::lines(const Session &session,uint32_t id,float sound,float music,bool microphone,bool online)const{
  std::vector<std::string> result;auto found=std::find_if(session.members.begin(),session.members.end(),[&](const auto &m){return m.id==id;});if(found==session.members.end())throw std::runtime_error("Pilot is not in the session");const auto &member=*found;
  result.push_back(page==MenuPage::Hangar?"HANGAR / "+member.name+" / "+serviceTitle(member.sheet):page==MenuPage::Pause?"PAUSED":"OPTIONS");
  if(page==MenuPage::Hangar)result.push_back(
      std::to_string(member.sheet.credits)+" CR  /  "+
      std::to_string(campaignCompleted(session.campaign))+" OF 9"+(campaignFinale(session.campaign)?"  /  FINALE":""));
  auto choices=actions(session.host==id&&canRegenerateCampaign(session),online);
  for(size_t i=0;i<choices.size();++i){std::string text;switch(choices[i]){
    case PlayerAction::Missions:text="MISSIONS";break;case PlayerAction::Equipment:text="EQUIPMENT";break;
    case PlayerAction::Inventory:text="INVENTORY";break;case PlayerAction::Trade:text="TRADE POST";break;
    case PlayerAction::Lobby:text=online?"LEAVE ONLINE LOBBY":"ONLINE CO-OP LOBBY";break;
    case PlayerAction::Sortie:text="SORTIE ROOM";break;case PlayerAction::Pilots:text="PILOTS";break;
    case PlayerAction::NewCampaign:text="REGENERATE CAMPAIGN";break;case PlayerAction::Options:text="OPTIONS";break;
    case PlayerAction::Resume:text="RESUME";break;case PlayerAction::Recenter:text="RECENTER COCKPIT";break;
    case PlayerAction::Sound:text="SOUND: "+std::to_string(int(std::lround(sound*100)))+"%";break;
    case PlayerAction::Music:text="MUSIC: "+std::to_string(int(std::lround(music*100)))+"%";break;
    case PlayerAction::Microphone:text=std::string("MICROPHONE: ")+(microphone?"ON":"MUTED");break;
    case PlayerAction::Back:text="BACK";break;case PlayerAction::Quit:text="EXIT GAME";break;default:break;}
    result.push_back(std::string(int(i)==selected?"> ":"  ")+text);
  }
  return result;
}
}
