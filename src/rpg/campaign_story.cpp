#include "rpg/campaign_story.hpp"
#include "rpg/session.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/original_assets.hpp"
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
namespace iron::rpg {
namespace {
using Json=nlohmann::json;
int sourceMission(const Json &beat){auto id=beat.at("mission").get<std::string>();auto source=manualMissionCatalog();for(size_t i=0;i<source.size();++i)if(id==source[i].id)return int(i);throw std::runtime_error("Unknown campaign source mission: "+id);}
const Json &arc(int index){if(index<0||index>=campaignStoryCount)throw std::runtime_error("Invalid story arc");return campaignStoryCatalog().at("campaigns").at(size_t(index));}
const Json &beat(const CampaignContext &x){return arc(x.storyArc).at("beats").at(size_t(std::clamp(x.stage,1,9)-1));}
const Json &offer(const MissionRecipe &r){auto &a=arc(r.storyArc);return r.finale?a.at("finales").at(size_t(r.storyApproach)):a.at("beats").at(size_t(r.campaignStage-1)).at("offers").at(size_t(r.storyApproach));}
bool custom(const GameSettings &s){return s.enemy>=0||s.objective>=0||s.location>=0||s.environment>=0||s.opener>=0||s.theme>=0||s.tactic>=0;}
int chooseArc(const Session &s){if(s.campaign.storyArc>=0)return s.campaign.storyArc;if(s.campaign.completed||custom(s.settings))return -1;if(s.campaign.legacyArc>=0){
    auto next=arc(s.campaign.legacyArc).at("finales").at(size_t(s.campaign.legacyEnding)).at("next_campaign").get<std::string>();
    const auto &all=campaignStoryCatalog().at("campaigns");for(size_t i=0;i<all.size();++i)if(all[i].at("id")==next)return int(i);
    throw std::runtime_error("Missing campaign continuation");
  }return int(missionBoardSeed(s.seed,s.boardRevision)%campaignStoryCount);}
// Each alternate front helps the same cause through a different field operation.
constexpr int frontObjectives[2][10]={
  {1,5,3,7,2,6,9,4,0,8},
  {5,2,8,0,1,7,3,6,9,4},
};
int frontObjective(int objective,int front){return front?frontObjectives[front-1][objective]:objective;}
const char *frontOrder(int objective){
  static constexpr const char *orders[]={
    "Clear the hostile force screening this approach so the main operation can go ahead.",
    "Protect the forward position long enough for its crew to finish the work.",
    "Stay with the support convoy and get its crews and cargo through alive.",
    "Survey the marked sites so the crews know which route is still usable.",
    "Recover the marked pods and bring them back through the insertion point.",
    "Take out the enemy stores feeding this front before the main operation starts.",
    "Intercept the force pushing toward our line before it reaches the crews behind it.",
    "Draw the hostile force to the marked beacon. Keep its attention here while the crews work.",
    "Reach the allied unit and hold nearby until it can secure its position.",
    "Disable the local commander, close in to secure the target, and return to extraction."
  };
  return orders[objective];
}
void copyRecipeContext(MissionRecipe &r,const CampaignContext &x){r.storyArc=x.storyArc;r.storyApproach=x.storyApproach;r.storyFront=x.storyFront;r.storyTracks=x.storyTracks;r.storyCollateral=x.storyCollateral;r.legacyArc=x.legacyArc;r.legacyEnding=x.legacyEnding;r.legacyScars=x.legacyScars;}
}
const Json &campaignStoryCatalog(){
  static std::mutex mutex;static std::map<std::string,Json> cache;std::scoped_lock lock(mutex);
  auto root=originalAssetRoot();auto key=root.lexically_normal().string();if(auto found=cache.find(key);found!=cache.end())return found->second;
  std::ifstream file(root/"stories/campaigns.json",std::ios::binary|std::ios::ate);
  if(!file||file.tellg()>512*1024)throw std::runtime_error("Missing or oversized campaign story library");
  file.seekg(0);Json data;file>>data;
  if(data.at("schema")!=1||data.at("campaigns").size()!=campaignStoryCount)throw std::runtime_error("Invalid campaign story library");
  std::vector<std::string> ids;
  for(const auto &a:data.at("campaigns")){
    auto id=a.at("id").get<std::string>();if(id.empty()||std::find(ids.begin(),ids.end(),id)!=ids.end()||a.at("beats").size()!=9||a.at("threads").size()!=3||a.at("finales").size()!=3)throw std::runtime_error("Invalid story graph");ids.push_back(id);
    for(const auto &t:a.at("threads"))if(t.at("employer").get<int>()<0||t.at("employer").get<int>()>2||t.at("adversary").get<int>()<0||t.at("adversary").get<int>()>3||t.at("employer")==t.at("adversary"))throw std::runtime_error("Invalid story faction");
    for(const auto &b:a.at("beats")){sourceMission(b);if(b.at("offers").size()!=3)throw std::runtime_error("Invalid story choices");for(const auto &o:b.at("offers"))if(o.at("variant").get<int>()<0||o.at("variant").get<int>()>2||o.at("title").get<std::string>().size()>58||o.at("briefing").get<std::string>().size()>150||o.at("result").get<std::string>().size()>150)throw std::runtime_error("Invalid story offer");}
  }
  for(const auto &a:data.at("campaigns"))for(const auto &f:a.at("finales"))if(std::find(ids.begin(),ids.end(),f.at("next_campaign").get<std::string>())==ids.end())throw std::runtime_error("Dangling campaign continuation");
  return cache.emplace(key,std::move(data)).first->second;
}
int storyDominant(const std::array<int,3> &tracks,int tie){int best=0;for(int i=1;i<3;++i)if(tracks[i]>tracks[best])best=i;if(tie>=0&&tie<3&&tracks[tie]==tracks[best])return tie;return best;}
CampaignContext storyContext(const Session &s,int slot,CampaignContext x){
  x.storyArc=chooseArc(s);if(x.storyArc<0)return x;
  x.storyTracks=s.campaign.storyTracks;x.storyCollateral=s.campaign.storyCollateral;
  x.legacyArc=s.campaign.legacyArc;x.legacyEnding=s.campaign.legacyEnding;x.legacyScars=s.campaign.legacyScars;
  int rank=0;for(int i=0;i<slot;++i)if(!(s.campaign.completed&(1u<<i)))++rank;
  int rotation=int((s.campaign.consequences+s.seed+unsigned(x.stage-1))%3);
  x.storyApproach=(rank+rotation)%3;x.storyFront=rank/3;
  auto &thread=arc(x.storyArc).at("threads").at(size_t(x.storyApproach));
  x.employer=thread.at("employer");x.adversary=thread.at("adversary");
  const auto &current=beat(x);x.employer=current.value("employer_faction",x.employer);x.adversary=current.value("opponent_faction",x.adversary);
  int widerHostility=std::max(0,-*std::min_element(s.campaign.standings.begin(),s.campaign.standings.end()));
  x.pressure=std::clamp(hostilityPressure(s.campaign.standings,x.adversary)+widerHostility/4+x.storyCollateral+x.legacyScars/3,0,20);
  return x;
}
GameSettings storySettings(const GameSettings &settings,const CampaignContext &x){
  if(x.storyArc<0)return settings;
  auto result=settings;auto &b=beat(x);const auto &m=manualMissionCatalog()[size_t(sourceMission(b))];
  result.objective=frontObjective(b.at("offers").at(size_t(x.storyApproach)).at("objective").get<int>(),x.storyFront);
  struct Front {int location;int biome;};
  static constexpr Front fronts[8][3]={
    {{2,5},{4,3},{5,0}}, {{2,0},{0,3},{6,7}}, {{6,7},{7,1},{0,3}},
    {{2,3},{1,5},{3,0}}, {{6,7},{7,1},{9,7}}, {{6,7},{4,2},{9,1}},
    {{2,6},{0,3},{5,0}}, {{6,7},{9,7},{7,1}}
  };
  result.location=m.preferredLocation;
  result.environment=result.opener=-1;
  if(x.storyFront){
    auto front=fronts[x.storyArc][x.storyFront];
    if(front.location==m.preferredLocation)front=fronts[x.storyArc][0];
    result.location=front.location;
    result.environment=front.biome;
  }
  const std::string id=m.id;
  if(id=="grand_theft_mariner"||id=="symbiotic_superdestroyer"){
    result.location=x.storyFront?4:8;
    result.environment=x.storyFront==2?3:2;
  }else if(id=="verglas"||id=="frozen_king"){
    if(x.storyFront)result.location=x.storyFront==1?1:4;
    result.environment=6;
  }else if(id=="eruption_extinction"){
    if(x.storyFront)result.location=x.storyFront==1?2:3;
    result.environment=4;
  }
  if(x.storyArc==4&&id!="cozen_job"&&id!="orions_arrow"&&id!="face_keros"&&id!="precious_cargo"){result.location=x.storyFront==2?9:x.storyFront==1?7:6;result.environment=x.storyFront?1:7;result.opener=result.objective==1?5:0;}
  if(!x.storyFront&&(id=="sewer_duty"||id=="race_to_bottom")){result.opener=3;if(result.environment<0)result.environment=3;}
  if(id=="seize_ronin"||id=="cozen_job"||id=="heavens_fall"||id=="salvaging_eclipse"){
    constexpr int locations[]={7,6,9};
    result.location=locations[x.storyFront];
    result.environment=x.storyFront==1?7:1;
    result.opener=x.storyFront==1?0:2;
  }
  // The prior strategy alters counterplay, rather than merely salting a seed.
  int dominant=storyDominant(x.storyTracks);
  result.tactic=x.stage<=1?4:dominant==0?9:dominant==1?8:3;
  return result;
}
void applyStoryMission(MissionRecipe &r,const CampaignContext &x,float extent){
  if(x.storyArc<0)return;
  copyRecipeContext(r,x);
  if(r.finale)return;
  const auto &b=beat(x);int id=sourceMission(b);const auto &m=manualMissionCatalog()[size_t(id)];
  if(!x.storyFront){
    r.manualMission=id;
    r.manualVariant=b.at("offers").at(size_t(x.storyApproach)).at("variant");
    r.objectiveSeconds=float(m.hold);
    r.countdownSeconds=m.countdown?std::max(float(m.countdown),120.f+50.f*r.objectiveCount+r.objectiveSeconds)*extent/500.f:0;
  }
  if(!r.objectiveCount&&(r.objective==1||r.objective==2||r.objective==7||r.objective==8))r.objectiveCount=1;
  const int dominant=storyDominant(x.storyTracks);
  r.siteCount=dominant==1&&x.storyTracks[1]>=2?2:1;
  if(dominant==1&&x.storyTracks[1]>=2){r.opportunity=2;r.siteCount=2;}
  if(dominant==0&&x.storyTracks[0]>=2&&r.objective==1)r.objectiveSeconds+=10.f;
  if(dominant==2&&x.storyTracks[2]>=2)r.complication=1;
  // Legacy effects are bounded and do not carry pilot stats or equipment.
  if(x.legacyArc>=0&&x.legacyEnding==1)r.siteCount=2;
}
void recordStoryVictory(CampaignState &c,const MissionRecipe &r,int losses){
  if(r.storyArc<0||r.finale)return;
  c.storyArc=r.storyArc;c.storyTracks[size_t(r.storyApproach)]=std::min(9,c.storyTracks[size_t(r.storyApproach)]+1);c.storyLastApproach=r.storyApproach;
  if(losses>=8)c.storyCollateral=std::min(9,c.storyCollateral+1);
}
void inheritCampaignLegacy(CampaignState &to,const CampaignState &from){to.legacyArc=from.legacyArc;to.legacyEnding=from.legacyEnding;to.legacyScars=from.legacyScars;}
CampaignState restartCampaign(const CampaignState &old){CampaignState next;inheritCampaignLegacy(next,old);next.standings=old.standings;return next;}
std::string storyMissionTitle(const MissionRecipe &r){
  if(r.storyArc<0)return {};
  if(r.finale)return "FINALE: "+offer(r).at("title").get<std::string>();
  const auto title=offer(r).at("title").get<std::string>();
  return r.storyFront?title+" / "+missionObjectives()[r.objective].name:title;
}
std::string storyMissionBrief(const MissionRecipe &r){
  if(r.storyArc<0)return {};
  if(!r.finale&&r.storyFront)return frontOrder(r.objective);
  return offer(r).at(r.finale?"setup":"briefing").get<std::string>();
}
std::vector<std::string> storyMissionBriefing(const MissionRecipe &r){
  if(r.storyArc<0)return {};
  const auto &campaign=arc(r.storyArc);
  const auto &thread=campaign.at("threads").at(size_t(r.storyApproach));
  if(r.finale)return {storyMissionBrief(r),storyContinuity(r),r.boss>=0?
      "Break the enemy command guard and destroy its lead unit. Their reinforcements cannot hold the route without them.":
      "The opposing field army has committed to this ground. Clear its combat units to secure the campaign's last route."};
  const auto &current=campaign.at("beats").at(size_t(r.campaignStage-1));
  return {storyMissionBrief(r),
          current.at("situation").get<std::string>()+" "+current.at("clue").get<std::string>(),
          thread.at("intent").get<std::string>()};
}
std::string storyMissionStakes(const MissionRecipe &r){if(r.storyArc<0)return {};return r.finale?offer(r).at("payoff").get<std::string>():offer(r).at("result").get<std::string>();}
std::string storyContinuity(const MissionRecipe &r){
  if(r.storyArc<0)return {};
  auto &a=arc(r.storyArc);
  if(std::accumulate(r.storyTracks.begin(),r.storyTracks.end(),0)==0){if(r.legacyArc>=0)return "AFTERMATH: "+arc(r.legacyArc).at("title").get<std::string>()+" / "+arc(r.legacyArc).at("finales").at(size_t(r.legacyEnding)).at("title").get<std::string>();return "OPENING: "+a.at("question").get<std::string>();}
  int dominant=storyDominant(r.storyTracks);return a.at("threads").at(size_t(dominant)).at(r.storyTracks[size_t(dominant)]>=2?"established":"neglected").get<std::string>();
}
std::string storyEnding(const CampaignState &c){
  if(c.storyArc<0||!c.victorious)return {};
  int ending=storyDominant(c.storyTracks,c.storyLastApproach);
  auto text=arc(c.storyArc).at("finales").at(size_t(ending)).at("payoff").get<std::string>();
  if(c.storyCollateral)text+=" Rebuilding remains part of the victory.";
  return text;
}
std::vector<std::string> storyDebrief(const Session &s){
  const auto &r=s.mission.recipe;if(r.storyArc<0)return {};
  if(r.finale){return {storyEnding(s.campaign),arc(r.storyArc).at("finales").at(size_t(r.storyApproach)).at("continuation").get<std::string>()};}
  std::vector<std::string> lines={offer(r).at("result").get<std::string>()};
  if(s.mission.structuresLost>=8)lines.push_back("WAR DAMAGE: next routes face added pressure.");
  return lines;
}
bool validStoryRecipe(const MissionRecipe &r){
  if((r.legacyArc<0)!=(r.legacyEnding<0)||r.storyFront<0||r.storyFront>2||r.storyArc< -1||r.storyArc>=campaignStoryCount||r.storyApproach< -1||r.storyApproach>2||r.storyCollateral<0||r.storyCollateral>9||r.legacyArc< -1||r.legacyArc>=campaignStoryCount||r.legacyEnding< -1||r.legacyEnding>2||r.legacyScars<0||r.legacyScars>9)return false;
  for(int value:r.storyTracks)if(value<0||value>9)return false;
  if(r.storyArc<0)return r.storyApproach==-1&&r.storyCollateral==0&&std::accumulate(r.storyTracks.begin(),r.storyTracks.end(),0)==0;
  return r.storyApproach>=0&&r.employer>=0&&r.employer<3&&r.storyCollateral<=r.campaignStage-1&&(r.finale==(r.campaignStage==10))&&std::accumulate(r.storyTracks.begin(),r.storyTracks.end(),0)==r.campaignStage-1;
}
namespace {
const char *storyRoleSource(int arc){switch(arc){case 0:case 1:case 4:return "mecha_recruit";case 5:case 6:return "mecha_ace";case 2:return "aeon_diode";case 3:return "kaiju_drone";case 7:return "aeon_diode";default:return "";}}
const char *storyRoleAsset(std::string_view source){if(source=="mecha_recruit")return "story_requisition_enforcer";if(source=="mecha_ace")return "story_arena_collector";if(source=="aeon_diode")return "story_archive_sentinel";if(source=="kaiju_drone")return "story_brood_guardian";return "";}
}
void preferStoryRoster(MissionRecipe &r){
  if(r.storyArc<0||r.finale||r.enemies.empty())return;
  auto source=std::string_view(storyRoleSource(r.storyArc));
  for(size_t i=0;i<enemyCatalog().size();++i){const auto &e=enemySpec(i);
    if(source!=e.id||e.hd>encounterThreatLimit(r.difficulty,r.campaignStage)||enemyFaction(i)!=r.adversary)continue;
    r.enemies[0]=int(i);if(!r.tactics.empty())r.tactics[0]=source=="kaiju_drone"?1:source=="mecha_ace"?5:8;break;
  }
}
std::string storyEnemyModel(const MissionRecipe *r,int spec,const std::string &fallback){
  if(!r||r->storyArc<0||r->finale)return fallback;
  auto source=std::string_view(storyRoleSource(r->storyArc));if(source!=enemySpec(size_t(spec)).id)return fallback;
  auto id=storyRoleAsset(source);
  // Missing optional authoring files cannot make a previously saved run unloadable.
  static std::mutex mutex;static std::map<std::filesystem::path,bool> available;
  auto path=originalAssetRoot()/"meshes"/(std::string(id)+".json");std::scoped_lock lock(mutex);
  auto found=available.find(path);if(found==available.end())found=available.emplace(path,std::filesystem::exists(path)).first;
  return found->second?id:fallback;
}
}
