#include "rpg/catalog.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/rules.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
Json read(const std::filesystem::path &path){std::ifstream in(path);if(!in)throw std::runtime_error("Missing authored manual asset: "+path.string());Json out;in>>out;return out;}
bool different(const WalkerPose &a,const WalkerPose &b){
  if(a.parts.size()!=b.parts.size())return true;
  for(size_t i=0;i<a.parts.size();++i)if(length(a.parts[i].offset-b.parts[i].offset)>.0001f||length(rotate(a.parts[i].orientation,{0,0,-1})-rotate(b.parts[i].orientation,{0,0,-1}))>.0001f)return true;
  return false;
}
Json shape(const Json &mesh){Json indices=Json::array();for(const auto &face:mesh.at("faces"))indices.push_back(face.at("indices"));return Json::array({mesh.at("vertices"),indices});}
}
int main(){try{
 const auto root=std::filesystem::path(MECHA_ASSETS);setOriginalAssetRoot(root);
 const auto approvedBudgets=read(root/"mecha-redesign-budgets.json");
 const auto approvedEnemyBudgets=read(root/"enemy-redesign-budgets.json");
 std::set<std::string> expectedEnemies,expectedItems,registered,shapes;size_t count=0,animated=0,triangles=0;
 for(size_t i=34;i<enemyCatalog().size();++i){const auto &spec=enemySpec(i);expectedEnemies.insert(spec.id);require(std::string(enemyModelId(i))==spec.id,"Manual enemy still selects a stand-in: "+std::string(spec.id));}
 expectedEnemies.insert("combiner_mecha_combined");
 for(int index=0;index<int(Gear::Count);++index)if(manualGear(Gear(index))){auto id=std::string("weapon_")+gearInfo(Gear(index)).id;expectedItems.insert(id);require(gearVisualId(Gear(index))==id,"Manual item still selects a stand-in: "+id);}
 require(expectedEnemies.size()==33&&expectedItems.size()==31,"Manual visual content scope changed without an explicit asset contract");
 for(const auto &[filename,expected]:std::array<std::pair<const char*,std::set<std::string>>,2>{{{"manual-enemy-assets.json",expectedEnemies},{"manual-item-assets.json",expectedItems}}}){
  const auto registry=read(root/filename);std::set<std::string> found;
  for(const auto &row:registry.at("assets")){
   const auto id=row.at("id").get<std::string>(),base=row.at("base").get<std::string>();
   require(found.insert(id).second&&registered.insert(id).second,"Duplicate dedicated manual asset: "+id);
   const auto source=read(root/row.at("file").get<std::string>()),previous=read(root/row.value("base_file",std::string("meshes/")+base+".json"));
   require(source.at("id")==id&&source.at("budget")==row.at("budget"),"Native identity/budget mismatch: "+id);
   require(shape(source)!=shape(previous)&&shapes.insert(shape(source).dump()).second,"Manual asset reuses stand-in or duplicate geometry: "+id);
   auto cap=expectedItems.contains(id)?502: id=="combiner_mecha_combined"?502:
    id=="sentry_gun"||id=="artillery_battery"?300:
    id=="bomber"||id=="interceptor"||id=="cyclone_drone"||id=="deepdiver_submarine"?248:752;
   for(const auto &approved:approvedBudgets.at("assets"))if(approved.at("id")==id)cap=approved.at("budget").get<int>();
   for(const auto &approved:approvedEnemyBudgets.at("assets"))if(approved.at("id")==id)cap=approved.at("budget").get<int>();
   const auto faces=source.at("faces").size();require(faces>0&&faces<=size_t(cap)&&faces<=row.at("budget").get<size_t>(),"Dedicated manual mesh exceeds its authorized budget: "+id);
   for(const char *edge:{"low","high"})for(int axis=0;axis<3;++axis)
    require(std::abs(source.at("bounds").at(edge)[axis].get<float>()-previous.at("bounds").at(edge)[axis].get<float>())<.0001f,"New visual changed gameplay/grip envelope: "+id);
   auto mesh=originalMesh(id);require(mesh->size()==faces*3,"Dedicated native import differs from exported triangles: "+id);
   for(const auto &vertex:*mesh)require(std::isfinite(length(vertex.position))&&std::isfinite(vertex.u)&&std::isfinite(vertex.v)&&vertex.u>=0&&vertex.u<=1&&vertex.v>=0&&vertex.v<=1,"Nonfinite native vertex/atlas coordinate: "+id);
   Target target;applyOriginalModel(target,id);require(target.generatorAsset==id&&target.ai.boundsReady&&target.half.x>0&&target.half.y>0&&target.half.z>0,"Dedicated visual lacks usable collision bounds: "+id);
   if(expectedEnemies.contains(id)){
    const auto *rig=enemyAnimationRig(root/"models",id,mesh->size());require(rig&&rig->vertices==mesh->size(),"Dedicated enemy lacks its own animation rig: "+id);
    const auto still=poseEnemy(*rig,0,0,0,0,0,0),idle=poseEnemy(*rig,0,0,0,0,0,1.3f),attack=poseEnemy(*rig,0,0,0,0,.12f,0);
    require(different(still,idle),"Enemy idle animation is static: "+id);require(different(still,attack),"Enemy attack/recoil animation is static: "+id);
    const auto start=poseEnemy(*rig,0,4,0,0,0,0),end=poseEnemy(*rig,18,4,0,0,0,0);
    require(!different(start,end),"Enemy gait does not close over the native18-frame cycle: "+id);
    bool articulated=false,contact=false;for(const auto &part:rig->parts){articulated|=part.joint!=EnemyJoint::Rigid;contact|=part.ground||part.joint==EnemyJoint::Foot;}
    require(articulated,"Enemy rigid-part rig has no moving mechanism: "+id);
    for(int sample=0;sample<=72;++sample){const auto pose=poseEnemy(*rig,float(sample)*.25f,4,.3f,.1f,.12f,float(sample)/30);
     for(size_t part=0;part<rig->parts.size();++part)for(auto [first,length]:rig->parts[part].ranges){
      require(first+length<=mesh->size(),"Enemy animation range escapes its native mesh: "+id);
      for(size_t i=first;i<first+length;++i){auto vertex=animatedPoint(pose.parts[part],(*mesh)[i].position);require(std::isfinite(iron::length(vertex)),"Enemy animation produces nonfinite vertices: "+id);if(contact)require(vertex.y>=rig->floor-.015f,"Grounded enemy animation penetrates its floor: "+id+" / "+rig->parts[part].name+" / phase="+std::to_string(sample*.25f)+" / y="+std::to_string(vertex.y)+" / floor="+std::to_string(rig->floor));}
     }
    }
    ++animated;
   }
   ++count;triangles+=faces;
  }
  require(found==expected,std::string(filename)+" omits or unexpectedly aliases required model identities");
  std::cout<<filename<<": "<<found.size()<<" dedicated native assets verified\n";
 }
 require(count==64&&animated==33,"Not all dedicated manual visuals were imported/animated");
 std::cout<<"64 dedicated manual assets imported, 33 articulated enemy forms and31 item bindings verified / "<<triangles<<" triangles\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
