#include "rpg/manual_missions.hpp"
#include "presentation.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <set>
using namespace iron;using namespace iron::rpg;
int main(){
  setOriginalAssetRoot(MECHA_ASSETS);
  const auto catalog=manualMissionCatalog();assert(catalog.size()==48);
  std::set<std::string> ids;for(const auto &entry:catalog){
    assert(ids.insert(entry.id).second&&entry.page>=12&&entry.page<=55);
    for(int objective:entry.objectives)assert(objective>=0&&objective<10);
    for(const char *task:entry.tasks){std::string text=task;assert(!text.empty()&&text.find("ON FOOT")==std::string::npos&&text.find("DISEMBARK")==std::string::npos);}
  }
  assert(instantActions().size()==20&&instantFoci().size()==20);
  assert(manualComplications().size()==6&&manualOpportunities().size()==6);
  assert(manualBriefings().size()==10&&planetoidFeatures().size()==12&&planetoidHazards().size()==12);
  assert(salvageForms().size()==12&&salvageFeatures().size()==12);
  assert(mechaPaints().size()==12&&mechaFeatures().size()==12&&kaijuLooks().size()==6&&kaijuFeatures().size()==6);
  assert(friendlyChatter().size()==20&&enemyChatter().size()==20&&techPrefixes().size()==12&&techSuffixes().size()==12);
  assert(criticalCockpitEffects().size()==10&&criticalExteriorEffects().size()==10);
  std::set<int> missions,complications,opportunities,features,hazards,styles,actions,foci,forms,salvage,inhabitants;
  GameSettings settings;settings.maps=1;settings.enemyCount=12;
  for(uint32_t revision=0;revision<256;++revision){
    auto board=generateMissionBoard(42871,settings,10,revision);
    for(const auto &recipe:board){
      assert(validManualRecipe(recipe)&&Json(Json(recipe).get<MissionRecipe>())==Json(recipe));
      assert(!manualMissionName(recipe).empty()&&!manualMissionTask(recipe).empty());
      missions.insert(recipe.manualMission);complications.insert(recipe.complication);opportunities.insert(recipe.opportunity);
      features.insert(recipe.planetoidFeature);hazards.insert(recipe.planetoidHazard);styles.insert(recipe.briefingStyle);
      actions.insert(recipe.instantAction);foci.insert(recipe.instantFocus);forms.insert(recipe.salvageForm);salvage.insert(recipe.salvageFeature);
      for(int enemy:recipe.enemies){inhabitants.insert(enemy);assert(enemySpec(enemy).hd<=encounterThreatLimit(recipe.difficulty));}
      size_t renderedRows=0;for(const auto &line:briefing(recipe))renderedRows+=missionLines(line,57).size();assert(renderedRows<=15);
      assert(briefing(recipe).size()==8&&recipe.rechargeSeconds>=18&&recipe.rechargeSeconds<=36);
      if(recipe.objective==3||recipe.objective==5)assert(recipe.objectiveCount>=3&&recipe.objectiveCount<=5);
      if(recipe.objective==4)assert(recipe.objectiveCount>=2&&recipe.objectiveCount<=4);
      // Cosmetic reads cannot consume or alter another generation stream.
      auto before=Json(recipe);for(unsigned i=0;i<3;++i){auto a=manualAppearance(recipe.seed,i,true);assert(!a.nickname.empty()&&std::isfinite(length(a.tint)));assert(a.nickname==manualAppearance(recipe.seed,i,true).nickname);manualTechnobabble(recipe.seed,i);}
      assert(Json(recipe)==before);
    }
  }
  assert(missions.size()==48&&complications.size()==6&&opportunities.size()==6&&features.size()==12&&hazards.size()==12);
  assert(styles.size()==10&&actions.size()==20&&foci.size()==20&&forms.size()==12&&salvage.size()==12);
  assert(inhabitants.size()==enemyCatalog().size());
  std::set<uint32_t> independent;
  for(unsigned channel=0;channel<=unsigned(ManualRng::Critical);++channel)independent.insert(manualRandom(723,ManualRng(channel),0));
  assert(independent.size()==10);
  auto bad=generateMission(723,0,GameSettings{},1);bad.countdownSeconds=std::numeric_limits<float>::quiet_NaN();assert(!validManualRecipe(bad));
  bad=MissionRecipe{};bad.manualMission=48;assert(!validManualRecipe(bad));bad=MissionRecipe{};bad.opportunity=6;assert(!validManualRecipe(bad));
  // Restoring an older offline/network fixture leaves supplemental effects off.
  Json old={{"seed",723},{"objective",3}};auto restored=old.get<MissionRecipe>();assert(restored.manualMission==-1&&restored.objectiveSeconds==0&&validManualRecipe(restored));
  // The workload roll creates that many actual reachable protected assets.
  for(int objective:{3,4,5}){
    GameSettings fixed;fixed.maps=1;fixed.objective=objective;fixed.location=0;fixed.environment=3;fixed.enemyCount=1;
    auto recipe=generateMission(731,unsigned(objective),fixed,5);World world;std::array<int,0> none{};
    buildMissionEnvironment(world,recipe.seed,3,500,none,0,objective,0,5);
    auto prefix=world.targets.size();MissionState state;state.recipe=recipe;addMissionAssets(world,state,500);
    assert(int(state.assets.size())==recipe.objectiveCount&&state.visited.size()==state.assets.size());
    assert(world.targets.size()>=prefix+state.assets.size());
    for(int id:state.assets){assert(id>=0&&size_t(id)<world.targets.size()-8&&world.targets[id].protectedTarget);assert(world.groundPassable(world.targets[id].origin,3.5f));}
  }
  std::cout<<"48 mech-only operational adaptations, all toolbox dimensions, independent seeded streams and physical workload verified\n";
}
