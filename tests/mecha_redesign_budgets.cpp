#include "rpg/original_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
using namespace iron::rpg;using Json=nlohmann::json;namespace fs=std::filesystem;
namespace {
void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
Json read(const fs::path &p){std::ifstream in(p);Json j;in>>j;return j;}
void write(const fs::path &p,const Json &j){std::ofstream out(p);out<<j;}
}
int main(){fs::path temp;try{
 const fs::path root=MECHA_ASSETS;auto approval=read(root/"mecha-redesign-budgets.json");
 require(approval.at("assets").size()==65,"Expected exact65 budget scope");
 temp=fs::temp_directory_path()/("iron-mecha-budget-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(temp/"meshes");
 for(const char *file:{"original-manifest.json","expansion-manifest.json","manual-enemy-assets.json","manual-item-assets.json"})fs::copy_file(root/file,temp/file);
 // Exercise the original65 policy in isolation from later kaiju authorization.
 const auto remaining=read(root/"enemy-redesign-budgets.json");
 for(const char *file:{"original-manifest.json","expansion-manifest.json","manual-enemy-assets.json","manual-item-assets.json"}){
  auto data=read(temp/file);for(auto &row:data["assets"])for(const auto &old:remaining["assets"])if(row["id"]==old["id"])row["budget"]=old["baseline_budget"];
  write(temp/file,data);
 }
 write(temp/"mecha-redesign-budgets.json",approval);
 auto attempt=[&](const std::string &id,size_t faces,size_t declared,bool accepted){
  Json mesh={{"schema",1},{"units","metres"},{"budget",declared},{"vertices",{{0,0,0},{1,0,0},{0,1,0}}},{"faces",Json::array()}};
  for(size_t n=0;n<faces;++n)mesh["faces"].push_back({{"indices",{0,1,2}},{"color",{128,128,128}}});
  write(temp/"meshes"/(id+".json"),mesh);setOriginalAssetRoot(temp);bool loaded=false;
  try{loaded=bool(originalMesh(id));}catch(const std::exception&){}
  require(loaded==accepted,id+": incorrect cap acceptance for "+std::to_string(faces)+" triangles / declared "+std::to_string(declared));
 };
 attempt("chassis_brawler_alt",1504,1504,true);attempt("chassis_brawler_alt",1505,1504,false);attempt("chassis_brawler_alt",1,1505,false);
 attempt("mechwalker_alt",1064,1064,true);attempt("mechwalker_alt",1065,1065,false);
 attempt("combiner_mecha_combined",1004,1004,true);attempt("combiner_mecha_combined",1005,1005,false);
 // These identities must retain their previous non-mecha per-model caps.
 for(const char *registry:{"original-manifest.json","expansion-manifest.json","manual-enemy-assets.json","manual-item-assets.json"}){const auto registryData=read(temp/registry);for(const auto &row:registryData["assets"]){auto id=row["id"].get<std::string>();if(id=="bomber"||id=="abomination"||id=="salvager_gunship_alt"||id=="weapon_funnel_drones")attempt(id,row["budget"].get<size_t>()+1,row["budget"].get<size_t>()+1,false);}}
 attempt("unregistered_mecha",503,503,false);
 auto invalid=approval;invalid["assets"][0]["budget"]=2008;write(temp/"mecha-redesign-budgets.json",invalid);attempt("chassis_brawler_alt",1504,1504,false);
 invalid=approval;invalid["assets"][0]["id"]="bomber";write(temp/"mecha-redesign-budgets.json",invalid);attempt("chassis_brawler_alt",1504,1504,false);
 invalid=approval;invalid["assets"].erase(invalid["assets"].begin());write(temp/"mecha-redesign-budgets.json",invalid);attempt("chassis_brawler_alt",1504,1504,false);
 invalid=approval;invalid["assets"][0]["baseline_file"]="meshes/chassis_brawler.json";write(temp/"mecha-redesign-budgets.json",invalid);attempt("chassis_brawler_alt",1504,1504,false);
 fs::remove(temp/"mecha-redesign-budgets.json");attempt("chassis_brawler_alt",752,752,true);attempt("chassis_brawler_alt",1504,1504,false);
 setOriginalAssetRoot(root);fs::remove_all(temp);std::cout<<"Explicit65 mecha doubled caps and non-mecha limits passed\n";return 0;
 }catch(const std::exception &e){setOriginalAssetRoot(MECHA_ASSETS);if(!temp.empty())fs::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}}
