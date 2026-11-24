#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include "coop.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
using namespace iron;using namespace iron::rpg;using Json=nlohmann::json;
namespace {
void check(bool pass,const std::string &message){if(!pass)throw std::runtime_error(message);}
Json read(const std::filesystem::path &path){std::ifstream file(path);check(bool(file),"Missing enemy redesign QA asset "+path.string());Json j;file>>j;return j;}
bool moved(const PartPose &a,const PartPose &b){return length(a.offset-b.offset)>.0001f||length(rotate(a.orientation,{0,0,-1})-rotate(b.orientation,{0,0,-1}))>.0001f||length(rotate(a.orientation,{0,1,0})-rotate(b.orientation,{0,1,0}))>.0001f;}
bool moved(const WalkerPose &a,const WalkerPose &b){check(a.parts.size()==b.parts.size(),"Pose count mismatch");for(size_t i=0;i<a.parts.size();++i)if(moved(a.parts[i],b.parts[i]))return true;return false;}
void finite(const WalkerPose &pose){for(const auto &part:pose.parts)check(valid(part.orientation)&&std::isfinite(length(part.offset)),"Nonfinite remaining enemy pose");}
}
int main(){try{
 const std::filesystem::path root=MECHA_ASSETS;setOriginalAssetRoot(root);
 const auto scope=read(root/"enemy-redesign-budgets.json");auto dedicated=read(root/"models/enemy-redesign-rigs.json");
 if(std::filesystem::exists(root/"models/enemy-reference-rigs.json")){const auto revision=read(root/"models/enemy-reference-rigs.json");for(const auto &[id,rig]:revision.at("rigs").items())if(dedicated.at("rigs").contains(id))dedicated["rigs"][id]=rig;}
 const auto proportions=read(root/"models/chassis-voidmaw-appearance.json").at("models");
 const auto corrected=read(root/"models/voidmaw-proportion-rigs.json").at("rigs");
 for(const auto &[id,profile]:corrected.items()){
  const auto scale=proportions.at(id).at("scale").get<std::array<float,3>>();
  const auto base=read(root/"meshes"/(id+".json")).at("bounds");
  const auto low=base.at("low").get<std::array<float,3>>(),high=base.at("high").get<std::array<float,3>>();
  Target actor;applyOriginalModel(actor,id);
  const Vec3 size{(high[0]-low[0])*scale[0],(high[1]-low[1])*scale[1],(high[2]-low[2])*scale[2]};
  check(length(actor.half*2.f-size)<.002f,id+": collision envelope does not match the corrected silhouette");
  const auto *rig=enemyAnimationRig(root/"models",id,actor.mesh->size());
  check(rig&&rig->parts.size()==profile.at("parts").size(),id+": corrected silhouette did not load its rig");
  for(size_t i=0;i<rig->parts.size();++i){
   const auto &part=rig->parts[i];
   const auto expected=profile.at("parts").at(i).at("pivot").get<std::array<float,3>>();
   check(length(part.pivot-Vec3{expected[0],expected[1],expected[2]})<.0001f,id+": rig pivot was left in the original proportions");
   for(auto [first,count]:part.ranges)for(size_t vertex=first;vertex<first+count;++vertex){
    const auto p=(*actor.mesh)[vertex].position;
    check(p.x>=part.low.x-.001f&&p.x<=part.high.x+.001f&&
          p.y>=part.low.y-.001f&&p.y<=part.high.y+.001f&&
          p.z>=part.low.z-.001f&&p.z<=part.high.z+.001f,id+": corrected geometry exceeds its culling bounds");
   }
  }
 }
 size_t checked=0,poses=0;std::set<std::string> categories;std::set<EnemyJoint> exercised;
 for(const auto &row:scope.at("assets")){
  const auto id=row.at("id").get<std::string>(),category=row.at("category").get<std::string>();categories.insert(category);
  const auto mesh=originalMesh(id);const auto *rig=enemyAnimationRig(root/"models",id,mesh->size());check(rig&&dedicated.at("rigs").contains(id),id+": dedicated enemy rig missing");
  const auto &authored=dedicated.at("rigs").at(id);
  check(authored.contains("motion_profile")&&rig->motionProfile==authored.at("motion_profile").get<std::string>()&&rig->motionProfile!="legacy",id+": missing explicit redesigned motion profile");
  const bool tank=id=="anti_mecha_hovertank"||id=="anti_mecha_hovertank_alt"||id=="heavy_hover_tank";
  const std::string expected=tank?"tracked":category=="fighter"||category=="gunship"?"flight":category=="capital_craft"||category=="submarine"?"naval":category=="installation"?"stationary":category=="hover_vehicle"||category=="hover_drone"?"hover":category=="anti_mecha_squad"?"infantry":"";
  if(!expected.empty())check(rig->motionProfile==expected,id+": inappropriate mechanical movement profile");
  else check(rig->motionProfile=="organic"||rig->motionProfile=="insect"||rig->motionProfile=="serpent",id+": organic/hybrid enemy lacks appropriate movement profile");
  check(rig->vertices==mesh->size()&&rig->parts.size()==authored.at("parts").size(),id+": runtime rig precedence mismatch");
  std::set<size_t> coverage;bool contacts=false;std::map<EnemyJoint,size_t> roles;
  for(size_t i=0;i<rig->parts.size();++i){const auto &part=rig->parts[i];check(part.name==authored.at("parts")[i].at("name").get<std::string>(),id+": runtime selected historical rig");
   check(part.parent>=-1&&part.parent<int(i),id+": invalid parent hierarchy");contacts|=part.ground||part.joint==EnemyJoint::Foot;++roles[part.joint];
   const auto axis=authored.at("parts")[i].value("rotation_axis",std::string{});
   check(part.rotationAxis==(axis.empty()?0:axis[0]),id+": authored hinge axis was not loaded");
   for(auto [first,count]:part.ranges){check(first%3==0&&count%3==0&&count>0&&first+count<=mesh->size(),id+": range escapes native mesh");for(size_t k=first;k<first+count;++k)check(coverage.insert(k).second,id+": duplicate rigid membership");}
  }
  check(coverage.size()==mesh->size(),id+": incomplete native rigid partition");
  const auto still=poseEnemy(*rig,0,0,0,0,0,0),idle=poseEnemy(*rig,0,0,0,0,0,1.3f),attack=poseEnemy(*rig,0,0,.5f,.25f,.12f,0);
  check(moved(still,idle),id+": no idle/flight/scan motion");check(moved(still,attack),id+": attack articulation has no effect");
  for(size_t i=0;i<rig->parts.size();++i){const auto role=rig->parts[i].joint;
   if(role==EnemyJoint::Tail||role==EnemyJoint::Wing||role==EnemyJoint::Rotor||role==EnemyJoint::Turret||role==EnemyJoint::Barrel||role==EnemyJoint::Jaw||role==EnemyJoint::Scanner){
    check(moved(still.parts[i],idle.parts[i])||moved(still.parts[i],attack.parts[i]),id+": functional appendage never animates: "+rig->parts[i].name);exercised.insert(role);
   }
  }
  if(roles[EnemyJoint::Hip])check(moved(poseEnemy(*rig,4.5f,4,0,0,0,0),poseEnemy(*rig,13.5f,4,0,0,0,0)),id+": ground limbs do not alternate");
  for(int step=0;step<=72;++step)for(float pitch:{-.35f,.35f}){
   const auto pose=poseEnemy(*rig,step*.25f,4,.45f,pitch,.06f,step/30.f);finite(pose);float bottom=1e9f;
   for(size_t i=0;i<rig->parts.size();++i){const auto &part=rig->parts[i];for(auto [first,count]:part.ranges)for(size_t k=first;k<first+count;++k){const auto p=animatedPoint(pose.parts[i],(*mesh)[k].position);check(std::isfinite(length(p)),id+": transformed vertex is nonfinite");if(part.ground||part.joint==EnemyJoint::Foot)bottom=std::min(bottom,p.y);
    if(contacts&&(rig->motionProfile=="organic"||rig->motionProfile=="insect"||rig->motionProfile=="serpent"||rig->motionProfile=="infantry")&&p.y<rig->floor-.001f)
     throw std::runtime_error(id+": "+part.name+" penetrates the floor at gait sample "+std::to_string(step)+" (height "+std::to_string(p.y)+")");}}
   if(contacts)check(bottom>=rig->floor-.001f,id+": grounded appendage penetrates floor");
   else check(std::abs(pose.vertical)<.0001f,id+": airborne craft incorrectly receives sole correction");
   ++poses;
  }
  check(!moved(poseEnemy(*rig,0,4,0,0,0,0),poseEnemy(*rig,18,4,0,0,0,0)),id+": locomotion cycle seam");
  finite(poseEnemy(*rig,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()));
  for(float time:{std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()})finite(poseEnemy(*rig,4.5f,4,.4f,.2f,.1f,time));
  World host;host.targets.clear();Target target;applyOriginalModel(target,id);target.active=true;target.health=100;target.spawnType=3;target.ai.gaitPhase=4.25f;target.ai.velocity={3,0,1};target.ai.weaponYaw=.4f;target.ai.weaponPitch=.2f;target.ai.muzzleFlash=.08f;
  host.targets.push_back(target);host.elapsed=2.3f;World guest=host;guest.targets[0].ai={};coop::worldRead(coop::worldWire(host),guest);
  auto replicated=[&](const World &w){const auto &t=w.targets[0];return poseEnemy(*rig,t.ai.gaitPhase,length(t.ai.velocity),t.ai.weaponYaw-t.yaw,t.ai.weaponPitch,t.ai.muzzleFlash,w.elapsed);};
  check(!moved(replicated(host),replicated(guest)),id+": host/client articulation differs");++checked;
 }
 check(checked==44&&categories.size()==10,"Remaining enemy scope/count changed");
 for(auto role:{EnemyJoint::Tail,EnemyJoint::Wing,EnemyJoint::Rotor,EnemyJoint::Turret,EnemyJoint::Barrel,EnemyJoint::Jaw,EnemyJoint::Scanner})check(exercised.contains(role),"Enemy expansion omitted a required functional animation role");
 std::cout<<"44 remaining enemies: "<<poses<<" native gait/flight/aim samples, functional tail/wing/rotor/turret/barrel/jaw/scanner motion, contact floors, finite clocks, loop seams and replicated poses verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
