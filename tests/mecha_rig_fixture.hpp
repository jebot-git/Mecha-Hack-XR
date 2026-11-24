#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
// Match native renderer precedence while leaving historical source metadata intact.
inline nlohmann::json mechaPlayerRigFixture(const std::filesystem::path &root){
 nlohmann::json merged={{"schema",1},{"rigs",nlohmann::json::object()}};
 for(const char *name:{"animation-rigs.json","expansion-animation-rigs.json","manual-player-rigs.json","mecha-redesign-player-rigs.json"}){
  std::ifstream file(root/"models"/name);if(!file){if(std::string(name)=="animation-rigs.json")throw std::runtime_error("Missing original walker rigs");continue;}
  nlohmann::json data;file>>data;
  if(data.at("schema")!=1)throw std::runtime_error("Invalid walker fixture schema");
  for(const auto &[id,rig]:data.at("rigs").items())merged["rigs"][id]=rig;
 }return merged;
}
