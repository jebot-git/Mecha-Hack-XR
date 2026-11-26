#include "rpg/preparation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/menu.hpp"
#include <algorithm>
#include <numeric>
#include <tuple>
namespace iron::rpg {
namespace {
const std::array<const char*,20> firstNames={"Atlas","Beacon","Cinder","Drifter","Echo","Falcon","Ghost","Hammer","Ion","Javelin","Knight","Lancer","Mantis","Nova","Onyx","Paladin","Quasar","Raven","Sentinel","Tempest"};
const std::array<const char*,20> lastNames={"Echo","Alpha","Beta","Zero","Prime","Dawn","Dusk","Steel","Spark","Horizon","Vector","Delta","Comet","Storm","Flare","Vanguard","Forge","Ember","Pulse","Cobalt"};
constexpr const char *mountNames[]={"LEFT ARM","RIGHT ARM","LEFT SHOULDER","RIGHT SHOULDER","LEFT HIP","RIGHT HIP"};
std::string oneDecimal(float value){
 const int tenths=int(std::lround(value*10.f));
 return std::to_string(tenths/10)+"."+std::to_string(tenths%10);
}
const char* armorName(Armor a){return a==Armor::Heavy?"HEAVY":a==Armor::Light?"LIGHT":"NONE";}
enum class Action {Open,Empty,Armor,Module,Frame,Refine,Mount};
struct Row {Action action;int index=0;bool award=false;};
bool saleItem(Gear gear){return playerEquipment(gear)&&gear!=Gear::Empty&&traderPrice(gear)>0&&(gearInfo(gear).module==Module::Count||gradedModule(gear));}
bool traderStock(Gear gear){return saleItem(gear)&&weaponRarity(gear)==Rarity::Common;}
bool recoveredWeaponPage(const Preparation &p){return p.trading&&weaponRarity(Gear(p.selectedGear))!=Rarity::Common;}
bool craftRow(const Preparation &p){auto gear=Gear(p.selectedGear);return saleItem(gear)&&weaponRarity(gear)==Rarity::Common&&gear!=Gear::Scrap&&(consumable(gear)||melee(gear)||ranged(gear));}
int tuneCopyRow(const Preparation &p){return 6+int(craftRow(p));}
int tradeBack(const Preparation &p){return tuneCopyRow(p)+(weaponTuning(Gear(p.selectedGear))&&p.selectedWeaponCopy()>=0?2+int(p.sheet.campaignAwards>0):0);}
bool tuneAction(const Preparation &p){return weaponTuning(Gear(p.selectedGear))&&p.selectedWeaponCopy()>=0&&p.selected>tuneCopyRow(p)&&p.selected<tradeBack(p);}
const char *refinementName(PilotRefinement stat){
 constexpr const char *names[]={"HULL","POWER","MOBILITY","SYSTEM","PRESENCE"};
 return names[size_t(stat)];
}
std::string refinementPreview(const Sheet &sheet,PilotRefinement stat){
 if(stat==PilotRefinement::Hull)
   return std::to_string(sheet.maxHp)+" TO "+std::to_string(sheet.maxHp+1)+" MAX HULL";
 const int value=sheet.attributes[size_t(stat)-1];
 const auto next=value+1;
 std::string effect;
 switch(stat){
 case PilotRefinement::Power:{
   auto refined=sheet;refined.attributes[0]=next;
   const auto change=1.f-spreadRadians(refined,Gear::Rifle,0)/spreadRadians(sheet,Gear::Rifle,0);
   effect=oneDecimal(change*100.f)+"% TIGHTER BASE SPREAD";
   break;
 }
 case PilotRefinement::Mobility:{
   const int base=value-sheet.refinements[size_t(PilotRefinement::Mobility)];
   const auto walk=[](int mobility){return std::clamp(float(mobility)/12.f,.4f,1.65f);};
   effect="WALK +"+oneDecimal((walk(next)/walk(value)-1.f)*100.f)+"% / "+
          std::to_string(std::clamp(1+base/5,1,4))+" BOOST CHARGES";
   break;
 }
 case PilotRefinement::System:
   effect="BLIND "+oneDecimal(std::clamp(72.f/value,2.f,10.f))+" TO "+
       oneDecimal(std::clamp(72.f/next,2.f,10.f))+" S / +3 M CAMO DETECTION";
   break;
 case PilotRefinement::Presence:
   effect="STUN "+oneDecimal(std::clamp(36.f/value,1.f,6.f))+" TO "+
       oneDecimal(std::clamp(36.f/next,1.f,6.f))+" S";
   break;
 default:break;
 }
 return std::to_string(value)+" TO "+std::to_string(next)+" / "+effect;
}
struct InventoryItem {Gear gear;std::string label;int copy=-1;};
const char *statName(Gear gear,WeaponStat stat){
 switch(stat){
 case WeaponStat::Cycle:return "CYCLE TIME";
 case WeaponStat::Heat:return "HEAT";
 default:return melee(gear)?"REACH":"DISPERSION";
 }
}
std::pair<Gear,int> nextEquipment(const Sheet &sheet,Gear gear,int copy,int delta){
 const int step=delta<0?-1:1;
 const int count=int(Gear::Count);
 if(equipmentCopyCount(sheet,gear)>0){
    const int stock=equipmentCopyCount(sheet,gear);
    if(copy+step>=0&&copy+step<stock)return {gear,copy+step};
 }
 for(int n=0;n<count;++n){
    gear=Gear((int(gear)+step+count)%count);
    if(!playerEquipment(gear)||gear==Gear::Scrap)continue;
    if(gear==Gear::Empty)return {gear,-1};
    const int stock=consumable(gear)?sheet.inventory[size_t(gear)]:availableEquipment(sheet,gear);
    if(stock>0||gear==Gear::Repair)return {gear,equipmentCopyCount(sheet,gear)>0?(step>0?0:equipmentCopyCount(sheet,gear)-1):-1};
 }
 return {Gear::Empty,-1};
}
std::vector<InventoryItem> stockFor(const Sheet &sheet){
 std::vector<InventoryItem> stock;
 for(int i=1;i<int(Gear::Count);++i){
  const auto gear=Gear(i);
  const int equipped=int(std::count(sheet.equipment.begin(),sheet.equipment.end(),gear));
  const int total=(consumable(gear)||gear==Gear::Scrap)?sheet.inventory[i]:std::max(sheet.owned[i],availableEquipment(sheet,gear));
  if(!playerEquipment(gear)||(total<=0&&equipped==0))continue;
  if(equipmentCopyCount(sheet,gear)>0){
    for(int copy=0;copy<equipmentCopyCount(sheet,gear);++copy){
      std::string line=std::string(rarityName(equipmentRarity(sheet,gear,copy)))+" / "+gearName(gear)+" / COPY "+std::to_string(copy+1);
      if(weaponCopyFitted(sheet,gear,copy))line+=" / MOUNTED";
      stock.push_back({gear,std::move(line),copy});
    }
    continue;
  }
  std::string line=std::string(gearName(gear))+"  x"+std::to_string(total);
  if(equipped)line+="  /  MOUNTED";
  stock.push_back({gear,std::move(line)});
 }
 for(auto armor:{Armor::Light,Armor::Heavy})
  if(sheet.ownedArmor[int(armor)])stock.push_back({Gear::Empty,
      std::string(armorName(armor))+" ARMOR"+(sheet.armor==armor?"  /  FITTED":"")});
 return stock;
}
bool upgradeAvailable(const Sheet &sheet,Module module,bool frame){
 return !retiredModule(module)&&!sheet.has(module)&&frameUpgrade(module)==frame;
}
constexpr std::array workshopNames={"EQUIPMENT","EQUIP","INSTALL","ARMOUR","FRAME","REFINE"};
constexpr std::array workshopCaptions={"","Weapons and mounts","Active modules","Armour plating","Passive upgrades","Hull and attributes"};
const std::vector<Gear>& catalogStock(){
 static const auto stock=[]{
   std::vector<Gear> items;
   for(int i=1;i<int(Gear::Count);++i)if(traderStock(Gear(i)))items.push_back(Gear(i));
   return items;
 }();
 return stock;
}
std::vector<Row> rowsFor(const Preparation &p){
 std::vector<Row> rows;
 switch(p.workshop){
 case Workshop::Home:
   for(int i=1;i<int(workshopNames.size());++i)rows.push_back({Action::Open,i});
   break;
 case Workshop::Equip:
   for(int slot:equipmentPanelOrder)rows.push_back({Action::Mount,slot});
   break;
 case Workshop::Armour:
   for(int i=0;i<3;++i)if((i==int(Armor::None)||p.sheet.ownedArmor[i])&&chassisArmorAllowed(p.sheet.chassis,Armor(i)))
      rows.push_back({Action::Armor,i});
   break;
 case Workshop::Install:case Workshop::Frame:{
   const bool frame=p.workshop==Workshop::Frame;
   if(frame||fittedModuleCount(p.sheet)<moduleLimit)
     for(int i=0;i<int(Module::Count);++i)if(upgradeAvailable(p.sheet,Module(i),frame))
       rows.push_back({frame?Action::Frame:Action::Module,i});
   break;
 }
 case Workshop::Refine:
   for(int i=0;i<int(PilotRefinement::Count);++i){
     rows.push_back({Action::Refine,i});
     if(p.sheet.campaignAwards>0)rows.push_back({Action::Refine,i,true});
   }
   break;
 }
 if(rows.empty())rows.push_back({Action::Empty});
 return rows;
}
int gridColumns(const Preparation &p){
 if(p.inventory||p.catalog)return 3;
 if(p.trading||p.workshop==Workshop::Equip)return 0;
 if(p.workshop==Workshop::Armour)return 3;
 if(p.workshop==Workshop::Refine&&p.sheet.campaignAwards==0)return 1;
 return 2;
}
void inspectMount(Preparation &p){
 const int slot=equipmentPanelOrder.at(p.selected);
 p.selectedGear=int(p.sheet.equipment[slot]);
 p.selectedCopy=std::max(0,equippedWeaponCopy(p.sheet,equipmentOwner(p.sheet,slot)));
}
std::string stockIcon(Gear gear){
 if(gear==Gear::Empty)return {};
 const auto family=weaponFamily(gear);
 const char *id=gearInfo(family).id;
 switch(family){
 case Gear::ChainCutter:id="cutter";break;
 case Gear::Scattergun:id="shotgun";break;
 case Gear::Rotary:id="gatling";break;
 case Gear::GrenadeLobber:id="auto_grenade";break;
 case Gear::RocketRack:id="rocket";break;
 case Gear::SeekerPod:id="homing";break;
 case Gear::PulseCarbine:id="rifle";break;
 case Gear::RailLance:id="rail";break;
 case Gear::ArcGrenade:case Gear::FragGrenade:id="scouting_drone";break;
 default:break;
 }
 return "icon-weapon_"+std::string(id)+"-0";
}
std::string stockStats(const Sheet &fitted,Gear gear,int copy){
 std::string detail;
 if(const auto *tuning=weaponTuning(gear)){
   const auto stats=weaponStats(fitted,gear,copy);
   const int damage=std::max(1,(damageDie(fitted)+2)/2+tuning->damageBias+2*int(heavy(gear)));
   detail=std::to_string(damage)+" HIT / "+std::to_string(int(std::lround(stats.range)))+
       " M / "+std::to_string(int(std::lround(stats.cooldown*1000)))+" MS / "+oneDecimal(stats.heat*100)+"% HEAT";
 }
 if(gradedModule(gear)){
   const auto stats=weaponStats(fitted,gear,copy);
   detail=std::string(rarityName(equipmentRarity(fitted,gear,copy)))+" / "+
     oneDecimal(stats.cooldown)+" S / "+oneDecimal(stats.heat*100)+"% HEAT";
   if(gear==Gear::LongRangeScanners)detail+=" / "+oneDecimal(5.f*stats.output)+" S TRACE";
   else if(stats.range>0)detail+=" / "+oneDecimal(stats.range)+" M";
   else detail+=" / "+std::to_string(int(std::lround(stats.output*100)))+"% OUTPUT";
 }
 if(gear==Gear::Shield)detail=gearDescription(gear);
 return detail;
}
std::pair<Gear,int> inspectedGear(const Preparation &p){
 if(!p.sheet.created)return {Gear::Empty,-1};
 if(p.inventory){
    auto stock=stockFor(p.sheet);
    if(p.selected<0||size_t(p.selected)>=stock.size())return {Gear::Empty,-1};
    return {stock[size_t(p.selected)].gear,stock[size_t(p.selected)].copy};
 }
 if(p.trading){
   if(p.catalog)return {catalogStock().at(std::clamp(p.selected,0,int(catalogStock().size())-1)),-1};
   if(recoveredWeaponPage(p))return {Gear(p.selectedGear),p.selectedWeaponCopy()};
   if(p.selected<3||(p.selected>=6&&p.selected<tradeBack(p)))
     return {Gear(p.selectedGear),p.selected==1?-1:p.selectedWeaponCopy()};
   return {Gear::Empty,-1};
 }
 const auto rows=rowsFor(p);
 if(p.selected<0||size_t(p.selected)>=rows.size())return {Gear::Empty,-1};
 const auto row=rows[size_t(p.selected)];
 if(row.action==Action::Mount)return {Gear(p.selectedGear),p.selectedWeaponCopy()};
 return {Gear::Empty,-1};
}
}
void Preparation::openEquipment(){
 workshop=Workshop::Home;selected=0;trading=inventory=catalog=false;
}
bool Preparation::back(){
 if(trading&&!catalog){
   const bool recovered=recoveredWeaponPage(*this);
   trading=!recovered;inventory=recovered;catalog=!recovered;
   selected=std::clamp(returnCursor,0,rowCount()-1);
   return true;
 }
 if(!inventory&&!trading&&workshop!=Workshop::Home){
   selected=int(workshop)-1;workshop=Workshop::Home;
   return true;
 }
 return false;
}
void Preparation::browseGear(int delta){
 const auto [gear,copy]=nextEquipment(sheet,Gear(selectedGear),selectedWeaponCopy(),delta);
 selectedGear=int(gear);selectedCopy=std::max(0,copy);
}
bool Preparation::navigate(int horizontal,int vertical,bool host){
 const int count=rowCount(host);
 selected=std::clamp(selected,0,count-1);
 const int cols=sheet.created?gridColumns(*this):0;
 if(cols){
   if(vertical){
     const int lastRow=(count-1)/cols;
     const int row=(selected/cols+vertical+lastRow+1)%(lastRow+1);
     selected=std::min(count-1,row*cols+selected%cols);
   }else if(horizontal)selected=(selected+horizontal+count)%count;
 }else if(vertical){
   selected=(selected+vertical+count)%count;
   if(sheet.created&&!trading&&!inventory&&workshop==Workshop::Equip)inspectMount(*this);
 }
 else if(horizontal)return edit(horizontal,host);
 return false;
}
void Preparation::openTrader(){
 trading=catalog=true;inventory=false;selected=0;selectedCopy=0;
 if(!chassisArmorAllowed(sheet.chassis,tradeArmor))tradeArmor=Armor::Light;
 if(!traderStock(Gear(selectedGear)))selectedGear=int(Gear::Rifle);
}
int Preparation::rowCount(bool)const{
 if(trading&&catalog)return int(catalogStock().size());
 if(recoveredWeaponPage(*this))return selectedWeaponCopy()<0?1:4+int(sheet.campaignAwards>0);
 return inventory?std::max(1,int(stockFor(sheet).size())):trading?tradeBack(*this)+1:sheet.created?int(rowsFor(*this).size()):4;}
int Preparation::selectedWeaponCopy()const{
 const auto gear=Gear(selectedGear);
 const auto stock=equipmentCopyCount(sheet,gear);
 return stock>0?std::clamp(selectedCopy,0,stock-1):-1;
}
Json Preparation::equipmentCommand(int slot,bool remove)const{
 const auto gear=remove?Gear::Empty:Gear(selectedGear);
 return {{"op","equip"},{"slot",slot},{"gear",gearInfo(gear).id},{"copy",remove?-1:selectedWeaponCopy()}};
}
Json Preparation::tradeCommand()const{
 if(catalog)return nullptr;
 if(!trading){
   if(!sheet.created||inventory)return nullptr;
   const auto rows=rowsFor(*this);
   if(selected<0||size_t(selected)>=rows.size())return nullptr;
   const auto row=rows[selected];
   if(row.action==Action::Frame)
     return {{"op","trade"},{"action","frame_upgrade"},{"module",moduleId(Module(row.index))}};
   if(row.action==Action::Refine)
     return {{"op","trade"},{"action","refine_pilot"},{"stat",row.index},{"award",row.award}};
   return nullptr;
 }
 if(recoveredWeaponPage(*this)){
   const auto gear=Gear(selectedGear);
   const int copy=selectedWeaponCopy();
   if(copy<0)return nullptr;
   if(selected==1)return {{"op","trade"},{"action","sell"},{"gear",gearInfo(gear).id},{"copy",copy}};
   if((selected==2||(selected==3&&sheet.campaignAwards>0))&&weaponTuneCount(weaponCopy(sheet,gear,copy))<weaponTuneLimit)
     return {{"op","trade"},{"action","tune"},{"gear",gearInfo(gear).id},{"copy",copy},{"stat",int(tuneStat)},{"award",selected==3}};
   return nullptr;
 }
 if((selected==1||selected==2)&&saleItem(Gear(selectedGear)))
   return {{"op","trade"},{"action",selected==1?"buy":"sell"},{"gear",gearInfo(Gear(selectedGear)).id},
           {"copy",selected==2?selectedWeaponCopy():-1}};
 if(selected==4||selected==5)return {{"op","trade"},{"action",selected==4?"buy_armor":"sell_armor"},{"armor",int(tradeArmor)}};
 if(selected==6&&craftRow(*this))return {{"op","trade"},{"action","craft"},{"gear",gearInfo(Gear(selectedGear)).id}};
 const auto gear=Gear(selectedGear);
 const auto copy=selectedWeaponCopy();
 if(tuneAction(*this)&&weaponTuneCount(weaponCopy(sheet,gear,copy))<weaponTuneLimit)
   return {{"op","trade"},{"action","tune"},{"gear",gearInfo(gear).id},{"copy",copy},{"stat",int(tuneStat)},
           {"award",selected==tuneCopyRow(*this)+2}};
 return nullptr;
}
Json Preparation::refillCommand()const{
 if(!sheet.created||trading||inventory)return nullptr;
 const auto rows=rowsFor(*this);
 if(selected<0||size_t(selected)>=rows.size()||rows[selected].action!=Action::Mount)return nullptr;
 const int slot=rows[selected].index;
 const auto gear=sheet.equipment[slot];
 if(gear!=Gear(selectedGear)||!consumable(gear)||equipmentOwner(sheet,slot)!=slot)return nullptr;
 Json request={{"op","trade"},{"action","refill"},{"slot",slot},{"gear",gearInfo(gear).id}};
 return request;
}
EquipmentPanel Preparation::loadoutPanel(const Sheet &fitted,int fieldMount,const std::string &status)const{
 const bool field=fieldMount>=0;
 EquipmentPanel panel;
 panel.title=field?"FIELD EQUIPMENT":"EQUIPMENT";
 if(!field)panel.title+=" / "+std::to_string(fitted.credits)+" CR";
 panel.status=status;
 for(int slot=0;slot<MountCount;++slot){
   const auto gear=fitted.equipment[slot];
   auto &card=panel.slots[slot];
   card.empty=gear==Gear::Empty;
   card.linked=equipmentOwner(fitted,slot)!=slot;
   card.item=gearName(gear);
   if(card.linked)card.detail="BRACED";
   else if(consumable(gear)){
     const int count=fitted.inventory[size_t(gear)];
     card.detail=count>0?std::to_string(count)+" CHARGES":"DEPLETED";
   }else if(const int copy=equippedWeaponCopy(fitted,slot);copy>=0)
     card.detail=std::string(rarityName(equipmentRarity(fitted,gear,copy)))+" / COPY "+std::to_string(copy+1);
 }
 const auto staged=Gear(selectedGear);
 const std::string item=gearName(staged);
 const int stagedCopy=selectedWeaponCopy();
 const auto fitLabel=item+(stagedCopy>=0?" / COPY "+std::to_string(stagedCopy+1):"");
 if(field){
   panel.selectedSlot=fieldMount<MountCount?fieldMount:-1;
   panel.inspection="ITEM / "+fitLabel;
   panel.detail="STICK LEFT / RIGHT: BROWSE ITEMS";
   if(panel.selectedSlot>=0){
     panel.actionAvailable=mountAllowed(staged,panel.selectedSlot)&&chassisGearAllowed(fitted.chassis,staged);
     panel.action=panel.actionAvailable?"FIT "+fitLabel:"CHOOSE AN ITEM FOR THIS MOUNT";
   }
   return panel;
 }
 panel.cursor=std::clamp(selected,0,rowCount()-1);
 panel.columns=gridColumns(*this);
 using Symbol=EquipmentPanel::Symbol;
 if(inventory||trading){
   panel.title=(inventory?"INVENTORY":"TRADE POST")+std::string(" / ")+std::to_string(fitted.credits)+" CR";
   if(inventory){
     const auto stock=stockFor(fitted);
     for(const auto &entry:stock){
       std::string caption;
       if(entry.copy>=0)caption=std::string(rarityName(equipmentRarity(fitted,entry.gear,entry.copy)))+" #"+std::to_string(entry.copy+1);
       else if(entry.gear!=Gear::Empty)caption=entry.label.substr(entry.label.find("  ")+2);
       panel.cards.push_back({entry.gear==Gear::Empty?entry.label:gearName(entry.gear),caption,stockIcon(entry.gear),entry.gear==Gear::Empty?Symbol::Plate:Symbol::Module});
     }
     if(stock.empty()){panel.inspection="NO STOCK";return panel;}
     const auto &entry=stock[panel.cursor];
     panel.inspection=entry.label;
     if(entry.gear!=Gear::Empty){
       panel.detail=stockStats(fitted,entry.gear,entry.copy);
       if(panel.detail.empty())panel.detail=gearDescription(entry.gear);
     }
     if(entry.copy>=0&&weaponRarity(entry.gear)!=Rarity::Common)panel.action="INSPECT COPY";
   }else if(catalog){
     for(auto gear:catalogStock())panel.cards.push_back({gearName(gear),std::to_string(traderPrice(gear))+" CR",stockIcon(gear),Symbol::Module});
     const auto gear=catalogStock()[panel.cursor];
     panel.inspection=gearName(gear);panel.detail=stockStats(fitted,gear,-1);
     if(panel.detail.empty())panel.detail=gearDescription(gear);
     panel.action="INSPECT STOCK";
   }else{
     panel.columns=1;
     for(const auto &label:tradeLabels())panel.cards.push_back({label,{},{}});
     panel.inspection=gearName(staged);
     panel.detail=std::to_string(fitted.inventory[size_t(Gear::Scrap)])+" SCRAP / "+std::to_string(availableCampaignAwards(fitted))+" AWARDS";
     if(panel.cursor==3&&!recoveredWeaponPage(*this))panel.detail="LEFT / RIGHT TO CHOOSE ARMOUR";
     if(tuneAction(*this)||(recoveredWeaponPage(*this)&&panel.cursor>=2&&panel.cursor<rowCount()-1))
       panel.detail="LEFT / RIGHT TO CHOOSE ATTRIBUTE";
     panel.action=panel.cards[panel.cursor].label;
     if(tradeCommand().is_null()&&panel.cursor!=rowCount()-1)panel.action.clear();
   }
   return panel;
 }
 panel.title=std::string(workshopNames[int(workshop)])+" / "+std::to_string(fitted.credits)+" CR";
 if(workshop==Workshop::Equip)panel.options.push_back("ITEM / "+fitLabel);
 if(workshop==Workshop::Install)panel.title+=" / "+std::to_string(fittedModuleCount(fitted))+" OF "+std::to_string(moduleLimit)+" INSTALLED";
 const auto rows=rowsFor(*this);
 const auto row=rows[panel.cursor];
 for(const auto &entry:rows){
   const auto i=entry.index;
   switch(entry.action){
   case Action::Open:{
     constexpr std::array symbols={Symbol::None,Symbol::Mount,Symbol::Module,Symbol::Plate,Symbol::Frame,Symbol::Attribute};
     panel.cards.push_back({workshopNames[i],workshopCaptions[i],{},symbols[i]});break;
   }
   case Action::Armor:panel.cards.push_back({armorName(Armor(i)),fitted.armor==Armor(i)?"FITTED":"AVAILABLE",{},Symbol::Plate});break;
   case Action::Module:case Action::Frame:
     panel.cards.push_back({moduleName(Module(i)),std::to_string(entry.action==Action::Frame?frameUpgradeCost:modulePurchaseCost)+" CR",stockIcon(moduleGear(Module(i))),entry.action==Action::Frame?Symbol::Frame:Symbol::Module});break;
   case Action::Refine:
     panel.cards.push_back({refinementName(PilotRefinement(i)),entry.award?"1 AWARD":std::to_string(pilotRefinementCost(fitted))+" SCRAP",{},Symbol::Attribute});break;
   default:break;
   }
 }
 const auto [gear,copy]=inspectedGear(*this);
 switch(row.action){
 case Action::Open:
   panel.inspection=workshopCaptions[row.index];panel.action=std::string("OPEN ")+workshopNames[row.index];break;
 case Action::Empty:
   panel.inspection=workshop==Workshop::Install?"NO INSTALLATIONS AVAILABLE":"NO UPGRADES AVAILABLE";break;
 case Action::Armor:
   panel.inspection=std::string(armorName(Armor(row.index)))+" ARMOUR";
   panel.detail=std::string("CURRENT / ")+armorName(fitted.armor);
   panel.actionAvailable=fitted.armor!=Armor(row.index);panel.action=panel.actionAvailable?"FIT ARMOUR":"ALREADY FITTED";
   break;
 case Action::Frame:{
   const auto frame=Module(row.index);
   panel.inspection=moduleName(frame);
   panel.detail=moduleDescription(frame);
   panel.action="UPGRADE / "+std::to_string(frameUpgradeCost)+" CR";
   panel.actionAvailable=fitted.credits>=frameUpgradeCost;
   break;
 }
 case Action::Module:
   panel.inspection=moduleName(Module(row.index));
   panel.detail=moduleDescription(Module(row.index));
   panel.action="INSTALL / "+std::to_string(modulePurchaseCost)+" CR";
   panel.actionAvailable=fitted.credits>=modulePurchaseCost;
   break;
 case Action::Refine:{
   const auto refinement=PilotRefinement(row.index);
   const int count=pilotRefinementCount(fitted);
   panel.inspection=std::string(refinementName(refinement))+" REFINEMENT / "+std::to_string(count)+"/"+
                    std::to_string(pilotRefinementLimit)+" USED";
   if(!canRefinePilot(fitted,refinement)){
     panel.detail=count>=pilotRefinementLimit?"CAMPAIGN REFINEMENTS COMPLETE":"THIS ATTRIBUTE IS ALREADY REFINED";
     panel.actionAvailable=false;
     break;
   }
   panel.detail=refinementPreview(fitted,refinement);
   if(row.award){
     const int awards=availableCampaignAwards(fitted);
     panel.action="REFINE / 1 AWARD ("+std::to_string(awards)+" AVAILABLE)";
     panel.actionAvailable=awards>0;
   }else{
     const int cost=pilotRefinementCost(fitted),scrap=fitted.inventory[size_t(Gear::Scrap)];
     panel.action="REFINE / "+std::to_string(cost)+" SCRAP ("+std::to_string(scrap)+" AVAILABLE)";
     panel.actionAvailable=scrap>=cost;
   }
   break;
 }
 case Action::Mount:{
   panel.selectedSlot=row.index;
   const auto &card=panel.slots[row.index];
   panel.inspection=std::string(mountNames[row.index])+" / "+card.item;
   if(consumable(gear)&&staged==fitted.equipment[row.index]&&equipmentOwner(fitted,row.index)==row.index){
     const auto cost=gearInfo(gear).cost;
     panel.detail=card.detail;
     if(gear==Gear::Repair||gear==Gear::ArcGrenade||gear==Gear::FragGrenade||gear==Gear::ExplosiveRounds)
       panel.detail+=" / "+std::string(gearDescription(gear));
     if(fitted.inventory[size_t(gear)]>=99){
       panel.action="STOCK FULL";
       panel.actionAvailable=false;
     }else if(fitted.credits<cost){
       panel.action="REFILL / "+std::to_string(cost)+" CR REQUIRED";
       panel.actionAvailable=false;
     }else panel.action="REFILL +1 / "+std::to_string(cost)+" CR";
   }else if(staged==fitted.equipment[row.index]&&stagedCopy==equippedWeaponCopy(fitted,equipmentOwner(fitted,row.index))){
     panel.action=staged==Gear::Empty?"MOUNT EMPTY":"ALREADY FITTED";panel.actionAvailable=false;
   }else if(mountAllowed(staged,row.index)&&chassisGearAllowed(fitted.chassis,staged))panel.action=staged==Gear::Empty?"CLEAR MOUNT":"FIT "+fitLabel;
   else{
     panel.action="CHOOSE AN ITEM FOR THIS MOUNT";
     panel.actionAvailable=false;
   }
   break;
 }
 }
 if(auto stats=stockStats(fitted,gear,copy);!stats.empty())panel.detail=std::move(stats);
 return panel;
}
std::string Preparation::previewModel(int* handling,bool field)const{
 const auto [gear,copy]=field?std::pair{Gear(selectedGear),selectedWeaponCopy()}:inspectedGear(*this);
 if(handling){
    const auto* item=weaponCopy(sheet,gear,copy);
    const auto stat=size_t(WeaponStat::Handling);
    *handling=item?item->roll[stat]+item->tuning[stat]:0;
 }
 Quality quality=Quality::None;
 if(sheet.created&&!inventory&&!trading&&!field){
    const auto rows=rowsFor(*this);
    if(selected>=0&&size_t(selected)<rows.size()&&rows[size_t(selected)].action==Action::Mount)
        quality=sheet.qualities[rows[size_t(selected)].index];
 }
 if(quality==Quality::None)for(int slot=0;slot<MountCount;++slot)
    if(sheet.equipment[slot]==gear&&equipmentOwner(sheet,slot)==slot){quality=sheet.qualities[slot];break;}
 return gear!=Gear::Empty&&playerEquipment(gear)?originalVariant(std::string(gearVisualId(gear)),sheet.creationSeed^uint32_t(quality)^uint32_t(gear)):"";
}
void Preparation::mount(int slot,bool remove){
 const auto gear=remove?Gear::Empty:Gear(selectedGear);
 if(!playerEquipment(gear))throw std::runtime_error("Choose usable equipment");
 const int copy=remove?-1:selectedWeaponCopy();
 if(sheet.equipment[slot]==gear&&equipmentOwner(sheet,slot)==slot&&equippedWeaponCopy(sheet,slot)==copy)return;
 equipGear(sheet,slot,gear,copy);prepare(sheet);
}
bool Preparation::confirm(bool host){
 if(inventory){
   const auto [gear,copy]=inspectedGear(*this);
   if(copy>=0&&weaponRarity(gear)!=Rarity::Common){
     returnCursor=selected;inventory=false;catalog=false;trading=true;selectedGear=int(gear);selectedCopy=copy;selected=0;
   }
   return false;
 }
 if(trading){
   if(catalog){
     returnCursor=selected;selectedGear=int(catalogStock().at(selected));selectedCopy=0;catalog=false;selected=0;
   }else if(selected>=rowCount()-1)back();
   return false;
 }
 if(!sheet.created){
   if(selected==rowCount()-1){finishCreation(sheet);openEquipment();return true;}
   return edit(1,host);
 }
 const auto rows=rowsFor(*this);
 if(selected<0||selected>=int(rows.size()))return false;
 const auto row=rows[selected];
 switch(row.action){
 case Action::Open:
   workshop=Workshop(row.index);selected=0;
   if(workshop==Workshop::Equip)inspectMount(*this);
   if(workshop==Workshop::Armour){
     const auto choices=rowsFor(*this);
     for(size_t i=0;i<choices.size();++i)if(choices[i].index==int(sheet.armor))selected=int(i);
   }
   break;
 case Action::Mount:
   if(sheet.equipment[row.index]==Gear(selectedGear)&&equippedWeaponCopy(sheet,equipmentOwner(sheet,row.index))==selectedWeaponCopy())return false;
   mount(row.index);return true;
 case Action::Armor:
   if(sheet.armor==Armor(row.index))return false;
   sheet.armor=Armor(row.index);prepare(sheet);return true;
 case Action::Module:
   installModule(sheet,Module(row.index));prepare(sheet);selected=std::min(selected,rowCount()-1);return true;
 default:break;
 }
 return false;
}
bool Preparation::edit(int delta,bool){
 if(inventory||catalog)return false;
 auto cycle=[&](int value,int count){return(value+delta%count+count)%count;};
 if(recoveredWeaponPage(*this)){
   const int stock=availableEquipment(sheet,Gear(selectedGear));
   if(selected<=1&&stock>0)selectedCopy=cycle(selectedWeaponCopy(),stock);
   else if(selected>=2&&selected<rowCount()-1)tuneStat=WeaponStat(cycle(int(tuneStat),int(WeaponStat::Count)));
   return false;
 }
 if(trading){
  if(selectedWeaponCopy()>=0&&(selected==tuneCopyRow(*this)||selected==2))
    selectedCopy=cycle(selectedWeaponCopy(),equipmentCopyCount(sheet,Gear(selectedGear)));
  else if(tuneAction(*this))
    tuneStat=WeaponStat(cycle(int(tuneStat),int(WeaponStat::Count)));
  if(selected==3)tradeArmor=tradeArmor==Armor::Light&&chassisArmorAllowed(sheet.chassis,Armor::Heavy)?Armor::Heavy:Armor::Light;
  return false;
 }
 if(!sheet.created){
  if(selected==0){Dice dice{sheet.creationSeed?sheet.creationSeed:0x4d454348u};auto next=createSheet(Chassis(cycle(int(sheet.chassis),int(Chassis::Count))),sheet.pilot,dice,1);next.pilotBonusChoice=sheet.pilotBonusChoice;normalizeCombatProfile(next);next.creationSwap=sheet.creationSwap;next.record=sheet.record;next.career=sheet.career;next.missionsCompleted=sheet.missionsCompleted;next.campaignAwards=sheet.campaignAwards;next.campaignAwardsSpent=sheet.campaignAwardsSpent;next.lastCampaignAward=sheet.lastCampaignAward;next.campaign=sheet.campaign;sheet=next;startingEquipment(sheet,false);}
  else if(selected==1){sheet.pilot=PilotKind(cycle(int(sheet.pilot),playablePilotCount));sheet.pilotBonusChoice=0;normalizeCombatProfile(sheet);}
  else if(selected==2){int choice=cycle(forename*20+surname,400);forename=choice/20;surname=choice%20;callsign=std::string(firstNames[forename])+" "+lastNames[surname];}
  else return false;
  validate(sheet);return true;
 }
 if(workshop==Workshop::Equip){
   const int slot=equipmentPanelOrder.at(selected);
   const int limit=int(Gear::Count)+std::accumulate(sheet.owned.begin(),sheet.owned.end(),0);
   for(int i=0;i<limit;++i){
     browseGear(delta);
     const auto gear=Gear(selectedGear);
     if(!mountAllowed(gear,slot)||!chassisGearAllowed(sheet.chassis,gear))continue;
     if(!pairedWeapon(sheet.chassis,gear)&&selectedWeaponCopy()>=0&&weaponCopyFitted(sheet,gear,selectedWeaponCopy(),slot))continue;
     break;
   }
 }
 return false;
}
std::vector<std::string> Preparation::tradeLabels()const{
 std::vector<std::string> rows;
   const auto gear=Gear(selectedGear);
   const int copy=selectedWeaponCopy();
   const int cost=tradeArmor==Armor::Heavy?70:tradeArmor==Armor::Light?40:0;
   if(recoveredWeaponPage(*this)&&copy<0){
     rows.clear();
   }else if(recoveredWeaponPage(*this)){
     const int count=weaponTuneCount(weaponCopy(sheet,gear,copy));
     rows={"OWNED COPY "+std::to_string(copy+1),
           "SELL COPY / "+std::to_string(traderPrice(gear)/2)+" CR",
           count==weaponTuneLimit?"FULLY TUNED":std::string("TUNE ")+statName(gear,tuneStat)+" / "+std::to_string(weaponTuneCost(gear,count))+" SCRAP"};
     if(sheet.campaignAwards>0)rows.push_back(count==weaponTuneLimit?"FULLY TUNED":std::string("TUNE ")+statName(gear,tuneStat)+" / 1 AWARD");
   }else{
     rows={"STOCK: "+std::string(gearName(gear))+(gradedModule(gear)?" / COMMON":""),
           "BUY / "+std::to_string(traderPrice(gear))+" CR",
           std::string("SELL ")+(copy>=0?"COPY "+std::to_string(copy+1):"SPARE")+" / "+std::to_string(traderPrice(gear)/2)+" CR",
           "ARMOR: "+std::string(armorName(tradeArmor)),sheet.ownedArmor[int(tradeArmor)]?"ARMOR OWNED":"BUY ARMOR / "+std::to_string(cost)+" CR",
           "SELL SPARE ARMOR / "+std::to_string(cost/2)+" CR"};
     if(craftRow(*this))rows.push_back("CRAFT / "+std::to_string((gearInfo(gear).cost+24)/25)+" SCRAP");
     if(weaponTuning(gear)&&copy>=0){
       const int count=weaponTuneCount(weaponCopy(sheet,gear,copy));
       rows.push_back("TUNE COPY "+std::to_string(copy+1)+" / "+std::to_string(availableEquipment(sheet,gear)));
       rows.push_back(count==weaponTuneLimit?"FULLY TUNED":
         std::string("TUNE ")+statName(gear,tuneStat)+" / "+std::to_string(weaponTuneCost(gear,count))+" SCRAP");
       if(sheet.campaignAwards>0)
         rows.push_back(count==weaponTuneLimit?"AWARD TUNING COMPLETE":
             std::string("TUNE ")+statName(gear,tuneStat)+" / 1 AWARD");
     }
   }
   rows.push_back("BACK");
 return rows;
}
std::vector<std::string> Preparation::lines(const Session &,uint32_t,const std::string &status)const{
 std::vector<std::string> rows;
 if(inventory){
  for(auto &item:stockFor(sheet))rows.push_back(std::move(item.label));
  if(rows.empty())rows.push_back("NO STOCK");
 }else if(!sheet.created){rows={"CHASSIS: "+std::string(chassisInfo(sheet.chassis).name),"PILOT ROLE: "+std::string(pilotName(sheet.pilot)),"CALLSIGN: "+callsign};
   rows.push_back("CREATE PILOT");}
 else if(trading&&!catalog)rows=tradeLabels();
 else {
   const auto panel=loadoutPanel(sheet,-1,status);
   for(const auto &card:panel.cards)rows.push_back(card.label);
   if(rows.empty())rows.push_back(panel.inspection);
 }
 int cursor=std::clamp(selected,0,int(rows.size())-1),start=cursor/7*7;
 std::vector<std::string> result={inventory?"INVENTORY":recoveredWeaponPage(*this)?"RECOVERED / "+std::string(gearName(Gear(selectedGear))):trading?"TRADE POST / "+std::string(gearName(Gear(selectedGear))):sheet.created?"EQUIPMENT / "+callsign:"NEW PILOT / "+callsign};
 for(int i=start;i<std::min(start+7,int(rows.size()));++i)result.push_back(std::string(i==cursor?"> ":"  ")+rows[i]);
 result.push_back(std::to_string(sheet.credits)+" CR"+(trading?" / "+std::to_string(sheet.inventory[size_t(Gear::Scrap)])+
     " SCRAP / "+std::to_string(availableCampaignAwards(sheet))+" AWARDS":"")+" / PAGE "+
     std::to_string(cursor/7+1)+"/"+std::to_string((rows.size()+6)/7));
 if(!sheet.created)result.push_back("POWER "+std::to_string(sheet.attributes[0])+" / MOBILITY "+std::to_string(sheet.attributes[1])+" / SYSTEM "+std::to_string(sheet.attributes[2])+" / PRESENCE "+std::to_string(sheet.attributes[3]));
 if(!sheet.created&&selected==0)result.push_back(chassisPowerDescription(sheet.chassis));
 if(!sheet.created&&selected==1)result.push_back(pilotRoleDescription(sheet.pilot));
 if(!sheet.created&&selected==3)result.push_back("STARTS WITH 1 NANITE CHARGE");


 if(sheet.created){
     const auto [gear,copy]=inspectedGear(*this);
     if(inventory&&copy>=0&&weaponRarity(gear)!=Rarity::Common)result.push_back("SELECT TO SELL OR TUNE");
     if(gradedModule(gear)){
         const auto stats=weaponStats(sheet,gear,copy);
         result.push_back(std::string(rarityName(equipmentRarity(sheet,gear,copy)))+" / "+oneDecimal(stats.cooldown)+" S / "+oneDecimal(stats.heat*100)+"% HEAT");
         if(gear==Gear::LongRangeScanners)result.push_back(oneDecimal(5.f*stats.output)+" S TRACE PERSISTENCE");
         else if(stats.range>0)result.push_back(oneDecimal(stats.range)+" M RANGE");
         else result.push_back(std::to_string(int(std::lround(stats.output*100)))+"% OUTPUT");
     }
     if(const auto *tuning=weaponTuning(gear)){
         const auto stats=weaponStats(sheet,gear,copy);
         const auto *item=weaponCopy(sheet,gear,copy);
         result.push_back(std::string(rarityName(weaponRarity(gear)))+" / "+
             std::to_string(int(std::lround(stats.cooldown*1000)))+" MS / HEAT "+
             oneDecimal(stats.heat*100)+"%");
         const int damage=std::max(1,(damageDie(sheet)+2)/2+tuning->damageBias+2*int(heavy(gear)));
         const auto force=tuning->pattern==AttackPattern::Scatter?
             std::to_string(tuning->projectiles)+" PELLETS":
             "BASE HIT "+std::to_string(damage);
         result.push_back(force+" / REACH "+
             (melee(gear)?oneDecimal(stats.range):std::to_string(int(std::lround(stats.range))))+" M");
         if(copy>=0){
           const int count=weaponTuneCount(item);
           std::string detail="TUNING "+std::to_string(count)+"/"+std::to_string(weaponTuneLimit);
           if(ranged(gear))detail+=" / DISPERSION "+std::to_string(int(std::lround(stats.spreadFactor*100)))+"%";
           result.push_back(std::move(detail));
         }else result.push_back(gearDescription(gear));
         if(tuning->windup>0)result.push_back("CHARGE "+std::to_string(int(std::lround(tuning->windup*1000)))+" MS");
     }
 }
 if(!status.empty())result.push_back(status.substr(0,58));
 return result;
}
}
