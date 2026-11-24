#include "mecha_rig_fixture.hpp"
#include "animation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
namespace {
void require(bool value,const std::string &message) {
  if(!value)throw std::runtime_error(message);
}
Json read(const std::filesystem::path &path) {
  std::ifstream file(path);require(bool(file),"Missing geometry fixture: "+path.string());
  Json value;file>>value;return value;
}
Vec3 vector(const Json &value) {
  auto v=value.get<std::array<float,3>>();return {v[0],v[1],v[2]};
}
bool near(Vec3 a,Vec3 b,float epsilon=.0002f) {return length(a-b)<epsilon;}
bool inside(Vec3 p,Vec3 low,Vec3 high,float epsilon=.0002f) {
  return p.x>=low.x-epsilon&&p.y>=low.y-epsilon&&p.z>=low.z-epsilon&&
         p.x<=high.x+epsilon&&p.y<=high.y+epsilon&&p.z<=high.z+epsilon;
}
std::vector<Vec3> decodedPoints(const Json &mesh,const std::string &name,const Json &handRig) {
  std::vector<Vec3> points;
  Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
  for(const auto &v:mesh.at("vertices")) {
    auto p=vector(v);points.push_back(p);
    low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
    high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
  }
  std::map<std::string,float> alignments;
  for(const auto &held:handRig.at("held"))alignments.emplace(held.at("model"),held.at("alignment_yaw"));
  auto pivot=vector(handRig.at("source_pivot"));
  for(auto &p:points) {
    bool preview=name=="shotgun"||name=="heavy-machinegun";
    if(alignments.contains(name)||preview)
      p=rotate(angles(alignments.at(preview?"mounted-"+name+"-a":name)),
               {-(p.x-pivot.x)*.01f,(p.z-pivot.z)*.01f,(p.y-pivot.y)*.01f});
    else if(name.find("upper-arm")!=std::string::npos||name.find("forearm")!=std::string::npos)
      p={(p.x-(low.x+high.x)/2)*.01f,-(p.z-(low.z+high.z)/2)*.01f,
         (p.y-high.y)/std::max(1.f,high.y-low.y)};
    else if(name=="left-shoulder"||name=="right-shoulder")p=(p-(low+high)*.5f)*.01f;
    else if(name=="iron-soldier")p={p.x*.01f,p.y*.01f,-p.z*.01f};
    else p=p*.01f;
  }
  return points;
}
WalkerRig rig(const Json &description) {
  WalkerRig result;result.floor=description.at("floor");
  const std::array<std::string,9> roles={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot","head"};
  for(const auto &p:description.at("parts")) {
    WalkerPart part;part.parent=p.at("parent");part.arm=p.value("arm",-1);
    auto role=std::find(roles.begin(),roles.end(),p.at("role").get<std::string>());
    require(role!=roles.end(),"Unknown exported joint role");
    part.joint=WalkerJoint(role-roles.begin());part.pivot=vector(p.at("pivot"));
    part.low=vector(p.at("low"));part.high=vector(p.at("high"));part.empty=p.at("faces").empty();
    require(part.parent>=-1&&part.parent<int(result.parts.size()),"Exported rig parent must precede child");
    require(inside(part.pivot,part.low,part.high,.3f),"Mechanical pivot lies outside its part");
    result.parts.push_back(part);
  }
  return result;
}
void validateAnimation(const std::string &name,const Json &description,const std::vector<Vec3> &points,const Json &model) {
  auto skeleton=rig(description);
  std::vector<Vec3> triangles;
  for(const auto &face:model.at("faces")) {
    const auto &ids=face.at("indices");
    for(size_t i=1;i+1<ids.size();++i) {
      Vec3 a=points.at(ids[0].get<size_t>()),b=points.at(ids[i].get<size_t>()),c=points.at(ids[i+1].get<size_t>());
      if(name!="iron-soldier"&&length(cross(b-a,c-a))<.000001f)continue;
      triangles.insert(triangles.end(),{a,b,c});
    }
  }
  require(triangles.size()==description.at("mesh_vertices").get<size_t>(),name+": model/rig triangle count mismatch");
  std::set<size_t> covered;
  for(size_t index=0;index<skeleton.parts.size();++index) {
    const auto &part=skeleton.parts[index];
    for(const auto &range:description.at("parts")[index].at("ranges")) {
      size_t first=range[0],count=range[1];
      require(first%3==0&&count%3==0&&first+count<=triangles.size(),name+": invalid rigid-part triangle range");
      for(size_t i=first;i<first+count;++i) {
        require(covered.insert(i).second,name+": triangle belongs to multiple joints");
        require(inside(triangles[i],part.low,part.high),name+": draw triangle lies outside its culling bounds");
      }
    }
  }
  require(covered.size()==triangles.size(),name+": unbound geometry in rig");
  auto idle=poseWalker(skeleton,0,0,0),loop=poseWalker(skeleton,18,8,.7f),start=poseWalker(skeleton,0,8,.7f);
  for(size_t i=0;i<skeleton.parts.size();++i) {
    require(near(idle.parts[i].offset,{})&&near(rotate(idle.parts[i].orientation,{0,1,0}),{0,1,0}),name+": bind pose shifts geometry");
    require(near(loop.parts[i].offset,start.parts[i].offset),name+": gait does not loop");
  }
  for(int sample=0;sample<=72;++sample) {
    auto pose=poseWalker(skeleton,sample*.25f,8,.7f);float lowest=1e9f;unsigned feet=0;
    for(size_t i=0;i<skeleton.parts.size();++i) {
      auto &part=skeleton.parts[i];
      require(valid(pose.parts[i].orientation)&&std::isfinite(length(pose.parts[i].offset)),name+": nonfinite native pose");
      if(part.arm>=0) {
        auto forward=rotate(pose.parts[i].orientation,{0,0,-1});
        require(std::abs(std::atan2(-forward.x,-forward.z)-.7f)<.0002f,name+": arm does not follow torso turn");
      }
      if(part.joint!=WalkerJoint::LeftFoot&&part.joint!=WalkerJoint::RightFoot)continue;
      ++feet;
      require(near(rotate(pose.parts[i].orientation,{0,1,0}),{0,1,0}),name+": foot counter-rotation requires hip/knee/foot hierarchy");
      for(float x:{part.low.x,part.high.x})for(float y:{part.low.y,part.high.y})for(float z:{part.low.z,part.high.z})
        lowest=std::min(lowest,animatedPoint(pose.parts[i],{x,y,z}).y);
    }
    require(feet==2&&std::abs(lowest-skeleton.floor)<.0002f,name+": native gait fails sole contact");
  }
  // These anchors are part of the retained native cockpit/tracked-arm contract.
  for(int hand=0;hand<2;++hand) {
    Vec3 anchor{hand?2.5f:-2.5f,10.8f,-.1f};bool attachment=false;
    for(const auto &part:skeleton.parts)if(part.arm==hand)
      attachment=attachment||inside(anchor,part.low-Vec3{.35f,.35f,.35f},part.high+Vec3{.35f,.35f,.35f});
    require(attachment,name+": bind arm does not cover native tracked shoulder anchor");
  }
}
}
int main(int argc,char **argv) {try {
  auto root=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(MECHA_ASSETS);
  setOriginalAssetRoot(root);
  auto modelManifest=read(root/"models/manifest.json"),handRig=read(root/"models/rig.json");
  auto animations=mechaPlayerRigFixture(root);
  // Compare the actual historical renderer conversion with native metre-space
  // exports: matching metadata alone cannot catch a 100x or axis mistake.
  size_t models=0;
  for(const auto &[name,record]:modelManifest.at("models").items()) {
    auto mesh=read(root/"models"/record.at("file").get<std::string>());
    auto points=decodedPoints(mesh,name,handRig);
    auto nativePath=root/"meshes"/(record.at("original_id").get<std::string>()+".json");
    // The historical IS alias retains its original body independently of the
    // redesigned RPG striker. Compare it against its immutable native baseline.
    if(name=="iron-soldier"&&std::filesystem::exists(root/"mecha-redesign-budgets.json")){
      require(record.at("file")=="iron-soldier.json"&&record.at("original_id")=="chassis_striker"&&record.at("triangles")==248&&record.at("budget")==376,"Original IS body alias routing/count/cap changed");
      nativePath=root/"provenance/mecha-redesign-baseline/meshes/chassis_striker.json";
    }
    auto native=read(nativePath);
    require(points.size()==native.at("vertices").size(),name+": render/native vertex membership mismatch");
    for(size_t i=0;i<points.size();++i)require(near(points[i],vector(native["vertices"][i])),name+": renderer/native scale, axis or grip origin mismatch");
    if(animations.at("rigs").contains(name))validateAnimation(name,animations.at("rigs").at(name),points,mesh);
    ++models;
  }
  ArmRig trackedRig;
  trackedRig.trackingScale=handRig.at("tracking_scale");
  trackedRig.upperLength=handRig.at("upper_length");trackedRig.foreLength=handRig.at("fore_length");
  require(handRig.at("held").size()==trackedRig.muzzle.size(),"Hand rig lost native muzzle variants");
  for(size_t i=0;i<trackedRig.muzzle.size();++i)trackedRig.muzzle[i]=vector(handRig.at("held")[i].at("muzzle"));
  for(int hand=0;hand<2;++hand)for(int held=0;held<5;++held)for(float yaw:{0.f,.7f}) {
    for(Vec3 position:std::array<Vec3,3>{{{hand?.4f:-.4f,-.13f,-.75f},{hand?.8f:-.8f,.4f,-1.1f},{0,-.7f,.4f}}}) {
      HandPose pose;pose.position=position;
      Vec3 eye{0,12,0};auto arm=solveArm(pose,hand,eye,yaw,trackedRig,held);
      require(arm.tracked,"Exported hand rig rejects finite reachable controller poses");
      require(std::abs(length(arm.elbow-arm.shoulder)-trackedRig.upperLength)<.0002f,"Upper-arm export/IK bone length mismatch");
      require(std::abs(length(arm.wrist-arm.elbow)-trackedRig.foreLength)<.0002f,"Forearm export/IK bone length mismatch");
      Vec3 anchor=eye+rotate(angles(yaw),{hand?2.5f:-2.5f,-1.2f,-.1f});
      require(near(arm.shoulder,anchor),"Tracked shoulder no longer matches retained cockpit/body anchor");
      Vec3 muzzle=trackedRig.muzzle[size_t(held)];muzzle.x*=hand?1.f:-1.f;
      require(near(arm.muzzle,arm.wrist+rotate(arm.aim,muzzle)),"Hand export muzzle/grip transform mismatch");
    }
  }
  for(size_t enemy=0;enemy<enemyCatalog().size();++enemy) {
    auto mesh=originalEnemyMesh(int(enemy));
    require(mesh==originalEnemyMesh(int(enemy)),"Original mesh cache does not reuse immutable geometry");
    for(float yaw:{0.f,pi/4,pi/2,pi}) {
      Target actor;actor.origin={37,0,-22};actor.yaw=yaw;applyOriginalEnemy(actor,int(enemy));
      require(actor.mesh==mesh&&actor.ai.boundsReady,"RPG target bypasses original mesh/bounds binding");
      Vec3 low=actor.center-actor.half,high=actor.center+actor.half;
      for(const auto &vertex:*mesh)require(inside(actor.origin+rotate(angles(yaw),vertex.position),low,high),"Rotated enemy render mesh escapes native collider");
      require(std::abs(actor.ai.localCenter.y-actor.ai.localHalf.y)<.0002f,"Enemy ground pivot differs from native terrain contact");
    }
  }
  // Maximum enemy populations and all four player chassis must leave their
  // insertion positions clear at both supported map extents.
  for(float extent:{500.f,1000.f})for(size_t enemy=0;enemy<enemyCatalog().size();++enemy) {
    Session game(723);GameSettings settings;settings.maps=1;settings.environment=3;settings.location=0;
    settings.objective=0;settings.opener=5;settings.enemy=int(enemy);settings.enemyCount=12;settings.extent=extent;
    for(uint32_t i=1;i<=4;++i)game.add(i,"Pilot "+std::to_string(i));
    game.command(1,{{"op","settings"},{"settings",settings}});
    for(uint32_t i=1;i<=4;++i) {
      game.command(i,{{"op","configure"},{"chassis",i-1},{"pilot",1}});
      game.command(i,{{"op","ready"},{"value",true}});
    }
    require(game.phase==Phase::Briefing,"Four-pilot ready barrier failed");
    for(const auto &pilot:game.members) {
      require(game.world.groundPassable(pilot.body.position,3.5f),"Player insertion overlaps water, steep ground or map boundary");
      require(clearPosition(game.world,pilot.body.position,3.5f),"Player insertion overlaps original enemy/scenery bounds");
      for(const auto &other:game.members)if(pilot.id!=other.id)
        require(std::hypot(pilot.body.position.x-other.body.position.x,pilot.body.position.z-other.body.position.z)>=7,"Four-player insertion footprints overlap");
    }
  }
  // The deployable barrier uses its real mesh pivot and rotated depth.
  {
    Session game(723);game.add(1,"Pilot");
    GameSettings settings;settings.maps=1;settings.environment=3;settings.location=0;settings.objective=0;
    settings.opener=5;settings.enemy=3;settings.enemyCount=1;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
    game.enemies[0].disabled=10000;
    auto &pilot=game.member(1);pilot.body.chassisYaw=.7f;
    pilot.sheet.equipment[RightShoulder]=Gear::ParticleBarrier;
    Input input;
    auto step=[&] {
      game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
      game.step(.05f);
    };
    step();input.fire[RightShoulder]=true;step();
    const auto &target=game.world.targets[game.world.targets.size()-8];
    require(target.active&&target.health>0,"Geometry regression did not deploy equipment");
    require(std::abs(target.yaw-pilot.body.facing())<.0001f,"Deployed equipment orientation ignores pilot heading");
    require(near(target.center,target.origin+rotate(angles(target.yaw),target.ai.localCenter)),"Deployed equipment discarded original pivot");
    for(const auto &vertex:*target.mesh)
      require(inside(target.origin+rotate(angles(target.yaw),vertex.position),target.center-target.half,target.center+target.half),"Deployed equipment collider excludes rendered geometry");
  }
  // Forced displacement must retain asymmetric mesh centers, not reconstruct
  // an old generic box centered at (0,halfHeight,0).
  for(auto gear:{Gear::QuakeGenerator,Gear::Grappler,Gear::ChestBeam}) {
    Session game(723);game.add(1,"Pilot");
    GameSettings settings;settings.maps=1;settings.environment=3;settings.location=0;settings.objective=0;
    settings.opener=5;settings.enemy=28;settings.enemyCount=1;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
    game.enemies[0].disabled=10000;
    auto &pilot=game.member(1);auto &target=game.world.targets[0];
    // The quake selects collider centers in a ten-metre sphere. Allow for
    // the enemy mesh having its center forward of its ground pivot.
    target.origin=pilot.body.position+Vec3{0,0,gear==Gear::QuakeGenerator?-6.f:-20.f};
    game.world.updateEnemyBounds(target);
    auto before=target.origin;
    pilot.sheet.equipment[RightShoulder]=gear;
    Input input;
    auto step=[&] {
      game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
      game.step(.05f);
    };
    step();
    if(gear==Gear::ChestBeam) {
      Shot shot;shot.body.position=target.center+Vec3{0,0,20};
      shot.body.velocity={0,0,-1000};shot.body.shooter=1;shot.body.damage=1;
      shot.effect=AttackEffect::Push;game.ordnance.push_back(shot);
    } else input.fire[RightShoulder]=true;
    step();
    require(length(target.origin-before)>5,"Geometry displacement regression did not move enemy");
    require(near(target.center,target.origin+rotate(angles(target.yaw),target.ai.localCenter)),"Displacement discarded asymmetric enemy center");
    for(const auto &vertex:*target.mesh)
      require(inside(target.origin+rotate(angles(target.yaw),vertex.position),target.center-target.half,target.center+target.half),"Displaced enemy collider excludes rendered geometry");
  }
  {
    Session game(723);game.add(1,"Pilot");
    GameSettings settings;settings.maps=1;settings.environment=3;settings.location=0;settings.objective=0;
    settings.opener=5;settings.enemy=26;settings.enemyCount=2;settings.tactic=8;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
    game.enemies[1].disabled=10000;
    // Let the aircraft reach its normal flight altitude before placing a
    // carried ground unit below it; overlapping both meshes at ground level
    // would correctly block takeoff and the intended unloading site.
    game.step(.05f);
    constexpr int salvagerThunderer=25,kaijuAbomination=28;
    game.enemies[1].spec=salvagerThunderer;
    auto &carrier=game.world.targets[0];auto &ally=game.world.targets[1];
    ally.origin=carrier.origin+Vec3{-12,0,0};
    ally.origin.y=game.world.surfaceHeight(ally.origin.x,ally.origin.z);
    ally.ai.airborne=false;applyOriginalEnemy(ally,salvagerThunderer);
    game.enemies[0].fireClock=0;
    // A disabled kaiju in the same loading position is still a hostile contact.
    auto hostile=game;
    hostile.enemies[1].spec=kaijuAbomination;
    auto &creature=hostile.world.targets[1];
    applyOriginalEnemy(creature,kaijuAbomination);
    const auto kaijuStart=creature.origin;
    hostile.step(.05f);
    require(length(creature.origin-kaijuStart)<1,"Human carrier redeployed a hostile kaiju");
    auto before=ally.origin;game.step(.05f);
    require(length(ally.origin-before)>5,"Carrier geometry regression did not redeploy ally");
    require(near(ally.center,ally.origin+rotate(angles(ally.yaw),ally.ai.localCenter)),"Carrier redeployment discarded asymmetric ally center");
  }
  auto previous=originalEnemyMesh(0);setOriginalAssetRoot(root);
  require(previous!=originalEnemyMesh(0)&&!previous->empty(),"Asset-root reload leaves cache stale or invalidates existing shared meshes");
  std::cout<<models<<" renderer models, four chassis rigid rigs, 34 original enemy colliders and 68 four-player maximum-population maps passed\n";
  return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
