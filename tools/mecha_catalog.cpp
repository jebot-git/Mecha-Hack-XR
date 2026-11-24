// Developer-only catalog exporter. It links the headless RPG core and never
// initializes a game, renderer, window, audio device or XR session.
#include "rpg/mission.hpp"
#include "rpg/menu.hpp"
#include "rpg/bosses.hpp"
#include "rpg/manual_missions.hpp"
#include <iostream>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
int main() {
  try {
  Json data = {{"schema", 3},
               {"enemies", Json::array()},
               {"modules", Json::array()},
               {"equipment", Json::array()},
               {"qualities", Json::array()},
               {"environments", Json::array()},
               {"factions", factions},
               {"locations", locations}};
  size_t enemyIndex=0;
  for (auto &e : enemyCatalog()) {
    Json attacks = Json::array();
    for (auto &attack : e.attacks)
      attacks.push_back({{"name", attack.name},
                         {"dice", attack.dice},
                         {"sides", attack.sides},
                         {"targets", attack.targets},
                         {"minimum", attack.minimum},
                         {"range", attack.range},
                         {"recharge", attack.recharge},
                         {"effect", int(attack.effect)},
                         {"reactor", attack.reactor},
                         {"defense_attribute", attack.defense},
                         {"alternate_defense_attribute",attack.alternateDefense}});
    data["enemies"].push_back({{"id", e.id},
                               {"name", e.name},
                               {"model", enemyModelId(enemyIndex)},
                               {"source",enemyIndex<34?"core":"mission_manual"},
                               {"source_page",enemyIndex<34?0:enemyIndex<37?86:enemyIndex<40?87:88+int(enemyIndex-40)/3},
                               {"hd", e.hd},
                               {"kind", int(e.kind)},
                               {"optimal", e.optimal},
                               {"plating", e.plating},
                               {"shield", e.shield},
                               {"shield_dice", e.shieldDice},
                               {"death_dice", e.deathDice},
                               {"death_sides", e.deathSides},
                               {"acid", e.acid},
                               {"traits", e.traits},
                               {"attacks", attacks}});
    ++enemyIndex;
  }
  for (int k = 0; k < int(Module::Count); ++k)
    data["modules"].push_back(
        {{"id", moduleId(Module(k))}, {"name", moduleName(Module(k))},{"source_page",manualSourcePage(Module(k))}});
  for (int k = 0; k < int(Gear::Count); ++k) {
    auto &g = gearInfo(Gear(k));
    data["equipment"].push_back({{"id", g.id},
                                 {"name", g.name},
                                 {"model", gearVisualId(Gear(k))},
                                 {"player_visual", k!=int(Gear::Empty)&&playerEquipment(Gear(k))},
                                 {"source_page",manualSourcePage(Gear(k))},
                                 {"cost", g.cost},
                                 {"cooldown", g.cooldown},
                                 {"range", g.range}});
  }
  for (int k = 1; k < int(Quality::Count); ++k)
    data["qualities"].push_back(qualityName(Quality(k)));
  for (auto &e : environmentCatalog())
    data["environments"].push_back({{"id", e.id},
                                    {"name", e.name},
                                    {"speed", e.speed},
                                    {"sensors", e.sensors}});
  auto choices = [&](const char *key,
                     std::span<const MissionChoice> entries) {
    data[key] = Json::array();
    for (auto &entry : entries)
      data[key].push_back({{"id", entry.id}, {"name", entry.name}});
  };
  choices("enemy_themes", enemyThemes());
  choices("objectives", missionObjectives());
  choices("mission_locations", missionLocations());
  choices("openers", missionOpeners());
  choices("tactics", enemyTactics());
  data["campaign_bosses"]=Json::array();
  for(const auto &b:campaignBosses()){Json attacks=Json::array();for(const auto &a:b.spec.attacks)attacks.push_back({{"name",a.name},{"dice",a.dice},{"sides",a.sides},{"range",a.range},{"minimum",a.minimum},{"effect",int(a.effect)}});data["campaign_bosses"].push_back({{"id",b.spec.id},{"name",b.spec.name},{"base_rig",b.base},{"faction",b.faction},{"counterplay",b.counterplay},{"windup_seconds",b.windup},{"reinforcement",b.reinforcement},{"phase_thresholds",{.66667,.33333}},{"attacks",attacks}});}
  data["manual_missions"]=Json::array();
  for(const auto &m:manualMissionCatalog())data["manual_missions"].push_back({{"id",m.id},{"name",m.name},{"source_page",m.page},{"objectives",m.objectives},{"countdown",m.countdown},{"hold",m.hold}});
  choices("instant_actions",instantActions());choices("instant_foci",instantFoci());
  choices("complications",manualComplications());choices("opportunities",manualOpportunities());
  choices("briefing_styles",manualBriefings());choices("planetoid_features",planetoidFeatures());choices("planetoid_hazards",planetoidHazards());
  choices("salvage_forms",salvageForms());choices("salvage_components",salvageFeatures());
  choices("mecha_paints",mechaPaints());choices("mecha_fittings",mechaFeatures());
  choices("kaiju_looks",kaijuLooks());choices("kaiju_features",kaijuFeatures());
  choices("kaiju_prefixes",kaijuNamePrefixes());choices("kaiju_suffixes",kaijuNameSuffixes());
  choices("friendly_chatter",friendlyChatter());choices("enemy_chatter",enemyChatter());
  choices("tech_prefixes",techPrefixes());choices("tech_suffixes",techSuffixes());
  choices("critical_cockpit",criticalCockpitEffects());choices("critical_exterior",criticalExteriorEffects());
  std::cout << data.dump(2) << "\n";
    return std::cout ? 0 : 1;
  } catch(const std::exception &error) {
    std::cerr<<error.what()<<'\n';
    return 1;
  }
}
