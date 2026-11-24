#include "rpg/preparation.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
namespace {
Preparation fixture(){Dice d{1931};Preparation p;p.sheet=createSheet(Chassis::Striker,PilotKind::Engineer,d);finishCreation(p.sheet);return p;}
int rowContaining(Preparation &p,const Session &lobby,const std::string &text){
  for(int i=0;i<p.rowCount(false);++i){p.selected=i;for(const auto &line:p.lines(lobby,2,""))if(line.starts_with("> ")&&line.find(text)!=std::string::npos)return i;}
  return -1;
}
bool pageContains(const std::vector<std::string> &lines,const std::string &text){return std::any_of(lines.begin(),lines.end(),[&](const auto &line){return line.find(text)!=std::string::npos;});}
}
int main(){
  setOriginalAssetRoot(MECHA_ASSETS);
  Session lobby;lobby.add(1,"Host");lobby.add(2,"Guest");
  auto pilot=fixture();equipQuality(pilot.sheet,LeftHand,Quality::Deadly);auto quality=pilot.sheet.qualities[LeftHand];
  for(int row=0;row<pilot.rowCount(false);++row){pilot.selected=row;for(const auto &line:pilot.lines(lobby,2,""))assert(line.find(" QUALITY:")==std::string::npos);}
  pilot.selected=0;pilot.edit(1,false);assert(pilot.sheet.qualities[LeftHand]==quality);
  auto trade=fixture();trade.trading=true;trade.selectedGear=int(Gear::Rifle);
  assert(trade.sheet.inventory[size_t(Gear::Scrap)]==0&&trade.rowCount()==10);
  trade.selected=trade.rowCount()-1;
  assert(trade.tradeCommand().is_null());trade.confirm();assert(!trade.trading);
  trade.trading=true;trade.sheet.inventory[size_t(Gear::Scrap)]=10;
  assert(trade.rowCount()==10);trade.selected=rowContaining(trade,lobby,"CRAFT / ");
  auto request=trade.tradeCommand();assert(request.at("op")=="trade"&&request.at("action")=="craft"&&request.at("gear")=="rifle");
  assert(pageContains(trade.lines(lobby,2,""),"CRAFT / "));
  // The menu requests one authoritative transaction; local confirmation never
  // spends salvage a second time while waiting for the host's updated sheet.
  auto before=Json(trade.sheet);trade.confirm();assert(trade.trading&&Json(trade.sheet)==before);
  lobby.member(2).sheet=trade.sheet;auto rifle=trade.sheet.owned[size_t(Gear::Rifle)];
  lobby.command(2,request);trade.sheet=lobby.member(2).sheet;
  assert(trade.sheet.owned[size_t(Gear::Rifle)]==rifle+1&&trade.sheet.inventory[size_t(Gear::Scrap)]<10);
  trade.selected=trade.rowCount()-1;assert(trade.tradeCommand().is_null());trade.confirm(false);assert(!trade.trading);
  trade.trading=true;trade.selectedGear=int(Gear::ChromePlating);assert(trade.rowCount()==7);
  trade.selected=6;assert(trade.tradeCommand().is_null());trade.confirm();assert(!trade.trading);
  // Crafting keeps its row when the last scrap is spent, so focus stays put.
  trade.trading=true;trade.selectedGear=int(Gear::Repair);trade.sheet.inventory[size_t(Gear::Scrap)]=0;
  assert(rowContaining(trade,lobby,"CRAFT / ")==6);
  trade.selected=trade.rowCount()-1;trade.confirm();assert(!trade.trading);
  const auto rare=weaponVariant(Gear::Rifle,Rarity::Rare);
  recoverWeapon(trade.sheet,rare,{});
  trade.selectedGear=int(rare);trade.openTrader();
  for(int n=0;n<int(Gear::Count);++n){
    trade.edit(1);const auto gear=Gear(trade.selectedGear);
    assert(weaponRarity(gear)==Rarity::Common);
    trade.selected=1;assert(trade.tradeCommand().at("action")=="buy");trade.selected=0;
  }
  trade.inventory=true;trade.trading=false;trade.selected=rowContaining(trade,lobby,gearName(rare));
  assert(trade.selected>=0);trade.confirm();
  assert(trade.trading&&!trade.inventory&&trade.selectedGear==int(rare));
  trade.selected=1;assert(trade.tradeCommand().at("action")=="sell");
  trade.selected=2;assert(trade.tradeCommand().at("action")=="tune");
  for(auto rarity:{Rarity::Uncommon,Rarity::Rare}){
    const auto id=gearInfo(weaponVariant(Gear::Rifle,rarity)).id;
    for(const char *action:{"buy","craft"}){
      bool rejected=false;try{lobby.command(2,{{"op","trade"},{"action",action},{"gear",id}});}
      catch(const std::runtime_error &){rejected=true;}
      assert(rejected);
    }
  }
  Json preset={{"modules",Json::array()},{"equipment",{{"left_hand",gearInfo(rare).id},{"right_hand","empty"},
    {"left_shoulder","empty"},{"right_shoulder","empty"},{"left_hip","empty"},{"right_hip","empty"}}}};
  auto fresh=fixture().sheet;
  bool unavailable=false;
  try{customize(fresh,preset);}catch(const std::runtime_error &){unavailable=true;}
  assert(unavailable&&fresh.owned[size_t(rare)]==0);
  recoverWeapon(fresh,rare,{});customize(fresh,preset);
  assert(fresh.owned[size_t(rare)]==1&&fresh.equipment[LeftHand]==rare);
  std::cout<<"Earned qualities preserved; authoritative scrap crafting and pagination verified\n";
}
