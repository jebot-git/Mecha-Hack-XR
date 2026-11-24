#include "rpg/mission_board.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <regex>
using namespace iron;
using namespace iron::rpg;
static void click(Frontend &f, Input &input) {
  f.update(input, .016f, false, false);
  f.update(input, .016f, true, false);
  f.update(input, .016f, false, false);
}
int main() {
  const std::regex gmHook(R"(\bGM\b)");
  GameSettings settings;
  auto board = generateMissionBoard(42, settings);
  auto repeated = generateMissionBoard(42, settings);
  assert(board.size() == 9);
  std::set<uint32_t> seeds;
  for (int slot=0; slot<missionBoardSize; ++slot) {
    assert(Json(board[slot]) == Json(repeated[slot]));
    seeds.insert(board[slot].seed);
    Session game(42);
    game.add(1,"Pilot");
    game.command(1, {{"op","settings"},{"settings",settings}});
    const auto offered=generateMissionBoard(game);
    game.command(1, missionSelectionCommand(slot));
    assert(game.selectedMission == slot);
    game.command(1, {{"op","ready"},{"value",true}});
    assert(game.phase == Phase::Briefing);
    assert(Json(game.mission.recipe) == Json(offered[slot]));
    game.command(1, {{"op","deploy"}});
    assert(game.phase == Phase::Combat);
    assert(Json(game.mission.recipe) == Json(offered[slot]));
    Session observer;
    observer.readSnapshot(game.snapshot());
    assert(observer.selectedMission == slot);
    assert(Json(observer.mission.recipe) == Json(offered[slot]));
  }
  assert(seeds.size()==9);
  for (int bad : {-1,9,99}) {
    bool rejected=false;
    try { missionSelectionCommand(bad); } catch(const std::out_of_range&) { rejected=true; }
    assert(rejected);
  }
  settings.theme=7;settings.objective=3;settings.location=9;settings.enemyCount=2;
  auto custom=generateMissionBoard(42,settings);
  for(const auto& card:custom) {
    assert(card.theme==7 && card.objective==3 && card.location==9 && card.enemies.size()==2);
  }
  Session edited(42);edited.add(1,"Pilot");
  edited.command(1,missionSelectionCommand(8));
  edited.command(1,{{"op","settings"},{"settings",settings}});
  assert(edited.selectedMission==8);
  edited.command(1,{{"op","ready"},{"value",true}});
  assert(Json(edited.mission.recipe)==Json(custom[8]));
  Frontend f;
  f.missionCards=missionCards(board);
  f.screen=Screen::Missions;
  f.campaign.completed=0; // No Iron Soldier progression is required.
  assert(f.missionCount()==9);
  for(int i=0;i<9;++i)assert(f.missionAvailable(i));
  assert(!f.missionAvailable(-1)&&!f.missionAvailable(9));
  Input input;input.focused=true;input.headValid=true;
  input.forward=-1;f.update(input,.016f,false,false);assert(f.mission==1);
  f.update(input,.016f,false,false);assert(f.mission==1); // Held stick cannot skip rows.
  input.forward=0;f.update(input,.016f,false,false);
  input.forward=1;f.update(input,.016f,false,false);assert(f.mission==0);
  input.forward=0;f.update(input,.016f,false,false);
  input.forward=1;f.update(input,.016f,false,false);assert(f.mission==8);
  input.forward=0;f.update(input,.016f,false,false);
  input.forward=-1;f.update(input,.016f,false,false);assert(f.mission==0);
  input.forward=0;f.update(input,.016f,false,false);
  input.steer=-1;f.update(input,.016f,false,false);assert(f.mission==8);
  input.steer=0;f.update(input,.016f,false,false);
  click(f,input);assert(f.screen==Screen::Briefing&&f.mission==8);
  assert(f.message=="MENU: EQUIPMENT / R CLICK: MISSIONS");
  click(f,input);assert(f.screen==Screen::Loadout&&f.mission==8);
  f.update(input,.016f,false,true);assert(f.screen==Screen::Briefing);
  auto cards=missionCards(board);
  assert(cards.size()==9);
  for(auto& card:cards)assert(!card.title.empty()&&!card.summary.empty()&&card.briefing.size()==8);
  // Each set offers distinct activities and sites rather than repeated contracts.
  std::set<int> objectives,themes,locations,tactics;
  for(const auto &recipe:board){objectives.insert(recipe.objective);themes.insert(recipe.theme);locations.insert(recipe.location);tactics.insert(recipe.tactic);}
  assert(objectives.size()==9&&themes.size()==9&&locations.size()==9&&tactics.size()==9);
  assert(missionBoardSeed(42,0)==42);
  assert(missionBoardSeed(42,1)!=missionBoardSeed(42,2));
  auto refreshed=generateMissionBoard(42,GameSettings{},1,1);
  auto refreshedAgain=generateMissionBoard(42,GameSettings{},1,1);
  for(int slot=0;slot<9;++slot){
    assert(Json(refreshed[slot])==Json(refreshedAgain[slot]));
    assert(refreshed[slot].seed!=board[slot].seed);
    assert(Json(refreshed[slot])==Json(generateMission(missionBoardSeed(42,1),unsigned(slot),GameSettings{},1)));
  }
  std::set<int> allEnemies,allClimates,allOpeners,allObjectives,allLocations,allTactics;
  GameSettings mixed;mixed.enemyCount=12;
  for(uint32_t revision=0;revision<32;++revision)for(const auto &recipe:generateMissionBoard(713,mixed,10,revision)){
    allClimates.insert(recipe.environment);allOpeners.insert(recipe.opener);
    allObjectives.insert(recipe.objective);allLocations.insert(recipe.location);
    assert(recipe.tactics.size()==recipe.enemies.size());
    assert(recipe.tactics.front()==recipe.tactic);
    for(int enemy:recipe.enemies)allEnemies.insert(enemy);
    for(int tactic:recipe.tactics){assert(tactic>=0&&tactic<10);allTactics.insert(tactic);}
    for(const auto &line:briefing(recipe))assert(line.find("SEED")==std::string::npos&&!std::regex_search(line,gmHook)&&line.find("HOST:")==std::string::npos);
  }
  assert(allEnemies.size()==enemyCatalog().size()&&allClimates.size()==8&&allOpeners.size()==6);
  assert(allObjectives.size()==10&&allLocations.size()==10&&allTactics.size()==10);
  GameSettings roster;roster.theme=6;roster.enemyCount=7;
  auto distinct=generateMission(42,2,roster,1);
  assert(std::set<int>(distinct.enemies.begin(),distinct.enemies.end()).size()==7);
  roster.enemy=3;roster.tactic=8;
  auto forced=generateMission(42,0,roster,10);
  for(size_t i=0;i<forced.enemies.size();++i)assert(forced.enemies[i]==3&&forced.tactics[i]==8);
  randomiseEnemies(roster);
  assert(roster.enemy==-1&&roster.theme==-1&&roster.tactic==-1&&roster.enemyCount==7);
  Session hosted(42);hosted.add(1,"Host");hosted.add(2,"Guest");
  bool rejected=false;
  try{hosted.command(2,missionSelectionCommand(8));}catch(const std::exception&){rejected=true;}
  assert(rejected&&hosted.selectedMission==0);
  std::cout << "Fresh diverse contracts, contextual rosters, full catalog coverage and matching deployment verified\n";
}
