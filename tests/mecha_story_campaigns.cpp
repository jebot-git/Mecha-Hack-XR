#include "rpg/campaign_story.hpp"
#include "presentation.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/preparation.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/manual_enemy_ai.hpp"
#include "rpg/menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
namespace {
Session fixture(int arc,int stage,int choice){
  Session s(7301);s.add(1,"Story pilot");Dice d{42};s.member(1).sheet=createSheet(Chassis::Striker,PilotKind::Engineer,d);finishCreation(s.member(1).sheet);
  s.settings.extent=500;s.settings.maps=1;s.campaign.storyArc=arc;s.campaign.completed=(1u<<unsigned(stage-1))-1;
  s.campaign.storyTracks={stage-1,0,0};s.campaign.storyLastApproach=stage>1?0:-1;
  s.campaign.consequences=unsigned((choice+3-int((s.seed+unsigned(stage-1))%3))%3);
  return s;
}
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
}
int main(){
  setOriginalAssetRoot(MECHA_ASSETS);const auto &library=campaignStoryCatalog();assert(library["campaigns"].size()==8);
  std::set<int> sources,objectives;std::set<std::string> titles;
  int count=0;size_t maximumRows=0,maximumDebriefRows=0;
  for(int arc=0;arc<campaignStoryCount;++arc)for(int stage=1;stage<=9;++stage)for(int choice=0;choice<3;++choice){
    auto s=fixture(arc,stage,choice);validateCampaign(s.campaign);auto r=campaignMission(s,stage-1);
    assert(r.storyArc==arc&&r.storyApproach==choice&&r.campaignStage==stage&&validManualRecipe(r));
    auto &data=library["campaigns"][size_t(arc)]["beats"][size_t(stage-1)]["offers"][size_t(choice)];
    assert(manualMissionName(r).starts_with(data["title"].get<std::string>())&&r.manualVariant==data["variant"]&&r.objective==data["objective"]);
    assert(r.employer>=0&&r.employer<3&&r.adversary>=0&&r.adversary<4);
    for(int id:r.enemies)assert(enemySpec(size_t(id)).hd<=encounterThreatLimit(r.difficulty,r.campaignStage));
    const auto authored=data.at("briefing").get<std::string>();
    const auto lines=briefing(r);
    assert(lines.front()==authored);
    const auto &beat=library["campaigns"][size_t(arc)]["beats"][size_t(stage-1)];
    assert(lines[1].starts_with(beat.at("situation").get<std::string>()));
    assert(lines[3].starts_with("OBJECTIVE:")&&lines[4].starts_with("OPTIONAL:"));
    assert(std::any_of(lines.begin(),lines.end(),[](const auto &line){return line.starts_with("CONDITIONS:");}));
    assert(Json(campaignMission(s,stage-1))==Json(r));
    MissionBoard preview;preview.fill(r);auto card=missionCards(preview)[0];
    assert(card.summary==authored&&card.briefing.front()==authored);
    size_t rows=0;for(const auto &line:card.briefing)rows+=missionLines(line,57).size();maximumRows=std::max(maximumRows,rows);
    if(rows>18)std::cerr<<"Briefing row overflow: "<<arc<<"/"<<stage<<"/"<<choice<<" rows="<<rows<<'\n';
    assert(rows<=18);
    s.mission.recipe=r;s.mission.structuresLost=9;
    size_t debriefRows=0;for(const auto &line:campaignDebriefLines(s,1))debriefRows+=missionLines(line,58).size();
    maximumDebriefRows=std::max(maximumDebriefRows,debriefRows);assert(debriefRows<=17);
    sources.insert(r.manualMission);objectives.insert(r.objective);titles.insert(manualMissionName(r));++count;
  }
  {
    for(int arc=0;arc<campaignStoryCount;++arc){
      auto first=fixture(arc,1,0);
      auto board=generateMissionBoard(first);
      std::set<std::string> names;
      std::set<int> fronts,objectives,locations,biomes;
      for(const auto &r:board){
        assert(validManualRecipe(r));
        names.insert(manualMissionName(r));fronts.insert(r.storyFront);
        objectives.insert(r.objective);locations.insert(r.location);biomes.insert(r.environment);
        assert(storyMissionBriefing(r).size()==3);
      }
      if(names.size()!=9||fronts.size()!=3||objectives.size()<5||locations.size()<3||biomes.size()<2)
        std::cerr<<"Arc "<<arc<<": "<<names.size()<<" titles, "<<objectives.size()<<" objectives, "
                 <<locations.size()<<" locations, "<<biomes.size()<<" biomes\n";
      assert(names.size()==9&&fronts.size()==3&&objectives.size()>=5&&locations.size()>=3&&biomes.size()>=2);
      for(int rank=0;rank<3;++rank)
        assert(board[rank].objective!=board[rank+3].objective&&board[rank].objective!=board[rank+6].objective&&board[rank+3].objective!=board[rank+6].objective);
    }
  }
  assert(count==216&&sources.size()==manualMissionCatalog().size()&&objectives.size()==10&&titles.size()>200);
  // Every arc/ending is reachable at the ninth-contract bottleneck. The last
  // choice breaks ties, while cumulative priorities survive the rejoin.
  int finales=0;
  for(int arc=0;arc<campaignStoryCount;++arc)for(int choice=0;choice<3;++choice){
    auto s=fixture(arc,9,choice);s.campaign.completed=campaignMissionMask;s.campaign.storyTracks={};s.campaign.storyTracks[size_t(choice)]=9;s.campaign.storyLastApproach=choice;
    auto r=campaignMission(s,0);assert(r.finale&&r.storyArc==arc&&r.storyApproach==choice&&validManualRecipe(r));
    assert(manualMissionName(r).starts_with("FINALE:"));
    const auto &ending=library["campaigns"][size_t(arc)]["finales"][size_t(choice)];
    auto card=campaignCards(s).front();
    assert(card.summary==ending.at("setup").get<std::string>()&&card.briefing.front()==card.summary);
    assert(std::none_of(card.briefing.begin(),card.briefing.end(),[&](const auto &line){
      return line==ending.at("payoff").get<std::string>()||line.starts_with("OPTIONAL:");
    }));
    s.campaign.victorious=true;assert(!storyEnding(s.campaign).empty());
    s.member(1).sheet.campaign=s.campaign;
    auto next=claimCampaignAward(s.member(1).sheet,s.campaign);
    Session sequel(99);sequel.add(1,"Next season");sequel.command(1,{{"op","configure"},{"profile",next}});auto nextOffer=campaignMission(sequel,0);
    assert(library["campaigns"][size_t(nextOffer.storyArc)]["id"]==library["campaigns"][size_t(arc)]["finales"][size_t(choice)]["next_campaign"]);
    assert(nextOffer.legacyArc==arc&&nextOffer.legacyEnding==choice);
    ++finales;
  }
  assert(finales==24);
  // Identical procedural seed and stage, different prior strategy: gameplay
  // parameters change even before any consequence hash changes.
  {
    auto a=fixture(0,3,0),b=a;b.campaign.storyTracks={0,2,0};b.campaign.storyLastApproach=1;
    auto ra=campaignMission(a,2),rb=campaignMission(b,2);
    assert(ra.seed==rb.seed&&ra.tactic!=rb.tactic&&ra.siteCount!=rb.siteCount);
    b.campaign.storyCollateral=2;auto damaged=campaignMission(b,2);assert(damaged.factionPressure>rb.factionPressure);
  }
  // Later joins and older saves must not invent an authored historical route.
  {
    Session old(88);old.add(1,"Legacy");old.campaign.completed=7;
    assert(campaignMission(old,3).storyArc==-1);
    auto custom=fixture(0,1,0);custom.campaign.storyArc=-1;custom.settings.enemy=3;assert(campaignMission(custom,0).storyArc==-1);
  }
  {
    auto s=fixture(7,4,1);auto before=campaignMission(s,3);Session copy;copy.readSnapshot(s.snapshot());assert(Json(campaignMission(copy,3))==Json(before));
    auto broken=s.campaign;broken.storyTracks={9,9,9};assert(rejects([&]{validateCampaign(broken);}));
    auto malformed=before;malformed.storyApproach=3;assert(!validManualRecipe(malformed));
    malformed=before;malformed.campaignStage=10;assert(!validManualRecipe(malformed));
  }
  {
    auto s=fixture(2,1,2);auto r=campaignMission(s,0);auto c=s.campaign;recordStoryVictory(c,r,9);c.completed=1;
    assert(c.storyTracks[2]==1&&c.storyCollateral==1&&c.storyLastApproach==2);validateCampaign(c);
    // The same pilot carries the local ending into the next campaign.
    auto old=s.member(1).sheet;old.campaign=s.campaign;old.campaign.completed=511;old.campaign.storyTracks={0,0,9};old.campaign.storyLastApproach=2;old.campaign.storyCollateral=3;old.campaign.victorious=true;
    auto fresh=claimCampaignAward(old,old.campaign);assert(fresh.level==1&&fresh.campaignAwards==old.campaignAwards+1&&!fresh.campaign.active);
    assert(fresh.campaign.legacyArc==2&&fresh.campaign.legacyEnding==2&&fresh.campaign.legacyScars==3);
    Session next(555);next.add(1,"Returning");next.command(1,{{"op","configure"},{"profile",fresh}});
    auto sequel=campaignMission(next,0);assert(sequel.storyArc==5&&sequel.legacyArc==2&&storyContinuity(sequel).starts_with("AFTERMATH:"));
    auto root=std::filesystem::temp_directory_path()/"mecha-story-roundtrip.json";
    fresh.campaign=restartCampaign(fresh.campaign);saveProfile(root,fresh,"Returning");std::ifstream in(root);Json document;in>>document;assert(document["schema"]==9);
    auto loaded=loadProfile(root);assert(Json(loaded.campaign)==Json(fresh.campaign));std::filesystem::remove(root);
  }
  {
    const std::array<std::pair<int,const char *>,4> roles={{{0,"mecha_recruit"},{6,"mecha_ace"},{2,"aeon_diode"},{3,"kaiju_drone"}}};
    for(auto [arc,source]:roles){
      int spec=-1;for(size_t i=0;i<enemyCatalog().size();++i)if(std::string_view(enemySpec(i).id)==source)spec=int(i);assert(spec>=0);
      MissionRecipe recipe;recipe.storyArc=arc;auto id=storyEnemyModel(&recipe,spec,source);assert(id.starts_with("story_"));
      World w;w.targets.clear();Target t;applyOriginalModel(t,source);t.active=true;t.health=20;w.targets.push_back(t);
      Enemy enemy;enemy.spec=spec;std::array<Enemy,1> actors{enemy};
      syncManualEnemyModels(w,actors,1,&recipe);assert(w.targets[0].generatorAsset==id);auto bound=w.targets[0].mesh;
      syncManualEnemyModels(w,actors,1,&recipe);assert(w.targets[0].mesh==bound);
      auto *rig=enemyAnimationRig(std::filesystem::path(MECHA_ASSETS)/"models",id,bound->size());assert(rig);
      bool animated=false;auto still=poseEnemy(*rig,0,0,0,0,0,0);
      for(int step=0;step<=36;++step){auto pose=poseEnemy(*rig,step*.5f,4,.4f,.2f,.08f,step/20.f);
        for(size_t i=0;i<rig->parts.size();++i){const auto &part=rig->parts[i];assert(valid(pose.parts[i].orientation));
          animated=animated||length(pose.parts[i].offset-still.parts[i].offset)>.0001f||length(rotate(pose.parts[i].orientation,{0,0,1})-rotate(still.parts[i].orientation,{0,0,1}))>.0001f;
          for(auto [first,count]:part.ranges)for(size_t k=first;k<first+count;++k){auto point=animatedPoint(pose.parts[i],(*bound)[k].position);assert(std::isfinite(length(point)));if(part.ground||part.joint==EnemyJoint::Foot)assert(point.y>=rig->floor-.001f);}
        }
      }
      assert(animated);
    }
  }
  std::cout<<"216 story offers,24 finales,48 source families,10 objectives,history,legacy,save and network context verified; maximum briefing rows "<<maximumRows<<"; maximum debrief rows "<<maximumDebriefRows<<"\n";
}
