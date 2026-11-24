#pragma once
#include "frontend.hpp"
#include "rpg/session.hpp"
namespace iron::rpg {
struct Preparation {
  Sheet sheet;
  GameSettings settings;
  int selected = 0;
  int selectedGear=int(Gear::Rifle),selectedCopy=0,forename=0,surname=0;
  WeaponStat tuneStat=WeaponStat::Cycle;
  PilotRefinement refinement=PilotRefinement::Hull;
  Module modulePreview=Module::RocketFists;
  Module framePreview=Module::RocketFists;
  std::string callsign="Atlas Echo";
  bool trading=false, inventory=false;
  Armor tradeArmor=Armor::Light;
  uint32_t randomState=0x70696c6fu;
  void openTrader();
  int rowCount(bool host=true) const;
  bool edit(int delta, bool host = true);
  bool confirm(bool host=true);
  void mount(int slot,bool remove=false);
  std::string previewModel(int* handling=nullptr) const;
  int selectedWeaponCopy() const;
  Json equipmentCommand(int slot,bool remove=false) const;
  Json tradeCommand() const;
  Json refillCommand() const;
  EquipmentPanel loadoutPanel(const Sheet &fitted,int fieldMount,const std::string &status) const;
  std::vector<std::string> lines(const Session &, uint32_t,
                                 const std::string &) const;
};
} // namespace iron::rpg
