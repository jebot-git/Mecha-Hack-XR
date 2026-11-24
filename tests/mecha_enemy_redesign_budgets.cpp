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
 const fs::path root=MECHA_ASSETS;const auto approval=read(root/"enemy-redesign-budgets.json");
 require(approval.at("assets").size()==44,"Expected exact44 remaining enemy scope");size_t doubled=0,unchanged=0;
 temp=fs::temp_directory_path()/("iron-enemy-budget-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(temp/"meshes");
 for(const char *file:{"original-manifest.json","expansion-manifest.json","manual-enemy-assets.json","manual-item-assets.json"}){
  auto data=read(root/file);for(auto &row:data["assets"])for(const auto &old:approval["assets"])if(row["id"]==old["id"])row["budget"]=old["baseline_budget"];
  write(temp/file,data);
 }
 fs::copy_file(root/"mecha-redesign-budgets.json",temp/"mecha-redesign-budgets.json");write(temp/"enemy-redesign-budgets.json",approval);
 auto attempt=[&](const std::string &id,size_t faces,size_t declared,bool accepted){
  Json mesh={{"schema",1},{"units","metres"},{"budget",declared},{"vertices",{{0,0,0},{1,0,0},{0,1,0}}},{"faces",Json::array()}};
  for(size_t n=0;n<faces;++n)mesh["faces"].push_back({{"indices",{0,1,2}},{"color",{128,128,128}}});
  write(temp/"meshes"/(id+".json"),mesh);setOriginalAssetRoot(temp);bool loaded=false;
  try{loaded=bool(originalMesh(id));}catch(const std::exception&){}
  require(loaded==accepted,id+": incorrect cap acceptance for "+std::to_string(faces)+" triangles / declared "+std::to_string(declared));
 };
 for(const auto &row:approval["assets"]){
  auto id=row["id"].get<std::string>();size_t cap=row["budget"],old=row["baseline_budget"];
  if(row["budget_multiplier"]==2){require(cap==old*2,"Kaiju cap must exactly double");++doubled;}else{require(cap==old,"Non-kaiju cap changed");++unchanged;}
  attempt(id,cap,cap,true);attempt(id,cap+1,cap+1,false);attempt(id,1,cap+1,false);
 }
 require(doubled==19&&unchanged==25,"Expected19 doubled kaiju and25 unchanged enemies");
 attempt("chassis_brawler_alt",1504,1504,true);attempt("mechwalker_alt",1064,1064,true);attempt("weapon_funnel_drones",257,257,false);attempt("unregistered_kaiju",503,503,false);
 auto invalid=approval;invalid["assets"][0]["budget"]=2008;write(temp/"enemy-redesign-budgets.json",invalid);attempt("kaiju_flea",1504,1504,false);
 invalid=approval;invalid["assets"][0]["id"]="chassis_scout";write(temp/"enemy-redesign-budgets.json",invalid);attempt("kaiju_flea",1504,1504,false);
 invalid=approval;invalid["assets"][0]=invalid["assets"][1];write(temp/"enemy-redesign-budgets.json",invalid);attempt("kaiju_flea",1504,1504,false);
 invalid=approval;invalid["assets"][0]["baseline_file"]="meshes/abomination.json";write(temp/"enemy-redesign-budgets.json",invalid);attempt("kaiju_flea",1504,1504,false);
 invalid=approval;invalid["assets"][0]["baseline_sha256"]="invalid";write(temp/"enemy-redesign-budgets.json",invalid);attempt("kaiju_flea",1504,1504,false);
 fs::remove(temp/"enemy-redesign-budgets.json");attempt("kaiju_flea",752,752,true);attempt("kaiju_flea",1504,1504,false);attempt("abomination",377,377,false);attempt("chassis_brawler_alt",1504,1504,true);
 setOriginalAssetRoot(root);fs::remove_all(temp);std::cout<<"Explicit44 enemy caps:19 doubled kaiju,25 unchanged, previous65 preserved\n";return 0;
 }catch(const std::exception &e){setOriginalAssetRoot(MECHA_ASSETS);if(!temp.empty())fs::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}}
