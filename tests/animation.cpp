#include "animation.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
using namespace iron;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static size_t joint(const WalkerRig& rig,WalkerJoint kind){for(size_t i=0;i<rig.parts.size();++i)if(rig.parts[i].joint==kind)return i;throw std::runtime_error("Missing test joint");}
static WalkerRig readRig(const nlohmann::json& data){
    WalkerRig rig;rig.floor=data.at("floor");
    const std::string roles[]={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot"};
    for(auto& p:data.at("parts")){WalkerPart part;part.parent=p.at("parent");part.arm=p.value("arm",-1);part.empty=p.at("faces").empty();
        auto vector=[&](const char* key){auto v=p.at(key).get<std::array<float,3>>();return Vec3{v[0],v[1],v[2]};};part.pivot=vector("pivot");part.low=vector("low");part.high=vector("high");
        auto role=std::find(std::begin(roles),std::end(roles),p.at("role").get<std::string>());check(role!=std::end(roles),"unknown role");part.joint=WalkerJoint(role-std::begin(roles));rig.parts.push_back(part);
    }return rig;
}
static void poses(const WalkerRig& rig){
    auto left=joint(rig,WalkerJoint::LeftFoot),right=joint(rig,WalkerJoint::RightFoot),torso=joint(rig,WalkerJoint::Torso);
    auto idle=poseWalker(rig,0,0,0),stopped=poseWalker(rig,7.3f,0,0);
    for(size_t p=0;p<rig.parts.size();++p){check(length(idle.parts[p].offset)<.0001f,"idle rig shifted bind geometry");check(length(stopped.parts[p].offset-idle.parts[p].offset)<.0001f,"idle animation advanced/froze a half-stride");}
    auto forward=poseWalker(rig,4.5f,8,0),reverse=poseWalker(rig,13.5f,8,0);
    auto displacement=[&](const WalkerPose& pose,size_t foot){return animatedPoint(pose.parts[foot],rig.parts[foot].pivot)-rig.parts[foot].pivot;};
    check(displacement(forward,left).z*displacement(forward,right).z<0,"legs fail to alternate");
    check(displacement(forward,left).z*displacement(reverse,left).z<0,"reverse phase does not reverse stride");
    auto turn=poseWalker(rig,0,0,.7f);Vec3 front{0,0,-1};
    check(length(rotate(turn.parts[left].orientation,front)-front)<.0001f&&length(rotate(turn.parts[right].orientation,front)-front)<.0001f,"torso turn also turns legs");
    auto torsoFront=rotate(turn.parts[torso].orientation,front);check(std::abs(std::atan2(-torsoFront.x,-torsoFront.z)-.7f)<.0001f,"torso does not turn independently");
    auto chassis=angles(.4f);auto worldTorso=rotate(compose(chassis,turn.parts[torso].orientation),front),worldLeg=rotate(compose(chassis,turn.parts[left].orientation),front);
    check(std::abs(std::atan2(-worldTorso.x,-worldTorso.z)-1.1f)<.0001f&&std::abs(std::atan2(-worldLeg.x,-worldLeg.z)-.4f)<.0001f,"chassis and torso rotations mixed");
    for(int sample=0;sample<=72;++sample){auto pose=poseWalker(rig,sample*.25f,8,.5f);float lowest=1e9f;
        for(auto foot:{left,right}){auto& part=rig.parts[foot];auto up=rotate(pose.parts[foot].orientation,{0,1,0});check(up.y>.9999f,"stride foot tilts through terrain");
            for(float x:{part.low.x,part.high.x})for(float y:{part.low.y,part.high.y})for(float z:{part.low.z,part.high.z}){float value=animatedPoint(pose.parts[foot],{x,y,z}).y;check(value>=rig.floor-.0001f,"animated foot penetrates ground");lowest=std::min(lowest,value);}}
        check(std::abs(lowest-rig.floor)<.0001f,"no planted foot during stride");
    }
    auto invalid=poseWalker(rig,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN());
    for(auto& p:invalid.parts)check(valid(p.orientation)&&std::isfinite(length(p.offset)),"nonfinite tracking contaminated pose");
}
int main(int argc,char** argv){try{
    WalkerRig synthetic;synthetic.floor=-5;
    synthetic.parts={{-1,-1,WalkerJoint::Torso,{0,0,0},{-1,0,-1},{1,4,1}},
        {-1,-1,WalkerJoint::LeftHip,{-1,0,0},{-1,-1,-1},{0,0,1}}, {1,-1,WalkerJoint::LeftKnee,{-1,-2,0},{-1,-4,-1},{0,-1,1}}, {2,-1,WalkerJoint::LeftFoot,{-1,-4,0},{-1.5f,-5,-1},{-.5f,-4,1}},
        {-1,-1,WalkerJoint::RightHip,{1,0,0},{0,-1,-1},{1,0,1}}, {4,-1,WalkerJoint::RightKnee,{1,-2,0},{0,-4,-1},{1,-1,1}}, {5,-1,WalkerJoint::RightFoot,{1,-4,0},{.5f,-5,-1},{1.5f,-4,1}}};
    poses(synthetic);
    auto headed=synthetic;headed.parts.push_back({0,-1,WalkerJoint::Head,{0,4,0},{-.5f,4,-.5f},{.5f,5,.5f}});
    auto aiming=poseWalker(headed,4.5f,8,.6f,.3f,.25f),neutralHead=poseWalker(headed,4.5f,8,.6f);
    const Vec3 front{0,0,-1};
    auto headDirection=rotate(aiming.parts.back().orientation,front);
    check(std::abs(std::atan2(-headDirection.x,-headDirection.z)-.9f)<.0001f,"head aim does not compose with torso yaw");
    check(std::abs(headDirection.y-std::sin(.25f))<.0001f,"head pitch missing");
    for(size_t i=0;i<synthetic.parts.size();++i){check(length(aiming.parts[i].offset-neutralHead.parts[i].offset)<.0001f,"head aim moved torso or gait");check(length(rotate(aiming.parts[i].orientation,front)-rotate(neutralHead.parts[i].orientation,front))<.0001f,"head aim rotated torso or gait");}
    auto clampedHead=poseWalker(headed,0,0,0,4,9);headDirection=rotate(clampedHead.parts.back().orientation,front);
    check(std::abs(std::atan2(-headDirection.x,-headDirection.z)+headYawLimit)<.0001f&&std::abs(headDirection.y-std::sin(1.45f))<.0001f,"head aim exceeds control limits");
    auto invalidHead=poseWalker(headed,0,0,0,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN());
    for(const auto &p:invalidHead.parts)check(valid(p.orientation)&&std::isfinite(length(p.offset)),"nonfinite head tracking contaminated pose");
    auto bad=synthetic;bad.parts[0].parent=0;bool rejected=false;try{poseWalker(bad,0,1,0);}catch(const std::runtime_error&){rejected=true;}check(rejected,"cyclic hierarchy accepted");
    unsigned decoded=0;
    if(argc>1){std::ifstream file(std::filesystem::path(argv[1])/"models/animation-rigs.json");if(file){nlohmann::json metadata;file>>metadata;check(metadata.at("schema")==1,"animation schema");
        for(auto& [name,data]:metadata.at("rigs").items()){auto rig=readRig(data);poses(rig);std::set<int> faces;size_t vertices=0;
            for(auto& part:data.at("parts")){for(auto& face:part.at("faces"))check(faces.insert(face.get<int>()).second,"source face assigned to two bones");for(auto& range:part.at("ranges"))vertices+=range[1].get<size_t>();}
            check(vertices==data.at("mesh_vertices").get<size_t>(),"animated face ranges omit geometry");
            if(name=="iron-soldier"){check(rig.parts[7].pivot.x<0&&rig.parts[11].pivot.x>0,"native conversion mirrored player arm sides");check(rig.parts[3].low.z<0&&rig.parts[6].low.z<0,"feet fail native minus-Z convention");}
            ++decoded;
        }}}
    std::cout<<"Native idle/travel/reverse gait, grounded feet, chassis/torso separation, hierarchy safety and "<<decoded<<" decoded rigs passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
