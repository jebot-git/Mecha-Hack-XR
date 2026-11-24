#include "mecha_rig_fixture.hpp"
#include "animation.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/world_generation.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
namespace {
void check(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
Json read(const std::filesystem::path &path){std::ifstream file(path);check(bool(file),"Missing clearance asset "+path.string());Json j;file>>j;return j;}
Vec3 vector(const Json &j){auto v=j.get<std::array<float,3>>();return {v[0],v[1],v[2]};}
Json point(Vec3 v){return Json::array({v.x,v.y,v.z});}
struct Envelope {
  Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};float radius=0;uint64_t vertices=0;
  void add(Vec3 p){check(std::isfinite(length(p)),"Animated clearance contains nonfinite vertex");
    low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
    high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
    radius=std::max(radius,std::hypot(p.x,p.z));++vertices;}
  void merge(const Envelope &e){low={std::min(low.x,e.low.x),std::min(low.y,e.low.y),std::min(low.z,e.low.z)};
    high={std::max(high.x,e.high.x),std::max(high.y,e.high.y),std::max(high.z,e.high.z)};
    radius=std::max(radius,e.radius);vertices+=e.vertices;}
  Json wire()const{return {{"low_m",point(low)},{"high_m",point(high)},{"horizontal_radius_m",radius},{"sampled_vertices",vertices}};}
};
WalkerRig rig(const Json &j){WalkerRig result;result.floor=j.at("floor");
  const std::array<std::string,9> roles={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot","head"};
  for(const auto &p:j.at("parts")){WalkerPart part;part.parent=p.at("parent");part.arm=p.value("arm",-1);
    auto role=std::find(roles.begin(),roles.end(),p.at("role").get<std::string>());check(role!=roles.end(),"Unknown chassis joint");
    part.joint=WalkerJoint(role-roles.begin());part.pivot=vector(p.at("pivot"));part.low=vector(p.at("low"));part.high=vector(p.at("high"));part.empty=p.at("faces").empty();result.parts.push_back(part);}
  return result;}
Vec3 bonePoint(Vec3 p,Vec3 start,Vec3 end,Quaternion roll){
  Vec3 z=unit(start-end),up=rotate(roll,{0,1,0});
  Vec3 hint=std::abs(z.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0};
  Vec3 x=unit(cross(up,z),unit(cross(hint,z))),y=unit(cross(z,x));
  return start+x*p.x+y*p.y+(start-end)*p.z;
}
void limb(Envelope &e,const WorldMesh &mesh,const Arm &arm,Quaternion torso,bool upper){
  for(const auto &v:mesh)e.add(bonePoint(v.position,upper?arm.shoulder:arm.elbow,upper?arm.elbow:arm.wrist,upper?torso:arm.grip));
}
float supportError(const World &world){float error=0;
  for(const auto &target:world.targets)if(target.active&&target.health>0&&!target.invulnerable&&target.spawnType<0){
    if(target.rpgDecoration){
      // Dense dressing fits each base vertex to the unchanged heightfield;
      // origin height alone no longer describes its sloping footing.
      size_t contacts=0;
      for(const auto &v:*target.mesh)if(v.position.y<=.05f){auto p=target.origin+rotate(angles(target.yaw),v.position);
        float gap=std::abs(world.surfaceHeight(p.x,p.z)-p.y);
        if(gap>=.1f)throw std::runtime_error("Decoration base floats: seed="+std::to_string(world.prototypeSeed)+" asset="+target.generatorAsset+" gap="+std::to_string(gap)+" base="+std::to_string(v.position.y)+" at="+std::to_string(p.x)+","+std::to_string(p.z));
        error=std::max(error,gap);++contacts;}
      check(contacts>0,"Decoration has no grounded base vertices");continue;
    }
    for(float x:{target.center.x-target.half.x,target.center.x+target.half.x})
      for(float z:{target.center.z-target.half.z,target.center.z+target.half.z})
        {float gap=std::abs(world.surfaceHeight(x,z)-target.origin.y);if(gap>.1f)throw std::runtime_error("Floating prop seed="+std::to_string(world.prototypeSeed)+" asset="+target.generatorAsset+" corner="+std::to_string(x)+","+std::to_string(z)+" ground="+std::to_string(world.surfaceHeight(x,z))+" origin="+std::to_string(target.origin.y));error=std::max(error,gap);}
  }
  return error;
}
}
int main(int argc,char **argv){try{
  auto root=std::filesystem::path(MECHA_ASSETS);setOriginalAssetRoot(root);
  auto metadata=mechaPlayerRigFixture(root),hand=read(root/"models/rig.json");
  Json report={{"schema",1},{"units","metres"},{"method","Actual native chassis triangle vertices through poseWalker; actual hand/gear vertices through solveArm and native rigid bone transforms. Conservative union across sampled legal gear and tracking extremes."}};
  Envelope combined;Json chassis=Json::object();
  for(const char *name:{"chassis_brawler","chassis_scout","chassis_striker","chassis_titan","chassis_brawler_alt","chassis_scout_alt","chassis_striker_alt","chassis_titan_alt"}){
    auto mesh=originalMesh(name);auto data=metadata.at("rigs").at(name);auto skeleton=rig(data);Envelope shape;
    for(int phase=0;phase<=72;++phase)for(float torso:{-pi/2,0.f,pi/2})
      for(Vec3 head:std::array<Vec3,5>{{{0,0,0},{-headYawLimit,-1.2f,0},{headYawLimit,-1.2f,0},{-headYawLimit,1.2f,0},{headYawLimit,1.2f,0}}}){
      auto pose=poseWalker(skeleton,phase*.25f,8,torso,head.x,head.y);
      for(size_t part=0;part<skeleton.parts.size();++part)for(const auto &range:data.at("parts")[part].at("ranges")){
        size_t first=range[0],count=range[1];check(first+count<=mesh->size(),"Rig range escapes actual chassis mesh");
        for(size_t i=first;i<first+count;++i)shape.add(animatedPoint(pose.parts[part],(*mesh)[i].position)-Vec3{0,skeleton.floor,0});
      }
    }
    check(shape.low.y>-.001f,"Animated chassis penetrates its floor");chassis[name]=shape.wire();combined.merge(shape);
  }
  ArmRig tracking;tracking.trackingScale=hand.at("tracking_scale");tracking.upperLength=hand.at("upper_length");tracking.foreLength=hand.at("fore_length");
  for(size_t i=0;i<tracking.muzzle.size();++i)tracking.muzzle[i]=vector(hand.at("held")[i].at("muzzle"));
  // Include preserved IS arms and all authored chassis-specific tracked arm pieces.
  // This is the same native-position normalization performed by Renderer::loadModels.
  std::vector<std::array<WorldMesh,6>> armProfiles(1);
  for(int side=0;side<2;++side)for(int segment=0;segment<3;++segment){
    const std::array<const char*,3> suffix={"shoulder","upper-arm","forearm"};
    armProfiles[0][side*3+segment]=*originalMesh(std::string(side?"right-":"left-")+suffix[segment]);
  }
  if(std::filesystem::exists(root/"models/mecha-redesign-player-rigs.json"))for(const auto &[name,data]:metadata.at("rigs").items()){
    if(!name.starts_with("chassis_"))continue;
    std::array<WorldMesh,6> profile;auto source=originalMesh(name);
    for(const auto &part:data.at("parts"))if(part.contains("tracked_segment")){
      const std::string label=part.at("tracked_segment");const int segment=label=="shoulder"?0:label=="upper-arm"?1:label=="forearm"?2:-1;
      const int side=part.at("arm");check(segment>=0&&side>=0&&side<2,"Invalid chassis tracked limb metadata");
      auto &piece=profile[side*3+segment];
      for(const auto &range:part.at("ranges")){size_t first=range[0],count=range[1];check(first+count<=source->size(),"Tracked limb range escapes mesh");piece.insert(piece.end(),source->begin()+first,source->begin()+first+count);}
    }
    for(size_t index=0;index<profile.size();++index){auto &piece=profile[index];check(!piece.empty(),"Missing authored tracked limb");Envelope bounds;for(const auto &v:piece)bounds.add(v.position);
      const auto center=(bounds.low+bounds.high)*.5f;const float span=std::max(.01f,bounds.high.y-bounds.low.y);
      for(auto &v:piece){auto p=v.position;v.position=index%3==0?p-center:Vec3{p.x-center.x,-(p.z-center.z),(p.y-bounds.high.y)/span};}
    }
    armProfiles.push_back(std::move(profile));
  }
  Envelope arms;uint64_t poses=0;
  for(const auto &profile:armProfiles)for(int side=0;side<2;++side){
    const auto *shoulder=&profile[side*3],*upper=&profile[side*3+1],*fore=&profile[side*3+2];
    for(int gear=0;gear<int(Gear::Count);++gear){auto g=Gear(gear);
      auto held=originalMesh(g==Gear::Empty?"weapon_manipulator":std::string(gearVisualId(g)));
      int category=ranged(g)?1:melee(g)?2:0;
      for(float torso:{-pi/2,0.f,pi/2})for(Vec3 position:std::array<Vec3,3>{{{side?.4f:-.4f,-.13f,-.75f},{side?.9f:-.9f,-.4f,-.8f},{side?.3f:-.3f,.8f,-.25f}}})
        for(float yaw:{-pi/2,0.f,pi/2})for(float pitch:{-pi/2,0.f,pi/2})for(float roll:{0.f,pi/2}){
          HandPose input;input.position=position;input.aim=input.grip=angles(yaw,pitch,roll);
          auto arm=solveArm(input,side,{0,12,0},torso,tracking,category);check(arm.tracked,"Valid tracking pose rejected");++poses;
          for(const auto &v:*shoulder)arms.add(arm.shoulder+rotate(angles(torso),v.position));
          limb(arms,*upper,arm,angles(torso),true);limb(arms,*fore,arm,angles(torso),false);
          for(const auto &v:*held){auto p=v.position;p.x*=side?1.f:-1.f;arms.add(arm.wrist+rotate(arm.aim,p));}
        }
    }
  }
  // Shoulder/hip equipment uses its actual retained native drawArms anchors.
  for(int gear=1;gear<int(Gear::Count);++gear){auto g=Gear(gear);if(melee(g)||g==Gear::Shield)continue;
    auto mesh=originalMesh(std::string(gearVisualId(g)));
    for(int slot=LeftShoulder;slot<MountCount;++slot)for(float torso:{-pi/2,0.f,pi/2}){
      float side=slot%2?1.f:-1.f;Vec3 anchor=rotate(angles(torso),{side*2.3f,slot<LeftHip?8.2f:4.4f,.4f});
      for(const auto &v:*mesh){auto p=v.position;p.x*=side;arms.add(anchor+rotate(angles(torso),p));}
    }
  }
  combined.merge(arms);report["chassis"]=chassis;report["tracked_arms_and_gear"]=arms.wire();report["player_union"]=combined.wire();
  report["sampling"]={{"tracked_arm_profiles",armProfiles.size()},{"gait_samples",73},{"torso_yaws",3},{"head_aim_extremes",5},{"hand_positions",3},{"aim_rotations",18},{"hand_gear_tracking_poses",poses},{"native_cockpit_eye_m",12},{"source_cockpit_changed",false}};
  report["nominal_clearance"]={{"door_height_m",32},{"door_width_m",48},{"room_ceiling_m",48},
    {"minimum_door_overhead_margin_m",32-combined.high.y},{"minimum_room_overhead_margin_m",48-combined.high.y},
    {"conservative_door_side_margin_m",24-combined.radius},{"reserved_route_side_margin_m",20-combined.radius}};
  check(combined.high.y<30&&combined.radius<18,"Actual animated/held player exceeds safe room or route budget");
  Json scenes=Json::array();uint64_t samples=0;float leastSide=1e9f,leastOverhead=1e9f,worstSupport=0;bool repeated=false;
  for(float extent:{500.f,1000.f})for(auto spec:std::array<std::array<int,2>,3>{{{7,0},{6,2},{1,3}}}){
    World world;std::array<int,0> empty{};buildMissionEnvironment(world,42,3,extent,empty,spec[0],0,spec[1]);
    check(world.targets.size()<=maxEncounterTargets,"Coherent dressing exceeded reserved scene target capacity");
    float sideGap=1e9f,overheadGap=1e9f;
    for(auto edge:world.interiorNavLinks){auto a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]];
      int count=std::max(1,int(std::ceil(std::hypot(a.x-b.x,a.z-b.z)/4)));
      for(int i=0;i<=count;++i){auto p=a+(b-a)*(float(i)/count);p.y=world.surfaceHeight(p.x,p.z);++samples;
        check(world.groundPassable(p,combined.radius),"Actual player envelope cannot traverse generated floor or banks");
        for(const auto &target:world.targets)if(target.active&&target.health>0){
          float bottom=target.center.y-target.half.y,top=target.center.y+target.half.y;
          float dx=std::max(0.f,std::abs(p.x-target.center.x)-target.half.x),dz=std::max(0.f,std::abs(p.z-target.center.z)-target.half.z);
          float distance=std::hypot(dx,dz);
          if(top<=p.y+.01f)continue;
          if(bottom>=p.y+combined.high.y){if(distance<=combined.radius)overheadGap=std::min(overheadGap,bottom-p.y-combined.high.y);continue;}
          sideGap=std::min(sideGap,distance-combined.radius);
          check(distance>combined.radius,"Actual animated/held envelope intersects a wall, pier or prop along route");
        }
      }
    }
    for(const auto &target:world.targets)if(target.active&&target.invulnerable&&target.half.y>8&&std::max(target.half.x,target.half.z)>32){
      float lowU=1e9f,highU=-1e9f,lowV=1e9f,highV=-1e9f;bool tile=false;
      for(const auto &v:*target.mesh)if(v.textured==3){tile=true;check(std::isfinite(v.u)&&std::isfinite(v.v)&&v.oneSided>=0&&v.oneSided<=63,"Trim repeat contains invalid atlas tile or UV");lowU=std::min(lowU,v.u);highU=std::max(highU,v.u);lowV=std::min(lowV,v.v);highV=std::max(highV,v.v);}
      if(tile){check(highU-lowU>1||highV-lowV>1,"Large wall still stretches a single trim tile");repeated=true;}
    }
    float support=supportError(world);worstSupport=std::max(worstSupport,support);check(support<.1f,"Interior/outdoor prop foundation leaves floating footprint corners");
    leastSide=std::min(leastSide,sideGap);leastOverhead=std::min(leastOverhead,overheadGap);
    scenes.push_back({{"location",spec[0]},{"region",spec[1]},{"extent_m",extent},{"targets",world.targets.size()},
      {"minimum_route_side_margin_m",sideGap},{"minimum_overhead_margin_m",overheadGap},{"worst_support_height_error_m",support}});
  }
  Json reused=Json::object();
  for(const char *id:{"is_warehouse","is_office","is_water_tower","is_relay"})reused[id]=0;
  for(int location:{0,4,6,5}){
    World world;std::array<int,0> empty{};buildMissionEnvironment(world,42,3,500,empty,location,0,0);
    float support=supportError(world);worstSupport=std::max(worstSupport,support);check(support<.1f,"Sloped-terrain cluster foundation leaves floating or buried footprint corners");
    for(auto it=reused.begin();it!=reused.end();++it){for(const auto &target:world.targets)if(target.generatorAsset==it.key()||target.generatorAsset==it.key()+"_alt")it.value()=it.value().get<int>()+1;}
  }
  for(auto it=reused.begin();it!=reused.end();++it)check(it.value().get<int>()>0,"Approved prior IS replacement prop is not integrated: "+it.key());
  check(repeated,"Generated interior does not use repeatable structural trim textures");
  report["scenes"]=scenes;report["reused_original_props_instances"]=reused;
  report["verification"]={{"route_samples",samples},{"minimum_measured_side_margin_m",leastSide},{"minimum_measured_overhead_margin_m",leastOverhead},
    {"worst_prop_support_height_error_m",worstSupport},{"wall_collisions",0},{"terrain_or_bank_failures",0},{"structural_trim_repeat",true}};
  if(argc==3&&std::string(argv[1])=="--report"){auto output=std::filesystem::path(argv[2]);if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());std::ofstream file(output);file<<report.dump(2)<<'\n';check(bool(file),"Cannot write quantitative clearance report");}
  std::cout<<"Actual player+gear height "<<combined.high.y<<"m / radius "<<combined.radius<<"m; door overhead margin "<<32-combined.high.y<<"m; "<<samples<<" route samples and grounded coherent clusters passed\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
