#include "../tools/mecha_generation_audit.hpp"
#include <iostream>
int main(){try{
  iron::rpg::setOriginalAssetRoot(MECHA_ASSETS);auto report=iron::rpg::auditMissionGeneration();auto &coverage=report.at("coverage");
  if(coverage["layouts"]!=5||coverage["height_profiles"]!=6||coverage["locations"]!=10||coverage["biomes"]!=8||coverage["section_styles"]!=4||coverage["underground"]==0||coverage["submerged"]==0||coverage["facilities"]==0||coverage["unique_navigation_geometries"]<20||coverage["asset_types"]<30||coverage["size_bands"][0]==0||coverage["size_bands"][2]==0)throw std::runtime_error("Generation corpus lost requested variety: "+coverage.dump());
  if(!report["failures"].empty())throw std::runtime_error(report["failures"].dump(2));
  std::cout<<"24 seeded worlds: "<<coverage.dump()<<'\n';
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
