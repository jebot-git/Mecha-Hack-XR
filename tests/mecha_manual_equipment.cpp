#include "rpg/rules.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace iron;
using namespace iron::rpg;
int main(){
  static_assert(int(Module::DismantlerArms)==21&&int(Gear::FunnelDrones)==31&&int(Quality::Artillery)==9);
  Dice dice{718};auto base=createSheet(Chassis::Striker,PilotKind::Engineer,dice);
  std::set<std::string> modules,items,qualities;unsigned gadgets=0,consumables=0;
  for(int n=int(Module::DismantlerArms);n<int(Module::Count);++n){auto m=Module(n);
    assert(manualModule(m)&&manualSourcePage(m)>=81&&manualSourcePage(m)<=82);
    assert(modules.insert(moduleId(m)).second&&std::string(moduleDescription(m)).size()>10);
    auto sheet=base;sheet.modules={m};prepare(sheet);validate(sheet);
    auto gear=moduleGear(m);if(gear!=Gear::Empty&&!retiredGear(gear)){equipGear(sheet,RightHip,gear);assert(sheet.equipment[RightHip]==gear);}
  }
  for(int n=int(Gear::FunnelDrones);n<=int(Gear::CargoSpider);++n){auto g=Gear(n);auto &info=gearInfo(g);
    assert(manualGear(g)&&items.insert(info.id).second&&std::string(gearDescription(g)).size()>10);
    assert(std::filesystem::exists(std::filesystem::path(MECHA_ASSETS)/"meshes"/(std::string(gearVisualId(g))+".json")));
    assert(manualSourcePage(g)>=81&&manualSourcePage(g)<=85);
    if(gadget(g))++gadgets;
    if(gadget(g)&&!retiredGear(g)){assert(!consumable(g)&&info.cost>0);auto s=base;s.credits=10000;purchase(s,g);equipGear(s,RightHip,g);assert(s.owned[size_t(g)]==1);bool blocked=false;try{sellEquipment(s,g);}catch(...){blocked=true;}assert(blocked);equipGear(s,RightHip,Gear::Empty);sellEquipment(s,g);assert(s.owned[size_t(g)]==0);}
    if(consumable(g))++consumables;
  }
  assert(modules.size()==13&&items.size()==31&&gadgets==21&&consumables==1);
  for(int n=int(Quality::Artillery);n<int(Quality::Count);++n){auto q=Quality(n);
    assert(qualities.insert(qualityName(q)).second&&manualSourcePage(q)==83&&std::string(qualityDescription(q)).size()>10);
    assert(!qualityAllowed(Gear::Repair,q));
  }
  assert(qualities.size()==12&&!qualityAllowed(Gear::Rifle,Quality::Artillery)&&qualityAllowed(Gear::Cannon,Quality::Artillery));
  assert(!qualityAllowed(Gear::Cannon,Quality::Bouncing)&&qualityAllowed(Gear::Rifle,Quality::Bouncing));
  auto passive=base;passive.modules={Module::EnergyModulator};assert(damageDie(passive)==10);
  passive.modules={Module::LodiumAlloy};prepare(passive);assert(passive.maxAp==4);
  passive.modules={Module::MicroReactor};prepare(passive);assert(passive.maxReactor==6);
  passive.modules={Module::GeistSystem};passive.attributes[3]=32;prepare(passive);int reactor=passive.reactor;
  auto ordinary=base;
  for(int i=0;i<4;++i){passive.reactorTest(dice);ordinary.reactorTest(dice);}
  assert(passive.reactor==reactor&&passive.reactorStress==ordinary.reactorStress&&retiredModule(Module::GeistSystem));
  for(int i=0;i<100;++i)passive.reactorTest(dice);
  assert(passive.reactor==reactor&&passive.overheated);
  auto battery=base;battery.batteryDie=6;reactor=battery.reactor;
  for(int i=0;i<200&&battery.batteryDie;++i)battery.reactorTest(dice);
  assert(battery.batteryDie==0&&battery.reactor==reactor);
  auto craft=base;craft.inventory[size_t(Gear::Scrap)]=10;craftFromScrap(craft,Gear::FragGrenade,2);
  assert(craft.inventory[size_t(Gear::Scrap)]==8&&craft.inventory[size_t(Gear::FragGrenade)]==2);
  auto before=Json(craft);bool rejected=false;try{craftFromScrap(craft,Gear::Cannon,3);}catch(...){rejected=true;}assert(rejected&&Json(craft)==before);
  craftFromScrap(craft,Gear::Rifle);assert(craft.inventory[size_t(Gear::Scrap)]==6&&craft.owned[size_t(Gear::Rifle)]==2);
  craft.equipment.fill(Gear::Empty);craft.qualities.fill(Quality::None);normalizeHeavyEquipment(craft);
  equipGear(craft,LeftHand,Gear::Rifle);equipGear(craft,RightHand,Gear::Rifle);equipQuality(craft,LeftHand,Quality::Dual);
  assert(craft.qualities[LeftHand]==Quality::Dual&&craft.qualities[RightHand]==Quality::Dual&&equipmentOwner(craft,RightHand)==RightHand);
  equipGear(craft,LeftHand,Gear::Empty);assert(craft.qualities[RightHand]==Quality::None&&craft.equipment[RightHand]==Gear::Rifle);
  std::cout<<"Mission Manual: 13 modules, 31 items, 12 qualities, scrap crafting, reserve batteries and matching dual hand inventory verified\n";
}
