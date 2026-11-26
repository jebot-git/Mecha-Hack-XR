#include "rpg/preparation.hpp"
#include "rpg/menu.hpp"
#include "rpg/network.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <cassert>
#include <iostream>
using namespace iron;using namespace iron::rpg;
int main(){
 Session lobby;lobby.add(1,"Host");lobby.add(2,"Guest");Dice dice{712};Preparation p;p.sheet=createSheet(Chassis::Striker,PilotKind::Engineer,dice);
 for(int chassis=0;chassis<int(Chassis::Count);++chassis){
  for(int pilot=0;pilot<int(PilotKind::Count);++pilot){
   Preparation draft;draft.sheet=createSheet(Chassis(chassis),PilotKind(pilot),dice);
   assert(draft.rowCount()==4&&int(draft.sheet.pilot)<playablePilotCount);
   assert(draft.sheet.modules.empty()&&draft.sheet.equipment[LeftHip]==Gear::Repair);
   assert(draft.sheet.inventory[size_t(Gear::Repair)]==1&&draft.sheet.repairKits==1);
   const auto originalPilot=draft.sheet.pilot;
   draft.selected=1;
   for(int n=0;n<playablePilotCount;++n){draft.edit(1);assert(int(draft.sheet.pilot)<playablePilotCount);}
   assert(draft.sheet.pilot==originalPilot);
   draft.selected=draft.rowCount()-1;draft.confirm();
   lobby.command(2,{{"op","configure"},{"profile",draft.sheet}});
   assert(lobby.member(2).sheet.attributes==draft.sheet.attributes&&lobby.member(2).sheet.created);
   assert(Json(draft.sheet).get<Sheet>().pilotBonusChoice==draft.sheet.pilotBonusChoice);
  }
 }
 {
  const auto path=std::filesystem::temp_directory_path()/("mecha-manual-pilot-"+std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count())+".json");
  Server server("127.0.0.1",0,713);Client visitor("127.0.0.1:"+std::to_string(server.port()),"Visitor");
  auto waitFor=[&](auto predicate){
   const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   while(!predicate()&&std::chrono::steady_clock::now()<stop){
    visitor.update(.016f);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }
   assert(predicate());
  };
  waitFor([&]{return visitor.id!=0;});
  for(int i=0;i<4;++i){
   auto manual=createSheet(Chassis(4+i),PilotKind(4+i),dice);
   manual.pilotBonusChoice=std::max(0,pilotBonusChoices(manual.pilot)-1);
   normalizeCombatProfile(manual);finishCreation(manual);
   if(i==1){manual.repairKits=0;manual.inventory[size_t(Gear::Repair)]=0;}
   saveProfile(path,manual,"Visitor");
   // Simulate a saved pre-retirement ID with its existing progression.
   Json legacy;{std::ifstream file(path);file>>legacy;}
   legacy["sheet"]["pilot"]=4+i;
   {std::ofstream file(path);file<<legacy;}
   auto saved=loadProfile(path);
   assert(saved.pilot==manual.pilot&&saved.inventory==manual.inventory);
   assert(saved.attributes==manual.attributes&&saved.pilotBonusChoice==manual.pilotBonusChoice);
   visitor.command({{"op","configure"},{"profile",saved}});
   waitFor([&]{return std::any_of(visitor.session.members.begin(),visitor.session.members.end(),[&](const Member &m){return m.id==visitor.id&&m.sheet.chassis==manual.chassis;});});
   const auto &remote=visitor.session.member(visitor.id).sheet;
   assert(remote.pilot==manual.pilot&&remote.attributes==manual.attributes&&remote.pilotBonusChoice==manual.pilotBonusChoice);
   assert(server.session.member(visitor.id).sheet.chassis==manual.chassis);
  }
  auto draft=createSheet(Chassis::Titan,PilotKind::Commander,dice);
  draft.modules={Module::RetractableShield};draft.starterModule=draft.modules.front();
  draft.equipment[RightHip]=moduleGear(draft.starterModule);
  draft.repairKits=draft.inventory[size_t(Gear::Repair)]=0;
  draft.credits=39;saveProfile(path,draft,"Draft");
  auto resumed=loadProfile(path);
  assert(!resumed.created&&resumed.modules.empty()&&resumed.credits==39&&resumed.starterModule==Module::Count);
  assert(resumed.equipment[RightHip]==Gear::Empty&&resumed.equipment[LeftHip]==Gear::Repair&&resumed.repairKits==1);
  std::filesystem::remove(path);
 }
 assert(p.rowCount()==4);auto original=p.sheet.attributes;p.selected=0;p.edit(1);assert(p.sheet.attributes!=original);
 p.selected=1;p.edit(1);assert(p.sheet.pilot==PilotKind::Maverick);p.selected=3;p.confirm();assert(p.sheet.created&&p.sheet.level==1);
 auto sheet=p.sheet;p.sheet.owned[size_t(Gear::Rifle)]=2;p.sheet.inventory[size_t(Gear::Repair)]=4;for(int slot=0;slot<MountCount;++slot){p.selectedGear=int(slot<2?Gear::Rifle:Gear::Repair);if(slot==1)p.sheet.owned[size_t(Gear::Rifle)]=2;p.mount(slot);assert(p.sheet.equipment[slot]==Gear(p.selectedGear));p.mount(slot,true);assert(p.sheet.equipment[slot]==Gear::Empty);}
 for(bool host:{false,true}){
  auto count=p.rowCount(host);assert(count==5);
  for(int row=0;row<count;++row){p.selected=row;auto lines=p.lines(lobby,host?1:2,"");assert(lines.size()<=12);for(const auto &line:lines)for(const char *obsolete:{"GM","TABLE","RANDOM","QUALITY:","HOSTILES:","FACTION:","WORLD:","MISSION LENGTH","REFRESH"})assert(line.find(obsolete)==std::string::npos);}
 }
 // Opening each workshop and navigating its controls must preserve the fitted sheet.
 for(int i=1;i<=int(Workshop::Refine);++i){
   p.openEquipment();p.selected=i-1;
   auto saved=Json(p.sheet);
   assert(p.tradeCommand().is_null()&&p.refillCommand().is_null());
   p.confirm();assert(p.workshop==Workshop(i));
   for(int n=0;n<p.rowCount();++n){
     p.navigate(0,1);p.navigate(1,0);
     auto view=p.loadoutPanel(p.sheet,-1,"");
     assert((view.columns==0)==(p.workshop==Workshop::Equip));
     if(p.workshop!=Workshop::Equip)assert(view.selectedSlot==-1&&p.refillCommand().is_null());
   }
   assert(p.back()&&p.selected==i-1&&Json(p.sheet)==saved);
 }
 {
   Preparation editor;editor.sheet=sheet;editor.openEquipment();
   const auto saved=editor.sheet.equipment;
   editor.confirm();assert(editor.workshop==Workshop::Equip);
   editor.navigate(0,1);editor.navigate(0,1);
   const int slot=equipmentPanelOrder[editor.selected];assert(slot==LeftHand);
   editor.edit(1);assert(editor.sheet.equipment==saved);
   editor.back();editor.confirm();editor.navigate(0,1);editor.navigate(0,1);
   assert(editor.selectedGear==int(saved[slot])&&!editor.confirm());
   while(editor.selectedGear!=int(Gear::Empty))editor.edit(1);
   assert(editor.confirm()&&editor.sheet.equipment[slot]==Gear::Empty);
   editor.back();editor.selected=int(Workshop::Armour)-1;editor.confirm();
   const auto armour=editor.sheet.armor;
   editor.navigate(1,0);editor.back();assert(editor.sheet.armor==armour);
   editor.confirm();assert(!editor.confirm()&&editor.sheet.armor==armour);
   editor.selected=0;assert(editor.confirm()&&editor.sheet.armor==Armor::None);
 }
 p.sheet.credits=modulePurchaseCost;p.workshop=Workshop::Install;p.selected=0;
 auto before=p.sheet.modules.size();p.confirm();assert(p.sheet.modules.size()==before+1&&p.sheet.credits==0);
 p.workshop=Workshop::Equip;p.selected=0;for(int n=0;n<int(Gear::Count)*2;++n){p.edit(1);assert(playerEquipment(Gear(p.selectedGear)));}
 p.inventory=true;p.selected=0;auto stock=p.lines(lobby,1,"");
 assert(stock.front()=="INVENTORY"&&p.rowCount()>0);
 auto unchanged=Json(p.sheet);p.edit(1);p.confirm();assert(Json(p.sheet)==unchanged);
 p.inventory=false;
 p.trading=true;
 p.selectedGear=int(Gear::Rifle);p.sheet.inventory[size_t(Gear::Scrap)]=5;
 p.selected=p.rowCount()-3;p.selectedCopy=0;p.edit(1);
 assert(p.selectedWeaponCopy()==1);
 ++p.selected;p.edit(1);
 auto order=p.tradeCommand();
 assert(order.at("action")=="tune"&&order.at("copy")==1&&order.at("stat")==int(WeaponStat::Heat));
 assert(p.equipmentCommand(RightHand).at("copy")==1);
 {
  Preparation fitted;
  fitted.sheet=sheet;fitted.workshop=Workshop::Equip;
  fitted.sheet.credits=400;
  equipGear(fitted.sheet,LeftShoulder,Gear::Repair);
  fitted.sheet.inventory[size_t(Gear::Repair)]=0;
  equipGear(fitted.sheet,LeftShoulder,Gear::Empty);
  equipGear(fitted.sheet,LeftHip,Gear::Empty);
  fitted.selected=0;
  fitted.selectedGear=int(Gear::Empty);
  for(int n=0;n<int(Gear::Count)&&fitted.selectedGear!=int(Gear::Repair);++n)fitted.edit(1);
  assert(fitted.selectedGear==int(Gear::Repair));
  fitted.mount(LeftShoulder);
  assert(fitted.sheet.equipment[LeftShoulder]==Gear::Repair&&fitted.sheet.inventory[size_t(Gear::Repair)]==0);
  auto selectMount=[&](int slot){
   for(int row=0;row<fitted.rowCount();++row){
    fitted.selected=row;
    if(fitted.loadoutPanel(fitted.sheet,-1,"").selectedSlot==slot)return;
   }
   throw std::runtime_error("Equipment mount missing");
  };
  selectMount(LeftShoulder);
  const auto panel=fitted.loadoutPanel(fitted.sheet,-1,"");
  assert(panel.slots[LeftShoulder].detail=="DEPLETED"&&panel.action.starts_with("REFILL +1"));
  const auto refill=fitted.refillCommand();
  assert(refill.at("slot")==LeftShoulder&&refill.at("gear")=="repair");
  auto &guest=lobby.member(2);
  guest.sheet=fitted.sheet;
  const auto originalEquipment=guest.sheet.equipment;
  lobby.playerCommand(2,refill);
  assert(guest.sheet.equipment==originalEquipment&&guest.sheet.inventory[size_t(Gear::Repair)]==1&&
         guest.sheet.repairKits==1&&guest.sheet.credits==400-gearInfo(Gear::Repair).cost);
  Session replica;replica.readSnapshot(lobby.snapshot());
  assert(replica.member(2).sheet.inventory[size_t(Gear::Repair)]==1&&replica.member(2).sheet.equipment==originalEquipment);
  fitted.sheet=guest.sheet;fitted.workshop=Workshop::Frame;
  for(int row=0;row<fitted.rowCount();++row){
   fitted.selected=row;
   if(!fitted.tradeCommand().is_null())break;
  }
  const auto install=fitted.tradeCommand();
  assert(install.at("action")=="frame_upgrade");
  const auto module=moduleRead(install.at("module"));
  const int oldCount=fittedModuleCount(guest.sheet);
  const int oldCredits=guest.sheet.credits;
  const int oldScrap=guest.sheet.inventory[size_t(Gear::Scrap)];
  lobby.playerCommand(2,install);
  assert(guest.sheet.has(module)&&frameUpgrade(module)&&fittedModuleCount(guest.sheet)==oldCount&&
         guest.sheet.credits==oldCredits-frameUpgradeCost&&guest.sheet.inventory[size_t(Gear::Scrap)]==oldScrap&&
         guest.sheet.equipment==originalEquipment);
  replica.readSnapshot(lobby.snapshot());
  assert(replica.member(2).sheet.has(module));
  auto rejected=[&](const Json &command){
   const auto saved=Json(guest.sheet);
   bool failed=false;
   try{lobby.playerCommand(2,command);}catch(const std::exception &){failed=true;}
   assert(failed&&Json(guest.sheet)==saved);
  };
  rejected(install);
  auto stale=refill;stale["slot"]=RightHip;rejected(stale);
  guest.sheet.credits=0;rejected(refill);
  guest.sheet.credits=400;guest.sheet.inventory[size_t(Gear::Repair)]=99;rejected(refill);
  guest.sheet.inventory[size_t(Gear::Repair)]=1;lobby.phase=Phase::Combat;rejected(refill);
 }
 {
  Session shop;shop.add(1,"Atlas");
  auto &pilot=shop.member(1);
  Dice creation{77};
  pilot.sheet=createSheet(Chassis::Titan,PilotKind::Engineer,creation);
  finishCreation(pilot.sheet);
  pilot.sheet.inventory[size_t(Gear::Scrap)]=16;
  pilot.sheet.campaignAwards=2;
  pilot.sheet.owned[size_t(Gear::Rifle)]=1;
  const int baseHull=pilot.sheet.maxHp;
  auto refine=[&](PilotRefinement stat,bool award){
   return Json{{"op","trade"},{"action","refine_pilot"},{"stat",int(stat)},{"award",award}};
  };
  auto tune=[&](WeaponStat stat,bool award){
   return Json{{"op","trade"},{"action","tune"},{"gear","rifle"},{"copy",0},{"stat",int(stat)},{"award",award}};
  };
  Preparation purchase;
  purchase.sheet=pilot.sheet;purchase.workshop=Workshop::Refine;
  bool pilotAward=false,weaponAward=false;
  for(int row=0;row<purchase.rowCount();++row){
   purchase.selected=row;
   const auto order=purchase.tradeCommand();
   if(order.is_null()||order.at("action")!="refine_pilot"||!order.at("award").get<bool>())continue;
   pilotAward=purchase.loadoutPanel(purchase.sheet,-1,"").action.starts_with("REFINE / 1 AWARD");
  }
  purchase.trading=true;
  for(int row=0;row<purchase.rowCount();++row){
   purchase.selected=row;
   const auto order=purchase.tradeCommand();
   weaponAward|=!order.is_null()&&order.at("action")=="tune"&&order.at("award").get<bool>();
  }
  assert(pilotAward&&weaponAward);
  auto rejected=[&](const Json &order){
   const auto saved=Json(pilot.sheet);
   bool failed=false;
   try{shop.playerCommand(1,order);}catch(const std::exception &){failed=true;}
   assert(failed&&Json(pilot.sheet)==saved);
  };
  shop.playerCommand(1,refine(PilotRefinement::Hull,false));
  assert(pilot.sheet.maxHp==baseHull+1&&pilot.sheet.hp==baseHull+1&&pilot.sheet.maxAp==8&&
         pilot.sheet.inventory[size_t(Gear::Scrap)]==10&&pilotRefinementCost(pilot.sheet)==10);
  shop.playerCommand(1,refine(PilotRefinement::Mobility,true));
  assert(pilotRefinementCount(pilot.sheet)==2&&pilot.sheet.attributes[1]==10&&
         pilot.sheet.inventory[size_t(Gear::Scrap)]==10&&availableCampaignAwards(pilot.sheet)==1);
  shop.playerCommand(1,tune(WeaponStat::Handling,true));
  assert(weaponTuneCount(weaponCopy(pilot.sheet,Gear::Rifle,0))==1&&
         pilot.sheet.inventory[size_t(Gear::Scrap)]==10&&availableCampaignAwards(pilot.sheet)==0&&
         pilot.sheet.campaignAwards==2);
  rejected(tune(WeaponStat::Heat,true));
  rejected(refine(PilotRefinement::Power,false));
  shop.playerCommand(1,tune(WeaponStat::Heat,false));
  rejected(tune(WeaponStat::Cycle,false));
  Session remote;remote.readSnapshot(shop.snapshot());
  assert(remote.member(1).sheet.refinements==pilot.sheet.refinements&&
         remote.member(1).sheet.campaignAwardsSpent==2);

  auto separate=createSheet(Chassis::Scout,PilotKind::Commander,creation);
  finishCreation(separate);
  separate.inventory[size_t(Gear::Scrap)]=16;
  refinePilot(separate,PilotRefinement::System);
  assert(!canRefinePilot(separate,PilotRefinement::System));
  refinePilot(separate,PilotRefinement::Hull);
  assert(separate.inventory[size_t(Gear::Scrap)]==0);
  auto loaded=Json(separate).get<Sheet>();
  normalizeCombatProfile(loaded);validate(loaded);
  assert(loaded.refinements==separate.refinements&&loaded.attributes==separate.attributes&&loaded.maxHp==separate.maxHp);
  auto legacy=Json(separate);
  legacy.erase("refinements");legacy.erase("campaignAwardsSpent");
  auto imported=legacy.get<Sheet>();normalizeCombatProfile(imported);validate(imported);
  assert(pilotRefinementCount(imported)==0&&imported.campaignAwardsSpent==0);
 }
 {
  auto stock=createSheet(Chassis::Striker,PilotKind::Engineer,dice);
  finishCreation(stock);stock.credits=600;
  constexpr Gear gear=Gear::MicroMissiles;
  purchase(stock,gear);
  assert(stock.credits==600-modulePurchaseCost&&equipmentRarity(stock,gear,0)==Rarity::Common);
  recoverWeapon(stock,gear,{-1,0,1},Rarity::Rare);
  equipGear(stock,LeftHip,gear,1);
  validate(stock);
  const auto stats=weaponStats(stock,gear,equippedWeaponCopy(stock,LeftHip));
  assert(std::abs(stats.cooldown-gearInfo(gear).cooldown*.85f)<.0001f&&
         std::abs(stats.heat-gearInfo(gear).stress*.82f)<.0001f&&
         std::abs(stats.range-gearInfo(gear).range*1.21f)<.001f);
  bool refused=false;
  try{tuneWeapon(stock,gear,1,WeaponStat::Heat);}catch(const std::exception&){refused=true;}
  assert(refused&&weaponTuneCount(weaponCopy(stock,gear,1))==0);
  stock=Json(stock).get<Sheet>();sellEquipment(stock,gear,0);validate(stock);
  assert(equippedWeaponCopy(stock,LeftHip)==0&&equipmentRarity(stock,gear,0)==Rarity::Rare);
  purchase(stock,gear);destroyModule(stock,Module::MicroMissiles);validate(stock);
  assert(stock.equipment[LeftHip]==Gear::Empty&&stock.owned[size_t(gear)]==1&&
         equipmentRarity(stock,gear,0)==Rarity::Common);
 }
 assert(!playerEquipment(Gear::ChromePlating)&&!playerEquipment(Gear::CockpitTrinket)&&!playerEquipment(Gear::CargoHullpack));
 std::cout<<"Creation, owned equipment, earned modules, symmetric host/guest menus and useful shop stock passed\n";
}
