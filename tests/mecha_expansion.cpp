#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
static Json read(const std::filesystem::path &p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing asset: "+p.string());Json j;f>>j;return j;}
static void check(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 auto root=std::filesystem::path(MECHA_ASSETS);setOriginalAssetRoot(root);auto manifest=read(root/"expansion-manifest.json");
 check(manifest.at("variants").size()==128,"Expansion must add all128 scoped variants");
 for(const char *family:{"enemies","mechas","equipment","environment"}){
  auto group=manifest.at("groups").at(family);check(group.at("added")==group.at("before")&&group.at("total").get<int>()==2*group.at("before").get<int>(),std::string("Family not doubled: ")+family);
 }
 std::map<std::string,size_t> approved;for(const auto &row:read(root/"mecha-redesign-budgets.json").at("assets"))approved[row.at("id").get<std::string>()]=row.at("budget").get<size_t>();
 size_t triangles=0;std::set<std::string> ids;
 for(const auto &row:manifest.at("assets")){
  auto id=row.at("id").get<std::string>(),base=row.at("base").get<std::string>();
  check(ids.insert(id).second,"Duplicate variant ID");auto j=read(root/"meshes"/(id+".json")),old=read(root/"meshes"/(base+".json"));
  check(j.at("vertices")!=old.at("vertices")||j.at("faces").size()!=old.at("faces").size(),id+" has no geometric variation");
  size_t budget=old.at("budget");if(row.at("family")=="enemies"||row.at("family")=="mechas")budget*=2;
  if(approved.contains(id))budget=approved.at(id);
  check(j.at("faces").size()<=budget,id+" exceeds its permitted triangle budget");
  for(const auto &face:j.at("faces")){check(face.at("texture")=="psx"&&face.at("uv").size()==3,id+" lacks atlas UVs");
   for(const auto &uv:face.at("uv"))for(const auto &value:uv)check(std::isfinite(value.get<float>())&&value.get<float>()>=0&&value.get<float>()<=1,id+" invalid UV");}
  auto mesh=originalMesh(id);check(mesh->size()==j.at("faces").size()*3,id+" fails runtime mesh import");triangles+=mesh->size()/3;
  check(originalVariant(base,0)==base&&originalVariant(base,1)==id,id+" unavailable to runtime selection");
  Target t;applyOriginalModel(t,id);check(t.generatorAsset==id&&t.ai.boundsReady,id+" model identity or collider missing");
  if(row.at("family")=="equipment")for(const char *edge:{"low","high"})for(int axis=0;axis<3;++axis)check(std::abs(j.at("bounds").at(edge)[axis].get<float>()-old.at("bounds").at(edge)[axis].get<float>())<.0001f,id+" changed shared grip/muzzle envelope");
 }
 std::cout<<"128 distinct textured variants imported; all four model families doubled / "<<triangles<<" additional triangles\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
