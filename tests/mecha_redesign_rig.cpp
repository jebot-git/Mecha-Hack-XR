#include "animation.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
using namespace iron;
using namespace iron::rpg;
using Json=nlohmann::json;
namespace {
void check(bool pass,const std::string &message){if(!pass)throw std::runtime_error(message);}
Json read(const std::filesystem::path &p){std::ifstream f(p);check(bool(f),"Missing redesign QA asset "+p.string());Json j;f>>j;return j;}
Vec3 vec(const Json &j){auto p=j.get<std::array<float,3>>();return {p[0],p[1],p[2]};}
WalkerRig playerRig(const Json &j){WalkerRig r;r.floor=j.at("floor");
 const std::array<std::string,9> roles={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot","head"};
 for(const auto &p:j.at("parts")){WalkerPart part;part.parent=p.at("parent");part.arm=p.value("arm",-1);
  auto role=std::find(roles.begin(),roles.end(),p.at("role").get<std::string>());check(role!=roles.end(),"Unknown redesigned player joint");
  part.joint=WalkerJoint(role-roles.begin());part.pivot=vec(p.at("pivot"));part.low=vec(p.at("low"));part.high=vec(p.at("high"));part.empty=p.at("faces").empty();r.parts.push_back(part);
 }return r;
}
bool different(const WalkerPose &a,const WalkerPose &b){check(a.parts.size()==b.parts.size(),"Pose length differs");
 for(size_t i=0;i<a.parts.size();++i)if(length(a.parts[i].offset-b.parts[i].offset)>.0001f||length(rotate(a.parts[i].orientation,{0,0,-1})-rotate(b.parts[i].orientation,{0,0,-1}))>.0001f)return true;return false;
}
void finite(const WalkerPose &p){for(const auto &part:p.parts)check(valid(part.orientation)&&std::isfinite(length(part.offset)),"Nonfinite native articulated pose");}
}
int main(){try{
 const auto root=std::filesystem::path(MECHA_ASSETS);setOriginalAssetRoot(root);
 const auto scope=read(root/"mecha-redesign-budgets.json"),players=read(root/"models/mecha-redesign-player-rigs.json");
 size_t playerCount=0,enemyCount=0,poses=0,handPoses=0;
 for(const auto &row:scope.at("assets")){
  const auto id=row.at("id").get<std::string>();auto mesh=originalMesh(id);check(bool(mesh)&&!mesh->empty(),"Missing redesigned native mesh "+id);
  if(row.at("category")=="player"){
   const auto &data=players.at("rigs").at(id);auto rig=playerRig(data);std::set<size_t> coverage;size_t head=rig.parts.size(),torso=rig.parts.size();
   for(size_t i=0;i<rig.parts.size();++i){if(rig.parts[i].joint==WalkerJoint::Head)head=i;if(rig.parts[i].joint==WalkerJoint::Torso)torso=i;
    for(const auto &range:data.at("parts")[i].at("ranges")){size_t first=range[0],count=range[1];check(first+count<=mesh->size(),id+": player range escapes mesh");for(size_t k=first;k<first+count;++k)check(coverage.insert(k).second,id+": duplicate player range");}}
   check(coverage.size()==mesh->size()&&head<rig.parts.size()&&torso<rig.parts.size(),id+": missing articulated head or rigid partition");
   const auto bind=poseWalker(rig,0,0,0),look=poseWalker(rig,0,0,0,.55f,.25f);
   check(length(rotate(bind.parts[head].orientation,{0,0,-1})-rotate(look.parts[head].orientation,{0,0,-1}))>.1f,id+": head aim has no effect");
   for(size_t i=0;i<rig.parts.size();++i){bool headChild=false;for(int ancestor=int(i);ancestor>=0;ancestor=rig.parts[size_t(ancestor)].parent)if(size_t(ancestor)==head)headChild=true;
    if(!headChild)check(length(rotate(bind.parts[i].orientation,{0,0,-1})-rotate(look.parts[i].orientation,{0,0,-1}))<.0001f,id+": head aim rotates an unrelated part");}
   for(int step=0;step<=72;++step)for(float turn:{-.9f,0.f,.9f}){
    auto pose=poseWalker(rig,step*.25f,4,turn,.4f,-.2f);finite(pose);float floor=1e9f;
    for(size_t i=0;i<rig.parts.size();++i){const bool foot=rig.parts[i].joint==WalkerJoint::LeftFoot||rig.parts[i].joint==WalkerJoint::RightFoot;
     for(const auto &range:data.at("parts")[i].at("ranges")){size_t first=range[0],count=range[1];for(size_t k=first;k<first+count;++k){auto p=animatedPoint(pose.parts[i],(*mesh)[k].position);check(std::isfinite(length(p)),id+": posed vertex is nonfinite");check(p.y>=rig.floor-.001f,id+": body penetrates floor");if(foot)floor=std::min(floor,p.y);}}}
    check(floor>=rig.floor-.0001f&&floor<rig.floor+.001f,id+": no sole planted during gait");++poses;
   }
   check(!different(poseWalker(rig,0,4,0),poseWalker(rig,18,4,0)),id+": player gait seam");
   const WalkerMotion walk{int(playerCount%4)};
   const WalkerMotion slide{walk.chassis,{.85f,0,-.44f}};
   const WalkerMotion settle{walk.chassis,{},.8f,1.f};
   const auto stride=poseWalker(rig,4.5f,4,.3f,.2f,0,walk);
   const auto dash=poseWalker(rig,4.5f,4,.3f,.2f,0,slide);
   const auto land=poseWalker(rig,4.5f,0,.3f,.2f,0,settle);
   check(different(stride,dash)&&different(dash,land),id+": boost and landing need distinct poses");
   check(!different(poseWalker(rig,0,4,0,0,0,walk),poseWalker(rig,18,4,0,0,0,walk)),id+": planted gait wrap seam");
   for(const auto &action:{stride,dash,land}){
    finite(action);
    float sole=1e9f;
    for(size_t i=0;i<rig.parts.size();++i){
     const auto &part=rig.parts[i];
     if(part.joint!=WalkerJoint::LeftFoot&&part.joint!=WalkerJoint::RightFoot)continue;
     for(const auto &range:data.at("parts")[i].at("ranges")){
      size_t first=range[0],count=range[1];
      for(size_t k=first;k<first+count;++k)sole=std::min(sole,animatedPoint(action.parts[i],(*mesh)[k].position).y);
     }
    }
    check(sole>=rig.floor-.001f&&sole<rig.floor+.08f,id+": animated sole lost ground contact");
   }
   finite(poseWalker(rig,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),0,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()));
   // Use the same authored tracking calibration and solveArm as actual OpenXR input.
   const auto hand=read(root/"models/rig.json");ArmRig tracking;tracking.trackingScale=hand.at("tracking_scale");tracking.upperLength=hand.at("upper_length");tracking.foreLength=hand.at("fore_length");
   for(size_t k=0;k<tracking.muzzle.size();++k)tracking.muzzle[k]=vec(hand.at("held")[k].at("muzzle"));
   for(int side=0;side<2;++side)for(float yaw:{-.9f,0.f,.9f})for(float pitch:{-.8f,0.f,.8f})for(float torsoYaw:{-.9f,0.f,.9f}){
    HandPose input;input.position={side?.4f:-.4f,-.13f,-.7f};input.aim=input.grip=angles(yaw,pitch);auto arm=solveArm(input,side,{0,12,0},torsoYaw,tracking,1);
    check(arm.tracked&&std::isfinite(length(arm.shoulder))&&std::isfinite(length(arm.elbow))&&std::isfinite(length(arm.wrist))&&valid(arm.aim),id+": invalid tracking solution");
    check(length(arm.elbow-arm.shoulder)<=tracking.upperLength+.001f&&length(arm.wrist-arm.elbow)<=tracking.foreLength+.001f,id+": tracking limb overextended");++handPoses;
   }++playerCount;
  }else{
   const auto *rig=enemyAnimationRig(root/"models",id,mesh->size());check(rig&&rig->vertices==mesh->size(),id+": missing redesigned enemy rig");std::set<size_t> coverage;bool contacts=false;
   for(const auto &p:rig->parts){contacts|=p.ground||p.joint==EnemyJoint::Foot;for(auto [first,count]:p.ranges){check(first+count<=mesh->size(),id+": enemy range escapes mesh");for(size_t k=first;k<first+count;++k)check(coverage.insert(k).second,id+": duplicate enemy range");}}
   check(coverage.size()==mesh->size()&&contacts,id+": incomplete grounded enemy rig");
   check(different(poseEnemy(*rig,0,0,0,0,0,0),poseEnemy(*rig,0,0,0,0,0,1.3f)),id+": no idle animation");
   check(different(poseEnemy(*rig,4.5f,4,0,0,0,0),poseEnemy(*rig,13.5f,4,0,0,0,0)),id+": no locomotion");
   check(different(poseEnemy(*rig,0,0,0,0,0,0),poseEnemy(*rig,0,0,.4f,.2f,.12f,0)),id+": no attack animation");
   for(int step=0;step<=72;++step){auto pose=poseEnemy(*rig,step*.25f,4,.4f,.2f,.06f,step/30.f);finite(pose);float floor=1e9f;
    for(size_t i=0;i<rig->parts.size();++i){const auto &p=rig->parts[i];for(auto [first,count]:p.ranges)for(size_t k=first;k<first+count;++k){auto v=animatedPoint(pose.parts[i],(*mesh)[k].position);check(std::isfinite(length(v)),id+": nonfinite enemy vertex");if(p.ground||p.joint==EnemyJoint::Foot)floor=std::min(floor,v.y);}}
    check(floor>=rig->floor-.001f,id+": enemy contact penetrates floor");++poses;
   }
   check(!different(poseEnemy(*rig,0,4,0,0,0,0),poseEnemy(*rig,18,4,0,0,0,0)),id+": enemy gait seam");
   finite(poseEnemy(*rig,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),0,0,0,0));
   // Large finite authoritative clocks must not overflow float multiplications
   // before head/wing/scanner animation reaches its sine/remainder functions.
   for(float seconds:{std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity()})
    finite(poseEnemy(*rig,4.5f,4,.4f,.2f,.06f,seconds));
   ++enemyCount;
  }
 }
 check(playerCount==8&&enemyCount==57,"Redesigned actor scope must be 8 players and 57 enemies");
 std::cout<<"65 redesigned actors: "<<poses<<" native gait/aim poses, "<<handPoses<<" actual arm tracking solves, independent head, rigid range coverage, finite vertices, planted soles and cycle seams verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
