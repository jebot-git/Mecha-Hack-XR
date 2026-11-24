#include "mecha_generation_audit.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char **argv){try{
  iron::rpg::setOriginalAssetRoot(MECHA_ASSETS);auto report=iron::rpg::auditMissionGeneration();
  if(argc==3&&std::string(argv[1])=="--report"){std::filesystem::path path=argv[2];if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());std::ofstream out(path);out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot write generation audit");}
  std::cout<<report.dump(2)<<'\n';return report["failures"].empty()?0:1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
